#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "csv_replay.hpp"
#include "strategy.hpp"

struct ParameterConfig {
    std::string strategy;
    std::map<std::string, double> values;
};

struct OptimizationRequest {
    std::string csv_path;
    std::string strategy = "Market Making";
    std::string rank_metric = "pnl";
    std::size_t train_window = 60;
    std::size_t test_window = 30;
};

struct WalkForwardWindow {
    std::size_t train_start = 0;
    std::size_t train_end = 0;
    std::size_t test_start = 0;
    std::size_t test_end = 0;
};

struct OptimizationResult {
    std::string strategy;
    std::map<std::string, double> parameters;
    StrategyPerformance in_sample;
    StrategyPerformance out_of_sample;
    double rank_score = 0.0;
};

class ParameterGrid {
public:
    static std::vector<ParameterConfig> defaults_for(const std::string& strategy);
};

class WalkForwardSplitter {
public:
    static std::vector<WalkForwardWindow> split(std::size_t event_count,
                                                std::size_t train_window,
                                                std::size_t test_window);
};

class StrategyOptimizer {
public:
    bool run(const OptimizationRequest& request,
             std::vector<OptimizationResult>& results,
             std::string& error) const;

    StrategyPerformance evaluate(const std::vector<ReplayEvent>& events,
                                 const ParameterConfig& config) const;

    double score(const StrategyPerformance& performance, const std::string& metric) const;

private:
    std::vector<int> price_series(const std::vector<ReplayEvent>& events) const;
    StrategyPerformance evaluate_prices(const std::vector<int>& prices,
                                        const ParameterConfig& config) const;
    void apply_trade(StrategyPerformance& performance,
                     std::vector<double>& returns,
                     int side,
                     int price,
                     int quantity,
                     double fee_per_share,
                     double slippage_per_share) const;
    void mark(StrategyPerformance& performance,
              std::vector<double>& returns,
              int mark_price,
              double previous_pnl) const;
};
