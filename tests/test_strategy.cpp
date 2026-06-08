#include <cassert>
#include <iostream>
#include <vector>

#include "strategy.hpp"

int main() {
    OrderBook book;
    book.process_order(Order{1, Side::Buy, OrderType::Limit, 100, 10, 1});
    book.process_order(Order{2, Side::Sell, OrderType::Limit, 102, 10, 2});

    StrategyRunner runner;
    std::uint64_t next_order_id = 1;
    std::vector<Trade> trades;

    auto decision = runner.on_market_update(book, trades, 3, next_order_id);
    assert(!decision.orders.empty());
    assert(runner.performances().size() == 4);

    Trade fill{decision.orders.front().id, 99, 101, 2, 4};
    runner.record_fills({fill}, 101);

    const auto& perf = runner.performances().front();
    assert(perf.trades == 1);
    assert(perf.volume == 2);
    assert(perf.inventory == 2);
    assert(perf.fees_paid > 0.0);

    std::cout << "Strategy tests passed!" << std::endl;
    return 0;
}
