#include "optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

double get_param(const ParameterConfig& config, const std::string& key, double fallback) {
    auto it = config.values.find(key);
    return it == config.values.end() ? fallback : it->second;
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
    double sum = 0.0;
    for (double value : values) {
        sum += (value - avg) * (value - avg);
    }
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

} // namespace

std::vector<ParameterConfig> ParameterGrid::defaults_for(const std::string& strategy) {
    std::vector<ParameterConfig> configs;
    auto add = [&](std::map<std::string, double> values) {
        configs.push_back(ParameterConfig{strategy, std::move(values)});
    };

    if (strategy == "Momentum") {
        for (double lookback : {3.0, 5.0, 8.0}) {
            for (double threshold : {1.0, 2.0}) {
                for (double size : {1.0, 3.0}) {
                    add({{"lookback", lookback}, {"momentum_threshold", threshold}, {"order_size", size}});
                }
            }
        }
        return configs;
    }

    if (strategy == "Mean Reversion") {
        for (double window : {6.0, 10.0, 14.0}) {
            for (double z : {0.75, 1.25}) {
                for (double size : {1.0, 3.0}) {
                    add({{"rolling_window", window}, {"z_score_threshold", z}, {"order_size", size}});
                }
            }
        }
        return configs;
    }

    if (strategy == "Book Imbalance") {
        for (double depth : {3.0, 5.0, 8.0}) {
            for (double threshold : {0.2, 0.35, 0.5}) {
                for (double size : {1.0, 3.0}) {
                    add({{"depth_levels", depth}, {"imbalance_threshold", threshold}, {"order_size", size}});
                }
            }
        }
        return configs;
    }

    for (double spread : {1.0, 2.0, 3.0}) {
        for (double size : {1.0, 3.0}) {
            for (double skew : {0.0, 0.5}) {
                add({{"spread_width", spread}, {"order_size", size}, {"inventory_skew", skew}, {"max_inventory", 20.0}});
            }
        }
    }
    return configs;
}

std::vector<WalkForwardWindow> WalkForwardSplitter::split(std::size_t event_count,
                                                          std::size_t train_window,
                                                          std::size_t test_window) {
    std::vector<WalkForwardWindow> windows;
    if (train_window == 0 || test_window == 0 || event_count < train_window + test_window) {
        return windows;
    }

    std::size_t step = test_window;
    for (std::size_t start = 0; start + train_window + test_window <= event_count; start += step) {
        windows.push_back(WalkForwardWindow{
            start,
            start + train_window,
            start + train_window,
            start + train_window + test_window
        });
    }
    return windows;
}

bool StrategyOptimizer::run(const OptimizationRequest& request,
                            std::vector<OptimizationResult>& results,
                            std::string& error) const {
    CsvReplayEngine replay;
    if (!replay.load(request.csv_path, error)) {
        return false;
    }

    auto windows = WalkForwardSplitter::split(replay.events().size(), request.train_window, request.test_window);
    if (windows.empty()) {
        error = "Not enough replay events for the selected train/test windows";
        return false;
    }

    auto configs = ParameterGrid::defaults_for(request.strategy);
    results.clear();
    for (const auto& config : configs) {
        OptimizationResult result;
        result.strategy = config.strategy;
        result.parameters = config.values;

        for (const auto& window : windows) {
            std::vector<ReplayEvent> train(replay.events().begin() + static_cast<std::ptrdiff_t>(window.train_start),
                                           replay.events().begin() + static_cast<std::ptrdiff_t>(window.train_end));
            std::vector<ReplayEvent> test(replay.events().begin() + static_cast<std::ptrdiff_t>(window.test_start),
                                          replay.events().begin() + static_cast<std::ptrdiff_t>(window.test_end));
            auto train_perf = evaluate(train, config);
            auto test_perf = evaluate(test, config);
            result.in_sample.pnl += train_perf.pnl;
            result.in_sample.trades += train_perf.trades;
            result.in_sample.volume += train_perf.volume;
            result.in_sample.max_drawdown = std::max(result.in_sample.max_drawdown, train_perf.max_drawdown);
            result.out_of_sample.pnl += test_perf.pnl;
            result.out_of_sample.trades += test_perf.trades;
            result.out_of_sample.volume += test_perf.volume;
            result.out_of_sample.max_drawdown = std::max(result.out_of_sample.max_drawdown, test_perf.max_drawdown);
            result.in_sample.pnl_curve.insert(result.in_sample.pnl_curve.end(), train_perf.pnl_curve.begin(), train_perf.pnl_curve.end());
            result.out_of_sample.pnl_curve.insert(result.out_of_sample.pnl_curve.end(), test_perf.pnl_curve.begin(), test_perf.pnl_curve.end());
            result.out_of_sample.drawdown_curve.insert(result.out_of_sample.drawdown_curve.end(), test_perf.drawdown_curve.begin(), test_perf.drawdown_curve.end());
            result.out_of_sample.inventory_curve.insert(result.out_of_sample.inventory_curve.end(), test_perf.inventory_curve.begin(), test_perf.inventory_curve.end());
        }
        result.rank_score = score(result.out_of_sample, request.rank_metric);
        results.push_back(result);
    }

    std::sort(results.begin(), results.end(), [](const OptimizationResult& a, const OptimizationResult& b) {
        return a.rank_score > b.rank_score;
    });
    return true;
}

StrategyPerformance StrategyOptimizer::evaluate(const std::vector<ReplayEvent>& events,
                                                const ParameterConfig& config) const {
    return evaluate_prices(price_series(events), config);
}

double StrategyOptimizer::score(const StrategyPerformance& performance, const std::string& metric) const {
    if (metric == "sharpe") return performance.sharpe_like;
    if (metric == "winRate") return performance.win_rate;
    if (metric == "drawdown") return -performance.max_drawdown;
    if (metric == "volume") return static_cast<double>(performance.volume);
    return performance.pnl;
}

std::vector<int> StrategyOptimizer::price_series(const std::vector<ReplayEvent>& events) const {
    std::vector<int> prices;
    int last = 100;
    for (const auto& event : events) {
        if (event.event_type == ReplayEventType::Trade && event.trade.price > 0) {
            last = event.trade.price;
            prices.push_back(last);
        } else if (event.event_type == ReplayEventType::Order && event.order.type == OrderType::Limit && event.order.price > 0) {
            last = event.order.price;
            prices.push_back(last);
        }
    }
    return prices;
}

StrategyPerformance StrategyOptimizer::evaluate_prices(const std::vector<int>& prices,
                                                       const ParameterConfig& config) const {
    StrategyPerformance perf;
    perf.name = config.strategy;
    std::vector<double> returns;
    const double fee = 0.01;
    const double slippage = 0.02;

    for (std::size_t i = 1; i < prices.size(); ++i) {
        int signal = 0;
        int quantity = static_cast<int>(get_param(config, "order_size", 1.0));

        if (config.strategy == "Momentum") {
            std::size_t lookback = static_cast<std::size_t>(get_param(config, "lookback", 4.0));
            double threshold = get_param(config, "momentum_threshold", 1.0);
            if (i >= lookback) {
                int diff = prices[i] - prices[i - lookback];
                signal = diff > threshold ? 1 : (diff < -threshold ? -1 : 0);
            }
        } else if (config.strategy == "Mean Reversion") {
            std::size_t window = static_cast<std::size_t>(get_param(config, "rolling_window", 8.0));
            double threshold = get_param(config, "z_score_threshold", 1.0);
            if (i >= window) {
                std::vector<double> slice;
                for (std::size_t j = i - window; j < i; ++j) slice.push_back(prices[j]);
                double sd = std::max(1.0, stdev(slice));
                double z = (static_cast<double>(prices[i]) - mean(slice)) / sd;
                signal = z > threshold ? -1 : (z < -threshold ? 1 : 0);
            }
        } else if (config.strategy == "Book Imbalance") {
            std::size_t depth = static_cast<std::size_t>(get_param(config, "depth_levels", 5.0));
            double threshold = get_param(config, "imbalance_threshold", 0.35);
            if (i >= depth) {
                int up = 0;
                int down = 0;
                for (std::size_t j = i - depth + 1; j <= i; ++j) {
                    up += prices[j] >= prices[j - 1] ? 1 : 0;
                    down += prices[j] < prices[j - 1] ? 1 : 0;
                }
                double imbalance = static_cast<double>(up - down) / static_cast<double>(depth);
                signal = imbalance > threshold ? 1 : (imbalance < -threshold ? -1 : 0);
            }
        } else {
            double spread = get_param(config, "spread_width", 1.0);
            double skew = get_param(config, "inventory_skew", 0.0);
            double max_inventory = get_param(config, "max_inventory", 20.0);
            if (std::abs(perf.inventory) < max_inventory && std::abs(prices[i] - prices[i - 1]) >= spread) {
                signal = prices[i] > prices[i - 1] ? -1 : 1;
                if (perf.inventory > 0) signal -= static_cast<int>(skew);
                if (perf.inventory < 0) signal += static_cast<int>(skew);
            }
        }

        double previous = perf.pnl;
        if (signal > 0) {
            apply_trade(perf, returns, 1, prices[i], quantity, fee, slippage);
        } else if (signal < 0) {
            apply_trade(perf, returns, -1, prices[i], quantity, fee, slippage);
        }
        mark(perf, returns, prices[i], previous);
    }

    if (perf.trades > 0) {
        perf.win_rate = static_cast<double>(perf.winning_trades) / static_cast<double>(perf.trades);
    }
    double sd = stdev(returns);
    perf.sharpe_like = sd == 0.0 ? 0.0 : mean(returns) / sd;
    return perf;
}

void StrategyOptimizer::apply_trade(StrategyPerformance& performance,
                                    std::vector<double>&,
                                    int side,
                                    int price,
                                    int quantity,
                                    double fee_per_share,
                                    double slippage_per_share) const {
    double before = performance.pnl;
    double fees = fee_per_share * quantity;
    double slip = slippage_per_share * quantity;
    if (side > 0) {
        performance.inventory += quantity;
        performance.cash -= price * quantity + fees + slip;
    } else {
        performance.inventory -= quantity;
        performance.cash += price * quantity - fees - slip;
    }
    performance.trades++;
    performance.volume += quantity;
    performance.fees_paid += fees;
    performance.slippage_paid += slip;
    performance.avg_fill_price =
        ((performance.avg_fill_price * static_cast<double>(performance.volume - quantity)) +
         static_cast<double>(price * quantity)) / static_cast<double>(performance.volume);
    if (performance.pnl > before) {
        performance.winning_trades++;
    }
}

void StrategyOptimizer::mark(StrategyPerformance& performance,
                             std::vector<double>& returns,
                             int mark_price,
                             double previous_pnl) const {
    performance.pnl = performance.cash + static_cast<double>(performance.inventory * mark_price);
    performance.peak_pnl = std::max(performance.peak_pnl, performance.pnl);
    performance.drawdown = performance.peak_pnl - performance.pnl;
    performance.max_drawdown = std::max(performance.max_drawdown, performance.drawdown);
    if (performance.inventory != 0) {
        performance.exposure_time++;
    }
    returns.push_back(performance.pnl - previous_pnl);
    performance.pnl_curve.push_back(performance.pnl);
    performance.drawdown_curve.push_back(performance.drawdown);
    performance.inventory_curve.push_back(performance.inventory);
}
