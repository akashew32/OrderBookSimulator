#include "strategy.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

constexpr std::uint64_t kStrategyOrderBase = 1'000'000'000ULL;
constexpr std::uint64_t kStrategyOrderBlock = 10'000'000ULL;

int best_bid(const StrategyContext& context) {
    return context.bids.empty() ? 0 : context.bids.front().price;
}

int best_ask(const StrategyContext& context) {
    return context.asks.empty() ? 0 : context.asks.front().price;
}

int mid_price(const StrategyContext& context) {
    int bid = best_bid(context);
    int ask = best_ask(context);
    if (bid > 0 && ask > 0) {
        return (bid + ask) / 2;
    }
    if (!context.recent_trades.empty()) {
        return context.recent_trades.front().price;
    }
    return 100;
}

Order make_strategy_order(int strategy_index,
                          std::uint64_t& next_order_id,
                          Side side,
                          OrderType type,
                          int price,
                          int quantity,
                          std::uint64_t timestamp) {
    return Order{
        kStrategyOrderBase + static_cast<std::uint64_t>(strategy_index) * kStrategyOrderBlock + next_order_id++,
        side,
        type,
        type == OrderType::Market ? 0 : price,
        quantity,
        timestamp
    };
}

double mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double stdev(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double avg = mean(values);
    double accum = 0.0;
    for (double value : values) {
        accum += (value - avg) * (value - avg);
    }
    return std::sqrt(accum / static_cast<double>(values.size() - 1));
}

class MarketMakingStrategy final : public Strategy {
public:
    std::string name() const override { return "Market Making"; }
    void on_book_update(const StrategyContext&) override {}
    void on_trade(const Trade&) override {}

    std::vector<Order> generate_orders(const StrategyContext& context, std::uint64_t& next_order_id) override {
        if (context.timestamp == last_timestamp_) {
            return {};
        }
        last_timestamp_ = context.timestamp;
        if (context.active_order_ids.size() >= 2) {
            return {};
        }

        int mid = mid_price(context);
        int skew = context.portfolio.inventory > 20 ? -1 : (context.portfolio.inventory < -20 ? 1 : 0);
        int bid = std::max(1, mid - 1 + skew);
        int ask = std::max(bid + 1, mid + 1 + skew);

        return {
            make_strategy_order(0, next_order_id, Side::Buy, OrderType::Limit, bid, 3, context.timestamp),
            make_strategy_order(0, next_order_id, Side::Sell, OrderType::Limit, ask, 3, context.timestamp)
        };
    }

    std::vector<std::uint64_t> cancel_orders(const StrategyContext& context) override {
        if (context.active_order_ids.size() > 4) {
            return context.active_order_ids;
        }
        return {};
    }

    std::vector<ModifyRequest> modify_orders(const StrategyContext& context) override {
        int mid = mid_price(context);
        int skew = context.portfolio.inventory > 20 ? -1 : (context.portfolio.inventory < -20 ? 1 : 0);
        int desired_bid = std::max(1, mid - 1 + skew);
        int desired_ask = std::max(desired_bid + 1, mid + 1 + skew);

        std::vector<ModifyRequest> requests;
        for (const auto& order : context.active_orders) {
            int desired = order.side == Side::Buy ? desired_bid : desired_ask;
            if (order.price != desired) {
                requests.push_back(ModifyRequest{order.id, desired, order.quantity});
            }
        }
        return requests;
    }

private:
    std::uint64_t last_timestamp_ = 0;
};

class MomentumStrategy final : public Strategy {
public:
    std::string name() const override { return "Momentum"; }
    void on_book_update(const StrategyContext&) override {}
    void on_trade(const Trade&) override {}

    std::vector<Order> generate_orders(const StrategyContext& context, std::uint64_t& next_order_id) override {
        if (context.recent_trades.size() < 4 || context.timestamp == last_timestamp_) {
            return {};
        }
        last_timestamp_ = context.timestamp;

        int latest = context.recent_trades[0].price;
        int older = context.recent_trades[3].price;
        if (latest > older) {
            return {make_strategy_order(1, next_order_id, Side::Buy, OrderType::Market, 0, 2, context.timestamp)};
        }
        if (latest < older) {
            return {make_strategy_order(1, next_order_id, Side::Sell, OrderType::Market, 0, 2, context.timestamp)};
        }
        return {};
    }

    std::vector<std::uint64_t> cancel_orders(const StrategyContext& context) override {
        if (context.recent_trades.size() < 4) {
            return context.active_order_ids;
        }
        return {};
    }

    std::vector<ModifyRequest> modify_orders(const StrategyContext&) override {
        return {};
    }

private:
    std::uint64_t last_timestamp_ = 0;
};

class MeanReversionStrategy final : public Strategy {
public:
    std::string name() const override { return "Mean Reversion"; }
    void on_book_update(const StrategyContext&) override {}
    void on_trade(const Trade&) override {}

    std::vector<Order> generate_orders(const StrategyContext& context, std::uint64_t& next_order_id) override {
        if (context.recent_trades.size() < 8 || context.timestamp == last_timestamp_) {
            return {};
        }
        last_timestamp_ = context.timestamp;

        int latest = context.recent_trades[0].price;
        double rolling = 0.0;
        for (std::size_t i = 0; i < 8; ++i) {
            rolling += context.recent_trades[i].price;
        }
        rolling /= 8.0;

        if (latest > rolling + 1.0) {
            return {make_strategy_order(2, next_order_id, Side::Sell, OrderType::Market, 0, 2, context.timestamp)};
        }
        if (latest < rolling - 1.0) {
            return {make_strategy_order(2, next_order_id, Side::Buy, OrderType::Market, 0, 2, context.timestamp)};
        }
        return {};
    }

    std::vector<std::uint64_t> cancel_orders(const StrategyContext& context) override {
        if (context.recent_trades.size() < 8) {
            return context.active_order_ids;
        }

        int latest = context.recent_trades[0].price;
        double rolling = 0.0;
        for (std::size_t i = 0; i < 8; ++i) {
            rolling += context.recent_trades[i].price;
        }
        rolling /= 8.0;

        if (std::abs(static_cast<double>(latest) - rolling) <= 1.0) {
            return context.active_order_ids;
        }
        return {};
    }

    std::vector<ModifyRequest> modify_orders(const StrategyContext&) override {
        return {};
    }

private:
    std::uint64_t last_timestamp_ = 0;
};

class OrderBookImbalanceStrategy final : public Strategy {
public:
    std::string name() const override { return "Book Imbalance"; }
    void on_book_update(const StrategyContext&) override {}
    void on_trade(const Trade&) override {}

    std::vector<Order> generate_orders(const StrategyContext& context, std::uint64_t& next_order_id) override {
        if (context.timestamp == last_timestamp_) {
            return {};
        }
        last_timestamp_ = context.timestamp;

        int bid_volume = 0;
        int ask_volume = 0;
        for (std::size_t i = 0; i < std::min<std::size_t>(5, context.bids.size()); ++i) {
            bid_volume += context.bids[i].quantity;
        }
        for (std::size_t i = 0; i < std::min<std::size_t>(5, context.asks.size()); ++i) {
            ask_volume += context.asks[i].quantity;
        }

        int total = bid_volume + ask_volume;
        if (total == 0) {
            return {};
        }

        double imbalance = static_cast<double>(bid_volume - ask_volume) / static_cast<double>(total);
        if (imbalance > 0.35) {
            return {make_strategy_order(3, next_order_id, Side::Buy, OrderType::Market, 0, 2, context.timestamp)};
        }
        if (imbalance < -0.35) {
            return {make_strategy_order(3, next_order_id, Side::Sell, OrderType::Market, 0, 2, context.timestamp)};
        }
        return {};
    }

    std::vector<std::uint64_t> cancel_orders(const StrategyContext& context) override {
        int bid_volume = 0;
        int ask_volume = 0;
        for (std::size_t i = 0; i < std::min<std::size_t>(5, context.bids.size()); ++i) {
            bid_volume += context.bids[i].quantity;
        }
        for (std::size_t i = 0; i < std::min<std::size_t>(5, context.asks.size()); ++i) {
            ask_volume += context.asks[i].quantity;
        }

        int total = bid_volume + ask_volume;
        if (total == 0) {
            return context.active_order_ids;
        }

        double imbalance = static_cast<double>(bid_volume - ask_volume) / static_cast<double>(total);
        if (std::abs(imbalance) < 0.2) {
            return context.active_order_ids;
        }
        return {};
    }

    std::vector<ModifyRequest> modify_orders(const StrategyContext&) override {
        return {};
    }

private:
    std::uint64_t last_timestamp_ = 0;
};

} // namespace

StrategyRunner::StrategyRunner() {
    strategies_.push_back(Slot{std::make_unique<MarketMakingStrategy>(), StrategyPerformance{}, {}});
    strategies_.push_back(Slot{std::make_unique<MomentumStrategy>(), StrategyPerformance{}, {}});
    strategies_.push_back(Slot{std::make_unique<MeanReversionStrategy>(), StrategyPerformance{}, {}});
    strategies_.push_back(Slot{std::make_unique<OrderBookImbalanceStrategy>(), StrategyPerformance{}, {}});

    for (auto& slot : strategies_) {
        slot.performance.name = slot.strategy->name();
    }
}

StrategyDecision StrategyRunner::on_market_update(const OrderBook& book,
                                                  const std::vector<Trade>& recent_trades,
                                                  std::uint64_t timestamp,
                                                  std::uint64_t& next_order_id) {
    StrategyDecision decision;
    if (!config_.enabled) {
        return decision;
    }

    int mark = 100;
    auto bids = book.bid_levels(10);
    auto asks = book.ask_levels(10);
    auto active_book_orders = book.active_orders(500);
    if (!bids.empty() && !asks.empty()) {
        mark = (bids.front().price + asks.front().price) / 2;
    } else if (!recent_trades.empty()) {
        mark = recent_trades.front().price;
    }

    for (auto& slot : strategies_) {
        std::vector<ActiveOrderInfo> slot_active_orders;
        for (const auto& order : active_book_orders) {
            if (std::find(slot.active_orders.begin(), slot.active_orders.end(), order.id) != slot.active_orders.end()) {
                slot_active_orders.push_back(order);
            }
        }

        StrategyContext context{bids, asks, recent_trades, slot.active_orders, slot_active_orders, timestamp, StrategyPortfolio{
            slot.performance.inventory,
            slot.performance.cash,
            slot.performance.pnl,
            slot.performance.peak_pnl,
            slot.performance.drawdown
        }};

        slot.strategy->on_book_update(context);
        for (const auto& trade : recent_trades) {
            slot.strategy->on_trade(trade);
        }

        auto cancels = slot.strategy->cancel_orders(context);
        auto modifies = slot.strategy->modify_orders(context);
        auto generated = slot.strategy->generate_orders(context, next_order_id);

        decision.cancel_order_ids.insert(decision.cancel_order_ids.end(), cancels.begin(), cancels.end());
        decision.modify_requests.insert(decision.modify_requests.end(), modifies.begin(), modifies.end());
        for (const auto& order : generated) {
            if (order.type == OrderType::Limit) {
                register_active_order(slot, order.id);
            }
            decision.orders.push_back(order);
        }
        mark_to_market(slot, mark);
    }

    return decision;
}

void StrategyRunner::record_fills(const std::vector<Trade>& trades, int mark_price, double fill_delay_ms) {
    for (const auto& trade : trades) {
        int buy_strategy = strategy_for_order(trade.buy_order_id);
        int sell_strategy = strategy_for_order(trade.sell_order_id);

        if (buy_strategy >= 0 && buy_strategy < static_cast<int>(strategies_.size())) {
            apply_fill(strategies_[buy_strategy], Side::Buy, trade.price, trade.quantity, mark_price);
            remove_active_order(strategies_[buy_strategy], trade.buy_order_id);
            strategies_[buy_strategy].total_fill_delay_ms += fill_delay_ms;
            strategies_[buy_strategy].fill_delay_samples++;
            strategies_[buy_strategy].performance.average_fill_delay_ms =
                strategies_[buy_strategy].total_fill_delay_ms / static_cast<double>(strategies_[buy_strategy].fill_delay_samples);
        }
        if (sell_strategy >= 0 && sell_strategy < static_cast<int>(strategies_.size())) {
            apply_fill(strategies_[sell_strategy], Side::Sell, trade.price, trade.quantity, mark_price);
            remove_active_order(strategies_[sell_strategy], trade.sell_order_id);
            strategies_[sell_strategy].total_fill_delay_ms += fill_delay_ms;
            strategies_[sell_strategy].fill_delay_samples++;
            strategies_[sell_strategy].performance.average_fill_delay_ms =
                strategies_[sell_strategy].total_fill_delay_ms / static_cast<double>(strategies_[sell_strategy].fill_delay_samples);
        }
    }

    for (auto& slot : strategies_) {
        mark_to_market(slot, mark_price);
    }
}

void StrategyRunner::record_cancel(std::uint64_t order_id) {
    if (Slot* slot = slot_for_order(order_id)) {
        slot->performance.canceled_orders++;
        remove_active_order(*slot, order_id);
    }
}

void StrategyRunner::record_modify(std::uint64_t order_id) {
    if (Slot* slot = slot_for_order(order_id)) {
        slot->performance.modified_orders++;
    }
}

void StrategyRunner::record_latency(std::uint64_t order_id, double latency_us) {
    if (Slot* slot = slot_for_order(order_id)) {
        slot->total_latency_us += latency_us;
        slot->latency_samples++;
        slot->performance.average_latency_us = slot->total_latency_us / static_cast<double>(slot->latency_samples);
    }
}

void StrategyRunner::record_missed_opportunity(std::uint64_t order_id) {
    if (Slot* slot = slot_for_order(order_id)) {
        slot->performance.missed_trade_opportunities++;
    }
}

void StrategyRunner::record_slippage(std::uint64_t order_id, double slippage) {
    if (Slot* slot = slot_for_order(order_id)) {
        slot->performance.slippage_vs_intended += slippage;
    }
}

void StrategyRunner::reset() {
    strategies_.clear();
    strategies_.push_back(Slot{std::make_unique<MarketMakingStrategy>(), StrategyPerformance{}, {}});
    strategies_.push_back(Slot{std::make_unique<MomentumStrategy>(), StrategyPerformance{}, {}});
    strategies_.push_back(Slot{std::make_unique<MeanReversionStrategy>(), StrategyPerformance{}, {}});
    strategies_.push_back(Slot{std::make_unique<OrderBookImbalanceStrategy>(), StrategyPerformance{}, {}});

    for (auto& slot : strategies_) {
        slot.performance.name = slot.strategy->name();
    }
}

const std::vector<StrategyPerformance>& StrategyRunner::performances() const {
    static thread_local std::vector<StrategyPerformance> copy;
    copy.clear();
    for (const auto& slot : strategies_) {
        StrategyPerformance perf = slot.performance;
        perf.active_order_ids = slot.active_orders;
        copy.push_back(perf);
    }
    return copy;
}

StrategyConfig& StrategyRunner::config() {
    return config_;
}

const StrategyConfig& StrategyRunner::config() const {
    return config_;
}

int StrategyRunner::strategy_for_order(std::uint64_t order_id) const {
    if (order_id < kStrategyOrderBase) {
        return -1;
    }
    return static_cast<int>((order_id - kStrategyOrderBase) / kStrategyOrderBlock);
}

StrategyRunner::Slot* StrategyRunner::slot_for_order(std::uint64_t order_id) {
    int index = strategy_for_order(order_id);
    if (index < 0 || index >= static_cast<int>(strategies_.size())) {
        return nullptr;
    }
    return &strategies_[index];
}

void StrategyRunner::register_active_order(Slot& slot, std::uint64_t order_id) {
    if (std::find(slot.active_orders.begin(), slot.active_orders.end(), order_id) == slot.active_orders.end()) {
        slot.active_orders.push_back(order_id);
    }
}

void StrategyRunner::remove_active_order(Slot& slot, std::uint64_t order_id) {
    slot.active_orders.erase(std::remove(slot.active_orders.begin(), slot.active_orders.end(), order_id), slot.active_orders.end());
}

void StrategyRunner::apply_fill(Slot& slot, Side side, int price, int quantity, int mark_price) {
    double previous_pnl = slot.performance.pnl;
    double fees = config_.fee_per_share * quantity;
    double slippage = config_.slippage_per_share * quantity;

    if (side == Side::Buy) {
        slot.performance.inventory += quantity;
        slot.performance.cash -= static_cast<double>(price * quantity) + fees + slippage;
    } else {
        slot.performance.inventory -= quantity;
        slot.performance.cash += static_cast<double>(price * quantity) - fees - slippage;
    }

    slot.performance.trades++;
    slot.performance.volume += quantity;
    slot.performance.fees_paid += fees;
    slot.performance.slippage_paid += slippage;
    slot.performance.avg_fill_price =
        ((slot.performance.avg_fill_price * static_cast<double>(slot.performance.volume - quantity)) +
         static_cast<double>(price * quantity)) /
        static_cast<double>(slot.performance.volume);

    mark_to_market(slot, mark_price);
    if (slot.performance.pnl > previous_pnl) {
        slot.performance.winning_trades++;
    }
    slot.performance.win_rate = slot.performance.trades == 0
        ? 0.0
        : static_cast<double>(slot.performance.winning_trades) / static_cast<double>(slot.performance.trades);
}

void StrategyRunner::mark_to_market(Slot& slot, int mark_price) {
    double previous = slot.performance.pnl;
    slot.performance.pnl = slot.performance.cash + static_cast<double>(slot.performance.inventory * mark_price);
    slot.performance.peak_pnl = std::max(slot.performance.peak_pnl, slot.performance.pnl);
    slot.performance.drawdown = slot.performance.peak_pnl - slot.performance.pnl;
    slot.performance.max_drawdown = std::max(slot.performance.max_drawdown, slot.performance.drawdown);

    if (slot.performance.inventory != 0) {
        slot.performance.exposure_time++;
    }

    double ret = slot.performance.pnl - previous;
    slot.returns.push_back(ret);
    if (slot.returns.size() > 120) {
        slot.returns.erase(slot.returns.begin());
    }

    double sd = stdev(slot.returns);
    slot.performance.sharpe_like = sd == 0.0 ? 0.0 : mean(slot.returns) / sd;

    slot.performance.pnl_curve.push_back(slot.performance.pnl);
    slot.performance.drawdown_curve.push_back(slot.performance.drawdown);
    slot.performance.inventory_curve.push_back(slot.performance.inventory);
    if (slot.performance.pnl_curve.size() > 180) {
        slot.performance.pnl_curve.erase(slot.performance.pnl_curve.begin());
        slot.performance.drawdown_curve.erase(slot.performance.drawdown_curve.begin());
        slot.performance.inventory_curve.erase(slot.performance.inventory_curve.begin());
    }
}
