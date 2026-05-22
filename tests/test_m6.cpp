#include "../src/lockfree_queue.hpp"
#include "../src/threaded_engine.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

// ============================================================
// SPSC QUEUE TESTS
// ============================================================

void test_spsc_basic() {
    std::cout << "[M6] SPSC basic push/pop\n";
    SPSCQueue<int, 16> q;

    assert(q.empty());
    assert(q.push(42));
    assert(!q.empty());

    auto v = q.pop();
    assert(v.has_value());
    assert(*v == 42);
    assert(q.empty());
    std::cout << "  PASS\n";
}

void test_spsc_full() {
    std::cout << "[M6] SPSC full queue rejects push\n";
    SPSCQueue<int, 4> q;

    // capacity=4 but ring buffer uses one slot as sentinel → 3 usable
    assert(q.push(1));
    assert(q.push(2));
    assert(q.push(3));
    assert(!q.push(4)); // full
    std::cout << "  PASS\n";
}

void test_spsc_order_preserved() {
    std::cout << "[M6] SPSC FIFO order preserved\n";
    SPSCQueue<int, 64> q;

    for (int i = 0; i < 10; ++i) q.push(i);
    for (int i = 0; i < 10; ++i) {
        auto v = q.pop();
        assert(v.has_value() && *v == i);
    }
    std::cout << "  PASS\n";
}

void test_spsc_threaded() {
    std::cout << "[M6] SPSC producer/consumer threads\n";
    SPSCQueue<int, 1024> q;
    constexpr int N = 500;
    std::atomic<int> sum_produced{0}, sum_consumed{0};

    std::thread producer([&]{
        for (int i = 1; i <= N; ++i) {
            while (!q.push(i)) std::this_thread::yield();
            sum_produced.fetch_add(i, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&]{
        int count = 0;
        while (count < N) {
            auto v = q.pop();
            if (v) {
                sum_consumed.fetch_add(*v, std::memory_order_relaxed);
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    assert(sum_produced.load() == sum_consumed.load());
    std::cout << "  PASS (sum=" << sum_consumed.load() << ")\n";
}

// ============================================================
// MPSC QUEUE TESTS
// ============================================================

void test_mpsc_basic() {
    std::cout << "[M6] MPSC basic push/pop\n";
    MPSCQueue<int, 64> q;

    assert(q.push(10));
    assert(q.push(20));
    auto v1 = q.pop(); assert(v1 && *v1 == 10);
    auto v2 = q.pop(); assert(v2 && *v2 == 20);
    assert(q.empty());
    std::cout << "  PASS\n";
}

void test_mpsc_multi_producer() {
    std::cout << "[M6] MPSC multiple producers, single consumer\n";
    MPSCQueue<int, 4096> q;
    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 100;
    std::atomic<int> total_consumed{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&q, p]{
            for (int i = 0; i < PER_PRODUCER; ++i) {
                while (!q.push(p * 1000 + i))
                    std::this_thread::yield();
            }
        });
    }

    std::thread consumer([&]{
        int count = 0;
        while (count < PRODUCERS * PER_PRODUCER) {
            auto v = q.pop();
            if (v) { ++count; total_consumed.fetch_add(1, std::memory_order_relaxed); }
            else   std::this_thread::yield();
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    assert(total_consumed.load() == PRODUCERS * PER_PRODUCER);
    std::cout << "  PASS (" << total_consumed.load() << " items consumed)\n";
}

// ============================================================
// THREADED ENGINE TEST
// ============================================================

void test_threaded_engine() {
    std::cout << "[M6] ThreadedMatchingEngine submit/stop\n";

    std::atomic<int> trade_count{0};

    ThreadedMatchingEngine eng([&](const Trade&){
        trade_count.fetch_add(1, std::memory_order_relaxed);
    });

    eng.start();

    // Submit matching pairs
    for (int i = 0; i < 100; ++i) {
        Price p = Price::from_double(100.0);
        while (!eng.submit(1, Side::SELL, OrderType::LIMIT, p, 1))
            std::this_thread::yield();
        while (!eng.submit(2, Side::BUY,  OrderType::LIMIT, p, 1))
            std::this_thread::yield();
    }

    // Give matching thread time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    eng.stop();

    std::cout << "  Orders processed: " << eng.orders_processed() << "\n";
    std::cout << "  Trades generated: " << eng.trades_generated() << "\n";
    assert(eng.orders_processed() == 200);
    assert(eng.trades_generated() == 100);
    std::cout << "  PASS\n";
}

int main() {
    std::cout << "=== M6 Lock-Free Tests ===\n";
    test_spsc_basic();
    test_spsc_full();
    test_spsc_order_preserved();
    test_spsc_threaded();
    test_mpsc_basic();
    test_mpsc_multi_producer();
    test_threaded_engine();
    std::cout << "ALL PASS\n\n";
}
