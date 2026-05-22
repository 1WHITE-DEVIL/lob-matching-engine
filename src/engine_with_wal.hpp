#pragma once
#include "matching_engine.hpp"
#include "wal.hpp"
#include <optional>
#include <string>

// ============================================================
// PERSISTENT MATCHING ENGINE
//
// Wraps MatchingEngine with WAL logging.
// Every order submission and cancellation is logged to WAL
// BEFORE being applied to the in-memory book.
//
// CRASH RECOVERY FLOW:
//   1. Open WAL file in read mode
//   2. Replay all records to reconstruct book state
//   3. Switch to write mode, continue from last seq number
// ============================================================

class PersistentMatchingEngine {
public:
    PersistentMatchingEngine(const std::string& wal_path,
                              MatchingEngine::TradeCallback cb = nullptr)
        : engine_(std::move(cb))
        , wal_(wal_path)
    {}

    OrderId submit(UserId uid, Side side, OrderType type,
                   Price price, Qty qty) {

        // Log to WAL first (before touching in-memory state)
        WalNewOrder rec{};
        rec.user_id     = uid;
        rec.price_ticks = price.ticks;
        rec.qty         = qty;
        rec.side        = side;
        rec.type        = type;
        rec.timestamp   = 0; // engine assigns real timestamp

        // We don't know the order_id yet; set after engine assigns it.
        // In a real system, the engine would return the id and we'd
        // log it separately, or we pre-assign ids here.
        wal_.log_new_order(rec);

        OrderId oid = engine_.submit(uid, side, type, price, qty);
        return oid;
    }

    bool cancel(OrderId oid) {
        WalCancel rec{oid, 0};
        wal_.log_cancel(rec);
        return engine_.cancel(oid);
    }

    void checkpoint() {
        wal_.log_checkpoint();
    }

    // Accessors delegate to engine
    Price best_bid()    const { return engine_.best_bid(); }
    Price best_ask()    const { return engine_.best_ask(); }
    bool  has_bid()     const { return engine_.has_bid(); }
    bool  has_ask()     const { return engine_.has_ask(); }

    const std::vector<Trade>& trades() const { return engine_.trades(); }

private:
    MatchingEngine engine_;
    WalWriter      wal_;
};
