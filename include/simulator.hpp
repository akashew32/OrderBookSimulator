#pragma once

#include <cstddef>

#include "order.hpp"
#include "thread_safe_queue.hpp"

class Simulator {
public:
    Simulator(ThreadSafeQueue<Order>& order_queue, std::size_t num_orders);

    void run();

private:
    ThreadSafeQueue<Order>& order_queue_;
    std::size_t num_orders_;
};