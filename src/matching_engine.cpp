#include "matching_engine.hpp"

#include <utility>

std::vector<Trade> MatchingEngine::process_order(Order order) {
    // Record that we received an order
    metrics_.record_order();

    // Send order to the order book
    auto trades = order_book_.process_order(std::move(order));

    // Record trades
    for (const auto& trade : trades) {
        metrics_.record_trade(trade.quantity);
    }

    return trades;
}

const Metrics& MatchingEngine::metrics() const {
    return metrics_;
}

Metrics& MatchingEngine::metrics() {
    return metrics_;
}

const OrderBook& MatchingEngine::order_book() const {
    return order_book_;
}

bool MatchingEngine::cancel_order(std::uint64_t order_id) {
    bool canceled = order_book_.cancel_order(order_id);
    if (canceled) {
        metrics_.record_cancel();
    }
    return canceled;
}

bool MatchingEngine::modify_order(std::uint64_t order_id, int new_price, int new_quantity) {
    bool modified = order_book_.modify_order(order_id, new_price, new_quantity);
    if (modified) {
        metrics_.record_modify();
    }
    return modified;
}
