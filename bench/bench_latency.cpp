#include "../src/matching_engine.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

// ============================================================
// LATENCY DISTRIBUTION BENCHMARK
//
// Measures per-operation latency, not just aggregate throughput.
// Throughput hides tail latency — a 99th percentile spike of 10us
// in a 1M/sec engine means 10,000 orders/sec are getting 10us hits.
// That matters in HFT.
//
// METRICS:
//   Min, Max       — absolute bounds
//   P50 (median)   — typical case
//   P95            — most orders land here
//   P99            — tail latency (what matters for risk systems)
//   P99.9          — extreme tail (hardware/OS jitter shows up here)
//   Avg            — mean (misleading if distribution is skewed)
//   Stddev         — spread
//
// WHY NOT JUST AVERAGE?
//   Average hides bimodal distributions. If 99% of orders take 80ns
//   and 1% take 10us, average = ~180ns. P99 = 10us tells the real story.
//
// SENTINEL PRICE HANDLING:
//   Market orders use INT64_MAX/0 as sentinel prices internally.
//   We never display raw book state after market orders — only
//   display best bid/ask when the book actually has resting orders.
// ============================================================

static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

struct LatencyStats {
    double min_ns;
    double max_ns;
    double avg_ns;
    double stddev_ns;
    double p50_ns;
    double p95_ns;
    double p99_ns;
    double p999_ns;
    std::size_t count;
};

LatencyStats compute_stats(std::vector<int64_t>& samples) {
    std::sort(samples.begin(), samples.end());

    LatencyStats s{};
    s.count   = samples.size();
    s.min_ns  = static_cast<double>(samples.front());
    s.max_ns  = static_cast<double>(samples.back());

    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    s.avg_ns  = sum / s.count;

    double sq_sum = 0;
    for (auto v : samples) {
        double diff = v - s.avg_ns;
        sq_sum += diff * diff;
    }
    s.stddev_ns = std::sqrt(sq_sum / s.count);

    auto percentile = [&](double p) -> double {
        std::size_t idx = static_cast<std::size_t>(p / 100.0 * s.count);
        if (idx >= s.count) idx = s.count - 1;
        return static_cast<double>(samples[idx]);
    };

    s.p50_ns  = percentile(50.0);
    s.p95_ns  = percentile(95.0);
    s.p99_ns  = percentile(99.0);
    s.p999_ns = percentile(99.9);

    return s;
}

void print_stats(const LatencyStats& s, const std::string& label) {
    auto ns_to_us = [](double ns) { return ns / 1000.0; };

    std::cout << "\n" << label << "\n";
    std::cout << std::string(56, '-') << "\n";
    std::cout << std::fixed;

    std::cout << "  Samples:  " << s.count << "\n";
    std::cout << "  Min:      " << std::setw(10) << std::setprecision(1)
              << s.min_ns << " ns\n";
    std::cout << "  Avg:      " << std::setw(10) << std::setprecision(1)
              << s.avg_ns << " ns  ("
              << std::setprecision(3) << ns_to_us(s.avg_ns) << " us)\n";
    std::cout << "  Stddev:   " << std::setw(10) << std::setprecision(1)
              << s.stddev_ns << " ns\n";
    std::cout << "  P50:      " << std::setw(10) << std::setprecision(1)
              << s.p50_ns  << " ns\n";
    std::cout << "  P95:      " << std::setw(10) << std::setprecision(1)
              << s.p95_ns  << " ns\n";
    std::cout << "  P99:      " << std::setw(10) << std::setprecision(1)
              << s.p99_ns  << " ns\n";
    std::cout << "  P99.9:    " << std::setw(10) << std::setprecision(1)
              << s.p999_ns << " ns\n";
    std::cout << "  Max:      " << std::setw(10) << std::setprecision(1)
              << s.max_ns  << " ns\n";
}

// ============================================================
// TEST 1: Order insertion latency (no match — resting limit orders)
// ============================================================
void bench_insert_latency(int N) {
    MatchingEngine eng;
    std::vector<int64_t> samples;
    samples.reserve(N);

    for (int i = 0; i < N; ++i) {
        // Spread prices so no order ever crosses — pure insertion path
        // Buy side: prices 1..N/2, sell side: prices N/2+1..N
        // This prevents any matching from occurring
        Price p{static_cast<int64_t>(i + 1)};
        Side  s = (i % 2 == 0) ? Side::BUY : Side::SELL;

        int64_t t0 = now_ns();
        eng.submit(1, s, OrderType::LIMIT, p, 1);
        int64_t t1 = now_ns();

        samples.push_back(t1 - t0);
    }

    auto stats = compute_stats(samples);
    print_stats(stats, "TEST 1: Insert Latency — resting limit orders (" +
                       std::to_string(N) + " orders, no match)");
}

// ============================================================
// TEST 2: Matching latency (alternating buy/sell — every order matches)
// ============================================================
void bench_match_latency(int N) {
    MatchingEngine eng;
    std::vector<int64_t> samples;
    samples.reserve(N);

    Price p = Price::from_double(100.0);

    // Pre-seed one resting order so first aggressor always has something to hit
    eng.submit(1, Side::SELL, OrderType::LIMIT, p, 1);

    for (int i = 0; i < N; ++i) {
        Side aggressor = (i % 2 == 0) ? Side::BUY : Side::SELL;
        Side resting   = (aggressor == Side::BUY) ? Side::SELL : Side::BUY;

        int64_t t0 = now_ns();
        eng.submit(2, aggressor, OrderType::LIMIT, p, 1);
        int64_t t1 = now_ns();

        samples.push_back(t1 - t0);

        // Re-seed resting side for next iteration
        eng.submit(1, resting, OrderType::LIMIT, p, 1);
    }

    auto stats = compute_stats(samples);
    print_stats(stats, "TEST 2: Match Latency — every order crosses (" +
                       std::to_string(N) + " orders)");
}

// ============================================================
// TEST 3: Cancel latency
// ============================================================
void bench_cancel_latency(int N) {
    MatchingEngine eng;
    std::vector<int64_t> samples;
    samples.reserve(N);

    // Pre-insert N orders, capture their IDs
    std::vector<OrderId> ids;
    ids.reserve(N);
    for (int i = 0; i < N; ++i) {
        Price p{static_cast<int64_t>(i + 1)};
        OrderId oid = eng.submit(1, Side::BUY, OrderType::LIMIT, p, 1);
        ids.push_back(oid);
    }

    // Now measure cancel latency
    for (int i = 0; i < N; ++i) {
        int64_t t0 = now_ns();
        eng.cancel(ids[i]);
        int64_t t1 = now_ns();
        samples.push_back(t1 - t0);
    }

    auto stats = compute_stats(samples);
    print_stats(stats, "TEST 3: Cancel Latency (" +
                       std::to_string(N) + " cancels)");
}

// ============================================================
// TEST 4: Market order latency (sweep)
// ============================================================
void bench_market_latency(int N) {
    std::vector<int64_t> samples;
    samples.reserve(N);

    for (int i = 0; i < N; ++i) {
        // Fresh engine each time — pre-seed 10 resting asks to sweep
        MatchingEngine eng;
        for (int j = 0; j < 10; ++j) {
            Price p = Price::from_double(100.0 + j);
            eng.submit(1, Side::SELL, OrderType::LIMIT, p, 1);
        }

        int64_t t0 = now_ns();
        eng.submit(2, Side::BUY, OrderType::MARKET, Price::INVALID(), 10);
        int64_t t1 = now_ns();

        samples.push_back(t1 - t0);
    }

    auto stats = compute_stats(samples);
    print_stats(stats, "TEST 4: Market Order Latency — sweeps 10 levels (" +
                       std::to_string(N) + " samples)");
}

// ============================================================
// TEST 5: Aggregate throughput (same as bench_throughput for comparison)
// ============================================================
void bench_throughput_summary(int N) {
    MatchingEngine eng;
    Price p = Price::from_double(100.0);

    int64_t start = now_ns();
    for (int i = 0; i < N; ++i) {
        if (i % 2 == 0)
            eng.submit(1, Side::SELL, OrderType::LIMIT, p, 1);
        else
            eng.submit(2, Side::BUY,  OrderType::LIMIT, p, 1);
    }
    int64_t elapsed = now_ns() - start;

    double ops_per_s = N / (elapsed / 1e9);
    double ns_per_op = static_cast<double>(elapsed) / N;

    std::cout << "\nTEST 5: Aggregate Throughput\n";
    std::cout << std::string(56, '-') << "\n";
    std::cout << "  Orders:     " << N << "\n";
    std::cout << "  Elapsed:    " << elapsed / 1'000'000 << " ms\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  Throughput: " << ops_per_s << " orders/sec\n";
    std::cout << std::setprecision(1);
    std::cout << "  Avg:        " << ns_per_op << " ns/order\n";
    std::cout << "  Trades:     " << eng.trades().size() << "\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  MATCHING ENGINE — LATENCY & THROUGHPUT BENCHMARK\n";
    std::cout << "========================================================\n";
    std::cout << "Note: run with -O3 -march=native and CPU affinity\n";
    std::cout << "      for production-representative numbers.\n";
    std::cout << "      First run may show higher latency (cold cache).\n";

    bench_insert_latency(10'000);
    bench_match_latency(10'000);
    bench_cancel_latency(10'000);
    bench_market_latency(1'000);
    bench_throughput_summary(200'000);

    std::cout << "\n========================================================\n";
    std::cout << "  ALL BENCHMARKS COMPLETE\n";
    std::cout << "========================================================\n";

    return 0;
}
