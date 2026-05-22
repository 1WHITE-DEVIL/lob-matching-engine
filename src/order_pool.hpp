#pragma once
#include "order.hpp"
#include <memory>
#include <cstddef>
#include <stdexcept>

// ============================================================
// ORDER POOL — fixed-size slab allocator
//
// WHY: new/delete per Order = heap fragmentation + cache misses.
//      A pool pre-allocates a contiguous array of Orders.
//      Allocation = O(1) pop from free list.
//      Deallocation = O(1) push to free list.
//      All Orders in one contiguous block → cache friendly.
//
// HEAP ALLOCATED: The slab is on the heap (not stack) because
//      CAPACITY * sizeof(Order) can exceed stack limits.
//      (500K orders * 128 bytes = 64MB — stack limit is ~8MB)
// ============================================================

template<std::size_t CAPACITY = 1'000'000>
class OrderPool {
public:
    OrderPool() : slots_(new Order[CAPACITY]) {
        // Build free list: each slot points to the next
        for (std::size_t i = 0; i < CAPACITY - 1; ++i)
            slots_[i].next = &slots_[i + 1];
        slots_[CAPACITY - 1].next = nullptr;
        free_head_ = &slots_[0];
        free_count_ = CAPACITY;
    }

    // Non-copyable
    OrderPool(const OrderPool&)            = delete;
    OrderPool& operator=(const OrderPool&) = delete;

    // Allocate one Order from the pool (placement-new)
    Order* acquire(OrderId oid, UserId uid, Side s, OrderType ot,
                   Price p, Qty q, int64_t ts) {
        if (!free_head_) throw std::bad_alloc();
        Order* o   = free_head_;
        free_head_ = o->next;
        --free_count_;
        new (o) Order(oid, uid, s, ot, p, q, ts);
        return o;
    }

    // Return Order to pool
    void release(Order* o) {
        o->~Order();
        o->next    = free_head_;
        o->prev    = nullptr;
        free_head_ = o;
        ++free_count_;
    }

    std::size_t free_count() const { return free_count_; }
    std::size_t capacity()   const { return CAPACITY; }

private:
    std::unique_ptr<Order[]> slots_;
    Order*      free_head_  {nullptr};
    std::size_t free_count_ {0};
};
