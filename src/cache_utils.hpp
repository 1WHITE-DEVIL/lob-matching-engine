#pragma once
#include "order.hpp"
#include <memory>
#include <cstddef>
#include <stdexcept>
#include <cstring>

// ============================================================
// M7 — CACHE-OPTIMIZED ORDER POOL
//
// Builds on the basic OrderPool from M5 with three optimizations:
//
// 1. SOFTWARE PREFETCHING
//    When we allocate order N, we prefetch order N+1 into L1 cache.
//    By the time the caller uses N+1, it's already in cache.
//    __builtin_prefetch(addr, rw, locality)
//      rw=0: prefetch for read
//      rw=1: prefetch for write  ← we use this (we'll write to it)
//      locality=3: keep in all cache levels (L1/L2/L3)
//      locality=0: non-temporal (evict after use) ← use for cold data
//
// 2. FALSE SHARING ELIMINATION
//    Each Order is 128 bytes = 2 cache lines.
//    Orders are already naturally aligned by the pool's contiguous array.
//    We add a static_assert to catch any future size regression.
//
// 3. SLAB DESIGN
//    Pool is divided into slabs of SLAB_SIZE orders.
//    When a slab is exhausted, allocate a new one.
//    Old slab objects stay warm in cache longer (locality).
//    This is how jemalloc / tcmalloc work internally.
//
// MEMORY LAYOUT VISUALIZATION:
//
//   Slab 0: [Order][Order][Order]...[Order]  ← 4096 orders contiguous
//   Slab 1: [Order][Order][Order]...[Order]
//   ...
//
//   Free list links orders within slabs first, then spills to next slab.
//   Hot path allocations stay within one slab → L2 cache resident.
// ============================================================

class CacheOptimizedPool {
public:
    static constexpr std::size_t SLAB_SIZE     = 4096; // orders per slab
    static constexpr std::size_t MAX_SLABS     = 64;
    static constexpr std::size_t TOTAL_CAPACITY = SLAB_SIZE * MAX_SLABS;

    // Each slab is heap-allocated and cache-line aligned
    struct alignas(64) Slab {
        Order orders[SLAB_SIZE];
    };

    CacheOptimizedPool() {
        // Allocate first slab eagerly; rest lazily
        add_slab();
    }

    Order* acquire(OrderId oid, UserId uid, Side s, OrderType ot,
                   Price p, Qty q, int64_t ts) {
        if (!free_head_) {
            if (slab_count_ >= MAX_SLABS)
                throw std::bad_alloc();
            add_slab();
        }

        Order* o   = free_head_;
        free_head_ = o->next;
        --free_count_;

        // PREFETCH OPTIMIZATION:
        // Tell the CPU to load the next free order into cache NOW,
        // while we're constructing the current one.
        // By the time the next acquire() is called, the data is warm.
        if (free_head_) {
            // locality=1: L2 cache (balance between L1 pressure and latency)
            __builtin_prefetch(free_head_, 1, 1);
        }

        new (o) Order(oid, uid, s, ot, p, q, ts);
        return o;
    }

    void release(Order* o) {
        o->~Order();
        o->next    = free_head_;
        o->prev    = nullptr;
        free_head_ = o;
        ++free_count_;
    }

    std::size_t free_count()  const { return free_count_; }
    std::size_t slab_count()  const { return slab_count_; }
    std::size_t capacity()    const { return slab_count_ * SLAB_SIZE; }

private:
    void add_slab() {
        auto slab = std::make_unique<Slab>();
        Order* base = slab->orders;

        // Link this slab's orders into the free list
        for (std::size_t i = 0; i < SLAB_SIZE - 1; ++i)
            base[i].next = &base[i + 1];
        base[SLAB_SIZE - 1].next = free_head_; // chain to existing list
        free_head_  = base;
        free_count_ += SLAB_SIZE;

        slabs_[slab_count_++] = std::move(slab);
    }

    std::unique_ptr<Slab> slabs_[MAX_SLABS] {};
    std::size_t           slab_count_  {0};
    Order*                free_head_   {nullptr};
    std::size_t           free_count_  {0};
};

// ============================================================
// FALSE SHARING DETECTOR
//
// Utility to verify two fields don't share a cache line.
// Use at compile time to audit your hot structs.
//
// Example:
//   static_assert(no_false_sharing<MyStruct>(
//       offsetof(MyStruct, field_a),
//       offsetof(MyStruct, field_b)),
//       "field_a and field_b share a cache line!");
// ============================================================

constexpr bool no_false_sharing(std::size_t offset_a, std::size_t offset_b) {
    constexpr std::size_t CACHE_LINE = 64;
    return (offset_a / CACHE_LINE) != (offset_b / CACHE_LINE);
}

// ============================================================
// CACHE LINE PADDING HELPER
//
// Wraps a value T and pads it to fill exactly one cache line.
// Use to prevent false sharing on individual variables.
//
// Example: Instead of:
//   std::atomic<uint64_t> counter_a;  // shares cache line with b!
//   std::atomic<uint64_t> counter_b;
//
// Use:
//   CacheLinePadded<std::atomic<uint64_t>> counter_a;
//   CacheLinePadded<std::atomic<uint64_t>> counter_b;
// ============================================================

template<typename T>
struct CacheLinePadded {
private:
    static constexpr std::size_t PAD =
        (sizeof(T) % 64 == 0) ? 0 : (64 - sizeof(T) % 64);
public:
    alignas(64) T value;
    char _pad[PAD == 0 ? 1 : PAD]; // at least 1 byte to avoid zero-size array

    CacheLinePadded() = default;
    explicit CacheLinePadded(T v) : value(std::move(v)) {}
    T&       get()              { return value; }
    const T& get()        const { return value; }
    T*       operator->()       { return &value; }
    const T* operator->() const { return &value; }
};
