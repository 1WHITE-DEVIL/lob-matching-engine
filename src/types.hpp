#pragma once
#include <cstdint>
#include <stdexcept>

// ============================================================
// FIXED-POINT PRICE
// ============================================================
struct Price {
    static constexpr int64_t SCALE = 100; // 2 decimal places: $1.23 = 123 ticks

    int64_t ticks{0};

    explicit constexpr Price(int64_t t) : ticks(t) {}

    static constexpr Price INVALID() { return Price{-1}; }

    static Price from_double(double d) {
        if (d < 0.0) throw std::invalid_argument("Price cannot be negative");
        return Price{static_cast<int64_t>(d * SCALE + 0.5)};
    }

    double to_double() const { return static_cast<double>(ticks) / SCALE; }
    bool   is_valid()  const { return ticks >= 0; }

    constexpr bool operator==(Price o) const { return ticks == o.ticks; }
    constexpr bool operator!=(Price o) const { return ticks != o.ticks; }
    constexpr bool operator< (Price o) const { return ticks <  o.ticks; }
    constexpr bool operator<=(Price o) const { return ticks <= o.ticks; }
    constexpr bool operator> (Price o) const { return ticks >  o.ticks; }
    constexpr bool operator>=(Price o) const { return ticks >= o.ticks; }
};

using OrderId = uint64_t;
using UserId  = uint64_t;
using Qty     = uint64_t;

constexpr OrderId INVALID_ORDER_ID = 0;

enum class Side : uint8_t { BUY = 0, SELL = 1 };

inline Side opposite(Side s) {
    return s == Side::BUY ? Side::SELL : Side::BUY;
}

enum class OrderType : uint8_t {
    LIMIT  = 0,
    MARKET = 1,
    IOC    = 2,
    FOK    = 3
};

enum class OrderStatus : uint8_t {
    NEW              = 0,
    PARTIALLY_FILLED = 1,
    FILLED           = 2,
    CANCELLED        = 3,
    REJECTED         = 4
};
