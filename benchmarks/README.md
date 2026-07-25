# Order Book Benchmark Suite

This directory contains an isolated benchmark for the C++17 `MatchingEngine` and `OrderBook`.

## What Is Measured

The benchmark measures calls into the existing matching-engine boundary:

- submitting limit orders
- submitting marketable limit orders
- canceling resting orders
- modifying resting orders
- generating and counting trades returned by the engine

Workloads are generated before timing begins, so random-number generation and input construction are excluded from timed measurements.

## What Is Excluded

The benchmark deliberately excludes HTTP/SSE networking, frontend rendering, CSV parsing, artificial sleeps, strategy computation, dashboard updates, and logging.

## Scenarios

- `resting`: away-from-spread limit orders that mostly measure insertion and price-level management.
- `marketable`: crossing limit orders with partial and full fills.
- `mixed`: roughly 55% resting limits, 20% marketable orders, 15% cancels, and 10% modifies.
- `deep-book`: orders spread across many price levels.
- `high-cancel`: frequent cancels using the existing order-id lookup path.

Each scenario is deterministic for a fixed seed.

## Timing Methodology

`std::chrono::steady_clock` measures only the operation call under test. The timed loop processes a pre-generated vector of operations. Trade vectors are consumed immediately by counting trades and volume, rather than storing all results indefinitely.

Latency sampling defaults to one sample every 10 operations. This keeps overhead bounded for multi-million-operation runs while still providing median, p95, p99, min, max, and mean latency estimates. The JSON output records the sampling interval.

Each scenario runs one warm-up by default before measured iterations. Throughput statistics report mean, median, standard deviation, best, and worst measured runs.

## Correctness Validation

Each measured run validates core invariants:

- best bid remains below best ask when both exist
- active sampled orders have unique IDs
- active sampled orders have positive remaining quantity
- trades have positive quantity
- invalid cancels/modifies are counted as not processed

Existing unit tests still cover detailed matching behavior, price-time priority, cancellation, modification, replay, strategy, latency, optimizer, and OSTEP systems scenarios.

## Build And Run

Recommended Release build:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --target order_book_benchmark test_benchmark_suite
./build-bench/test_benchmark_suite
./build-bench/order_book_benchmark --scenario mixed --operations 1000000 --iterations 5 --seed 42
```

Run every scenario at 100K, 1M, and 5M operations:

```sh
./build-bench/order_book_benchmark --all
```

Convenience script:

```sh
./scripts/run_benchmarks.sh
```

Optional native local build:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=native"
```

`-march=native` is intentionally optional because it makes results less portable.

Sanitizer builds are useful for validation, but their timings should never be reported as performance results. Example:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

## Output

Readable summaries are printed to stdout. CSV and JSON files are written to `benchmarks/results/` with timestamped filenames. Generated results are ignored by git, while `.gitkeep` preserves the directory.

Use the resume helper on a JSON file:

```sh
./scripts/summarize_benchmark.py benchmarks/results/benchmark_YYYY-MM-DDTHHMMSS.json
```

## Interpreting Results Honestly

Dashboard throughput is not the same as isolated matching-engine throughput. The dashboard includes networking, rendering, JSON assembly, streaming, and user-facing controls. This benchmark is narrower: it times the core in-memory order-book path.

Synthetic benchmark numbers are also not production exchange latency. They do not include kernel bypass, colocated networking, market-data feed handlers, risk controls, persistence, failover, or real exchange protocol work.

Known noise sources include CPU frequency scaling, other running applications, thermal throttling, debug builds, sanitizer builds, and small sample counts. Generate final numbers on an idle machine with a Release build.

For resume language, prefer conservative measured medians, include workload and hardware, and avoid phrases like "low latency", "high frequency", or comparisons to production exchanges.
