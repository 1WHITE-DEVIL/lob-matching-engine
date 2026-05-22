#pragma once
#include "matching_engine.hpp"
#include "lockfree_queue.hpp"
#include <atomic>
#include <thread>
#include <functional>

// ============================================================
// M6 — THREADED MATCHING ENGINE
//
// ARCHITECTURE:
//
//   [Network Thread(s)]  →  [OrderQueue SPSC/MPSC]  →  [Matching Thread]
//                                                            ↓
//                                                     [TradeQueue SPSC]
//                                                            ↓
//                                                    [Reporter Thread]
//
// WHY THIS SEPARATION:
//   Matching must never block on I/O or network.
//   A single dedicated matching thread owns the order book exclusively —
//   no locks needed inside the matching engine itself.
//   All cross-thread communication is through lock-free queues.
//
// ORDER COMMAND — what goes into the queue
// ============================================================

enum class CommandType : uint8_t {
    SUBMIT = 0,
    CANCEL = 1,
    STOP   = 2   // sentinel to shut down matching thread
};

struct OrderCommand {
    CommandType type    {CommandType::SUBMIT};
    UserId      user_id {0};
    Side        side    {Side::BUY};
    OrderType   order_type {OrderType::LIMIT};
    Price       price   {Price::INVALID()};
    Qty         qty     {0};
    OrderId     order_id{0};   // used for CANCEL commands
};

// ============================================================
// THREADED ENGINE
//
// Single matching thread owns the book.
// External threads submit via lock-free queue.
// ============================================================

class ThreadedMatchingEngine {
public:
    static constexpr std::size_t QUEUE_CAPACITY = 1 << 16; // 65536

    using TradeCallback = MatchingEngine::TradeCallback;

    explicit ThreadedMatchingEngine(TradeCallback cb = nullptr)
        : engine_(std::move(cb))
        , running_(false)
    {}

    ~ThreadedMatchingEngine() { stop(); }

    // Start the matching thread
    void start() {
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this]{ run(); });
    }

    // Stop the matching thread (blocks until it exits)
    void stop() {
        if (running_.load(std::memory_order_acquire)) {
            OrderCommand cmd;
            cmd.type = CommandType::STOP;
            // Spin until we can enqueue STOP (queue might be full)
            while (!queue_.push(cmd))
                std::this_thread::yield();
            if (thread_.joinable()) thread_.join();
            running_.store(false, std::memory_order_release);
        }
    }

    // Submit order from any thread. Returns false if queue full.
    bool submit(UserId uid, Side side, OrderType ot,
                Price price, Qty qty) {
        OrderCommand cmd;
        cmd.type       = CommandType::SUBMIT;
        cmd.user_id    = uid;
        cmd.side       = side;
        cmd.order_type = ot;
        cmd.price      = price;
        cmd.qty        = qty;
        return queue_.push(cmd);
    }

    // Cancel from any thread
    bool cancel(OrderId oid) {
        OrderCommand cmd;
        cmd.type     = CommandType::CANCEL;
        cmd.order_id = oid;
        return queue_.push(cmd);
    }

    bool is_running() const {
        return running_.load(std::memory_order_acquire);
    }

    // Stats — safe to read from any thread (approximate)
    uint64_t orders_processed() const {
        return orders_processed_.load(std::memory_order_relaxed);
    }
    uint64_t trades_generated() const {
        return trades_generated_.load(std::memory_order_relaxed);
    }

private:
    // Matching thread main loop
    void run() {
        while (true) {
            auto cmd_opt = queue_.pop();
            if (!cmd_opt) {
                // Queue empty — spin/yield
                // Production: use busy-spin with _mm_pause() for lower latency
                std::this_thread::yield();
                continue;
            }

            const OrderCommand& cmd = *cmd_opt;

            if (cmd.type == CommandType::STOP) break;

            if (cmd.type == CommandType::SUBMIT) {
                engine_.submit(cmd.user_id, cmd.side, cmd.order_type,
                               cmd.price, cmd.qty);
                orders_processed_.fetch_add(1, std::memory_order_relaxed);
                trades_generated_.store(
                    engine_.trades().size(),
                    std::memory_order_relaxed);
            } else if (cmd.type == CommandType::CANCEL) {
                engine_.cancel(cmd.order_id);
            }
        }
    }

    // SPSC queue: one producer (caller) → one consumer (matching thread)
    // For multi-producer: swap to MPSCQueue
    SPSCQueue<OrderCommand, QUEUE_CAPACITY> queue_;

    MatchingEngine engine_;
    std::thread    thread_;

    // Atomics for cross-thread stats (relaxed — approximate is fine)
    std::atomic<uint64_t> orders_processed_{0};
    std::atomic<uint64_t> trades_generated_{0};
    std::atomic<bool>     running_;
};
