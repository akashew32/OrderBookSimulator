#include <cassert>
#include <iostream>
#include <vector>

#include "matching_engine.hpp"

void test_no_trade_when_only_buy_order() {
    MatchingEngine engine;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 10, 1};

    auto trades = engine.process_order(buy);

    assert(trades.empty());
    assert(engine.metrics().orders_processed() == 1);
    assert(engine.metrics().trades_executed() == 0);
    assert(engine.metrics().volume_executed() == 0);
}

void test_no_trade_when_only_sell_order() {
    MatchingEngine engine;

    Order sell{1, Side::Sell, OrderType::Limit, 100, 10, 1};

    auto trades = engine.process_order(sell);

    assert(trades.empty());
    assert(engine.metrics().orders_processed() == 1);
    assert(engine.metrics().trades_executed() == 0);
    assert(engine.metrics().volume_executed() == 0);
}

void test_exact_match_buy_then_sell() {
    MatchingEngine engine;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 10, 1};
    Order sell{2, Side::Sell, OrderType::Limit, 100, 10, 2};

    engine.process_order(buy);
    auto trades = engine.process_order(sell);

    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 1);
    assert(trades[0].sell_order_id == 2);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 10);

    assert(engine.metrics().orders_processed() == 2);
    assert(engine.metrics().trades_executed() == 1);
    assert(engine.metrics().volume_executed() == 10);
}

void test_exact_match_sell_then_buy() {
    MatchingEngine engine;

    Order sell{1, Side::Sell, OrderType::Limit, 100, 10, 1};
    Order buy{2, Side::Buy, OrderType::Limit, 100, 10, 2};

    engine.process_order(sell);
    auto trades = engine.process_order(buy);

    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 2);
    assert(trades[0].sell_order_id == 1);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 10);

    assert(engine.metrics().orders_processed() == 2);
    assert(engine.metrics().trades_executed() == 1);
    assert(engine.metrics().volume_executed() == 10);
}

void test_no_match_when_buy_price_too_low() {
    MatchingEngine engine;

    Order sell{1, Side::Sell, OrderType::Limit, 105, 10, 1};
    Order buy{2, Side::Buy, OrderType::Limit, 100, 10, 2};

    engine.process_order(sell);
    auto trades = engine.process_order(buy);

    assert(trades.empty());
    assert(engine.metrics().orders_processed() == 2);
    assert(engine.metrics().trades_executed() == 0);
    assert(engine.metrics().volume_executed() == 0);
}

void test_no_match_when_sell_price_too_high() {
    MatchingEngine engine;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 10, 1};
    Order sell{2, Side::Sell, OrderType::Limit, 105, 10, 2};

    engine.process_order(buy);
    auto trades = engine.process_order(sell);

    assert(trades.empty());
    assert(engine.metrics().orders_processed() == 2);
    assert(engine.metrics().trades_executed() == 0);
    assert(engine.metrics().volume_executed() == 0);
}

void test_partial_fill_incoming_sell_smaller() {
    MatchingEngine engine;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 10, 1};
    Order sell{2, Side::Sell, OrderType::Limit, 100, 4, 2};

    engine.process_order(buy);
    auto trades = engine.process_order(sell);

    assert(trades.size() == 1);
    assert(trades[0].quantity == 4);
    assert(trades[0].price == 100);

    assert(engine.metrics().orders_processed() == 2);
    assert(engine.metrics().trades_executed() == 1);
    assert(engine.metrics().volume_executed() == 4);
}

void test_partial_fill_incoming_buy_smaller() {
    MatchingEngine engine;

    Order sell{1, Side::Sell, OrderType::Limit, 100, 10, 1};
    Order buy{2, Side::Buy, OrderType::Limit, 100, 3, 2};

    engine.process_order(sell);
    auto trades = engine.process_order(buy);

    assert(trades.size() == 1);
    assert(trades[0].quantity == 3);
    assert(trades[0].price == 100);

    assert(engine.metrics().orders_processed() == 2);
    assert(engine.metrics().trades_executed() == 1);
    assert(engine.metrics().volume_executed() == 3);
}

void test_incoming_buy_matches_multiple_sells() {
    MatchingEngine engine;

    Order sell1{1, Side::Sell, OrderType::Limit, 100, 4, 1};
    Order sell2{2, Side::Sell, OrderType::Limit, 100, 6, 2};
    Order buy{3, Side::Buy, OrderType::Limit, 100, 10, 3};

    engine.process_order(sell1);
    engine.process_order(sell2);
    auto trades = engine.process_order(buy);

    assert(trades.size() == 2);

    assert(trades[0].buy_order_id == 3);
    assert(trades[0].sell_order_id == 1);
    assert(trades[0].quantity == 4);

    assert(trades[1].buy_order_id == 3);
    assert(trades[1].sell_order_id == 2);
    assert(trades[1].quantity == 6);

    assert(engine.metrics().orders_processed() == 3);
    assert(engine.metrics().trades_executed() == 2);
    assert(engine.metrics().volume_executed() == 10);
}

void test_incoming_sell_matches_multiple_buys() {
    MatchingEngine engine;

    Order buy1{1, Side::Buy, OrderType::Limit, 100, 5, 1};
    Order buy2{2, Side::Buy, OrderType::Limit, 100, 5, 2};
    Order sell{3, Side::Sell, OrderType::Limit, 100, 10, 3};

    engine.process_order(buy1);
    engine.process_order(buy2);
    auto trades = engine.process_order(sell);

    assert(trades.size() == 2);

    assert(trades[0].buy_order_id == 1);
    assert(trades[0].sell_order_id == 3);
    assert(trades[0].quantity == 5);

    assert(trades[1].buy_order_id == 2);
    assert(trades[1].sell_order_id == 3);
    assert(trades[1].quantity == 5);

    assert(engine.metrics().orders_processed() == 3);
    assert(engine.metrics().trades_executed() == 2);
    assert(engine.metrics().volume_executed() == 10);
}

void test_price_time_priority_for_buys() {
    MatchingEngine engine;

    Order buy1{1, Side::Buy, OrderType::Limit, 100, 5, 1};
    Order buy2{2, Side::Buy, OrderType::Limit, 101, 5, 2};
    Order sell{3, Side::Sell, OrderType::Limit, 100, 5, 3};

    engine.process_order(buy1);
    engine.process_order(buy2);
    auto trades = engine.process_order(sell);

    assert(trades.size() == 1);

    // Better price buy order should match first.
    assert(trades[0].buy_order_id == 2);
    assert(trades[0].sell_order_id == 3);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 5);
}

void test_fifo_priority_same_buy_price() {
    MatchingEngine engine;

    Order buy1{1, Side::Buy, OrderType::Limit, 100, 5, 1};
    Order buy2{2, Side::Buy, OrderType::Limit, 100, 5, 2};
    Order sell{3, Side::Sell, OrderType::Limit, 100, 5, 3};

    engine.process_order(buy1);
    engine.process_order(buy2);
    auto trades = engine.process_order(sell);

    assert(trades.size() == 1);

    // Same price, earlier order should match first.
    assert(trades[0].buy_order_id == 1);
    assert(trades[0].sell_order_id == 3);
    assert(trades[0].quantity == 5);
}

void test_price_time_priority_for_sells() {
    MatchingEngine engine;

    Order sell1{1, Side::Sell, OrderType::Limit, 100, 5, 1};
    Order sell2{2, Side::Sell, OrderType::Limit, 99, 5, 2};
    Order buy{3, Side::Buy, OrderType::Limit, 100, 5, 3};

    engine.process_order(sell1);
    engine.process_order(sell2);
    auto trades = engine.process_order(buy);

    assert(trades.size() == 1);

    // Better sell price should match first.
    assert(trades[0].buy_order_id == 3);
    assert(trades[0].sell_order_id == 2);
    assert(trades[0].price == 99);
    assert(trades[0].quantity == 5);
}

void test_fifo_priority_same_sell_price() {
    MatchingEngine engine;

    Order sell1{1, Side::Sell, OrderType::Limit, 100, 5, 1};
    Order sell2{2, Side::Sell, OrderType::Limit, 100, 5, 2};
    Order buy{3, Side::Buy, OrderType::Limit, 100, 5, 3};

    engine.process_order(sell1);
    engine.process_order(sell2);
    auto trades = engine.process_order(buy);

    assert(trades.size() == 1);

    // Same price, earlier sell order should match first.
    assert(trades[0].buy_order_id == 3);
    assert(trades[0].sell_order_id == 1);
    assert(trades[0].quantity == 5);
}

void test_market_buy_consumes_best_asks() {
    MatchingEngine engine;

    Order sell1{1, Side::Sell, OrderType::Limit, 99, 3, 1};
    Order sell2{2, Side::Sell, OrderType::Limit, 100, 4, 2};
    Order market_buy{3, Side::Buy, OrderType::Market, 0, 7, 3};

    engine.process_order(sell1);
    engine.process_order(sell2);
    auto trades = engine.process_order(market_buy);

    assert(trades.size() == 2);

    assert(trades[0].sell_order_id == 1);
    assert(trades[0].price == 99);
    assert(trades[0].quantity == 3);

    assert(trades[1].sell_order_id == 2);
    assert(trades[1].price == 100);
    assert(trades[1].quantity == 4);

    assert(engine.metrics().volume_executed() == 7);
}

void test_market_sell_consumes_best_bids() {
    MatchingEngine engine;

    Order buy1{1, Side::Buy, OrderType::Limit, 101, 3, 1};
    Order buy2{2, Side::Buy, OrderType::Limit, 100, 4, 2};
    Order market_sell{3, Side::Sell, OrderType::Market, 0, 7, 3};

    engine.process_order(buy1);
    engine.process_order(buy2);
    auto trades = engine.process_order(market_sell);

    assert(trades.size() == 2);

    assert(trades[0].buy_order_id == 1);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 3);

    assert(trades[1].buy_order_id == 2);
    assert(trades[1].price == 100);
    assert(trades[1].quantity == 4);

    assert(engine.metrics().volume_executed() == 7);
}

void test_zero_quantity_order_does_nothing() {
    MatchingEngine engine;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 0, 1};

    auto trades = engine.process_order(buy);

    assert(trades.empty());

    // MatchingEngine still records that an order was received.
    assert(engine.metrics().orders_processed() == 1);
    assert(engine.metrics().trades_executed() == 0);
    assert(engine.metrics().volume_executed() == 0);
}

int main() {
    test_no_trade_when_only_buy_order();
    test_no_trade_when_only_sell_order();
    test_exact_match_buy_then_sell();
    test_exact_match_sell_then_buy();
    test_no_match_when_buy_price_too_low();
    test_no_match_when_sell_price_too_high();
    test_partial_fill_incoming_sell_smaller();
    test_partial_fill_incoming_buy_smaller();
    test_incoming_buy_matches_multiple_sells();
    test_incoming_sell_matches_multiple_buys();
    test_price_time_priority_for_buys();
    test_fifo_priority_same_buy_price();
    test_price_time_priority_for_sells();
    test_fifo_priority_same_sell_price();
    test_market_buy_consumes_best_asks();
    test_market_sell_consumes_best_bids();
    test_zero_quantity_order_does_nothing();

    std::cout << "All MatchingEngine tests passed!" << std::endl;
    return 0;
}