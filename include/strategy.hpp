#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "order.hpp"
#include "order_book.hpp"
#include "trade.hpp"

struct StrategyPortfolio {
    int inventory = 0;
    double cash = 0.0;
    double pnl = 0.0;
    double peak_pnl = 0.0;
    double drawdown = 0.0;
};

struct StrategyContext {
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
    std::vector<Trade> recent_trades;
    std::vector<std::uint64_t> active_order_ids;
    std::vector<ActiveOrderInfo> active_orders;
    std::uint64_t timestamp = 0;
    StrategyPortfolio portfolio;
};

struct ModifyRequest {
    std::uint64_t order_id;
    int new_price;
    int new_quantity;
};

struct StrategyDecision {
    std::vector<Order> orders;
    std::vector<std::uint64_t> cancel_order_ids;
    std::vector<ModifyRequest> modify_requests;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::string name() const = 0;
    virtual void on_book_update(const StrategyContext& context) = 0;
    virtual void on_trade(const Trade& trade) = 0;
    virtual std::vector<Order> generate_orders(const StrategyContext& context, std::uint64_t& next_order_id) = 0;
    virtual std::vector<std::uint64_t> cancel_orders(const StrategyContext& context) = 0;
    virtual std::vector<ModifyRequest> modify_orders(const StrategyContext& context) = 0;
};

struct StrategyConfig {
    double fee_per_share = 0.01;
    double slippage_per_share = 0.02;
    bool enabled = true;
};

struct StrategyPerformance {
    std::string name;
    int inventory = 0;
    double cash = 0.0;
    double pnl = 0.0;
    double peak_pnl = 0.0;
    double drawdown = 0.0;
    double max_drawdown = 0.0;
    std::uint64_t trades = 0;
    std::uint64_t winning_trades = 0;
    std::uint64_t volume = 0;
    double avg_fill_price = 0.0;
    double win_rate = 0.0;
    double sharpe_like = 0.0;
    std::uint64_t exposure_time = 0;
    double fees_paid = 0.0;
    double slippage_paid = 0.0;
    std::uint64_t canceled_orders = 0;
    std::uint64_t modified_orders = 0;
    std::uint64_t missed_trade_opportunities = 0;
    double average_latency_us = 0.0;
    double average_fill_delay_ms = 0.0;
    double slippage_vs_intended = 0.0;
    std::vector<std::uint64_t> active_order_ids;
    std::vector<double> pnl_curve;
    std::vector<double> drawdown_curve;
    std::vector<int> inventory_curve;
};

class StrategyRunner {
public:
    StrategyRunner();

    StrategyDecision on_market_update(const OrderBook& book,
                                      const std::vector<Trade>& recent_trades,
                                      std::uint64_t timestamp,
                                      std::uint64_t& next_order_id);
    void record_fills(const std::vector<Trade>& trades, int mark_price, double fill_delay_ms = 0.0);
    void record_cancel(std::uint64_t order_id);
    void record_modify(std::uint64_t order_id);
    void record_latency(std::uint64_t order_id, double latency_us);
    void record_missed_opportunity(std::uint64_t order_id);
    void record_slippage(std::uint64_t order_id, double slippage);
    void reset();

    const std::vector<StrategyPerformance>& performances() const;
    StrategyConfig& config();
    const StrategyConfig& config() const;

private:
    struct Slot {
        std::unique_ptr<Strategy> strategy;
        StrategyPerformance performance;
        std::vector<double> returns;
        std::vector<std::uint64_t> active_orders;
        double total_latency_us = 0.0;
        std::uint64_t latency_samples = 0;
        double total_fill_delay_ms = 0.0;
        std::uint64_t fill_delay_samples = 0;
    };

    std::vector<Slot> strategies_;
    StrategyConfig config_;

    int strategy_for_order(std::uint64_t order_id) const;
    Slot* slot_for_order(std::uint64_t order_id);
    void register_active_order(Slot& slot, std::uint64_t order_id);
    void remove_active_order(Slot& slot, std::uint64_t order_id);
    void apply_fill(Slot& slot, Side side, int price, int quantity, int mark_price);
    void mark_to_market(Slot& slot, int mark_price);
};
