# Limit Order Book Simulator

This project contains a C++ limit order book and matching engine, plus a live browser frontend served by a lightweight C++ HTTP/SSE server.

## Run the Live Dashboard

Build the live server:

```sh
cmake -S . -B build
cmake --build build --target live_order_book_server
```

Start it:

```sh
./build/live_order_book_server
```

Open the URL printed by the server, usually:

```text
http://localhost:8080
```

If port `8080` is busy, the server automatically tries the next available port through `8099`.

## What the Dashboard Shows

- live Level II bid/ask book
- real-time trade tape
- metrics for orders, trades, volume, spread, latency, and throughput
- last traded price chart
- cumulative depth chart
- volume histogram
- simulation start/stop, speed, and order limit controls
- manual order entry sent directly to the C++ backend
- historical CSV replay controls
- CSV dropdown selection, metadata display, and browser upload
- strategy comparison table and strategy risk charts
- active, canceled, and modified order lifecycle panels
- latency controls showing pending actions, fill delay, missed opportunities, and slippage
- parameter optimization dashboard with walk-forward train/test comparison

## Historical CSV Replay

The replay engine reads CSV files from disk, converts rows into internal replay events, sorts them by timestamp, and feeds order events through the same C++ `MatchingEngine` used by live simulation and manual order entry. Strategy orders created during replay move through the central event queue, so replay can show delayed strategy responses against historical order flow.

Sample file:

```text
data/sample_replay.csv
```

Online-derived replay files:

```text
data/online_replay/aapl_lobster_replay.csv
data/online_replay/amzn_lobster_replay.csv
data/online_replay/goog_lobster_replay.csv
data/online_replay/intc_lobster_replay.csv
data/online_replay/msft_lobster_replay.csv
```

These five files were converted from public LOBSTER sample message CSVs mirrored on Hugging Face. The raw downloaded source files are kept in:

```text
data/online_sources/
```

Source metadata is in:

```text
data/online_replay/SOURCES.json
```

CSV files are discovered recursively under:

```text
data/
```

Files uploaded from the dashboard are saved into:

```text
data/uploads/
```

Use the CSV dropdown in the dashboard to select a replay file. The metadata panel shows file name, event count, start timestamp, end timestamp, and detected schema. Selecting a CSV loads it into the replay engine; uploading a valid CSV adds it to the dropdown automatically.

Expected order CSV columns:

```csv
timestamp,event_type,order_id,side,type,price,quantity
1,order,101,Buy,Limit,100,12
2,order,102,Sell,Limit,102,8
3,order,103,Buy,Market,0,5
```

Supported values:

- `event_type`: `order`, `trade`, or `note`
- `side`: `Buy` or `Sell`
- `type`: `Limit` or `Market`
- `price`: required for limit orders, use `0` for market orders
- `quantity`: positive integer

Trade tape rows are also supported:

```csv
timestamp,event_type,buy_order_id,sell_order_id,price,quantity
1,trade,101,202,100,7
```

Upload validation checks that the file is a `.csv`, stays inside the replay data folder, includes a `timestamp` column, and can be parsed into replay events. Malformed rows return an error with the failing line number when available.

The frontend can:

- load a CSV by path
- play/pause replay
- step forward one event
- adjust replay events per second
- jump to a timestamp

## Strategy Models

All trading strategies are implemented in C++ in `src/strategy.cpp` behind the common `Strategy` interface:

- `on_book_update`: receives the current bid/ask book state
- `on_trade`: receives recent trades
- `generate_orders`: returns orders that are sent into the matching engine
- `cancel_orders`: returns active order IDs the strategy wants removed
- `modify_orders`: returns active order IDs with replacement price/quantity

Implemented strategies:

- **Market Making**: cancels stale quotes, re-quotes bid and ask orders around the mid price, and skews quotes based on inventory.
- **Momentum**: buys after short-term price increases, sells after short-term price decreases, and clears old signals when the trend disappears.
- **Mean Reversion**: trades against deviations from an 8-trade rolling average and cancels when the price normalizes.
- **Book Imbalance**: uses top-of-book depth imbalance to buy when bid pressure dominates, sell when ask pressure dominates, and cancel when imbalance fades.

## Order Lifecycle

The order book now tracks every resting order by ID. `OrderBook::cancel_order(order_id)` removes an order from its price level and deletes the empty level if needed. `OrderBook::modify_order(order_id, new_price, new_quantity)` updates size in place when the price is unchanged, preserving time priority. If the price changes, the old order is removed and the modified order is inserted at the back of the new price level, resetting time priority.

Canceled orders cannot execute because they are removed from both the level queue and the active order lookup. Modified orders are matched using their current price, size, and priority.

## Latency Model

The backend has a central time-ordered `EventQueue`. Strategy-generated submits, cancels, and modifies are queued with:

```text
execution timestamp = current timestamp + order_submission_latency_ms
```

The dashboard exposes:

- order submission latency in milliseconds
- market data latency in milliseconds
- pending queued events
- average fill delay
- missed trade opportunities
- slippage versus the intended price

Example effect: with zero latency, a marketable strategy order can hit the current best ask immediately. With latency, another order may consume that ask first, so the delayed order either fills worse, rests unfilled, or is counted as a missed opportunity.

## Strategy Metrics

The backend tracks each strategy separately:

- P&L: mark-to-market cash plus inventory value
- inventory: current net position
- trades: number of fills attributed to the strategy
- win rate: share of fills that improved marked P&L
- average fill price: volume-weighted average execution price
- drawdown: distance from peak P&L
- Sharpe-like score: average recent P&L change divided by standard deviation
- exposure time: number of backend marks where inventory was nonzero
- fees/slippage: configurable per-share assumptions
- total traded volume
- active order count
- canceled and modified order count
- average latency and fill delay
- missed trade opportunities
- slippage versus intended execution price

The frontend displays these in the strategy comparison dashboard, P&L curves, drawdown curves, and inventory curves.

## Parameter Optimization

The optimizer runs C++ strategy models across default parameter grids and ranks the results by the selected metric.

Supported parameter sweeps:

- **Market Making**: spread width, order size, inventory skew, max inventory
- **Momentum**: lookback window, momentum threshold, order size
- **Mean Reversion**: rolling window, z-score threshold, order size
- **Book Imbalance**: depth levels, imbalance threshold, order size

The dashboard controls:

- selected CSV file
- selected strategy
- train event window size
- test event window size
- ranking metric: P&L, Sharpe-like score, win rate, drawdown, or volume
- run optimization
- export results to CSV

The optimization dashboard shows a ranked results table, best configuration summary, out-of-sample P&L/drawdown/inventory curves, and a heatmap-style score view for parameter combinations.

## Walk-Forward Backtesting

Replay data is split into sequential windows. Each window uses earlier events for in-sample parameter evaluation and later events for out-of-sample testing:

```text
[ train window ][ test window ][ train window ][ test window ] ...
```

The test window always occurs after its corresponding training window, which avoids lookahead bias. Results report in-sample and out-of-sample metrics separately so a configuration that only fits the training slice is easy to spot.

Optimization metrics:

- P&L: cash plus marked inventory value
- inventory: final net position
- trades: number of simulated strategy fills
- win rate: share of fills that improved marked P&L
- average fill price: volume-weighted fill price
- max drawdown: largest decline from peak P&L
- Sharpe-like score: average return divided by return volatility
- exposure time: number of marks where inventory was nonzero
- fees/slippage impact: per-share assumptions charged on fills
- total volume: total traded quantity

## API

The C++ live server exposes:

- `GET /events`: Server-Sent Events stream with book, trades, metrics, replay state, and strategy performance
- `GET /api/state`: current JSON snapshot
- `GET /api/csv/list`: available replay CSV files and metadata
- `POST /api/csv/upload`: multipart CSV upload with validation
- `POST /api/control`: start/stop synthetic generation or replay, set speed/order limits, set fees/slippage, and set `orderSubmissionLatencyMs` / `marketDataLatencyMs`
- `POST /api/order`: submit a manual order
- `POST /api/replay/load`: load a CSV path, for example `{ "path": "data/sample_replay.csv" }`
- `POST /api/replay/step`: process one replay event
- `POST /api/replay/jump`: jump replay to a timestamp
- `POST /api/optimization/run`: run a parameter sweep and walk-forward backtest
- `GET /api/optimization/export`: download optimization results as CSV
- `POST /api/reset`: reset engine, replay position, trades, charts, and strategy metrics

## OSTEP Systems Concepts

This project now intentionally demonstrates the core operating-systems ideas from OSTEP Sections 1 and 2 while still behaving like a trading simulator.

### Process Lifecycle

The live server has an explicit process lifecycle in `src/live_server.cpp`:

- startup: process enters `main()` and constructs `LiveServer`
- initialization: engine, queues, sockets, strategies, and replay/catalog state are prepared
- running: server accept loop, simulation/replay thread, metrics thread, and frontend event-stream handlers run
- graceful shutdown: `SIGINT` or `SIGTERM` flips an atomic stop flag
- cleanup: threads are joined, sockets/event clients are closed, and a final report is printed

Run it and stop with `Ctrl-C`:

```sh
./build/live_order_book_server
```

The console prints lifecycle lines such as `[ostep][lifecycle] startup`, `[ostep][thread] simulation/replay started`, and the final order/trade/drop counts.

### Memory Management, Stack vs Heap, and RAII

`Order` and `Trade` remain compact value types in `include/order.hpp` and `include/trade.hpp`. Incoming orders are short-lived stack values. Resting orders are moved into the order book's container-managed storage. Match results are returned in `std::vector<Trade>` batches for contiguous storage.

`OrderBook::process_order(Order order)` takes the incoming order by value so callers can pass either a stack object or a temporary. Leftover limit orders are moved into the book with `add_to_book(Order order)`, which avoids one extra copy in the hot path.

`include/object_pool.hpp` adds an RAII object pool. A pool handle returns its object automatically when the handle is destroyed, demonstrating ownership and cleanup without manual `delete`.

Set this environment variable to print major object creation/destruction diagnostics:

```sh
LOB_OS_DIAGNOSTICS=1 ./build/live_order_book_server
```

### Allocation Efficiency

High-frequency values use preallocation and reuse:

- trade vectors reserve expected match capacity before filling
- book snapshots reserve output vector capacity
- `ObjectPool<Order>` and `ObjectPool<Trade>` demonstrate preallocated reusable objects
- `tests/test_ostep_systems.cpp` benchmarks allocation-heavy `new/delete` against pooled reuse

Run:

```sh
./build/test_ostep_systems
```

The benchmark prints both allocation modes and confirms the pool performs zero extra allocations after startup when preallocated.

### Data Layout and Container Tradeoffs

The book intentionally uses containers that match market-structure needs:

- `std::map<int, std::deque<Order>, std::greater<int>> bids_`: sorted bid price levels, best bid at `begin()`, O(log price levels) insert/find
- `std::map<int, std::deque<Order>> asks_`: sorted ask price levels, best ask at `begin()`, O(log price levels) insert/find
- `std::deque<Order>` per price level: FIFO time priority with efficient front removal and back insertion
- `std::unordered_map<order_id, location>`: expected O(1) cancel/modify lookup by order id
- `std::vector<Trade>` and snapshot vectors: contiguous result batches, better cache locality for iteration
- `std::priority_queue<QueuedEvent>`: time-ordered delayed event processing for latency modeling

The tradeoff is intentional: `std::map` nodes are not cache-contiguous, but they keep prices sorted and make best-price lookup simple. Vectors are used where scanning contiguous memory matters more than stable node identity.

### Multithreading and Producer-Consumer Flow

The live server uses separate roles:

- simulation/replay thread: produces synthetic or CSV-driven market events
- matching engine path: consumes events and mutates the book under `state_mutex_`
- metrics thread: periodically updates throughput
- frontend/event-stream handler threads: serve HTTP/SSE clients and publish snapshots
- strategy/event queue path: strategy orders, cancels, modifies, and latency-delayed events flow through `EventQueue`

`ThreadSafeQueue<T>` in `include/thread_safe_queue.hpp` is a bounded producer-consumer queue using `std::mutex`, `std::condition_variable`, close/drain semantics, and a non-blocking `try_push` overload for overload scenarios.

### Synchronization and Race Prevention

Shared live-server state is protected by two mutexes:

- `state_mutex_`: matching engine, replay engine, strategy state, metrics, recent trades, price history, lifecycle events, and optimization results
- `clients_mutex_`: SSE client file descriptors only

Atomic variables hold control flags and simple shared counters such as running state, speed, order limits, latency settings, and order ids.

`tests/test_ostep_systems.cpp` includes a race-condition demonstration: an intentionally unsafe counter is incremented from multiple threads while an atomic counter performs the corrected implementation. The unsafe value is printed for visibility; only the atomic result is asserted.

### Backpressure and Deadlock Prevention

`EventQueue` now has a capacity and dropped-event count. `ThreadSafeQueue` can block producers when full or reject with `try_push`, making backpressure visible instead of allowing unbounded growth.

Deadlock prevention rules are documented in `src/live_server.cpp`:

- `state_mutex_` protects trading state
- `clients_mutex_` protects only client descriptors
- code avoids mutating book state while holding `clients_mutex_`
- queue code uses one mutex per queue and releases the lock before notifications

The systems test launches multiple producers and consumers against the bounded queue to stress synchronization and close/drain behavior.

## Validation

Build and run the validation binaries:

```sh
cmake -S . -B build
cmake --build build --target live_order_book_server test_csv_replay test_strategy test_order_lifecycle test_latency test_csv_files test_optimizer test_ostep_systems
./build/test_csv_files
./build/test_optimizer
./build/test_order_lifecycle
./build/test_latency
./build/test_csv_replay
./build/test_strategy
./build/test_ostep_systems
./test_order_book
./test_matching_engine
```

Validate a replay CSV through the parser and matching engine:

```sh
cmake --build build --target validate_replay_file
./build/validate_replay_file data/online_replay/aapl_lobster_replay.csv
```

The existing order book and matching engine tests still cover matching behavior, partial fills, market orders, and price-time priority.

## Benchmarking

The project includes an isolated matching-engine benchmark target that excludes the live dashboard, HTTP/SSE networking, CSV parsing, frontend rendering, strategy computation, sleeps, and logging.

Build and run a Release benchmark:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --target order_book_benchmark test_benchmark_suite
./build-bench/test_benchmark_suite
./build-bench/order_book_benchmark --scenario mixed --operations 1000000 --iterations 5 --seed 42
```

Run every benchmark scenario:

```sh
./scripts/run_benchmarks.sh
```

Benchmark results are written as CSV and JSON under `benchmarks/results/`. Generated result files are ignored by git. See `benchmarks/README.md` for methodology, scenarios, output fields, limitations, and resume-safe reporting guidance.
