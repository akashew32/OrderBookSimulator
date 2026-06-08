#pragma once

#include <map>
#include <deque>
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "order.hpp"
#include "trade.hpp"

struct BookLevel {
    int price;
    int quantity;
};

struct ActiveOrderInfo {
    std::uint64_t id;
    Side side;
    int price;
    int quantity;
    std::uint64_t timestamp;
};

class OrderBook {
public:
    // Process a new order and return any resulting trades
    std::vector<Trade> process_order(Order order);

    // Best prices
    bool has_best_bid() const;
    bool has_best_ask() const;
    int best_bid() const;
    int best_ask() const;

    bool cancel_order(std::uint64_t order_id);
    bool modify_order(std::uint64_t order_id, int new_price, int new_quantity);
    bool has_order(std::uint64_t order_id) const;
    std::vector<ActiveOrderInfo> active_orders(std::size_t max_orders = 100) const;

    std::vector<BookLevel> bid_levels(std::size_t max_levels = 20) const;
    std::vector<BookLevel> ask_levels(std::size_t max_levels = 20) const;

private:
    struct OrderLocation {
        Side side;
        int price;
    };

    // std::map keeps price levels sorted with O(log levels) insert/lookup.
    // Bids sort highest price first; asks use the default lowest-price-first
    // ordering. Each map node is heap allocated, which costs cache locality but
    // gives stable level nodes and predictable best-price access.
    std::map<int, std::deque<Order>, std::greater<int>> bids_;

    // std::deque stores FIFO orders within a price level. It avoids moving every
    // resting order when pushing/popping at the front, preserving time priority.
    std::map<int, std::deque<Order>> asks_;

    // Hash lookup maps an order id to its price level so cancel/modify avoids a
    // full book scan. The actual Order still lives inside the map/deque book.
    std::unordered_map<std::uint64_t, OrderLocation> order_locations_;

    // Helper to add leftover order to the book. Takes by value so callers can
    // pass either a stack object or a moved temporary; the container owns it.
    void add_to_book(Order order);
    bool erase_from_level(std::uint64_t order_id, const OrderLocation& location, Order* removed_order = nullptr);
};
