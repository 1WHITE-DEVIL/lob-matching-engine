#include "../src/cache_utils.hpp"
#include <cassert>
#include <iostream>
#include <atomic>
#include <chrono>
#include <vector>

void test_cache_optimized_pool() {
    std::cout << "[M7] CacheOptimizedPool alloc/free\n";
    CacheOptimizedPool pool;

    assert(pool.slab_count() == 1);
    assert(pool.free_count() == CacheOptimizedPool::SLAB_SIZE);

    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

    Order* o1 = pool.acquire(1, 1, Side::BUY, OrderType::LIMIT,
                             Price::from_double(100.0), 100, ts);
    Order* o2 = pool.acquire(2, 1, Side::SELL, OrderType::LIMIT,
                             Price::from_double(100.0), 100, ts);

    assert(o1->order_id == 1);
    assert(o2->order_id == 2);
    assert(pool.free_count() == CacheOptimizedPool::SLAB_SIZE - 2);

    pool.release(o1);
    pool.release(o2);
    assert(pool.free_count() == CacheOptimizedPool::SLAB_SIZE);
    std::cout << "  PASS\n";
}

void test_slab_expansion() {
    std::cout << "[M7] CacheOptimizedPool slab expansion\n";
    CacheOptimizedPool pool;

    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

    // Exhaust first slab
    std::vector<Order*> orders;
    orders.reserve(CacheOptimizedPool::SLAB_SIZE + 10);

    for (std::size_t i = 0; i < CacheOptimizedPool::SLAB_SIZE + 1; ++i) {
        orders.push_back(pool.acquire(
            static_cast<OrderId>(i + 1), 1,
            Side::BUY, OrderType::LIMIT,
            Price::from_double(100.0), 1, ts));
    }

    // Should have expanded to 2 slabs
    assert(pool.slab_count() == 2);

    // Release all
    for (auto* o : orders) pool.release(o);
    assert(pool.free_count() == pool.capacity());
    std::cout << "  PASS (expanded to " << pool.slab_count() << " slabs)\n";
}

void test_cache_line_padded() {
    std::cout << "[M7] CacheLinePadded false sharing prevention\n";

    CacheLinePadded<std::atomic<uint64_t>> counter_a;
    CacheLinePadded<std::atomic<uint64_t>> counter_b;

    counter_a.get().store(0);
    counter_b.get().store(0);

    // Verify they're on different cache lines
    auto addr_a = reinterpret_cast<uintptr_t>(&counter_a);
    auto addr_b = reinterpret_cast<uintptr_t>(&counter_b);
    assert((addr_a / 64) != (addr_b / 64));

    counter_a->fetch_add(1, std::memory_order_relaxed);
    counter_b->fetch_add(2, std::memory_order_relaxed);

    assert(counter_a->load() == 1);
    assert(counter_b->load() == 2);
    std::cout << "  PASS (a@" << addr_a % 64 << " b@" << addr_b % 64
              << " diff=" << (addr_b - addr_a) << " bytes)\n";
}

void test_no_false_sharing_util() {
    std::cout << "[M7] no_false_sharing compile-time check\n";

    // Fields 0 and 8 share cache line 0
    assert(!no_false_sharing(0, 8));
    // Fields 0 and 64 are on different cache lines
    assert(no_false_sharing(0, 64));
    // Fields 60 and 68 straddle a boundary
    assert(no_false_sharing(60, 68));

    std::cout << "  PASS\n";
}

void bench_pool_vs_new() {
    std::cout << "[M7] Pool vs new/delete throughput comparison\n";

    auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    constexpr int N = 50000;

    // Benchmark: CacheOptimizedPool
    {
        CacheOptimizedPool pool;
        std::vector<Order*> orders(N);

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i)
            orders[i] = pool.acquire(i+1, 1, Side::BUY, OrderType::LIMIT,
                                     Price::from_double(100.0), 1, ts);
        for (int i = 0; i < N; ++i)
            pool.release(orders[i]);
        auto end = std::chrono::high_resolution_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
        std::cout << "  Pool:    " << ns/N << " ns/op  (total " << ns/1000 << " us)\n";
    }

    // Benchmark: raw new/delete
    {
        std::vector<Order*> orders(N);

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; ++i)
            orders[i] = new Order(i+1, 1, Side::BUY, OrderType::LIMIT,
                                  Price::from_double(100.0), 1, ts);
        for (int i = 0; i < N; ++i)
            delete orders[i];
        auto end = std::chrono::high_resolution_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
        std::cout << "  new/del: " << ns/N << " ns/op  (total " << ns/1000 << " us)\n";
    }
}

int main() {
    std::cout << "=== M7 Cache Optimization Tests ===\n";
    test_cache_optimized_pool();
    test_slab_expansion();
    test_cache_line_padded();
    test_no_false_sharing_util();
    bench_pool_vs_new();
    std::cout << "ALL PASS\n\n";
}
