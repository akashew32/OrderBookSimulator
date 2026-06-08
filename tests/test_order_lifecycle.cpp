#include <cassert>
#include <iostream>

#include "matching_engine.hpp"

void test_cancel_removes_order() {
    MatchingEngine engine;
    engine.process_order(Order{1, Side::Buy, OrderType::Limit, 100, 10, 1});

    assert(engine.order_book().has_order(1));
    assert(engine.cancel_order(1));
    assert(!engine.order_book().has_order(1));
    assert(!engine.order_book().has_best_bid());
    assert(engine.metrics().canceled_orders() == 1);
}

void test_canceled_order_never_executes() {
    MatchingEngine engine;
    engine.process_order(Order{1, Side::Buy, OrderType::Limit, 100, 10, 1});
    assert(engine.cancel_order(1));

    auto trades = engine.process_order(Order{2, Side::Sell, OrderType::Limit, 99, 10, 2});
    assert(trades.empty());
    assert(engine.order_book().has_best_ask());
    assert(engine.order_book().best_ask() == 99);
}

void test_modify_updates_price() {
    MatchingEngine engine;
    engine.process_order(Order{1, Side::Buy, OrderType::Limit, 99, 10, 1});

    assert(engine.modify_order(1, 101, 10));
    assert(engine.order_book().has_order(1));
    assert(engine.order_book().best_bid() == 101);
    assert(engine.metrics().modified_orders() == 1);
}

void test_modify_same_price_preserves_priority() {
    MatchingEngine engine;
    engine.process_order(Order{1, Side::Buy, OrderType::Limit, 100, 5, 1});
    engine.process_order(Order{2, Side::Buy, OrderType::Limit, 100, 5, 2});

    assert(engine.modify_order(1, 100, 3));
    auto trades = engine.process_order(Order{3, Side::Sell, OrderType::Market, 0, 3, 3});
    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 1);
    assert(trades[0].quantity == 3);
    assert(!engine.order_book().has_order(1));
    assert(engine.order_book().has_order(2));
}

void test_modify_price_resets_priority() {
    MatchingEngine engine;
    engine.process_order(Order{1, Side::Buy, OrderType::Limit, 100, 5, 1});
    engine.process_order(Order{2, Side::Buy, OrderType::Limit, 100, 5, 2});

    assert(engine.modify_order(1, 101, 5));
    assert(engine.modify_order(1, 100, 5));

    auto trades = engine.process_order(Order{3, Side::Sell, OrderType::Market, 0, 5, 3});
    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 2);
}

void test_cancel_modify_sequences() {
    MatchingEngine engine;
    engine.process_order(Order{1, Side::Sell, OrderType::Limit, 104, 8, 1});

    assert(engine.modify_order(1, 103, 6));
    assert(engine.cancel_order(1));
    assert(!engine.modify_order(1, 102, 4));
    assert(!engine.cancel_order(1));

    auto trades = engine.process_order(Order{2, Side::Buy, OrderType::Market, 0, 6, 2});
    assert(trades.empty());
}

int main() {
    test_cancel_removes_order();
    test_canceled_order_never_executes();
    test_modify_updates_price();
    test_modify_same_price_preserves_priority();
    test_modify_price_resets_priority();
    test_cancel_modify_sequences();

    std::cout << "Order lifecycle tests passed!" << std::endl;
    return 0;
}
