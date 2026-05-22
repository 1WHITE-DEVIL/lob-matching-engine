CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I src
OPT      := -O2
FAST     := -O3 -march=native

BUILD    := build
TESTS    := $(BUILD)/test_m1 \
            $(BUILD)/test_m2 \
            $(BUILD)/test_m3_m4 \
            $(BUILD)/test_m6 \
            $(BUILD)/test_m7 \
            $(BUILD)/test_wal

.PHONY: all test bench clean

all: $(TESTS) $(BUILD)/bench_latency

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_m1: tests/test_m1.cpp src/types.hpp src/order.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(OPT) -o $@ $

$(BUILD)/test_m2: tests/test_m2.cpp src/order_pool.hpp src/order_book_side.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(OPT) -o $@ $

$(BUILD)/test_m3_m4: tests/test_m3_m4.cpp src/matching_engine.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(OPT) -o $@ $

$(BUILD)/test_m6: tests/test_m6.cpp src/lockfree_queue.hpp src/threaded_engine.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(OPT) -pthread -o $@ $

$(BUILD)/test_m7: tests/test_m7.cpp src/cache_utils.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(OPT) -o $@ $

$(BUILD)/test_wal: tests/test_wal.cpp src/wal.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(OPT) -o $@ $

$(BUILD)/bench_latency: bench/bench_latency.cpp src/matching_engine.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(FAST) -o $@ $

test: $(TESTS)
	@echo "--- Running all tests ---"
	@$(BUILD)/test_m1
	@$(BUILD)/test_m2
	@$(BUILD)/test_m3_m4
	@$(BUILD)/test_m6
	@$(BUILD)/test_m7
	@$(BUILD)/test_wal
	@echo "=== ALL TESTS PASSED ==="

bench: $(BUILD)/bench_latency
	@$(BUILD)/bench_latency

clean:
	rm -rf $(BUILD)