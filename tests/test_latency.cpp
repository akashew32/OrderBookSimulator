#include <cassert>
#include <iostream>
#include <vector>

#include "event_queue.hpp"
#include "matching_engine.hpp"

void test_event_queue_time_order() {
    EventQueue queue;
    queue.push(QueuedEvent{30, 0, EventType::OrderSubmission, Order{3, Side::Buy, OrderType::Limit, 100, 1, 30}});
    queue.push(QueuedEvent{10, 0, EventType::CancelOrder, Order{}, 1});
    queue.push(QueuedEvent{20, 0, EventType::ModifyOrder, Order{}, 2, 101, 4});

    assert(queue.pop_next().timestamp == 10);
    assert(queue.pop_next().timestamp == 20);
    assert(queue.pop_next().timestamp == 30);
    assert(queue.empty());
}

void test_event_queue_sequence_order() {
    EventQueue queue;
    queue.push(QueuedEvent{10, 0, EventType::OrderSubmission, Order{1, Side::Buy, OrderType::Limit, 100, 1, 10}});
    queue.push(QueuedEvent{10, 0, EventType::OrderSubmission, Order{2, Side::Sell, OrderType::Limit, 101, 1, 10}});

    assert(queue.pop_next().order.id == 1);
    assert(queue.pop_next().order.id == 2);
}

void test_latency_delays_execution() {
    MatchingEngine engine;
    EventQueue queue;

    engine.process_order(Order{1, Side::Sell, OrderType::Limit, 100, 5, 1});
    queue.push(QueuedEvent{
        25,
        0,
        EventType::OrderSubmission,
        Order{2, Side::Buy, OrderType::Market, 0, 5, 1},
        2,
        0,
        0,
        1,
        100,
        true,
        false
    });

    assert(!queue.has_due(24));
    assert(engine.metrics().trades_executed() == 0);

    auto events = queue.pop_due(25);
    assert(events.size() == 1);
    Order delayed = events[0].order;
    delayed.timestamp = 25;
    auto trades = engine.process_order(delayed);

    assert(trades.size() == 1);
    assert(trades[0].price == 100);
}

void test_latency_causes_missed_trade() {
    MatchingEngine engine;
    EventQueue queue;

    engine.process_order(Order{1, Side::Sell, OrderType::Limit, 100, 5, 1});
    queue.push(QueuedEvent{
        30,
        0,
        EventType::OrderSubmission,
        Order{2, Side::Buy, OrderType::Limit, 100, 5, 1},
        2,
        0,
        0,
        1,
        100,
        true,
        false
    });

    auto faster = engine.process_order(Order{3, Side::Buy, OrderType::Market, 0, 5, 2});
    assert(faster.size() == 1);
    assert(faster[0].buy_order_id == 3);

    auto delayed_events = queue.pop_due(30);
    assert(delayed_events.size() == 1);
    Order delayed = delayed_events[0].order;
    delayed.timestamp = 30;
    auto trades = engine.process_order(delayed);

    if (delayed_events[0].marketable_at_submission && trades.empty()) {
        engine.metrics().record_missed_opportunity();
    }

    assert(trades.empty());
    assert(engine.metrics().missed_trade_opportunities() == 1);
    assert(engine.order_book().has_order(2));
}

int main() {
    test_event_queue_time_order();
    test_event_queue_sequence_order();
    test_latency_delays_execution();
    test_latency_causes_missed_trade();

    std::cout << "Latency tests passed!" << std::endl;
    return 0;
}
