#pragma once
#include "order.hpp"

// ============================================================
// PRICE LEVEL — FIFO queue of orders at one price
//
// WHY intrusive linked list (not std::queue / std::list):
//   std::list allocates each node on the heap separately.
//   Traversing it = pointer chasing = cache miss per node.
//   Here next/prev are embedded IN the Order struct itself.
//   If orders come from a pool (contiguous memory), traversal
//   hits warm cache lines.
//
// Price-time priority: orders at the same price are matched
// in arrival order (FIFO). Head = oldest = matched first.
// ============================================================

class PriceLevel {
public:
    PriceLevel() = default;

    // Non-copyable (owns pointer structure)
    PriceLevel(const PriceLevel&)            = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;

    void push_back(Order* o) {
        o->next = nullptr;
        o->prev = tail_;
        if (tail_) tail_->next = o;
        else       head_ = o;
        tail_ = o;
        ++count_;
        total_qty_ += o->leaves_qty();
    }

    // Remove arbitrary order (cancel path)
    void remove(Order* o) {
        if (o->prev) o->prev->next = o->next;
        else         head_ = o->next;
        if (o->next) o->next->prev = o->prev;
        else         tail_ = o->prev;
        o->next = o->prev = nullptr;
        --count_;
        total_qty_ -= o->leaves_qty();
    }

    Order* front() const { return head_; }
    bool   empty() const { return head_ == nullptr; }
    Qty    total_qty()  const { return total_qty_; }
    std::size_t count() const { return count_; }

    // Pop head (after it is fully filled or cancelled)
    void pop_front() {
        if (!head_) return;
        Order* o = head_;
        head_ = o->next;
        if (head_) head_->prev = nullptr;
        else       tail_ = nullptr;
        o->next = o->prev = nullptr;
        --count_;
        // Note: caller must update total_qty_ via update_qty()
    }

    // Call after partially filling head to sync qty
    void update_qty(Qty delta_filled) {
        if (delta_filled <= total_qty_) total_qty_ -= delta_filled;
        else                            total_qty_ = 0;
    }

private:
    Order*      head_      {nullptr};
    Order*      tail_      {nullptr};
    std::size_t count_     {0};
    Qty         total_qty_ {0};
};
