#include "../src/matching_engine.hpp"
#include <cassert>
#include <iostream>

void test_simple_match() {
    std::cout << "[M3] Simple full match\n";
    MatchingEngine eng;

    // Resting ask at 100.0
    eng.submit(1, Side::SELL, OrderType::LIMIT, Price::from_double(100.0), 100);
    assert(eng.has_ask());

    // Incoming buy at 100.0 → full match
    eng.submit(2, Side::BUY, OrderType::LIMIT, Price::from_double(100.0), 100);
    assert(!eng.has_ask());
    assert(!eng.has_bid());

    assert(eng.trades().size() == 1);
    assert(eng.trades()[0].qty == 100);
    assert(eng.trades()[0].price == Price::from_double(100.0));
    std::cout << "  PASS\n";
}

void test_partial_fill() {
    std::cout << "[M3] Partial fill\n";
    MatchingEngine eng;

    eng.submit(1, Side::SELL, OrderType::LIMIT, Price::from_double(50.0), 300);
    eng.submit(2, Side::BUY,  OrderType::LIMIT, Price::from_double(50.0), 100);

    // Ask should still be there with 200 remaining
    assert(eng.has_ask());
    assert(eng.best_ask() == Price::from_double(50.0));
    assert(!eng.has_bid()); // buyer fully filled
    assert(eng.trades().size() == 1);
    assert(eng.trades()[0].qty == 100);
    std::cout << "  PASS\n";
}

void test_price_improvement() {
    std::cout << "[M3] Price improvement (buy at higher than ask)\n";
    MatchingEngine eng;

    eng.submit(1, Side::SELL, OrderType::LIMIT, Price::from_double(99.0), 100);
    // Buy at 101 — crosses the 99 ask → executes at 99 (maker's price)
    eng.submit(2, Side::BUY,  OrderType::LIMIT, Price::from_double(101.0), 100);

    assert(!eng.has_ask());
    assert(!eng.has_bid()); // buy remainder rests if any — none here
    assert(eng.trades()[0].price == Price::from_double(99.0)); // maker price
    std::cout << "  PASS\n";
}

void test_no_cross() {
    std::cout << "[M3] No cross — bid < ask\n";
    MatchingEngine eng;

    eng.submit(1, Side::BUY,  OrderType::LIMIT, Price::from_double(99.0), 100);
    eng.submit(2, Side::SELL, OrderType::LIMIT, Price::from_double(101.0), 100);

    assert(eng.has_bid());
    assert(eng.has_ask());
    assert(eng.trades().empty());
    assert(eng.best_bid() == Price::from_double(99.0));
    assert(eng.best_ask() == Price::from_double(101.0));
    std::cout << "  PASS\n";
}

void test_sweep_multiple_levels() {
    std::cout << "[M3] Sweep multiple price levels\n";
    MatchingEngine eng;

    eng.submit(1, Side::SELL, OrderType::LIMIT, Price::from_double(100.0), 100);
    eng.submit(2, Side::SELL, OrderType::LIMIT, Price::from_double(101.0), 100);
    eng.submit(3, Side::SELL, OrderType::LIMIT, Price::from_double(102.0), 100);

    // Big buy sweeps all three levels
    eng.submit(4, Side::BUY, OrderType::LIMIT, Price::from_double(105.0), 300);

    assert(!eng.has_ask());
    assert(!eng.has_bid());
    assert(eng.trades().size() == 3);
    // Trades at 100, 101, 102 respectively
    assert(eng.trades()[0].price == Price::from_double(100.0));
    assert(eng.trades()[1].price == Price::from_double(101.0));
    assert(eng.trades()[2].price == Price::from_double(102.0));
    std::cout << "  PASS\n";
}

void test_fifo_at_same_price() {
    std::cout << "[M3] FIFO at same price level\n";
    MatchingEngine eng;

    // Two sellers at same price — first in, first matched
    eng.submit(1, Side::SELL, OrderType::LIMIT, Price::from_double(100.0), 100);
    eng.submit(2, Side::SELL, OrderType::LIMIT, Price::from_double(100.0), 100);

    eng.submit(3, Side::BUY, OrderType::LIMIT, Price::from_double(100.0), 100);

    // Order 1 should be matched (not order 2)
    assert(eng.trades().size() == 1);
    assert(eng.trades()[0].sell_order_id == 1);
    assert(eng.has_ask()); // order 2 still resting
    std::cout << "  PASS\n";
}

void test_market_order() {
    std::cout << "[M4] Market order\n";
    MatchingEngine eng;

    eng.submit(1, Side::SELL, OrderType::LIMIT, Price::from_double(100.0), 200);
    // Market buy — gets best available price
    eng.submit(2, Side::BUY, OrderType::MARKET, Price::INVALID(), 200);

    assert(!eng.has_ask());
    assert(eng.trades().size() == 1);
    assert(eng.trades()[0].qty == 200);
    std::cout << "  PASS\n";
}

void test_ioc_order() {
    std::cout << "[M4] IOC — partial fill, remainder cancelled\n";
    MatchingEngine eng;

    eng.submit(1, Side::SELL, OrderType::LIMIT, Price::from_double(100.0), 50);
    // IOC buy for 100 — only 50 available → fill 50, cancel rest
    eng.submit(2, Side::BUY, OrderType::IOC, Price::from_double(100.0), 100);

    assert(eng.trades().size() == 1);
    assert(eng.trades()[0].qty == 50);
    assert(!eng.has_bid()); // remainder cancelled, not resting
    assert(!eng.has_ask());
    std::cout << "  PASS\n";
}

void test_cancel() {
    std::cout << "[M4] Cancel resting order\n";
    MatchingEngine eng;

    OrderId oid = eng.submit(1, Side::BUY, OrderType::LIMIT,
                             Price::from_double(100.0), 500);
    assert(eng.has_bid());

    bool ok = eng.cancel(oid);
    assert(ok);
    assert(!eng.has_bid());

    // Cancel non-existent
    assert(!eng.cancel(99999));
    std::cout << "  PASS\n";
}

void test_self_trade_prevention() {
    std::cout << "[M4] Same user both sides (informational — no STP yet)\n";
    // Self-trade prevention is an exchange policy, not always enforced
    // at engine level. Noting this as a known gap for interview.
    std::cout << "  NOTE: STP not implemented (interview talking point)\n";
}

int main() {
    std::cout << "=== M3/M4 Matching Tests ===\n";
    test_simple_match();
    test_partial_fill();
    test_price_improvement();
    test_no_cross();
    test_sweep_multiple_levels();
    test_fifo_at_same_price();
    test_market_order();
    test_ioc_order();
    test_cancel();
    test_self_trade_prevention();
    std::cout << "ALL PASS\n\n";
}
