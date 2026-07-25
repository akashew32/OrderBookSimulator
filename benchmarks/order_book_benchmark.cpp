#include "benchmark_common.hpp"

#include <iomanip>
#include <iostream>
#include <sys/stat.h>

namespace {

void usage() {
    std::cout
        << "Usage: order_book_benchmark [--scenario mixed] [--operations N] [--iterations N]\n"
        << "                            [--warmups N] [--seed N] [--latency-sample-interval N]\n"
        << "                            [--results-dir benchmarks/results] [--all]\n";
}

void ensure_dir(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

void print_run(const lob_bench::RunMetrics& run) {
    std::cout << std::left << std::setw(13) << run.scenario
              << std::right << std::setw(12) << run.processed_operations
              << std::setw(14) << static_cast<std::uint64_t>(run.operations_per_second)
              << std::setw(12) << run.generated_trades
              << std::setw(12) << std::fixed << std::setprecision(2) << run.median_latency_us
              << std::setw(12) << run.p99_latency_us
              << std::setw(10) << (run.invariants_ok ? "ok" : "fail")
              << '\n';
}

std::vector<lob_bench::Scenario> scenarios_for(const lob_bench::Options& options) {
    if (!options.all) return {options.scenario};
    return {
        lob_bench::Scenario::Resting,
        lob_bench::Scenario::Marketable,
        lob_bench::Scenario::Mixed,
        lob_bench::Scenario::DeepBook,
        lob_bench::Scenario::HighCancel
    };
}

std::vector<std::size_t> sizes_for(const lob_bench::Options& options) {
    if (!options.all) return {options.operations};
    return {100000, 1000000, 5000000};
}

} // namespace

int main(int argc, char** argv) {
    lob_bench::Options options;
    try {
        options = lob_bench::parse_args(argc, argv);
    } catch (const std::invalid_argument& error) {
        usage();
        if (std::string(error.what()) == "help") return 0;
        std::cerr << error.what() << '\n';
        return 2;
    }

    if (lob_bench::build_mode_string() != "Release") {
        std::cerr << "Warning: Debug benchmark results are for correctness only, not resume metrics.\n";
    }

    ensure_dir("benchmarks");
    ensure_dir(options.results_dir);

    std::cout << std::left << std::setw(13) << "scenario"
              << std::right << std::setw(12) << "ops"
              << std::setw(14) << "ops/sec"
              << std::setw(12) << "trades"
              << std::setw(12) << "median_us"
              << std::setw(12) << "p99_us"
              << std::setw(10) << "valid"
              << '\n';

    std::vector<lob_bench::RunMetrics> all_runs;
    std::vector<lob_bench::AggregateMetrics> aggregates;
    for (lob_bench::Scenario scenario : scenarios_for(options)) {
        for (std::size_t operations : sizes_for(options)) {
            std::vector<lob_bench::RunMetrics> measured;
            for (int i = 0; i < options.warmups + options.iterations; ++i) {
                const std::uint64_t seed = options.seed + static_cast<std::uint64_t>(i);
                lob_bench::WorkloadGenerator generator(scenario, operations, seed);
                const auto workload = generator.generate();
                const auto run = lob_bench::execute_workload(workload, scenario, seed, options.sample_interval);
                if (!run.invariants_ok) {
                    std::cerr << "Invariant validation failed for " << run.scenario << '\n';
                    return 3;
                }
                if (i >= options.warmups) {
                    measured.push_back(run);
                    all_runs.push_back(run);
                    print_run(run);
                }
            }
            aggregates.push_back(lob_bench::aggregate_runs(measured));
        }
    }

    const std::string stamp = lob_bench::timestamp_for_filename();
    const std::string prefix = options.results_dir + "/benchmark_" + stamp;
    lob_bench::write_csv(prefix + ".csv", all_runs);
    lob_bench::write_json(prefix + ".json", all_runs, lob_bench::aggregate_runs(all_runs));

    std::cout << "\nAggregate throughput\n";
    for (const auto& aggregate : aggregates) {
        std::cout << aggregate.scenario << " ops=" << aggregate.operations
                  << " median_ops_sec=" << static_cast<std::uint64_t>(aggregate.median_throughput)
                  << " mean_ops_sec=" << static_cast<std::uint64_t>(aggregate.mean_throughput)
                  << " stddev=" << static_cast<std::uint64_t>(aggregate.stddev_throughput)
                  << " best=" << static_cast<std::uint64_t>(aggregate.best_throughput)
                  << " worst=" << static_cast<std::uint64_t>(aggregate.worst_throughput)
                  << " median_us=" << std::fixed << std::setprecision(2) << aggregate.median_latency_us
                  << " p95_us=" << aggregate.p95_latency_us
                  << " p99_us=" << aggregate.p99_latency_us << '\n';
    }
    std::cout << "\nWrote " << prefix << ".csv and " << prefix << ".json\n";
    return 0;
}
