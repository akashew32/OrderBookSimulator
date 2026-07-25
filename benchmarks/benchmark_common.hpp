#pragma once

#include "matching_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lob_bench {

enum class Scenario { Resting, Marketable, Mixed, DeepBook, HighCancel };
enum class OperationType { Submit, Cancel, Modify };

struct Operation {
    OperationType type = OperationType::Submit;
    Order order{};
    std::uint64_t order_id = 0;
    int new_price = 0;
    int new_quantity = 0;
};

struct Options {
    Scenario scenario = Scenario::Mixed;
    std::size_t operations = 100000;
    int iterations = 5;
    int warmups = 1;
    std::uint64_t seed = 42;
    std::size_t sample_interval = 10;
    bool all = false;
    std::string results_dir = "benchmarks/results";
};

struct RunMetrics {
    std::string scenario;
    std::size_t requested_operations = 0;
    std::size_t processed_operations = 0;
    std::size_t submitted_orders = 0;
    std::size_t cancellations = 0;
    std::size_t modifications = 0;
    std::size_t generated_trades = 0;
    std::uint64_t total_traded_volume = 0;
    std::size_t peak_active_orders = 0;
    std::size_t final_active_orders = 0;
    double elapsed_seconds = 0.0;
    double operations_per_second = 0.0;
    double orders_per_second = 0.0;
    double trades_per_second = 0.0;
    double mean_latency_us = 0.0;
    double median_latency_us = 0.0;
    double p95_latency_us = 0.0;
    double p99_latency_us = 0.0;
    double min_latency_us = 0.0;
    double max_latency_us = 0.0;
    std::size_t latency_samples = 0;
    std::size_t sample_interval = 1;
    long peak_rss_kb = 0;
    std::uint64_t seed = 0;
    std::string compiler;
    std::string build_mode;
    std::string os;
    std::string cpu;
    bool debug_build = false;
    bool invariants_ok = false;
};

struct AggregateMetrics {
    std::string scenario;
    std::size_t operations = 0;
    int measured_iterations = 0;
    double mean_throughput = 0.0;
    double median_throughput = 0.0;
    double stddev_throughput = 0.0;
    double best_throughput = 0.0;
    double worst_throughput = 0.0;
    double median_latency_us = 0.0;
    double p95_latency_us = 0.0;
    double p99_latency_us = 0.0;
};

inline std::string scenario_name(Scenario scenario) {
    switch (scenario) {
        case Scenario::Resting: return "resting";
        case Scenario::Marketable: return "marketable";
        case Scenario::Mixed: return "mixed";
        case Scenario::DeepBook: return "deep-book";
        case Scenario::HighCancel: return "high-cancel";
    }
    return "mixed";
}

inline Scenario parse_scenario(const std::string& value) {
    if (value == "resting") return Scenario::Resting;
    if (value == "marketable") return Scenario::Marketable;
    if (value == "mixed") return Scenario::Mixed;
    if (value == "deep-book" || value == "deep_book") return Scenario::DeepBook;
    if (value == "high-cancel" || value == "high_cancel") return Scenario::HighCancel;
    throw std::invalid_argument("unknown scenario: " + value);
}

inline Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&](const std::string& name) {
            if (i + 1 >= argc) throw std::invalid_argument(name + " requires a value");
            return std::string(argv[++i]);
        };
        if (arg == "--all") options.all = true;
        else if (arg == "--scenario") options.scenario = parse_scenario(value(arg));
        else if (arg == "--operations") options.operations = static_cast<std::size_t>(std::stoull(value(arg)));
        else if (arg == "--iterations") options.iterations = std::stoi(value(arg));
        else if (arg == "--warmups") options.warmups = std::stoi(value(arg));
        else if (arg == "--seed") options.seed = static_cast<std::uint64_t>(std::stoull(value(arg)));
        else if (arg == "--latency-sample-interval") options.sample_interval = static_cast<std::size_t>(std::stoull(value(arg)));
        else if (arg == "--results-dir") options.results_dir = value(arg);
        else if (arg == "--help" || arg == "-h") throw std::invalid_argument("help");
        else throw std::invalid_argument("unknown argument: " + arg);
    }
    if (options.operations == 0) throw std::invalid_argument("--operations must be positive");
    if (options.iterations < 1) throw std::invalid_argument("--iterations must be at least 1");
    if (options.warmups < 0) throw std::invalid_argument("--warmups cannot be negative");
    if (options.sample_interval == 0) options.sample_interval = 1;
    return options;
}

inline double percentile(std::vector<double> values, double pct) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double rank = (pct / 100.0) * static_cast<double>(values.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(rank));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(rank));
    if (lo == hi) return values[lo];
    const double weight = rank - static_cast<double>(lo);
    return values[lo] * (1.0 - weight) + values[hi] * weight;
}

inline double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

inline double stddev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double avg = mean(values);
    double total = 0.0;
    for (double value : values) {
        const double diff = value - avg;
        total += diff * diff;
    }
    return std::sqrt(total / static_cast<double>(values.size() - 1));
}

inline std::string json_escape(const std::string& value) {
    std::string escaped;
    for (char ch : value) {
        if (ch == '"' || ch == '\\') escaped.push_back('\\');
        if (ch == '\n') escaped += "\\n";
        else escaped.push_back(ch);
    }
    return escaped;
}

class WorkloadGenerator {
public:
    WorkloadGenerator(Scenario scenario, std::size_t operations, std::uint64_t seed)
        : scenario_(scenario), operations_(operations), rng_(seed) {}

    std::vector<Operation> generate() {
        std::vector<Operation> ops;
        ops.reserve(operations_);
        switch (scenario_) {
            case Scenario::Resting: resting(ops); break;
            case Scenario::Marketable: marketable(ops); break;
            case Scenario::Mixed: mixed(ops); break;
            case Scenario::DeepBook: deep_book(ops); break;
            case Scenario::HighCancel: high_cancel(ops); break;
        }
        if (ops.size() > operations_) ops.resize(operations_);
        return ops;
    }

private:
    Order order(Side side, int price, int quantity) {
        return Order{next_id_++, side, OrderType::Limit, price, quantity, timestamp_++};
    }

    std::uint64_t pick(std::vector<std::uint64_t>& active) {
        if (active.empty()) return 0;
        std::uniform_int_distribution<std::size_t> dist(0, active.size() - 1);
        const std::size_t index = dist(rng_);
        const std::uint64_t id = active[index];
        active[index] = active.back();
        active.pop_back();
        return id;
    }

    void seed_spread(std::vector<Operation>& ops, std::vector<std::uint64_t>& active, int pairs) {
        for (int i = 0; i < pairs && ops.size() + 1 < operations_; ++i) {
            Order bid = order(Side::Buy, 99 - (i % 5), 100);
            Order ask = order(Side::Sell, 101 + (i % 5), 100);
            active.push_back(bid.id);
            active.push_back(ask.id);
            ops.push_back(Operation{OperationType::Submit, bid});
            ops.push_back(Operation{OperationType::Submit, ask});
        }
    }

    void resting(std::vector<Operation>& ops) {
        std::uniform_int_distribution<int> qty(1, 20);
        for (std::size_t i = 0; i < operations_; ++i) {
            const bool buy = i % 2 == 0;
            ops.push_back(Operation{OperationType::Submit, order(buy ? Side::Buy : Side::Sell, buy ? 90 - static_cast<int>(i % 4) : 110 + static_cast<int>(i % 4), qty(rng_))});
        }
    }

    void marketable(std::vector<Operation>& ops) {
        std::vector<std::uint64_t> active;
        seed_spread(ops, active, 500);
        std::uniform_int_distribution<int> qty(1, 40);
        while (ops.size() < operations_) {
            const bool buy = ops.size() % 2 == 0;
            ops.push_back(Operation{OperationType::Submit, order(buy ? Side::Buy : Side::Sell, buy ? 102 : 98, qty(rng_))});
            if (ops.size() % 6 == 0) seed_spread(ops, active, 1);
        }
    }

    void mixed(std::vector<Operation>& ops) {
        std::vector<std::uint64_t> active;
        seed_spread(ops, active, 200);
        std::uniform_int_distribution<int> pct(1, 100);
        std::uniform_int_distribution<int> qty(1, 35);
        while (ops.size() < operations_) {
            const int roll = pct(rng_);
            if (roll <= 55 || active.size() < 10) {
                const bool buy = roll % 2 == 0;
                Order resting_order = order(buy ? Side::Buy : Side::Sell, buy ? 98 : 102, qty(rng_));
                active.push_back(resting_order.id);
                ops.push_back(Operation{OperationType::Submit, resting_order});
            } else if (roll <= 75) {
                const bool buy = roll % 2 == 0;
                ops.push_back(Operation{OperationType::Submit, order(buy ? Side::Buy : Side::Sell, buy ? 103 : 97, qty(rng_))});
            } else if (roll <= 90) {
                ops.push_back(Operation{OperationType::Cancel, Order{}, pick(active)});
            } else {
                const std::uint64_t id = pick(active);
                ops.push_back(Operation{OperationType::Modify, Order{}, id, roll % 2 == 0 ? 98 : 102, qty(rng_)});
                if (id != 0) active.push_back(id);
            }
        }
    }

    void deep_book(std::vector<Operation>& ops) {
        std::uniform_int_distribution<int> qty(1, 25);
        std::uniform_int_distribution<int> offset(1, 5000);
        for (std::size_t i = 0; i < operations_; ++i) {
            const bool buy = i % 2 == 0;
            ops.push_back(Operation{OperationType::Submit, order(buy ? Side::Buy : Side::Sell, buy ? 10000 - offset(rng_) : 10002 + offset(rng_), qty(rng_))});
        }
    }

    void high_cancel(std::vector<Operation>& ops) {
        std::vector<std::uint64_t> active;
        std::uniform_int_distribution<int> qty(1, 20);
        while (ops.size() < operations_) {
            for (int i = 0; i < 3 && ops.size() < operations_; ++i) {
                const bool buy = ops.size() % 2 == 0;
                Order resting_order = order(buy ? Side::Buy : Side::Sell, buy ? 97 : 103, qty(rng_));
                active.push_back(resting_order.id);
                ops.push_back(Operation{OperationType::Submit, resting_order});
            }
            for (int i = 0; i < 2 && !active.empty() && ops.size() < operations_; ++i) {
                ops.push_back(Operation{OperationType::Cancel, Order{}, pick(active)});
            }
        }
    }

    Scenario scenario_;
    std::size_t operations_;
    std::mt19937_64 rng_;
    std::uint64_t next_id_ = 1;
    std::uint64_t timestamp_ = 1;
};

inline std::string compiler_string() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#else
    return "unknown";
#endif
}

inline std::string build_mode_string() {
#ifdef NDEBUG
    return "Release";
#else
    return "Debug";
#endif
}

inline std::string os_string() {
#if defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

inline std::string cpu_string() {
#if defined(__APPLE__)
    FILE* pipe = popen("sysctl -n machdep.cpu.brand_string 2>/dev/null", "r");
#elif defined(__linux__)
    FILE* pipe = popen("grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2-", "r");
#else
    FILE* pipe = nullptr;
#endif
    if (!pipe) return "unknown";
    char buffer[256];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe)) result = buffer;
    pclose(pipe);
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    while (!result.empty() && result.front() == ' ') result.erase(result.begin());
    return result.empty() ? "unknown" : result;
}

inline long peak_rss_kb();

inline bool validate_invariants(const MatchingEngine& engine) {
    const auto& book = engine.order_book();
    if (book.has_best_bid() && book.has_best_ask() && book.best_bid() >= book.best_ask()) return false;
    std::unordered_set<std::uint64_t> ids;
    for (const auto& order : book.active_orders(1000000)) {
        if (order.quantity <= 0) return false;
        if (!ids.insert(order.id).second) return false;
    }
    return true;
}

inline RunMetrics execute_workload(const std::vector<Operation>& operations, Scenario scenario, std::uint64_t seed, std::size_t sample_interval) {
    MatchingEngine engine;
    RunMetrics metrics;
    metrics.scenario = scenario_name(scenario);
    metrics.requested_operations = operations.size();
    metrics.seed = seed;
    metrics.compiler = compiler_string();
    metrics.build_mode = build_mode_string();
    metrics.os = os_string();
    metrics.cpu = cpu_string();
    metrics.debug_build = metrics.build_mode != "Release";
    metrics.sample_interval = sample_interval;
    std::vector<double> latencies;
    latencies.reserve((operations.size() / sample_interval) + 1);
    std::unordered_set<std::uint64_t> active_ids;

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < operations.size(); ++i) {
        const Operation& op = operations[i];
        const bool sample = i % sample_interval == 0;
        const auto op_start = sample ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        bool processed = false;
        if (op.type == OperationType::Submit) {
            auto trades = engine.process_order(op.order);
            processed = true;
            ++metrics.submitted_orders;
            metrics.generated_trades += trades.size();
            for (const auto& trade : trades) {
                if (trade.quantity <= 0) throw std::runtime_error("non-positive trade quantity");
                metrics.total_traded_volume += static_cast<std::uint64_t>(trade.quantity);
                if (!engine.order_book().has_order(trade.buy_order_id)) active_ids.erase(trade.buy_order_id);
                if (!engine.order_book().has_order(trade.sell_order_id)) active_ids.erase(trade.sell_order_id);
            }
            if (engine.order_book().has_order(op.order.id)) active_ids.insert(op.order.id);
        } else if (op.type == OperationType::Cancel) {
            processed = engine.cancel_order(op.order_id);
            if (processed) {
                ++metrics.cancellations;
                active_ids.erase(op.order_id);
            }
        } else {
            processed = engine.modify_order(op.order_id, op.new_price, op.new_quantity);
            if (processed) ++metrics.modifications;
        }
        if (sample) {
            const auto op_end = std::chrono::steady_clock::now();
            latencies.push_back(std::chrono::duration<double, std::micro>(op_end - op_start).count());
        }
        if (processed) ++metrics.processed_operations;
        metrics.peak_active_orders = std::max(metrics.peak_active_orders, active_ids.size());
    }
    const auto end = std::chrono::steady_clock::now();

    metrics.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    metrics.operations_per_second = metrics.processed_operations / std::max(metrics.elapsed_seconds, 1e-12);
    metrics.orders_per_second = metrics.submitted_orders / std::max(metrics.elapsed_seconds, 1e-12);
    metrics.trades_per_second = metrics.generated_trades / std::max(metrics.elapsed_seconds, 1e-12);
    metrics.latency_samples = latencies.size();
    metrics.mean_latency_us = mean(latencies);
    metrics.median_latency_us = percentile(latencies, 50);
    metrics.p95_latency_us = percentile(latencies, 95);
    metrics.p99_latency_us = percentile(latencies, 99);
    if (!latencies.empty()) {
        metrics.min_latency_us = *std::min_element(latencies.begin(), latencies.end());
        metrics.max_latency_us = *std::max_element(latencies.begin(), latencies.end());
    }
    metrics.final_active_orders = active_ids.size();
    metrics.peak_rss_kb = peak_rss_kb();
    metrics.invariants_ok = validate_invariants(engine);
    return metrics;
}

inline AggregateMetrics aggregate_runs(const std::vector<RunMetrics>& runs) {
    AggregateMetrics aggregate;
    if (runs.empty()) return aggregate;
    aggregate.scenario = runs.front().scenario;
    aggregate.operations = runs.front().requested_operations;
    aggregate.measured_iterations = static_cast<int>(runs.size());
    std::vector<double> throughput, medians, p95s, p99s;
    for (const auto& run : runs) {
        throughput.push_back(run.operations_per_second);
        medians.push_back(run.median_latency_us);
        p95s.push_back(run.p95_latency_us);
        p99s.push_back(run.p99_latency_us);
    }
    aggregate.mean_throughput = mean(throughput);
    aggregate.median_throughput = percentile(throughput, 50);
    aggregate.stddev_throughput = stddev(throughput);
    aggregate.best_throughput = *std::max_element(throughput.begin(), throughput.end());
    aggregate.worst_throughput = *std::min_element(throughput.begin(), throughput.end());
    aggregate.median_latency_us = percentile(medians, 50);
    aggregate.p95_latency_us = percentile(p95s, 50);
    aggregate.p99_latency_us = percentile(p99s, 50);
    return aggregate;
}

inline std::string timestamp_for_filename() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H%M%S");
    return out.str();
}

inline void write_csv(const std::string& path, const std::vector<RunMetrics>& runs) {
    std::ofstream out(path);
    out << "scenario,requested_operations,processed_operations,submitted_orders,cancellations,modifications,generated_trades,total_traded_volume,peak_active_order_count,final_active_order_count,elapsed_seconds,operations_per_second,orders_per_second,trades_per_second,mean_latency_us,median_latency_us,p95_latency_us,p99_latency_us,min_latency_us,max_latency_us,latency_samples,latency_sample_interval,peak_rss_kb,seed,compiler,build_mode,os,cpu,invariants_ok\n";
    for (const auto& run : runs) {
        out << run.scenario << ',' << run.requested_operations << ',' << run.processed_operations << ','
            << run.submitted_orders << ',' << run.cancellations << ',' << run.modifications << ','
            << run.generated_trades << ',' << run.total_traded_volume << ',' << run.peak_active_orders << ','
            << run.final_active_orders << ',' << run.elapsed_seconds << ',' << run.operations_per_second << ','
            << run.orders_per_second << ',' << run.trades_per_second << ',' << run.mean_latency_us << ','
            << run.median_latency_us << ',' << run.p95_latency_us << ',' << run.p99_latency_us << ','
            << run.min_latency_us << ',' << run.max_latency_us << ',' << run.latency_samples << ','
            << run.sample_interval << ',' << run.peak_rss_kb << ',' << run.seed << ",\""
            << json_escape(run.compiler) << "\"," << run.build_mode << ',' << run.os << ",\""
            << json_escape(run.cpu) << "\"," << (run.invariants_ok ? "true" : "false") << '\n';
    }
}

inline void write_json(const std::string& path, const std::vector<RunMetrics>& runs, const AggregateMetrics& aggregate) {
    std::ofstream out(path);
    out << "{\n  \"aggregate\": {\n";
    out << "    \"scenario\": \"" << json_escape(aggregate.scenario) << "\",\n";
    out << "    \"operations\": " << aggregate.operations << ",\n";
    out << "    \"measured_iterations\": " << aggregate.measured_iterations << ",\n";
    out << "    \"mean_throughput\": " << aggregate.mean_throughput << ",\n";
    out << "    \"median_throughput\": " << aggregate.median_throughput << ",\n";
    out << "    \"stddev_throughput\": " << aggregate.stddev_throughput << ",\n";
    out << "    \"best_throughput\": " << aggregate.best_throughput << ",\n";
    out << "    \"worst_throughput\": " << aggregate.worst_throughput << ",\n";
    out << "    \"aggregate_median_latency_us\": " << aggregate.median_latency_us << ",\n";
    out << "    \"aggregate_p95_latency_us\": " << aggregate.p95_latency_us << ",\n";
    out << "    \"aggregate_p99_latency_us\": " << aggregate.p99_latency_us << "\n  },\n  \"runs\": [\n";
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        out << "    {\n";
        out << "      \"scenario\": \"" << json_escape(run.scenario) << "\",\n";
        out << "      \"requested_operations\": " << run.requested_operations << ",\n";
        out << "      \"processed_operations\": " << run.processed_operations << ",\n";
        out << "      \"submitted_orders\": " << run.submitted_orders << ",\n";
        out << "      \"cancellations\": " << run.cancellations << ",\n";
        out << "      \"modifications\": " << run.modifications << ",\n";
        out << "      \"generated_trades\": " << run.generated_trades << ",\n";
        out << "      \"total_traded_volume\": " << run.total_traded_volume << ",\n";
        out << "      \"peak_active_order_count\": " << run.peak_active_orders << ",\n";
        out << "      \"final_active_order_count\": " << run.final_active_orders << ",\n";
        out << "      \"elapsed_seconds\": " << run.elapsed_seconds << ",\n";
        out << "      \"operations_per_second\": " << run.operations_per_second << ",\n";
        out << "      \"orders_per_second\": " << run.orders_per_second << ",\n";
        out << "      \"trades_per_second\": " << run.trades_per_second << ",\n";
        out << "      \"mean_latency_us\": " << run.mean_latency_us << ",\n";
        out << "      \"median_latency_us\": " << run.median_latency_us << ",\n";
        out << "      \"p95_latency_us\": " << run.p95_latency_us << ",\n";
        out << "      \"p99_latency_us\": " << run.p99_latency_us << ",\n";
        out << "      \"min_latency_us\": " << run.min_latency_us << ",\n";
        out << "      \"max_latency_us\": " << run.max_latency_us << ",\n";
        out << "      \"latency_samples\": " << run.latency_samples << ",\n";
        out << "      \"latency_sample_interval\": " << run.sample_interval << ",\n";
        out << "      \"peak_rss_kb\": " << run.peak_rss_kb << ",\n";
        out << "      \"seed\": " << run.seed << ",\n";
        out << "      \"compiler\": \"" << json_escape(run.compiler) << "\",\n";
        out << "      \"build_mode\": \"" << json_escape(run.build_mode) << "\",\n";
        out << "      \"debug_build\": " << (run.debug_build ? "true" : "false") << ",\n";
        out << "      \"os\": \"" << json_escape(run.os) << "\",\n";
        out << "      \"cpu\": \"" << json_escape(run.cpu) << "\",\n";
        out << "      \"invariants_ok\": " << (run.invariants_ok ? "true" : "false") << "\n";
        out << "    }" << (i + 1 == runs.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
}

} // namespace lob_bench

#if defined(__APPLE__)
#include <mach/mach.h>
namespace lob_bench {
inline long peak_rss_kb() {
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return static_cast<long>(info.resident_size / 1024);
}
}
#elif defined(__linux__)
#include <sys/resource.h>
namespace lob_bench {
inline long peak_rss_kb() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return usage.ru_maxrss;
}
}
#else
namespace lob_bench {
inline long peak_rss_kb() { return 0; }
}
#endif
