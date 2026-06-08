#include <cassert>
#include <iostream>

#include "order_book.hpp"

void test_empty_book_has_no_best_prices() {
    OrderBook book;

    assert(!book.has_best_bid());
    assert(!book.has_best_ask());
}

void test_buy_order_added_to_book() {
    OrderBook book;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 10, 1};
    auto trades = book.process_order(buy);

    assert(trades.empty());
    assert(book.has_best_bid());
    assert(book.best_bid() == 100);
    assert(!book.has_best_ask());
}

void test_sell_order_added_to_book() {
    OrderBook book;

    Order sell{1, Side::Sell, OrderType::Limit, 105, 10, 1};
    auto trades = book.process_order(sell);

    assert(trades.empty());
    assert(book.has_best_ask());
    assert(book.best_ask() == 105);
    assert(!book.has_best_bid());
}

void test_exact_match_buy_then_sell() {
    OrderBook book;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 10, 1};
    Order sell{2, Side::Sell, OrderType::Limit, 100, 10, 2};

    book.process_order(buy);
    auto trades = book.process_order(sell);

    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 1);
    assert(trades[0].sell_order_id == 2);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 10);
}

void test_exact_match_sell_then_buy() {
    OrderBook book;

    Order sell{1, Side::Sell, OrderType::Limit, 100, 10, 1};
    Order buy{2, Side::Buy, OrderType::Limit, 100, 10, 2};

    book.process_order(sell);
    auto trades = book.process_order(buy);

    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 2);
    assert(trades[0].sell_order_id == 1);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 10);
}

void test_no_match_buy_price_too_low() {
    OrderBook book;

    Order sell{1, Side::Sell, OrderType::Limit, 105, 10, 1};
    Order buy{2, Side::Buy, OrderType::Limit, 100, 10, 2};

    book.process_order(sell);
    auto trades = book.process_order(buy);

    assert(trades.empty());
    assert(book.has_best_ask());
    assert(book.best_ask() == 105);
    assert(book.has_best_bid());
    assert(book.best_bid() == 100);
}

void test_no_match_sell_price_too_high() {
    OrderBook book;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 10, 1};
    Order sell{2, Side::Sell, OrderType::Limit, 105, 10, 2};

    book.process_order(buy);
    auto trades = book.process_order(sell);

    assert(trades.empty());
    assert(book.has_best_bid());
    assert(book.best_bid() == 100);
    assert(book.has_best_ask());
    assert(book.best_ask() == 105);
}

void test_partial_fill_leaves_bid_resting() {
    OrderBook book;

    Order buy{1, Side::Buy, OrderType::Limit, 100, 10, 1};
    Order sell{2, Side::Sell, OrderType::Limit, 100, 4, 2};

    book.process_order(buy);
    auto trades = book.process_order(sell);

    assert(trades.size() == 1);
    assert(trades[0].quantity == 4);
    assert(book.has_best_bid());
    assert(book.best_bid() == 100);
    assert(!book.has_best_ask());
}

void test_partial_fill_leaves_ask_resting() {
    OrderBook book;

    Order sell{1, Side::Sell, OrderType::Limit, 100, 10, 1};
    Order buy{2, Side::Buy, OrderType::Limit, 100, 3, 2};

    book.process_order(sell);
    auto trades = book.process_order(buy);

    assert(trades.size() == 1);
    assert(trades[0].quantity == 3);
    assert(book.has_best_ask());
    assert(book.best_ask() == 100);
    assert(!book.has_best_bid());
}

void test_incoming_buy_matches_multiple_asks() {
    OrderBook book;

    Order sell1{1, Side::Sell, OrderType::Limit, 99, 3, 1};
    Order sell2{2, Side::Sell, OrderType::Limit, 100, 4, 2};
    Order buy{3, Side::Buy, OrderType::Limit, 100, 7, 3};

    book.process_order(sell1);
    book.process_order(sell2);
    auto trades = book.process_order(buy);

    assert(trades.size() == 2);

    assert(trades[0].sell_order_id == 1);
    assert(trades[0].price == 99);
    assert(trades[0].quantity == 3);

    assert(trades[1].sell_order_id == 2);
    assert(trades[1].price == 100);
    assert(trades[1].quantity == 4);

    assert(!book.has_best_ask());
}

void test_incoming_sell_matches_multiple_bids() {
    OrderBook book;

    Order buy1{1, Side::Buy, OrderType::Limit, 101, 3, 1};
    Order buy2{2, Side::Buy, OrderType::Limit, 100, 4, 2};
    Order sell{3, Side::Sell, OrderType::Limit, 100, 7, 3};

    book.process_order(buy1);
    book.process_order(buy2);
    auto trades = book.process_order(sell);

    assert(trades.size() == 2);

    assert(trades[0].buy_order_id == 1);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 3);

    assert(trades[1].buy_order_id == 2);
    assert(trades[1].price == 100);
    assert(trades[1].quantity == 4);

    assert(!book.has_best_bid());
}

void test_fifo_same_bid_price() {
    OrderBook book;

    Order buy1{1, Side::Buy, OrderType::Limit, 100, 5, 1};
    Order buy2{2, Side::Buy, OrderType::Limit, 100, 5, 2};
    Order sell{3, Side::Sell, OrderType::Limit, 100, 5, 3};

    book.process_order(buy1);
    book.process_order(buy2);
    auto trades = book.process_order(sell);

    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 1);
    assert(trades[0].sell_order_id == 3);
    assert(trades[0].quantity == 5);
}

void test_fifo_same_ask_price() {
    OrderBook book;

    Order sell1{1, Side::Sell, OrderType::Limit, 100, 5, 1};
    Order sell2{2, Side::Sell, OrderType::Limit, 100, 5, 2};
    Order buy{3, Side::Buy, OrderType::Limit, 100, 5, 3};

    book.process_order(sell1);
    book.process_order(sell2);
    auto trades = book.process_order(buy);

    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 3);
    assert(trades[0].sell_order_id == 1);
    assert(trades[0].quantity == 5);
}

void test_best_bid_is_highest_price() {
    OrderBook book;

    book.process_order(Order{1, Side::Buy, OrderType::Limit, 99, 5, 1});
    book.process_order(Order{2, Side::Buy, OrderType::Limit, 101, 5, 2});
    book.process_order(Order{3, Side::Buy, OrderType::Limit, 100, 5, 3});

    assert(book.has_best_bid());
    assert(book.best_bid() == 101);
}

void test_best_ask_is_lowest_price() {
    OrderBook book;

    book.process_order(Order{1, Side::Sell, OrderType::Limit, 105, 5, 1});
    book.process_order(Order{2, Side::Sell, OrderType::Limit, 103, 5, 2});
    book.process_order(Order{3, Side::Sell, OrderType::Limit, 104, 5, 3});

    assert(book.has_best_ask());
    assert(book.best_ask() == 103);
}

void test_market_buy_consumes_asks() {
    OrderBook book;

    book.process_order(Order{1, Side::Sell, OrderType::Limit, 99, 3, 1});
    book.process_order(Order{2, Side::Sell, OrderType::Limit, 100, 4, 2});

    auto trades = book.process_order(Order{3, Side::Buy, OrderType::Market, 0, 7, 3});

    assert(trades.size() == 2);
    assert(trades[0].price == 99);
    assert(trades[0].quantity == 3);
    assert(trades[1].price == 100);
    assert(trades[1].quantity == 4);
    assert(!book.has_best_ask());
}

void test_market_sell_consumes_bids() {
    OrderBook book;

    book.process_order(Order{1, Side::Buy, OrderType::Limit, 101, 3, 1});
    book.process_order(Order{2, Side::Buy, OrderType::Limit, 100, 4, 2});

    auto trades = book.process_order(Order{3, Side::Sell, OrderType::Market, 0, 7, 3});

    assert(trades.size() == 2);
    assert(trades[0].price == 101);
    assert(trades[0].quantity == 3);
    assert(trades[1].price == 100);
    assert(trades[1].quantity == 4);
    assert(!book.has_best_bid());
}

void test_zero_quantity_order_does_nothing() {
    OrderBook book;

    auto trades = book.process_order(Order{1, Side::Buy, OrderType::Limit, 100, 0, 1});

    assert(trades.empty());
    assert(!book.has_best_bid());
    assert(!book.has_best_ask());
}

int main() {
    test_empty_book_has_no_best_prices();
    test_buy_order_added_to_book();
    test_sell_order_added_to_book();
    test_exact_match_buy_then_sell();
    test_exact_match_sell_then_buy();
    test_no_match_buy_price_too_low();
    test_no_match_sell_price_too_high();
    test_partial_fill_leaves_bid_resting();
    test_partial_fill_leaves_ask_resting();
    test_incoming_buy_matches_multiple_asks();
    test_incoming_sell_matches_multiple_bids();
    test_fifo_same_bid_price();
    test_fifo_same_ask_price();
    test_best_bid_is_highest_price();
    test_best_ask_is_lowest_price();
    test_market_buy_consumes_asks();
    test_market_sell_consumes_bids();
    test_zero_quantity_order_does_nothing();

    std::cout << "All OrderBook tests passed!" << std::endl;
    return 0;
}