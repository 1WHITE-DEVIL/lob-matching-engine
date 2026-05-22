#include "../src/types.hpp"
#include "../src/order.hpp"
#include <cassert>
#include <iostream>
#include <chrono>

static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

void test_price() {
    std::cout << "[M1] Fixed-point price\n";
    Price p1 = Price::from_double(100.10);
    Price p2 = Price::from_double(100.10);
    assert(p1 == p2);
    assert(p1.ticks == 10010);

    Price p3 = Price::from_double(99.99);
    assert(p3 < p1);
    assert(p1 > p3);

    assert(!Price::INVALID().is_valid());
    std::cout << "  PASS\n";
}

void test_order_layout() {
    std::cout << "[M1] Order layout\n";
    assert(sizeof(Order)  == 128);
    assert(alignof(Order) == 64);
    std::cout << "  sizeof=" << sizeof(Order)
              << " alignof=" << alignof(Order) << " PASS\n";
}

void test_order_fill() {
    std::cout << "[M1] Order fill\n";
    Order o{1, 1, Side::SELL, OrderType::LIMIT,
            Price::from_double(50.0), 1000, now_ns()};

    Qty f = o.fill(300);
    assert(f == 300);
    assert(o.status == OrderStatus::PARTIALLY_FILLED);
    assert(o.leaves_qty() == 700);

    f = o.fill(700);
    assert(f == 700);
    assert(o.is_fully_filled());
    assert(o.status == OrderStatus::FILLED);

    // Overfill protection
    Order o2{2,1,Side::BUY,OrderType::LIMIT,Price::from_double(10.0),100,now_ns()};
    f = o2.fill(999);
    assert(f == 100);
    assert(o2.is_fully_filled());
    std::cout << "  PASS\n";
}

int main() {
    std::cout << "=== M1 Tests ===\n";
    test_price();
    test_order_layout();
    test_order_fill();
    std::cout << "ALL PASS\n\n";
}
