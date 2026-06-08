#include "order_book.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

std::vector<Trade> OrderBook::process_order(Order incoming) {
    std::vector<Trade> trades;
    trades.reserve(4);

    if (incoming.quantity <= 0) {
        return trades;
    }

    if (incoming.side == Side::Buy) {
        while (incoming.quantity > 0 &&
               !asks_.empty() &&
               (incoming.type == OrderType::Market || asks_.begin()->first <= incoming.price)) {
            
            auto best_ask_it = asks_.begin();
            auto& resting_orders = best_ask_it->second;
            Order& resting = resting_orders.front();

            int traded_quantity = std::min(incoming.quantity, resting.quantity);

            trades.emplace_back(Trade{
                incoming.id,
                resting.id,
                resting.price,
                traded_quantity,
                incoming.timestamp
            });

            incoming.quantity -= traded_quantity;
            resting.quantity -= traded_quantity;

            if (resting.quantity == 0) {
                order_locations_.erase(resting.id);
                resting_orders.pop_front();
            }

            if (resting_orders.empty()) {
                asks_.erase(best_ask_it);
            }
        }

        if (incoming.quantity > 0 && incoming.type == OrderType::Limit) {
            add_to_book(std::move(incoming));
        }
    } else {
        while (incoming.quantity > 0 &&
               !bids_.empty() &&
               (incoming.type == OrderType::Market || bids_.begin()->first >= incoming.price)) {
            
            auto best_bid_it = bids_.begin();
            auto& resting_orders = best_bid_it->second;
            Order& resting = resting_orders.front();

            int traded_quantity = std::min(incoming.quantity, resting.quantity);

            trades.emplace_back(Trade{
                resting.id,
                incoming.id,
                resting.price,
                traded_quantity,
                incoming.timestamp
            });

            incoming.quantity -= traded_quantity;
            resting.quantity -= traded_quantity;

            if (resting.quantity == 0) {
                order_locations_.erase(resting.id);
                resting_orders.pop_front();
            }

            if (resting_orders.empty()) {
                bids_.erase(best_bid_it);
            }
        }

        if (incoming.quantity > 0 && incoming.type == OrderType::Limit) {
            add_to_book(std::move(incoming));
        }
    }

    return trades;
}

bool OrderBook::has_best_bid() const {
    return !bids_.empty();
}

bool OrderBook::has_best_ask() const {
    return !asks_.empty();
}

int OrderBook::best_bid() const {
    if (bids_.empty()) {
        throw std::runtime_error("No best bid available");
    }

    return bids_.begin()->first;
}

int OrderBook::best_ask() const {
    if (asks_.empty()) {
        throw std::runtime_error("No best ask available");
    }

    return asks_.begin()->first;
}

bool OrderBook::cancel_order(std::uint64_t order_id) {
    auto it = order_locations_.find(order_id);
    if (it == order_locations_.end()) {
        return false;
    }

    return erase_from_level(order_id, it->second);
}

bool OrderBook::modify_order(std::uint64_t order_id, int new_price, int new_quantity) {
    auto location_it = order_locations_.find(order_id);
    if (location_it == order_locations_.end()) {
        return false;
    }

    if (new_quantity <= 0) {
        return cancel_order(order_id);
    }

    OrderLocation location = location_it->second;
    auto* orders = static_cast<std::deque<Order>*>(nullptr);
    if (location.side == Side::Buy) {
        auto level_it = bids_.find(location.price);
        if (level_it == bids_.end()) {
            order_locations_.erase(order_id);
            return false;
        }
        orders = &level_it->second;
    } else {
        auto level_it = asks_.find(location.price);
        if (level_it == asks_.end()) {
            order_locations_.erase(order_id);
            return false;
        }
        orders = &level_it->second;
    }

    auto order_it = std::find_if(orders->begin(), orders->end(), [order_id](const Order& order) {
        return order.id == order_id;
    });

    if (order_it == orders->end()) {
        order_locations_.erase(order_id);
        return false;
    }

    if (new_price == location.price) {
        order_it->quantity = new_quantity;
        return true;
    }

    Order modified = *order_it;
    modified.price = new_price;
    modified.quantity = new_quantity;
    erase_from_level(order_id, location);
    add_to_book(std::move(modified));
    return true;
}

bool OrderBook::has_order(std::uint64_t order_id) const {
    return order_locations_.find(order_id) != order_locations_.end();
}

std::vector<ActiveOrderInfo> OrderBook::active_orders(std::size_t max_orders) const {
    std::vector<ActiveOrderInfo> orders;
    orders.reserve(max_orders);
    auto append = [&](const auto& levels, Side side) {
        for (const auto& [price, level_orders] : levels) {
            for (const auto& order : level_orders) {
                orders.push_back(ActiveOrderInfo{order.id, side, price, order.quantity, order.timestamp});
                if (orders.size() >= max_orders) {
                    return;
                }
            }
        }
    };

    append(bids_, Side::Buy);
    if (orders.size() < max_orders) {
        append(asks_, Side::Sell);
    }

    return orders;
}

std::vector<BookLevel> OrderBook::bid_levels(std::size_t max_levels) const {
    std::vector<BookLevel> levels;
    levels.reserve(max_levels);

    for (const auto& [price, orders] : bids_) {
        int quantity = 0;
        for (const auto& order : orders) {
            quantity += order.quantity;
        }

        levels.push_back(BookLevel{price, quantity});
        if (levels.size() >= max_levels) {
            break;
        }
    }

    return levels;
}

std::vector<BookLevel> OrderBook::ask_levels(std::size_t max_levels) const {
    std::vector<BookLevel> levels;
    levels.reserve(max_levels);

    for (const auto& [price, orders] : asks_) {
        int quantity = 0;
        for (const auto& order : orders) {
            quantity += order.quantity;
        }

        levels.push_back(BookLevel{price, quantity});
        if (levels.size() >= max_levels) {
            break;
        }
    }

    return levels;
}

void OrderBook::add_to_book(Order order) {
    if (order.id != 0 && order_locations_.find(order.id) != order_locations_.end()) {
        cancel_order(order.id);
    }

    const std::uint64_t order_id = order.id;
    const int price = order.price;
    if (order.side == Side::Buy) {
        bids_[price].push_back(std::move(order));
        order_locations_[order_id] = OrderLocation{Side::Buy, price};
    } else {
        asks_[price].push_back(std::move(order));
        order_locations_[order_id] = OrderLocation{Side::Sell, price};
    }
}

bool OrderBook::erase_from_level(std::uint64_t order_id, const OrderLocation& location, Order* removed_order) {
    auto erase_from = [&](auto& levels) {
        auto level_it = levels.find(location.price);
        if (level_it == levels.end()) {
            order_locations_.erase(order_id);
            return false;
        }

        auto& orders = level_it->second;
        auto order_it = std::find_if(orders.begin(), orders.end(), [order_id](const Order& order) {
            return order.id == order_id;
        });

        if (order_it == orders.end()) {
            order_locations_.erase(order_id);
            return false;
        }

        if (removed_order) {
            *removed_order = *order_it;
        }

        orders.erase(order_it);
        order_locations_.erase(order_id);

        if (orders.empty()) {
            levels.erase(level_it);
        }

        return true;
    };

    if (location.side == Side::Buy) {
        return erase_from(bids_);
    }
    return erase_from(asks_);
}
