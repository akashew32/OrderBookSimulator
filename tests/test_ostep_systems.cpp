#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "event_queue.hpp"
#include "object_pool.hpp"
#include "order.hpp"
#include "thread_safe_queue.hpp"
#include "trade.hpp"

void test_bounded_queue_backpressure() {
    ThreadSafeQueue<int> queue(2);
    assert(queue.try_push(1));
    assert(queue.try_push(2));
    assert(!queue.try_push(3));
    assert(queue.dropped() == 1);

    int value = 0;
    assert(queue.try_pop(value));
    assert(value == 1);
    assert(queue.try_push(3));
}

void test_multi_producer_consumer_queue() {
    ThreadSafeQueue<int> queue(128);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    constexpr int producers = 4;
    constexpr int consumers = 3;
    constexpr int per_producer = 1000;

    std::vector<std::thread> producer_threads;
    for (int producer = 0; producer < producers; ++producer) {
        producer_threads.emplace_back([&]() {
            for (int i = 0; i < per_producer; ++i) {
                queue.push(i);
                ++produced;
            }
        });
    }

    std::vector<std::thread> consumer_threads;
    for (int consumer = 0; consumer < consumers; ++consumer) {
        consumer_threads.emplace_back([&]() {
            int value = 0;
            while (queue.pop(value)) {
                ++consumed;
            }
        });
    }

    for (auto& thread : producer_threads) {
        thread.join();
    }
    queue.close();
    for (auto& thread : consumer_threads) {
        thread.join();
    }

    assert(produced == producers * per_producer);
    assert(consumed == produced);
}

void test_event_queue_backpressure() {
    EventQueue queue(2);
    assert(queue.push(QueuedEvent{10, 0, EventType::OrderSubmission, Order{1, Side::Buy, OrderType::Limit, 100, 1, 1}}));
    assert(queue.push(QueuedEvent{11, 0, EventType::OrderSubmission, Order{2, Side::Buy, OrderType::Limit, 101, 1, 2}}));
    assert(!queue.push(QueuedEvent{12, 0, EventType::OrderSubmission, Order{3, Side::Buy, OrderType::Limit, 102, 1, 3}}));
    assert(queue.dropped() == 1);
    assert(queue.size() == 2);
}

void test_object_pool_reuses_orders_and_trades() {
    ObjectPool<Order> order_pool(4);
    ObjectPool<Trade> trade_pool(2);
    {
        auto order = order_pool.acquire(std::uint64_t{1}, Side::Buy, OrderType::Limit, 100, 10, std::uint64_t{1});
        auto trade = trade_pool.acquire(std::uint64_t{1}, std::uint64_t{2}, 100, 5, std::uint64_t{1});
        assert(order->price == 100);
        assert(trade->quantity == 5);
    }

    assert(order_pool.available() == 4);
    assert(trade_pool.available() == 2);
    auto reused = order_pool.acquire(std::uint64_t{2}, Side::Sell, OrderType::Limit, 101, 8, std::uint64_t{2});
    assert(reused->id == 2);
    assert(order_pool.allocated_after_start() == 0);
    assert(order_pool.reused() >= 2);
}

void test_race_condition_and_atomic_fix() {
    constexpr int threads = 8;
    constexpr int increments = 20000;
    std::atomic<int> racy_counter{0};
    std::atomic<int> safe_counter{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&]() {
            for (int i = 0; i < increments; ++i) {
                int snapshot = racy_counter.load(std::memory_order_relaxed);
                std::this_thread::yield();
                racy_counter.store(snapshot + 1, std::memory_order_relaxed);
                safe_counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    assert(safe_counter == threads * increments);
    // The split load/store is race-prone even though it is data-race-free C++:
    // competing threads overwrite each other's increments. fetch_add is the
    // corrected atomic read-modify-write.
    std::cout << "Race demo racy_counter=" << racy_counter.load()
              << " atomic_counter=" << safe_counter.load() << '\n';
}

void benchmark_allocation_modes() {
    constexpr int iterations = 50000;
    auto start_raw = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto order = new Order{static_cast<std::uint64_t>(i), Side::Buy, OrderType::Limit, 100, 1, static_cast<std::uint64_t>(i)};
        delete order;
    }
    auto end_raw = std::chrono::steady_clock::now();

    ObjectPool<Order> pool(iterations);
    auto start_pool = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto order = pool.acquire(static_cast<std::uint64_t>(i), Side::Buy, OrderType::Limit, 100, 1, static_cast<std::uint64_t>(i));
        (void)order;
    }
    auto end_pool = std::chrono::steady_clock::now();

    auto raw_us = std::chrono::duration_cast<std::chrono::microseconds>(end_raw - start_raw).count();
    auto pool_us = std::chrono::duration_cast<std::chrono::microseconds>(end_pool - start_pool).count();
    std::cout << "Allocation benchmark raw_new_delete_us=" << raw_us
              << " pooled_reuse_us=" << pool_us
              << " pool_extra_allocations=" << pool.allocated_after_start() << '\n';
}

int main() {
    test_bounded_queue_backpressure();
    test_multi_producer_consumer_queue();
    test_event_queue_backpressure();
    test_object_pool_reuses_orders_and_trades();
    test_race_condition_and_atomic_fix();
    benchmark_allocation_modes();

    std::cout << "OSTEP systems tests passed!" << std::endl;
    return 0;
}
