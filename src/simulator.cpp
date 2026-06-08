#include "simulator.hpp"

#include <random>

Simulator::Simulator(ThreadSafeQueue<Order>& order_queue, std::size_t num_orders)
    : order_queue_(order_queue), num_orders_(num_orders) {}

void Simulator::run() {
    std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> price_dist(95, 105);
    std::uniform_int_distribution<int> qty_dist(1, 10);

    for (std::size_t i = 0; i < num_orders_; ++i) {
        Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;

        Order order{
            static_cast<std::uint64_t>(i + 1),   // id
            side,
            OrderType::Limit,
            price_dist(rng),                    // price around 100
            qty_dist(rng),                      // random quantity
            static_cast<std::uint64_t>(i + 1)   // timestamp
        };

        order_queue_.push(order);
    }
}