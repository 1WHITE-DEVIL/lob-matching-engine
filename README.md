# C++17 Limit Order Book Matching Engine

A production-grade **Limit Order Book (LOB) Matching Engine** written in C++17.  
Built from scratch with HFT-grade design decisions: fixed-point arithmetic, cache-line alignment, lock-free queues, slab allocation, and WAL persistence.

---

## What Is a Limit Order Book?

Every exchange — NSE, NYSE, Binance — runs a matching engine at its core. The LOB maintains a two-sided sorted book of resting orders and matches incoming orders when prices cross.

```
       BIDS (buyers)          ASKS (sellers)
       ─────────────          ──────────────
Price  Qty                    Price  Qty
101    200   ← best bid       102    150  ← best ask
100    500                    103    300
 99    800                    104    100
```

A buy order at ≥ 102 crosses the spread and triggers a trade. The engine enforces **price-time priority**: best price first, then FIFO within the same price level.

---

## Quick Start

```bash
make test    # build and run all tests
make bench   # latency benchmark
make clean   # remove build artifacts
```

**Prerequisites:** g++ with C++17 support.
- Ubuntu/Debian: `sudo apt install g++ make`
- macOS: `xcode-select --install`

---

## Architecture

```
src/
  types.hpp           M1    Fixed-point Price, enums, type aliases
  order.hpp           M1    Cache-line aligned Order struct (128 bytes)
  order_pool.hpp      M2    Heap slab allocator — O(1) alloc/free
  price_level.hpp     M2    Intrusive FIFO doubly-linked list per price
  order_book_side.hpp M2    std::map<ticks, PriceLevel> — one side of book
  trade.hpp           M4    Immutable trade record
  matching_engine.hpp M3/M4 Full engine: LIMIT/MARKET/IOC/FOK
  lockfree_queue.hpp  M6    SPSC and MPSC lock-free ring buffers
  threaded_engine.hpp M6    Matching thread + lock-free order ingestion
  cache_utils.hpp     M7    CacheOptimizedPool, CacheLinePadded, prefetch
  wal.hpp             M8    Binary Write-Ahead Log with fdatasync
  engine_with_wal.hpp M8    MatchingEngine + WAL persistence wrapper

tests/
  test_m1.cpp         Order struct, fixed-point price
  test_m2.cpp         PriceLevel FIFO, OrderBookSide best-price
  test_m3_m4.cpp      Matching logic, all order types, cancel
  test_m6.cpp         SPSC/MPSC queues, threaded engine
  test_m7.cpp         Slab pool, false sharing, prefetch benchmark
  test_wal.cpp        WAL write and replay

bench/
  bench_throughput.cpp  Raw throughput measurement
  bench_latency.cpp     Per-operation latency distribution
```

---

## Benchmark Results

> Environment: WSL2, Linux, compiled with `-O3 -march=native`, CPU affinity pinned via `taskset -c 2`.  
> All numbers are **median of 3 runs**. Single-run numbers are noise.

### Throughput

| Path | Median Throughput | Median Latency |
|------|------------------|----------------|
| Matching (alternating buy/sell, every order crosses) | **~10.4M orders/sec** | ~96 ns/order |
| Insert only (resting limit orders, no match) | **~4.9M inserts/sec** | — |
| Aggregate throughput | **~11.5M orders/sec** | ~87 ns/order |
| Pool alloc vs raw new/delete | 75 ns vs 190 ns | **2.5x faster** |

### Latency Distribution (median of 3 runs)

| Operation | P50 | P99 | P99.9 |
|-----------|-----|-----|-------|
| Limit order insert (no match) | **72 ns** | 157 ns | 3,462 ns |
| Limit order match (crosses book) | **72 ns** | 100 ns | 1,758 ns |
| Cancel | **57 ns** | 199 ns | 495 ns |
| Market order (sweeps 10 levels) | **2,470 ns** | 5,743 ns | 30,833 ns |

**Reading the numbers:**
- P50 = what a typical order experiences
- P99 = 1 in 100 orders hits this — risk systems must handle it
- P99.9 spikes on market sweep = OS jitter, not engine overhead

Run `make bench` for numbers on your hardware.

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Price type | `int64_t` ticks | `double` fails exact equality: `0.1+0.2 != 0.3` |
| Order size | 128 bytes (2 cache lines) | Hot fields in line 0; ptrs in line 1 |
| Alignment | `alignas(64)` | Pin to cache line boundary; never straddle |
| Book structure | `std::map<ticks, PriceLevel>` | Ordered: O(log n), `begin()`/`rbegin()` = best price |
| Order queue | Intrusive doubly-linked list | Zero heap alloc per node; O(1) remove |
| Allocation | Slab pool (heap) | Contiguous memory, O(1), no fragmentation |
| Thread model | SPSC queue → matching thread | Network thread never touches book directly |
| Persistence | Binary WAL + fdatasync | Crash-safe; replay reconstructs exact state |
| False sharing | `CacheLinePadded<T>` | Producer/consumer counters on separate cache lines |

---

## Order Types Supported

| Type | Behavior |
|------|----------|
| `LIMIT` | Rests in book if not immediately matchable |
| `MARKET` | Sweeps book at any price; remainder cancelled |
| `IOC` (Immediate or Cancel) | Fills what it can immediately; remainder cancelled |
| `FOK` (Fill or Kill) | Fills entirely or rejected outright — never partially rests |

---

## Module Map

| Module | Status | What it covers |
|--------|--------|----------------|
| M1 | ✅ | Order struct, fixed-point price, cache alignment |
| M2 | ✅ | Single-side book, price level, order pool |
| M3 | ✅ | Matching logic: partial fills, FIFO, price sweep |
| M4 | ✅ | Full book, trade reports, MARKET/IOC/FOK/cancel |
| M5 | ✅ | Performance baseline (bench_throughput) |
| M6 | ✅ | Lock-free SPSC/MPSC queues, threaded engine, ABA discussion |
| M7 | ✅ | Cache-optimized slab pool, prefetch, false sharing utils |
| M8 | ✅ | Write-Ahead Log, crash recovery, fdatasync |
| M9 | ⬜ | Full architecture review + mock interview |

---

## Known Gaps

These are intentional omissions, not oversights. Each is a real production design decision:

- **Self-trade prevention (STP):** Exchange policy, not always enforced at engine level. Production engines tag orders with firm/trader IDs and reject crosses within the same firm.
- **FOK liquidity check:** Current implementation is optimistic. A production FOK must walk all crossing levels and sum available qty read-only before touching the book.
- **WAL group commit:** Current implementation syncs per-record. Production uses group commit — batch N records, one `fdatasync` — trading durability granularity for throughput.
- **Order amendment:** Not implemented. Production engines support price/qty modification: cancel + reinsert loses time priority; in-place qty reduction preserves it.
- **Array-based book:** For production HFT, price level container would be a sorted array or skip list for cache-friendly sequential access. `std::map` is correct and sufficient here.

---

## Interview Talking Points

1. **Why not `double` for price?** `0.1 + 0.2 != 0.3` in IEEE 754. Exact integer ticks required for order matching — one rounding error in a fill = compliance violation.
2. **Why `std::map` not `unordered_map`?** Need ordering — `begin()`/`rbegin()` gives best price O(1). `unordered_map` is O(n) to find min/max and has unpredictable hash collision latency spikes.
3. **Why intrusive list?** `std::list` = heap alloc per node = pointer chasing = cache miss per traversal. Intrusive embeds pointers in Order; pool keeps them contiguous in memory.
4. **Why SPSC not mutex?** Mutex = kernel syscall on contention = microseconds. SPSC = pure userspace acquire-release ring buffer = nanoseconds. No CAS loop needed — single producer, single consumer means no contention by design.
5. **What is false sharing?** Two threads write to different variables that share a cache line. Each write invalidates the other core's cache line — "cache ping-pong". Fix: `alignas(64)` padding between hot variables.
6. **What is the ABA problem?** Thread reads ptr X, another thread pops X and pushes new node at same address, first thread's CAS succeeds incorrectly. Fix: tagged pointers or epoch-based reclamation.
7. **Why WAL before state change?** Crash after state change but before WAL write = lost order with no recovery path. WAL-first = always recoverable by replay.
8. **Why does throughput vary between runs?** CPU frequency scaling, OS timer interrupts, cold vs warm cache, `std::vector<Trade>` reallocation. P50 latency (~72 ns) is stable. Throughput numbers are order-of-magnitude: read as "10M+ orders/sec", not a precise figure.

---

## What I Built and Why

This project was built as deep preparation for HFT engineering roles. Every decision was made through the lens of: *"why this data structure, why this memory layout, what breaks at 1M orders/sec."*

The progression M1 → M8 mirrors how a real trading system is built: correctness first, then measurement, then optimization, then persistence. Skipping that order produces fast code that is wrong, or correct code you cannot defend under load.

---

## Author

**Aditya** — Final year CS (AI), BIT Bhilai  
[LinkedIn](https://www.linkedin.com/in/aditya-gupta-74b6b7171) · [GitHub](https://github.com/1WHITE-DEVIL)
