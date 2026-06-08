#include <iostream>
#include <thread>
#include <atomic>

#include "matching_engine.hpp"
#include "thread_safe_queue.hpp"
#include "simulator.hpp"

int main() {
    ThreadSafeQueue<Order> queue;
    MatchingEngine engine;

    std::atomic<bool> done{false};

    // Producer thread (simulator)
    Simulator simulator(queue, 100);

    std::thread producer([&]() {
        simulator.run();
        done = true;
    });

    // Consumer thread (matching engine)
    std::thread consumer([&]() {
        while (!done || !queue.empty()) {
            Order order;
            if (queue.try_pop(order)) {
                auto trades = engine.process_order(order);

                for (const auto& trade : trades) {
                    std::cout << "Trade | "
                            << "Buy: " << trade.buy_order_id
                            << " | Sell: " << trade.sell_order_id
                            << " | Price: " << trade.price
                            << " | Qty: " << trade.quantity
                            << '\n';
                } 
            }
        }
    });

    producer.join();
    consumer.join();

    // Print metrics
    const auto& metrics = engine.metrics();
    std::cout << "\nMetrics:\n";
    std::cout << "Orders processed: " << metrics.orders_processed() << "\n";
    std::cout << "Trades executed: " << metrics.trades_executed() << "\n";
    std::cout << "Volume executed: " << metrics.volume_executed() << "\n";

    return 0;
}