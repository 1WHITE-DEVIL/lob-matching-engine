#include "../src/matching_engine.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>

// ============================================================
// THROUGHPUT BENCHMARK
//
// Measures raw order submission throughput.
// Target: 1,000,000 orders/second
//
// Methodology:
//   - Alternate BUY/SELL at same price → every other order matches
//   - This exercises the full hot path: match + pool release
//   - Timed with high_resolution_clock
// ============================================================

static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

void bench_matching(int N) {
    MatchingEngine eng;
    Price p = Price::from_double(100.0);

    int64_t start = now_ns();

    for (int i = 0; i < N; ++i) {
        if (i % 2 == 0)
            eng.submit(1, Side::SELL, OrderType::LIMIT, p, 1);
        else
            eng.submit(2, Side::BUY,  OrderType::LIMIT, p, 1);
    }

    int64_t elapsed_ns = now_ns() - start;
    double  elapsed_s  = elapsed_ns / 1e9;
    double  ops_per_s  = N / elapsed_s;
    double  ns_per_op  = static_cast<double>(elapsed_ns) / N;

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  Orders    : " << N << "\n";
    std::cout << "  Elapsed   : " << elapsed_ns / 1'000'000 << " ms\n";
    std::cout << "  Throughput: " << ops_per_s << " orders/sec\n";
    std::cout << "  Latency   : " << std::setprecision(1) << ns_per_op << " ns/order\n";
    std::cout << "  Trades    : " << eng.trades().size() << "\n";
}

void bench_no_match(int N) {
    std::cout << "\n[BENCH] No-match path (limit orders resting)\n";
    MatchingEngine eng;

    int64_t start = now_ns();
    for (int i = 0; i < N; ++i) {
        // Bids from 1 to N, asks from N+1 upward — no crossing
        Price p{static_cast<int64_t>(i + 1)};
        eng.submit(1, Side::BUY, OrderType::LIMIT, p, 1);
    }
    int64_t elapsed_ns = now_ns() - start;
    double  ops_per_s  = N / (elapsed_ns / 1e9);
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << ops_per_s << " inserts/sec\n";
    std::cout << "  Book depth: " << eng.bid_depth() << " levels\n";
}

int main() {
    std::cout << "=== Throughput Benchmark ===\n\n";

    std::cout << "[BENCH] Matching path (alternating buy/sell at same price)\n";
    bench_matching(200'000); // conservative to stay within pool size

    bench_no_match(100'000);

    std::cout << "\nTarget: 1,000,000 orders/sec\n";
    std::cout << "Note: run with -O3 and CPU affinity for production numbers.\n";
}
