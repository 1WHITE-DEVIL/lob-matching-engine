#pragma once
#include <atomic>
#include <cstddef>
#include <optional>
#include <new>

// ============================================================
// M6 — LOCK-FREE SINGLE-PRODUCER SINGLE-CONSUMER (SPSC) QUEUE
//
// USE CASE IN THIS ENGINE:
//   Network thread receives orders → pushes to queue
//   Matching thread consumes orders → processes them
//   These are the only two threads touching this queue.
//   SPSC = no contention → no CAS loops → fastest possible.
//
// WHY NOT std::mutex + std::queue?
//   Mutex = kernel syscall on contention = microseconds of latency.
//   At 10M+ orders/sec you cannot afford mutex overhead.
//   Lock-free = pure userspace, no kernel involvement.
//
// ALGORITHM: Ring buffer with head/tail indices
//   Producer writes at tail, advances tail.
//   Consumer reads at head, advances head.
//   No locks because producer only writes tail,
//   consumer only writes head — no shared write location.
//
// MEMORY ORDERING:
//   tail_.store(release) — all writes before this are visible
//   tail_.load(acquire)  — all writes before the paired release
//                          are visible to this thread
//   This is the acquire-release pairing that makes lock-free safe.
//
// FALSE SHARING PREVENTION:
//   head_ and tail_ are on SEPARATE cache lines (alignas(64)).
//   If they shared a cache line, every producer write (tail_)
//   would invalidate the consumer's cache line containing head_,
//   and vice versa — "false sharing" = cache ping-pong between cores.
//
// CAPACITY must be power of 2 for cheap modulo via bitmask.
// ============================================================

template<typename T, std::size_t CAPACITY>
class SPSCQueue {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0,
        "CAPACITY must be a power of 2");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Producer: push item. Returns false if queue is full.
    // Called from ONE thread only.
    bool push(const T& item) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & MASK;

        // Check if full: next write position == head (consumer side)
        if (next == head_.load(std::memory_order_acquire))
            return false; // full

        slots_[tail] = item;

        // Release: make the slot write visible to consumer
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: pop item. Returns empty optional if queue is empty.
    // Called from ONE thread only.
    std::optional<T> pop() {
        const std::size_t head = head_.load(std::memory_order_relaxed);

        // Check if empty: head == tail (nothing written yet)
        if (head == tail_.load(std::memory_order_acquire))
            return std::nullopt; // empty

        T item = slots_[head];

        // Release: advance head, making slot available to producer
        head_.store((head + 1) & MASK, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    std::size_t size() const {
        std::size_t tail = tail_.load(std::memory_order_acquire);
        std::size_t head = head_.load(std::memory_order_acquire);
        return (tail - head) & MASK;
    }

    static constexpr std::size_t capacity() { return CAPACITY; }

private:
    static constexpr std::size_t MASK = CAPACITY - 1;

    // Slots buffer — plain array, no atomic needed on elements
    // because producer/consumer never access the same slot simultaneously
    T slots_[CAPACITY];

    // head_ and tail_ on SEPARATE cache lines to prevent false sharing.
    // Without this, both atomics share a 64-byte cache line.
    // Producer writes tail_ → invalidates cache line on consumer core.
    // Consumer writes head_ → invalidates cache line on producer core.
    // This is "false sharing": two logically independent variables
    // fighting over the same physical cache line.
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
};


// ============================================================
// LOCK-FREE MPSC QUEUE (Multi-Producer, Single-Consumer)
//
// USE CASE: Multiple network threads push orders;
//           one matching thread consumes.
//
// ALGORITHM: Michael-Scott queue variant using CAS on tail.
//   Each producer does a CAS to claim the next tail slot.
//   Only one succeeds; others retry.
//
// ABA PROBLEM:
//   Thread A reads tail = ptr X.
//   Thread B pops X, pushes new node, tail = X again (same addr).
//   Thread A's CAS succeeds incorrectly — it sees X and thinks
//   nothing changed, but the queue state has changed.
//
//   SOLUTION OPTIONS:
//   1. Tagged pointers: pack a version counter into pointer bits
//      (works on 64-bit where top 16 bits are unused)
//   2. Hazard pointers: track which pointers threads are reading
//   3. Use epoch-based reclamation (most production systems)
//
//   HERE: We use a simpler index-based ring buffer for MPSC
//   which avoids ABA entirely — indices only increment, never reuse.
// ============================================================

template<typename T, std::size_t CAPACITY>
class MPSCQueue {
    static_assert((CAPACITY & (CAPACITY - 1)) == 0,
        "CAPACITY must be a power of 2");

    struct Slot {
        std::atomic<std::size_t> sequence;
        T                        data;
    };

public:
    MPSCQueue() {
        for (std::size_t i = 0; i < CAPACITY; ++i)
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    // Producer (multiple threads OK): CAS-based claim of slot
    bool push(const T& item) {
        Slot*       slot;
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            slot = &slots_[pos & (CAPACITY - 1)];
            std::size_t seq = slot->sequence.load(std::memory_order_acquire);
            intptr_t diff   = static_cast<intptr_t>(seq)
                            - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Slot is ready: try to claim it with CAS
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed))
                    break; // claimed
                // CAS failed: another producer got here first, retry
            } else if (diff < 0) {
                return false; // queue full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        slot->data = item;
        // Signal consumer: slot is ready to read
        slot->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // Consumer (single thread): dequeue next item
    std::optional<T> pop() {
        Slot*       slot;
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        slot = &slots_[pos & (CAPACITY - 1)];
        std::size_t seq = slot->sequence.load(std::memory_order_acquire);
        intptr_t diff   = static_cast<intptr_t>(seq)
                        - static_cast<intptr_t>(pos + 1);

        if (diff < 0) return std::nullopt; // empty

        T item = slot->data;
        dequeue_pos_.store(pos + 1, std::memory_order_relaxed);
        // Mark slot as available for next producer wrap-around
        slot->sequence.store(pos + CAPACITY, std::memory_order_release);
        return item;
    }

    bool empty() const {
        std::size_t pos  = dequeue_pos_.load(std::memory_order_relaxed);
        const Slot* slot = &slots_[pos & (CAPACITY - 1)];
        std::size_t seq  = slot->sequence.load(std::memory_order_acquire);
        return static_cast<intptr_t>(seq) -
               static_cast<intptr_t>(pos + 1) < 0;
    }

private:
    // Slots on their own cache region
    Slot slots_[CAPACITY];

    alignas(64) std::atomic<std::size_t> enqueue_pos_;
    alignas(64) std::atomic<std::size_t> dequeue_pos_;
};
