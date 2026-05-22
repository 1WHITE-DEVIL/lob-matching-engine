#pragma once
#include "types.hpp"

// ============================================================
// TRADE — immutable record of a match event
//
// Generated every time a buy and sell order cross.
// Partial fills produce one Trade per fill event.
// ============================================================

struct Trade {
    OrderId buy_order_id;
    OrderId sell_order_id;
    UserId  buyer_id;
    UserId  seller_id;
    Price   price;       // execution price (resting order's price)
    Qty     qty;         // matched quantity
    int64_t timestamp;   // nanoseconds

    Trade(OrderId bid, OrderId sid, UserId buid, UserId suid,
          Price p, Qty q, int64_t ts)
        : buy_order_id{bid}, sell_order_id{sid}
        , buyer_id{buid},    seller_id{suid}
        , price{p}, qty{q},  timestamp{ts}
    {}
};
