#pragma once
#include "types.hpp"
#include <cstdint>

// ============================================================
// ORDER STRUCT — 128 bytes / 2 cache lines
//
// Layout rationale:
//   All hot fields (price, qty, filled_qty, timestamp) sit in
//   cache line 0 (bytes 0–63). The intrusive list pointers used
//   only during insertion/removal sit at bytes 56–71.
//   alignas(64) ensures the struct starts on a cache line boundary.
// ============================================================

struct alignas(64) Order {
    // --- Cache line 0: hot matching fields ---
    OrderId     order_id   {INVALID_ORDER_ID};
    UserId      user_id    {0};
    Price       price      {Price::INVALID()};
    Qty         qty        {0};
    Qty         filled_qty {0};
    int64_t     timestamp  {0};    // nanoseconds since epoch

    Side        side   {Side::BUY};
    OrderType   type   {OrderType::LIMIT};
    OrderStatus status {OrderStatus::NEW};
    uint8_t     _pad[5]{};

    Order*      next {nullptr};    // intrusive list (price level queue)

    // --- Cache line 1 ---
    Order*      prev {nullptr};
    uint8_t     _pad2[56]{};       // reserved / future fields

    // --------------------------------------------------------
    Order() = default;

    Order(OrderId oid, UserId uid, Side s, OrderType ot,
          Price p, Qty q, int64_t ts)
        : order_id{oid}, user_id{uid}, price{p}, qty{q}
        , filled_qty{0}, timestamp{ts}
        , side{s}, type{ot}, status{OrderStatus::NEW}
        , next{nullptr}, prev{nullptr}
    {}

    Qty  leaves_qty()      const { return qty - filled_qty; }
    bool is_fully_filled() const { return filled_qty >= qty; }
    bool is_active()       const {
        return status == OrderStatus::NEW ||
               status == OrderStatus::PARTIALLY_FILLED;
    }
    bool is_buy()  const { return side == Side::BUY;  }
    bool is_sell() const { return side == Side::SELL; }

    // Fill delta qty; returns actual amount filled
    Qty fill(Qty delta) {
        Qty available = leaves_qty();
        Qty actual    = (delta < available) ? delta : available;
        filled_qty   += actual;
        status = is_fully_filled() ? OrderStatus::FILLED
                                   : OrderStatus::PARTIALLY_FILLED;
        return actual;
    }

    void cancel() { status = OrderStatus::CANCELLED; }
};

static_assert(sizeof(Order)  == 128, "Order must be 128 bytes");
static_assert(alignof(Order) == 64,  "Order must be 64-byte aligned");
static_assert(__builtin_offsetof(Order, order_id)   == 0);
static_assert(__builtin_offsetof(Order, price)      == 16);
static_assert(__builtin_offsetof(Order, qty)        == 24);
static_assert(__builtin_offsetof(Order, filled_qty) == 32);
static_assert(__builtin_offsetof(Order, timestamp)  == 40);
static_assert(__builtin_offsetof(Order, next)       == 56);
static_assert(__builtin_offsetof(Order, prev)       == 64);
