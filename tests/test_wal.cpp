#include "../src/wal.hpp"
#include <cassert>
#include <iostream>
#include <cstdio>

void test_wal_write_read() {
    std::cout << "[M8] WAL write and replay\n";
    const std::string path = "/tmp/test_engine.wal";
    std::remove(path.c_str());

    // Write records
    {
        WalWriter w(path);

        WalNewOrder no{};
        no.order_id    = 1;
        no.user_id     = 42;
        no.price_ticks = 10000;
        no.qty         = 500;
        no.side        = Side::BUY;
        no.type        = OrderType::LIMIT;
        w.log_new_order(no);

        WalCancel c{2, 0};
        w.log_cancel(c);

        WalTrade t{1, 3, 10000, 100, 0};
        w.log_trade(t);

        w.log_checkpoint();
        assert(w.seq() == 4);
    }

    // Read and verify
    {
        WalReader r(path);
        WalReader::Record rec;

        // Record 1: NEW_ORDER
        assert(r.read_next(rec));
        assert(rec.type == WalRecordType::NEW_ORDER);
        assert(rec.new_order.order_id == 1);
        assert(rec.new_order.user_id  == 42);
        assert(rec.new_order.qty      == 500);

        // Record 2: CANCEL
        assert(r.read_next(rec));
        assert(rec.type == WalRecordType::CANCEL);
        assert(rec.cancel.order_id == 2);

        // Record 3: TRADE
        assert(r.read_next(rec));
        assert(rec.type == WalRecordType::TRADE);
        assert(rec.trade.qty == 100);

        // Record 4: CHECKPOINT
        assert(r.read_next(rec));
        assert(rec.type == WalRecordType::CHECKPOINT);

        // EOF
        assert(!r.read_next(rec));
    }

    std::remove(path.c_str());
    std::cout << "  PASS\n";
}

int main() {
    std::cout << "=== M8 WAL Tests ===\n";
    test_wal_write_read();
    std::cout << "ALL PASS\n\n";
}
