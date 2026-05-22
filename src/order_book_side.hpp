#pragma once
#include "price_level.hpp"
#include "types.hpp"
#include <map>
#include <unordered_map>

// ============================================================
// ORDER BOOK SIDE — one half of the book (bids or asks)
//
// DATA STRUCTURE CHOICE: std::map<int64_t, PriceLevel>
//
//   Option A: std::unordered_map  O(1) avg lookup, O(n) worst
//     - No ordering: can't find best bid/ask in O(1)
//     - Need separate tracking of best price
//     - Hash collisions cause unpredictable latency spikes
//
//   Option B: std::map (red-black tree)  O(log n) all ops
//     - Ordered: begin()/rbegin() = best price instantly
//     - Predictable worst-case latency (no hash collisions)
//     - For typical order books (< 1000 price levels), log(n) ≈ 10
//     - Chosen for correctness and simplicity
//
//   Option C: sorted array / skip list (production HFT)
//     - Cache-friendly, SIMD-able, sub-microsecond
//     - Complex to implement correctly
//     - We'll discuss in M6/M7
//
// KEY: price.ticks (int64_t) — exact, no float comparison bugs
// ============================================================

class OrderBookSide {
public:
    explicit OrderBookSide(Side side) : side_(side) {}

    // Add order to appropriate price level (creates level if needed)
    void add(Order* o) {
        levels_[o->price.ticks].push_back(o);
        order_index_[o->order_id] = o;
    }

    // Cancel order by ID. Returns true if found and removed.
    bool cancel(OrderId oid) {
        auto it = order_index_.find(oid);
        if (it == order_index_.end()) return false;
        Order* o = it->second;
        auto   li = levels_.find(o->price.ticks);
        if (li != levels_.end()) {
            li->second.remove(o);
            if (li->second.empty()) levels_.erase(li);
        }
        o->cancel();
        order_index_.erase(it);
        return true;
    }

    // Best price on this side
    //   Bids: highest price (rbegin)
    //   Asks: lowest price (begin)
    Price best_price() const {
        if (levels_.empty()) return Price::INVALID();
        if (side_ == Side::BUY)
            return Price{levels_.rbegin()->first};
        else
            return Price{levels_.begin()->first};
    }

    // Best price level (null if empty)
    PriceLevel* best_level() {
        if (levels_.empty()) return nullptr;
        if (side_ == Side::BUY)
            return &levels_.rbegin()->second;
        else
            return &levels_.begin()->second;
    }

    // Remove best level if empty
    void clean_best() {
        if (levels_.empty()) return;
        if (side_ == Side::BUY) {
            if (levels_.rbegin()->second.empty())
                levels_.erase(std::prev(levels_.end()));
        } else {
            if (levels_.begin()->second.empty())
                levels_.erase(levels_.begin());
        }
    }

    // Remove order from index (after fill)
    void remove_from_index(OrderId oid) {
        order_index_.erase(oid);
    }

    bool  empty()       const { return levels_.empty(); }
    Side  side()        const { return side_; }
    std::size_t depth() const { return levels_.size(); }

    // Lookup order by id (for cancel/amend)
    Order* find(OrderId oid) const {
        auto it = order_index_.find(oid);
        return (it != order_index_.end()) ? it->second : nullptr;
    }

private:
    Side side_;
    // price ticks → price level
    std::map<int64_t, PriceLevel> levels_;
    // order_id → Order* for O(1) cancel
    std::unordered_map<OrderId, Order*> order_index_;
};
