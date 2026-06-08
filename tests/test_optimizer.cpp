#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "optimizer.hpp"

namespace fs = std::filesystem;

int main() {
    auto mm = ParameterGrid::defaults_for("Market Making");
    auto mom = ParameterGrid::defaults_for("Momentum");
    auto mean = ParameterGrid::defaults_for("Mean Reversion");
    auto imb = ParameterGrid::defaults_for("Book Imbalance");
    assert(!mm.empty());
    assert(!mom.empty());
    assert(!mean.empty());
    assert(!imb.empty());
    assert(mm.front().values.count("spread_width") == 1);
    assert(mom.front().values.count("lookback") == 1);
    assert(mean.front().values.count("rolling_window") == 1);
    assert(imb.front().values.count("depth_levels") == 1);

    auto windows = WalkForwardSplitter::split(100, 40, 20);
    assert(windows.size() == 3);
    assert(windows[0].train_start == 0);
    assert(windows[0].train_end == 40);
    assert(windows[0].test_start == 40);
    assert(windows[0].test_end == 60);
    assert(windows[1].train_start == 20);
    assert(windows[1].test_start == 60);

    StrategyOptimizer optimizer;
    std::vector<ReplayEvent> events;
    for (std::uint64_t i = 1; i <= 80; ++i) {
        ReplayEvent event;
        event.event_type = ReplayEventType::Trade;
        event.timestamp = i;
        event.trade = Trade{i, i + 100, static_cast<int>(100 + (i % 9)), 2, i};
        events.push_back(event);
    }

    auto perf = optimizer.evaluate(events, mom.front());
    assert(perf.pnl_curve.size() > 0);
    assert(perf.trades > 0);
    assert(perf.volume > 0);
    assert(optimizer.score(perf, "pnl") == perf.pnl);

    fs::path path = fs::temp_directory_path() / "optimizer_replay.csv";
    {
        std::ofstream out(path);
        out << "timestamp,event_type,buy_order_id,sell_order_id,price,quantity\n";
        for (int i = 1; i <= 120; ++i) {
            out << i << ",trade," << i << "," << (i + 1000) << "," << (100 + (i % 7)) << ",2\n";
        }
    }

    OptimizationRequest request;
    request.csv_path = path.string();
    request.strategy = "Momentum";
    request.rank_metric = "pnl";
    request.train_window = 40;
    request.test_window = 20;

    std::vector<OptimizationResult> results;
    std::string error;
    assert(optimizer.run(request, results, error));
    assert(results.size() == mom.size());
    assert(results.front().out_of_sample.pnl_curve.size() > 0);
    for (const auto& result : results) {
        assert(result.strategy == "Momentum");
    }

    const auto same_path = request.csv_path;
    for (const std::string strategy : {"Market Making", "Momentum", "Mean Reversion", "Book Imbalance"}) {
        request.csv_path = same_path;
        request.strategy = strategy;
        results.clear();
        assert(optimizer.run(request, results, error));
        assert(!results.empty());
        assert(results.front().strategy == strategy);
        assert(request.csv_path == same_path);
    }

    fs::remove(path);
    std::cout << "Optimizer tests passed!" << std::endl;
    return 0;
}
