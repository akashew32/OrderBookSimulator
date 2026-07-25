#include <cassert>
#include <fstream>
#include <iostream>

#include "benchmark_common.hpp"

void test_cli_parsing() {
    const char* argv[] = {"bench", "--scenario", "mixed", "--operations", "1234", "--iterations", "3", "--seed", "9"};
    auto options = lob_bench::parse_args(9, const_cast<char**>(argv));
    assert(options.scenario == lob_bench::Scenario::Mixed);
    assert(options.operations == 1234);
    assert(options.iterations == 3);
    assert(options.seed == 9);
}

void test_deterministic_generation() {
    lob_bench::WorkloadGenerator a(lob_bench::Scenario::DeepBook, 1000, 42);
    lob_bench::WorkloadGenerator b(lob_bench::Scenario::DeepBook, 1000, 42);
    auto left = a.generate();
    auto right = b.generate();
    assert(left.size() == right.size());
    for (std::size_t i = 0; i < left.size(); ++i) {
        assert(left[i].type == right[i].type);
        assert(left[i].order.id == right[i].order.id);
        assert(left[i].order.price == right[i].order.price);
        assert(left[i].order.quantity == right[i].order.quantity);
    }
}

void test_percentiles_and_stats() {
    std::vector<double> values{1, 2, 3, 4, 5};
    assert(lob_bench::percentile(values, 50) == 3);
    assert(lob_bench::percentile(values, 0) == 1);
    assert(lob_bench::percentile(values, 100) == 5);
    assert(lob_bench::mean(values) == 3);
    assert(lob_bench::stddev(values) > 1.5);
}

void test_counts_and_invalid_ops() {
    std::vector<lob_bench::Operation> ops{
        {lob_bench::OperationType::Submit, Order{1, Side::Buy, OrderType::Limit, 100, 10, 1}},
        {lob_bench::OperationType::Cancel, Order{}, 999},
        {lob_bench::OperationType::Modify, Order{}, 999, 101, 5},
        {lob_bench::OperationType::Submit, Order{2, Side::Sell, OrderType::Limit, 100, 4, 2}}
    };
    auto run = lob_bench::execute_workload(ops, lob_bench::Scenario::Mixed, 7, 1);
    assert(run.requested_operations == 4);
    assert(run.processed_operations == 2);
    assert(run.submitted_orders == 2);
    assert(run.cancellations == 0);
    assert(run.modifications == 0);
    assert(run.generated_trades == 1);
    assert(run.invariants_ok);
}

void test_output_generation() {
    std::vector<lob_bench::Operation> ops{
        {lob_bench::OperationType::Submit, Order{1, Side::Buy, OrderType::Limit, 99, 1, 1}}
    };
    auto run = lob_bench::execute_workload(ops, lob_bench::Scenario::Resting, 1, 1);
    auto aggregate = lob_bench::aggregate_runs({run});
    lob_bench::write_csv("/tmp/lob_bench_test.csv", {run});
    lob_bench::write_json("/tmp/lob_bench_test.json", {run}, aggregate);
    std::ifstream csv("/tmp/lob_bench_test.csv");
    std::ifstream json("/tmp/lob_bench_test.json");
    assert(csv.good());
    assert(json.good());
}

void test_price_time_priority_validation() {
    MatchingEngine engine;
    engine.process_order(Order{1, Side::Buy, OrderType::Limit, 100, 5, 1});
    engine.process_order(Order{2, Side::Buy, OrderType::Limit, 100, 5, 2});
    auto trades = engine.process_order(Order{3, Side::Sell, OrderType::Market, 0, 5, 3});
    assert(trades.size() == 1);
    assert(trades[0].buy_order_id == 1);
    assert(lob_bench::validate_invariants(engine));
}

int main() {
    test_cli_parsing();
    test_deterministic_generation();
    test_percentiles_and_stats();
    test_counts_and_invalid_ops();
    test_output_generation();
    test_price_time_priority_validation();
    std::cout << "Benchmark suite tests passed!" << std::endl;
    return 0;
}
