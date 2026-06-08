#pragma once

#include <vector>

#include "order.hpp"
#include "trade.hpp"
#include "order_book.hpp"
#include "metrics.hpp"

class MatchingEngine {
public:
    // Process an order and return resulting trades
    std::vector<Trade> process_order(Order order);

    // Access metrics
    const Metrics& metrics() const;
    Metrics& metrics();
    const OrderBook& order_book() const;
    bool cancel_order(std::uint64_t order_id);
    bool modify_order(std::uint64_t order_id, int new_price, int new_quantity);

private:
    OrderBook order_book_;
    Metrics metrics_;
};
