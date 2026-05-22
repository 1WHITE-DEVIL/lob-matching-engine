#include "../src/order_pool.hpp"
#include "../src/order_book_side.hpp"
#include <cassert>
#include <iostream>
#include <chrono>

static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

// Small pool for tests
OrderPool<1000> pool;

Order* make_order(OrderId oid, Side side, double price, Qty qty) {
    return pool.acquire(oid, 1, side, OrderType::LIMIT,
                        Price::from_double(price), qty, now_ns());
}

void test_price_level_fifo() {
    std::cout << "[M2] PriceLevel FIFO order\n";
    PriceLevel lvl;

    Order* o1 = make_order(1, Side::BUY, 100.0, 100);
    Order* o2 = make_order(2, Side::BUY, 100.0, 200);
    Order* o3 = make_order(3, Side::BUY, 100.0, 300);

    lvl.push_back(o1);
    lvl.push_back(o2);
    lvl.push_back(o3);

    assert(lvl.count() == 3);
    assert(lvl.total_qty() == 600);
    assert(lvl.front()->order_id == 1); // oldest first

    lvl.pop_front();
    assert(lvl.front()->order_id == 2);
    assert(lvl.count() == 2);

    pool.release(o1);
    pool.release(o2);
    pool.release(o3);
    std::cout << "  PASS\n";
}

void test_price_level_remove_middle() {
    std::cout << "[M2] PriceLevel remove middle\n";
    PriceLevel lvl;

    Order* o1 = make_order(10, Side::BUY, 50.0, 100);
    Order* o2 = make_order(11, Side::BUY, 50.0, 100);
    Order* o3 = make_order(12, Side::BUY, 50.0, 100);

    lvl.push_back(o1);
    lvl.push_back(o2);
    lvl.push_back(o3);

    lvl.remove(o2); // cancel middle order
    assert(lvl.count() == 2);
    assert(lvl.front()->order_id == 10);

    lvl.pop_front();
    assert(lvl.front()->order_id == 12);

    pool.release(o1);
    pool.release(o2);
    pool.release(o3);
    std::cout << "  PASS\n";
}

void test_book_side_best_price() {
    std::cout << "[M2] OrderBookSide best price\n";
    OrderPool<1000> p2;
    OrderBookSide bids(Side::BUY);

    auto add = [&](OrderId oid, double price, Qty qty) {
        Order* o = p2.acquire(oid, 1, Side::BUY, OrderType::LIMIT,
                              Price::from_double(price), qty, now_ns());
        bids.add(o);
    };

    add(1, 99.0, 100);
    add(2, 101.0, 100);
    add(3, 100.0, 100);

    // Best bid = highest price
    assert(bids.best_price() == Price::from_double(101.0));
    assert(bids.depth() == 3);

    // Cancel best
    bids.cancel(2);
    assert(bids.best_price() == Price::from_double(100.0));

    std::cout << "  PASS\n";
}

void test_ask_side_best_price() {
    std::cout << "[M2] OrderBookSide ask best price\n";
    OrderPool<1000> p3;
    OrderBookSide asks(Side::SELL);

    auto add = [&](OrderId oid, double price, Qty qty) {
        Order* o = p3.acquire(oid, 1, Side::SELL, OrderType::LIMIT,
                              Price::from_double(price), qty, now_ns());
        asks.add(o);
    };

    add(1, 102.0, 100);
    add(2, 100.5, 100);
    add(3, 103.0, 100);

    // Best ask = lowest price
    assert(asks.best_price() == Price::from_double(100.5));

    std::cout << "  PASS\n";
}

int main() {
    std::cout << "=== M2 Tests ===\n";
    test_price_level_fifo();
    test_price_level_remove_middle();
    test_book_side_best_price();
    test_ask_side_best_price();
    std::cout << "ALL PASS\n\n";
}
