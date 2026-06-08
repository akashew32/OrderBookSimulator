#include "csv_replay.hpp"
#include "csv_files.hpp"
#include "event_queue.hpp"
#include "matching_engine.hpp"
#include "os_concepts.hpp"
#include "optimizer.hpp"
#include "strategy.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kDefaultPort = 8080;
constexpr std::size_t kMaxRecentTrades = 80;
constexpr std::size_t kMaxPricePoints = 180;
constexpr std::size_t kMaxVolumeBuckets = 80;

std::atomic<bool> g_stop_requested{false};

void request_stop(int) {
    g_stop_requested = true;
}

struct VolumeBucket {
    std::uint64_t timestamp;
    int volume;
};

struct RuntimeStats {
    std::uint64_t generated_orders = 0;
    std::uint64_t sequence = 0;
    double avg_latency_us = 0.0;
    double orders_per_second = 0.0;
    std::uint64_t replay_events_processed = 0;
};

struct LifecycleEvent {
    std::string type;
    std::uint64_t order_id = 0;
    std::uint64_t timestamp = 0;
    int price = 0;
    int quantity = 0;
};

class LiveServer {
public:
    void run() {
        lifecycle_.startup();
        osdemo::DiagnosticScope server_scope("LiveServer process object", this, false);
        signal(SIGPIPE, SIG_IGN);
        signal(SIGINT, request_stop);
        signal(SIGTERM, request_stop);
        lifecycle_.initializing();

        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error("Could not create socket");
        }

        int yes = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        int port = kDefaultPort;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;

        bool bound = false;
        for (; port < kDefaultPort + 20; ++port) {
            address.sin_port = htons(port);
            if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                bound = true;
                break;
            }
        }

        if (!bound) {
            close(server_fd);
            throw std::runtime_error("Could not bind to any port from 8080 through 8099");
        }

        if (listen(server_fd, 16) < 0) {
            close(server_fd);
            throw std::runtime_error("Could not listen on port 8080");
        }

        std::cout << "Live order book server running at http://localhost:" << port << "\n";
        lifecycle_.running();

        std::thread simulation([this]() { simulation_loop(); });
        std::thread metrics([this]() { metrics_loop(); });

        while (!g_stop_requested.load()) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(server_fd, &read_set);
            timeval timeout{0, 250000};
            int ready = select(server_fd + 1, &read_set, nullptr, nullptr, &timeout);
            if (ready > 0 && FD_ISSET(server_fd, &read_set)) {
                int client_fd = accept(server_fd, nullptr, nullptr);
                if (client_fd >= 0) {
                    std::thread(&LiveServer::handle_client, this, client_fd).detach();
                }
            }
        }

        lifecycle_.shutdown();
        running_ = false;
        replay_running_ = false;
        if (simulation.joinable()) {
            simulation.join();
        }
        if (metrics.joinable()) {
            metrics.join();
        }
        lifecycle_.cleanup();
        close(server_fd);
        close_event_clients();
        lifecycle_.report(engine_.metrics().orders_processed(),
                          engine_.metrics().trades_executed(),
                          event_queue_.dropped());
    }

private:
    MatchingEngine engine_;
    CsvReplayEngine replay_;
    CsvFileCatalog csv_catalog_;
    StrategyOptimizer optimizer_;
    StrategyRunner strategies_;
    EventQueue event_queue_;
    std::vector<Trade> recent_trades_;
    std::vector<int> price_points_;
    std::vector<VolumeBucket> volume_buckets_;
    std::vector<LifecycleEvent> lifecycle_events_;
    std::vector<OptimizationResult> optimization_results_;
    std::string optimization_status_ = "Idle";
    std::vector<int> event_clients_;
    RuntimeStats stats_;
    osdemo::Lifecycle lifecycle_;

    // Synchronization map:
    // state_mutex_ protects engine_, replay_, strategy state, metrics snapshots,
    // recent trades, price history, lifecycle rows, and optimization results.
    // clients_mutex_ protects only event_clients_. Code never holds both locks
    // while mutating book state; that lock ordering rule prevents deadlocks
    // between simulation work and frontend streaming.
    std::mutex state_mutex_;
    std::mutex clients_mutex_;

    std::atomic<bool> running_{false};
    std::atomic<bool> replay_running_{false};
    std::atomic<int> speed_{8};
    std::atomic<int> order_limit_{500};
    std::atomic<int> replay_speed_{8};
    std::atomic<int> order_submission_latency_ms_{25};
    std::atomic<int> market_data_latency_ms_{0};
    std::atomic<std::uint64_t> next_order_id_{1};
    std::atomic<std::uint64_t> next_strategy_order_id_{1};
    std::atomic<std::uint64_t> next_timestamp_{1};

    std::chrono::steady_clock::time_point last_rate_time_ = std::chrono::steady_clock::now();
    std::uint64_t last_rate_orders_ = 0;

    void simulation_loop() {
        osdemo::log_thread("simulation/replay", "started");
        std::mt19937 rng(std::random_device{}());

        while (!g_stop_requested.load()) {
            if (replay_running_) {
                process_due_events(next_timestamp_.load());
                if (!process_next_replay_event()) {
                    replay_running_ = false;
                    broadcast_state();
                    continue;
                }

                int per_second = std::max(1, replay_speed_.load());
                std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, 1000 / per_second)));
                continue;
            }

            if (!running_) {
                process_due_events(next_timestamp_.load());
                std::this_thread::sleep_for(std::chrono::milliseconds(35));
                continue;
            }

            bool can_generate = order_limit_ <= 0 || stats_.generated_orders < static_cast<std::uint64_t>(order_limit_.load());
            if (!can_generate) {
                running_ = false;
                broadcast_state();
                continue;
            }

            Order order = make_random_order(rng);
            process_due_events(order.timestamp);
            process_order(order, true, true);
            process_due_events(next_timestamp_.load());

            int per_second = std::max(1, speed_.load());
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, 1000 / per_second)));
        }
        osdemo::log_thread("simulation/replay", "stopped");
    }

    void metrics_loop() {
        osdemo::log_thread("metrics", "started");
        while (!g_stop_requested.load()) {
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                update_throughput();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        osdemo::log_thread("metrics", "stopped");
    }

    Order make_random_order(std::mt19937& rng) {
        std::uniform_int_distribution<int> side_dist(0, 1);
        std::uniform_int_distribution<int> type_dist(1, 100);
        std::uniform_int_distribution<int> price_dist(95, 105);
        std::uniform_int_distribution<int> quantity_dist(1, 18);

        Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        OrderType type = type_dist(rng) <= 14 ? OrderType::Market : OrderType::Limit;

        return Order{
            next_order_id_++,
            side,
            type,
            type == OrderType::Market ? 0 : price_dist(rng),
            quantity_dist(rng),
            next_timestamp_++
        };
    }

    void process_order(const Order& order,
                       bool generated,
                       bool run_strategies,
                       double fill_delay_ms = 0.0,
                       int intended_price = 0,
                       bool marketable_at_submission = false,
                       bool strategy_event = false) {
        auto start = std::chrono::steady_clock::now();
        std::vector<Trade> trades;
        int mark_price = 100;
        std::uint64_t timestamp = order.timestamp;

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            trades = engine_.process_order(order);
            mark_price = current_mark_price();

            if (generated) {
                stats_.generated_orders++;
            }

            auto elapsed = std::chrono::steady_clock::now() - start;
            double latency = std::chrono::duration<double, std::micro>(elapsed).count();
            double n = static_cast<double>(engine_.metrics().orders_processed());
            stats_.avg_latency_us += (latency - stats_.avg_latency_us) / std::max(1.0, n);

            for (const auto& trade : trades) {
                recent_trades_.insert(recent_trades_.begin(), trade);
                price_points_.push_back(trade.price);
                add_volume(trade.timestamp, trade.quantity);
            }

            if (!trades.empty()) {
                engine_.metrics().record_fill_delay(fill_delay_ms);
            }

            if (marketable_at_submission && trades.empty()) {
                engine_.metrics().record_missed_opportunity();
                if (strategy_event) {
                    strategies_.record_missed_opportunity(order.id);
                }
            }

            if (intended_price > 0) {
                double slippage = 0.0;
                for (const auto& trade : trades) {
                    slippage += std::abs(trade.price - intended_price) * static_cast<double>(trade.quantity);
                }
                if (slippage > 0.0) {
                    engine_.metrics().record_slippage(slippage);
                    if (strategy_event) {
                        strategies_.record_slippage(order.id, slippage);
                    }
                }
            }

            strategies_.record_fills(trades, mark_price, fill_delay_ms);
            trim(recent_trades_, kMaxRecentTrades);
            trim_oldest(price_points_, kMaxPricePoints);
            trim_oldest(volume_buckets_, kMaxVolumeBuckets);
            update_throughput();
            stats_.sequence++;
        }

        if (run_strategies) {
            run_strategy_cycle(timestamp);
        }

        broadcast_state();
    }

    void process_due_events(std::uint64_t timestamp) {
        std::vector<QueuedEvent> events;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            events = event_queue_.pop_due(timestamp);
        }

        for (const auto& event : events) {
            apply_queued_event(event, timestamp);
        }
    }

    void apply_queued_event(const QueuedEvent& event, std::uint64_t now) {
        double delay_ms = event.source_timestamp == 0 || now < event.source_timestamp
            ? 0.0
            : static_cast<double>(now - event.source_timestamp);

        if (event.type == EventType::OrderSubmission) {
            Order delayed = event.order;
            delayed.timestamp = now;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                engine_.metrics().record_latency(delay_ms * 1000.0);
                if (event.strategy_event) {
                    strategies_.record_latency(event.order.id, delay_ms * 1000.0);
                }
                lifecycle_events_.insert(lifecycle_events_.begin(), LifecycleEvent{
                    "submitted", event.order.id, now, event.order.price, event.order.quantity
                });
                trim(lifecycle_events_, 40);
            }
            process_order(delayed, false, false, delay_ms, event.intended_price,
                          event.marketable_at_submission, event.strategy_event);
            return;
        }

        if (event.type == EventType::CancelOrder) {
            bool canceled = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                canceled = engine_.cancel_order(event.order_id);
                if (canceled) {
                    strategies_.record_cancel(event.order_id);
                    lifecycle_events_.insert(lifecycle_events_.begin(), LifecycleEvent{
                        "canceled", event.order_id, now, 0, 0
                    });
                    trim(lifecycle_events_, 40);
                    stats_.sequence++;
                }
            }
            if (canceled) {
                broadcast_state();
            }
            return;
        }

        if (event.type == EventType::ModifyOrder) {
            bool modified = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                modified = engine_.modify_order(event.order_id, event.new_price, event.new_quantity);
                if (modified) {
                    strategies_.record_modify(event.order_id);
                    lifecycle_events_.insert(lifecycle_events_.begin(), LifecycleEvent{
                        "modified", event.order_id, now, event.new_price, event.new_quantity
                    });
                    trim(lifecycle_events_, 40);
                    stats_.sequence++;
                }
            }
            if (modified) {
                broadcast_state();
            }
        }
    }

    bool process_next_replay_event() {
        ReplayEvent event;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            const ReplayEvent* next = replay_.next();
            if (!next) {
                return false;
            }
            event = *next;
            stats_.replay_events_processed++;
        }

        apply_replay_event(event);
        return true;
    }

    void apply_replay_event(const ReplayEvent& event) {
        if (event.event_type == ReplayEventType::Order) {
            process_order(event.order, false, true);
            process_due_events(event.order.timestamp + static_cast<std::uint64_t>(order_submission_latency_ms_.load()));
            return;
        }

        if (event.event_type == ReplayEventType::Trade) {
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                recent_trades_.insert(recent_trades_.begin(), event.trade);
                price_points_.push_back(event.trade.price);
                add_volume(event.trade.timestamp, event.trade.quantity);
                trim(recent_trades_, kMaxRecentTrades);
                trim_oldest(price_points_, kMaxPricePoints);
                trim_oldest(volume_buckets_, kMaxVolumeBuckets);
                stats_.sequence++;
            }
            broadcast_state();
        }
    }

    void run_strategy_cycle(std::uint64_t timestamp) {
        StrategyDecision decision;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            std::uint64_t next_id = next_strategy_order_id_.load();
            decision = strategies_.on_market_update(
                engine_.order_book(),
                recent_trades_,
                timestamp + static_cast<std::uint64_t>(market_data_latency_ms_.load()),
                next_id
            );
            next_strategy_order_id_ = next_id;
        }

        enqueue_strategy_decision(decision, timestamp);
    }

    void enqueue_strategy_decision(const StrategyDecision& decision, std::uint64_t timestamp) {
        int delay = std::max(0, order_submission_latency_ms_.load());
        std::lock_guard<std::mutex> lock(state_mutex_);

        for (std::uint64_t order_id : decision.cancel_order_ids) {
            event_queue_.push(QueuedEvent{
                timestamp + static_cast<std::uint64_t>(delay),
                0,
                EventType::CancelOrder,
                Order{},
                order_id,
                0,
                0,
                timestamp,
                0,
                false,
                true
            });
        }

        for (const auto& modify : decision.modify_requests) {
            event_queue_.push(QueuedEvent{
                timestamp + static_cast<std::uint64_t>(delay),
                0,
                EventType::ModifyOrder,
                Order{},
                modify.order_id,
                modify.new_price,
                modify.new_quantity,
                timestamp,
                0,
                false,
                true
            });
        }

        for (const auto& order : decision.orders) {
            bool marketable = would_cross_locked(order);
            int intended = intended_execution_price_locked(order);
            event_queue_.push(QueuedEvent{
                timestamp + static_cast<std::uint64_t>(delay),
                0,
                EventType::OrderSubmission,
                order,
                order.id,
                0,
                0,
                timestamp,
                intended,
                marketable,
                true
            });
            lifecycle_events_.insert(lifecycle_events_.begin(), LifecycleEvent{
                "pending", order.id, timestamp, order.price, order.quantity
            });
        }
        trim(lifecycle_events_, 40);
        stats_.sequence++;
    }

    bool would_cross_locked(const Order& order) const {
        const auto& book = engine_.order_book();
        if (order.type == OrderType::Market) {
            return order.side == Side::Buy ? book.has_best_ask() : book.has_best_bid();
        }
        if (order.side == Side::Buy) {
            return book.has_best_ask() && book.best_ask() <= order.price;
        }
        return book.has_best_bid() && book.best_bid() >= order.price;
    }

    int intended_execution_price_locked(const Order& order) const {
        const auto& book = engine_.order_book();
        if (order.side == Side::Buy && book.has_best_ask()) {
            return book.best_ask();
        }
        if (order.side == Side::Sell && book.has_best_bid()) {
            return book.best_bid();
        }
        return order.price;
    }

    int current_mark_price() const {
        const auto& book = engine_.order_book();
        if (book.has_best_bid() && book.has_best_ask()) {
            return (book.best_bid() + book.best_ask()) / 2;
        }
        if (!recent_trades_.empty()) {
            return recent_trades_.front().price;
        }
        return 100;
    }

    void add_volume(std::uint64_t timestamp, int quantity) {
        if (volume_buckets_.empty() || volume_buckets_.back().timestamp != timestamp) {
            volume_buckets_.push_back(VolumeBucket{timestamp, quantity});
        } else {
            volume_buckets_.back().volume += quantity;
        }
    }

    template <typename T>
    void trim(std::vector<T>& values, std::size_t max_size) {
        if (values.size() > max_size) {
            values.resize(max_size);
        }
    }

    template <typename T>
    void trim_oldest(std::vector<T>& values, std::size_t max_size) {
        if (values.size() > max_size) {
            values.erase(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(values.size() - max_size));
        }
    }

    void update_throughput() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_rate_time_).count();
        if (elapsed < 0.5) {
            return;
        }

        auto orders = engine_.metrics().orders_processed();
        stats_.orders_per_second = static_cast<double>(orders - last_rate_orders_) / elapsed;
        last_rate_orders_ = orders;
        last_rate_time_ = now;
    }

    void handle_client(int client_fd) {
        osdemo::ThreadScope thread_scope("frontend/event-stream");
        std::string request = read_request(client_fd);
        if (request.empty()) {
            close(client_fd);
            return;
        }

        auto [method, path] = parse_request_line(request);
        std::string body = request_body(request);

        if (path == "/events") {
            handle_events(client_fd);
            return;
        }

        if (method == "POST" && path == "/api/control") {
            handle_control(body);
            send_json(client_fd, "{\"ok\":true}");
            close(client_fd);
            return;
        }

        if (method == "POST" && path == "/api/order") {
            handle_manual_order(body);
            send_json(client_fd, "{\"ok\":true}");
            close(client_fd);
            return;
        }

        if (method == "POST" && path == "/api/replay/load") {
            std::string result = handle_replay_load(body);
            send_json(client_fd, result);
            close(client_fd);
            return;
        }

        if (path == "/api/csv/list") {
            send_json(client_fd, csv_list_json());
            close(client_fd);
            return;
        }

        if (method == "POST" && path == "/api/csv/upload") {
            std::string result = handle_csv_upload(request);
            send_json(client_fd, result);
            close(client_fd);
            return;
        }

        if (method == "POST" && path == "/api/optimization/run") {
            std::string result = handle_optimization_run(body);
            send_json(client_fd, result);
            close(client_fd);
            return;
        }

        if (path == "/api/optimization/export") {
            send_csv(client_fd, optimization_csv());
            close(client_fd);
            return;
        }

        if (method == "POST" && path == "/api/replay/step") {
            bool ok = process_next_replay_event();
            send_json(client_fd, std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
            close(client_fd);
            return;
        }

        if (method == "POST" && path == "/api/replay/jump") {
            handle_replay_jump(body);
            send_json(client_fd, "{\"ok\":true}");
            close(client_fd);
            return;
        }

        if (method == "POST" && path == "/api/reset") {
            reset_session();
            send_json(client_fd, "{\"ok\":true}");
            close(client_fd);
            return;
        }

        if (path == "/api/state") {
            send_json(client_fd, snapshot_json());
            close(client_fd);
            return;
        }

        serve_static(client_fd, path);
        close(client_fd);
    }

    void close_event_clients() {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int client_fd : event_clients_) {
            close(client_fd);
        }
        event_clients_.clear();
    }

    std::string read_request(int client_fd) {
        std::string request;
        char buffer[4096];

        while (request.find("\r\n\r\n") == std::string::npos) {
            ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                return request;
            }
            request.append(buffer, static_cast<std::size_t>(received));
            if (request.size() > 65536) {
                break;
            }
        }

        std::size_t header_end = request.find("\r\n\r\n");
        int content_length = parse_int_header(request, "Content-Length");
        while (header_end != std::string::npos &&
               request.size() < header_end + 4 + static_cast<std::size_t>(content_length)) {
            ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                break;
            }
            request.append(buffer, static_cast<std::size_t>(received));
        }

        return request;
    }

    std::pair<std::string, std::string> parse_request_line(const std::string& request) {
        std::istringstream stream(request);
        std::string method;
        std::string path;
        stream >> method >> path;
        if (path == "/") {
            path = "/index.html";
        }
        return {method, path};
    }

    std::string request_body(const std::string& request) {
        std::size_t pos = request.find("\r\n\r\n");
        if (pos == std::string::npos) {
            return "";
        }
        return request.substr(pos + 4);
    }

    std::string header_value(const std::string& request, const std::string& name) {
        std::size_t pos = request.find(name + ":");
        if (pos == std::string::npos) {
            return "";
        }
        pos += name.size() + 1;
        while (pos < request.size() && request[pos] == ' ') {
            pos++;
        }
        std::size_t end = request.find("\r\n", pos);
        return request.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }

    bool parse_multipart_file(const std::string& request,
                              const std::string& content_type,
                              std::string& file_name,
                              std::string& content) {
        std::size_t boundary_pos = content_type.find("boundary=");
        if (boundary_pos == std::string::npos) {
            return false;
        }
        std::string boundary = "--" + content_type.substr(boundary_pos + 9);
        std::string body = request_body(request);
        std::size_t filename_pos = body.find("filename=\"");
        if (filename_pos == std::string::npos) {
            return false;
        }
        std::size_t name_start = filename_pos + 10;
        std::size_t name_end = body.find('"', name_start);
        if (name_end == std::string::npos) {
            return false;
        }
        file_name = body.substr(name_start, name_end - name_start);

        std::size_t data_start = body.find("\r\n\r\n", name_end);
        if (data_start == std::string::npos) {
            return false;
        }
        data_start += 4;
        std::size_t data_end = body.find(boundary, data_start);
        if (data_end == std::string::npos || data_end < data_start) {
            return false;
        }
        content = body.substr(data_start, data_end - data_start);
        while (!content.empty() && (content.back() == '\r' || content.back() == '\n')) {
            content.pop_back();
        }
        return true;
    }

    int parse_int_header(const std::string& request, const std::string& name) {
        std::size_t pos = request.find(name + ":");
        if (pos == std::string::npos) {
            return 0;
        }
        pos += name.size() + 1;
        while (pos < request.size() && request[pos] == ' ') {
            pos++;
        }
        return std::atoi(request.c_str() + pos);
    }

    void handle_events(int client_fd) {
        std::ostringstream headers;
        headers << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: text/event-stream\r\n"
                << "Cache-Control: no-cache\r\n"
                << "Connection: keep-alive\r\n"
                << "Access-Control-Allow-Origin: *\r\n\r\n";
        write_all(client_fd, headers.str());

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            event_clients_.push_back(client_fd);
        }

        send_event(client_fd, snapshot_json());
    }

    void handle_control(const std::string& body) {
        if (body.find("\"running\":true") != std::string::npos) {
            running_ = true;
        }
        if (body.find("\"running\":false") != std::string::npos) {
            running_ = false;
        }
        if (body.find("\"replayRunning\":true") != std::string::npos) {
            replay_running_ = true;
            running_ = false;
        }
        if (body.find("\"replayRunning\":false") != std::string::npos) {
            replay_running_ = false;
        }

        int speed = json_int(body, "speed", speed_.load());
        int limit = json_int(body, "orderLimit", order_limit_.load());
        int replay_speed = json_int(body, "replaySpeed", replay_speed_.load());
        int order_latency = json_int(body, "orderSubmissionLatencyMs", order_submission_latency_ms_.load());
        int market_latency = json_int(body, "marketDataLatencyMs", market_data_latency_ms_.load());
        speed_ = std::max(1, std::min(100, speed));
        order_limit_ = std::max(0, limit);
        replay_speed_ = std::max(1, std::min(200, replay_speed));
        order_submission_latency_ms_ = std::max(0, std::min(5000, order_latency));
        market_data_latency_ms_ = std::max(0, std::min(5000, market_latency));

        double fee = json_double(body, "feePerShare", strategies_.config().fee_per_share);
        double slippage = json_double(body, "slippagePerShare", strategies_.config().slippage_per_share);
        strategies_.config().fee_per_share = std::max(0.0, fee);
        strategies_.config().slippage_per_share = std::max(0.0, slippage);

        broadcast_state();
    }

    std::string handle_replay_load(const std::string& body) {
        std::string path = json_string(body, "path", "data/sample_replay.csv");
        std::string error;
        CsvMetadata metadata;
        if (!csv_catalog_.inspect(path, metadata, error)) {
            return "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}";
        }
        CsvReplayEngine loaded;
        if (!loaded.load(path, error)) {
            return "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}";
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            replay_ = loaded;
            replay_running_ = false;
            stats_.replay_events_processed = 0;
        }

        reset_session();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            replay_ = loaded;
        }
        broadcast_state();
        return "{\"ok\":true}";
    }

    std::string handle_csv_upload(const std::string& request) {
        std::string file_name;
        std::string content;
        if (!parse_multipart_file(request, header_value(request, "Content-Type"), file_name, content)) {
            return "{\"ok\":false,\"error\":\"Could not read uploaded CSV file\"}";
        }

        CsvMetadata metadata;
        std::string error;
        if (!csv_catalog_.save_upload(file_name, content, metadata, error)) {
            return "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}";
        }
        broadcast_state();
        return "{\"ok\":true,\"file\":" + csv_metadata_json(metadata) + "}";
    }

    std::string handle_optimization_run(const std::string& body) {
        OptimizationRequest request;
        request.csv_path = json_string(body, "csvPath", replay_.source_path().empty() ? "data/sample_replay.csv" : replay_.source_path());
        request.strategy = json_string(body, "strategy", "Market Making");
        request.rank_metric = json_string(body, "rankMetric", "pnl");
        request.train_window = static_cast<std::size_t>(std::max(1, json_int(body, "trainWindow", 60)));
        request.test_window = static_cast<std::size_t>(std::max(1, json_int(body, "testWindow", 30)));

        std::vector<OptimizationResult> results;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            optimization_status_ = "Running";
            stats_.sequence++;
        }
        broadcast_state();

        if (!optimizer_.run(request, results, error)) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            optimization_status_ = "Error: " + error;
            optimization_results_.clear();
            return "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}";
        }

        std::string json;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            optimization_results_ = std::move(results);
            optimization_status_ = "Complete";
            stats_.sequence++;
            json = optimization_results_json_locked();
        }
        broadcast_state();
        return "{\"ok\":true,\"results\":" + json + "}";
    }

    void handle_replay_jump(const std::string& body) {
        std::uint64_t timestamp = static_cast<std::uint64_t>(std::max(0, json_int(body, "timestamp", 0)));
        std::string source;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            source = replay_.source_path();
        }
        if (source.empty()) {
            broadcast_state();
            return;
        }

        CsvReplayEngine loaded;
        std::string error;
        if (!loaded.load(source, error)) {
            broadcast_state();
            return;
        }

        reset_session();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            replay_ = loaded;
        }

        std::vector<ReplayEvent> events;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            events = replay_.take_until(timestamp);
            stats_.replay_events_processed = replay_.position();
        }
        for (const auto& event : events) {
            apply_replay_event(event);
        }
        broadcast_state();
    }

    void reset_session() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        engine_ = MatchingEngine();
        strategies_.reset();
        recent_trades_.clear();
        price_points_.clear();
        volume_buckets_.clear();
        lifecycle_events_.clear();
        event_queue_.clear();
        stats_ = RuntimeStats();
        running_ = false;
        replay_running_ = false;
        next_order_id_ = 1;
        next_strategy_order_id_ = 1;
        next_timestamp_ = 1;
        replay_.reset();
    }

    void handle_manual_order(const std::string& body) {
        std::string side_text = json_string(body, "side", "Buy");
        std::string type_text = json_string(body, "type", "Limit");
        int price = json_int(body, "price", 100);
        int quantity = std::max(1, json_int(body, "quantity", 1));

        Order order{
            next_order_id_++,
            side_text == "Sell" ? Side::Sell : Side::Buy,
            type_text == "Market" ? OrderType::Market : OrderType::Limit,
            type_text == "Market" ? 0 : price,
            quantity,
            next_timestamp_++
        };

        process_order(order, false, true);
    }

    int json_int(const std::string& body, const std::string& key, int fallback) {
        std::size_t pos = body.find("\"" + key + "\"");
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = body.find(':', pos);
        if (pos == std::string::npos) {
            return fallback;
        }
        pos++;
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '"')) {
            pos++;
        }
        return std::atoi(body.c_str() + pos);
    }

    double json_double(const std::string& body, const std::string& key, double fallback) {
        std::size_t pos = body.find("\"" + key + "\"");
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = body.find(':', pos);
        if (pos == std::string::npos) {
            return fallback;
        }
        pos++;
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '"')) {
            pos++;
        }
        return std::atof(body.c_str() + pos);
    }

    std::string json_string(const std::string& body, const std::string& key, const std::string& fallback) {
        std::size_t pos = body.find("\"" + key + "\"");
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = body.find(':', pos);
        if (pos == std::string::npos) {
            return fallback;
        }
        pos = body.find('"', pos + 1);
        if (pos == std::string::npos) {
            return fallback;
        }
        std::size_t end = body.find('"', pos + 1);
        if (end == std::string::npos) {
            return fallback;
        }
        return body.substr(pos + 1, end - pos - 1);
    }

    std::string json_escape(const std::string& value) {
        std::string escaped;
        for (char ch : value) {
            if (ch == '"' || ch == '\\') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        return escaped;
    }

    void broadcast_state() {
        std::string json = snapshot_json();
        std::lock_guard<std::mutex> lock(clients_mutex_);

        for (auto it = event_clients_.begin(); it != event_clients_.end();) {
            if (send_event(*it, json)) {
                ++it;
            } else {
                close(*it);
                it = event_clients_.erase(it);
            }
        }
    }

    bool send_event(int client_fd, const std::string& json) {
        return write_all(client_fd, "event: state\n") &&
               write_all(client_fd, "data: " + json + "\n\n");
    }

    std::string snapshot_json() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto& metrics = engine_.metrics();
        const auto& book = engine_.order_book();
        auto bids = book.bid_levels(24);
        auto asks = book.ask_levels(24);

        int best_bid = book.has_best_bid() ? book.best_bid() : 0;
        int best_ask = book.has_best_ask() ? book.best_ask() : 0;
        int spread = (best_bid > 0 && best_ask > 0) ? best_ask - best_bid : 0;

        std::ostringstream json;
        json << "{";
        json << "\"sequence\":" << stats_.sequence << ",";
        json << "\"running\":" << (running_ ? "true" : "false") << ",";
        json << "\"replayRunning\":" << (replay_running_ ? "true" : "false") << ",";
        json << "\"speed\":" << speed_.load() << ",";
        json << "\"replaySpeed\":" << replay_speed_.load() << ",";
        json << "\"orderLimit\":" << order_limit_.load() << ",";
        json << "\"orderSubmissionLatencyMs\":" << order_submission_latency_ms_.load() << ",";
        json << "\"marketDataLatencyMs\":" << market_data_latency_ms_.load() << ",";
        json << "\"generatedOrders\":" << stats_.generated_orders << ",";
        json << "\"system\":{";
        json << "\"processPhase\":\"running\",";
        json << "\"threads\":[";
        json << "{\"name\":\"simulation/replay\",\"state\":\"active\",\"primitive\":\"std::thread + atomic stop flag\"},";
        json << "{\"name\":\"matching engine\",\"state\":\"event-driven\",\"primitive\":\"state_mutex_\"},";
        json << "{\"name\":\"metrics\",\"state\":\"active\",\"primitive\":\"std::thread + state_mutex_\"},";
        json << "{\"name\":\"frontend/event-stream\",\"state\":\"on demand\",\"primitive\":\"clients_mutex_\"}";
        json << "],";
        json << "\"queues\":[";
        json << "{\"name\":\"latency event queue\",\"size\":" << event_queue_.size()
             << ",\"capacity\":" << event_queue_.capacity()
             << ",\"dropped\":" << event_queue_.dropped()
             << ",\"primitive\":\"priority_queue under state_mutex_\"}";
        json << "],";
        json << "\"synchronization\":[";
        json << "{\"state\":\"engine/replay/strategies/metrics\",\"guard\":\"state_mutex_\"},";
        json << "{\"state\":\"SSE clients\",\"guard\":\"clients_mutex_\"},";
        json << "{\"state\":\"run controls and ids\",\"guard\":\"std::atomic\"}";
        json << "]";
        json << "},";
        json << "\"replay\":{";
        json << "\"loaded\":" << (replay_.size() > 0 ? "true" : "false") << ",";
        json << "\"source\":\"" << json_escape(replay_.source_path()) << "\",";
        json << "\"position\":" << replay_.position() << ",";
        json << "\"total\":" << replay_.size() << ",";
        json << "\"timestamp\":" << replay_.current_timestamp() << ",";
        json << "\"eventsProcessed\":" << stats_.replay_events_processed;
        json << "},";
        json << "\"strategyConfig\":{";
        json << "\"feePerShare\":" << strategies_.config().fee_per_share << ",";
        json << "\"slippagePerShare\":" << strategies_.config().slippage_per_share;
        json << "},";
        json << "\"metrics\":{";
        json << "\"ordersProcessed\":" << metrics.orders_processed() << ",";
        json << "\"tradesExecuted\":" << metrics.trades_executed() << ",";
        json << "\"volumeExecuted\":" << metrics.volume_executed() << ",";
        json << "\"canceledOrders\":" << metrics.canceled_orders() << ",";
        json << "\"modifiedOrders\":" << metrics.modified_orders() << ",";
        json << "\"missedTradeOpportunities\":" << metrics.missed_trade_opportunities() << ",";
        json << "\"avgFillDelayMs\":" << metrics.average_fill_delay_ms() << ",";
        json << "\"totalSlippage\":" << metrics.total_slippage() << ",";
        json << "\"spread\":" << spread << ",";
        json << "\"avgLatencyUs\":" << stats_.avg_latency_us << ",";
        json << "\"engineAvgLatencyUs\":" << metrics.average_latency_us() << ",";
        json << "\"ordersPerSecond\":" << stats_.orders_per_second << ",";
        json << "\"pendingEvents\":" << event_queue_.size() << ",";
        json << "\"droppedEvents\":" << event_queue_.dropped();
        json << "},";
        json << "\"bids\":" << levels_json(bids) << ",";
        json << "\"asks\":" << levels_json(asks) << ",";
        json << "\"activeOrders\":" << active_orders_json(book.active_orders(60)) << ",";
        json << "\"lifecycleEvents\":" << lifecycle_json() << ",";
        json << "\"trades\":" << trades_json() << ",";
        json << "\"prices\":" << ints_json(price_points_) << ",";
        json << "\"volumeBuckets\":" << volume_json() << ",";
        json << "\"strategies\":" << strategies_json() << ",";
        json << "\"csvFiles\":" << csv_files_array_json() << ",";
        json << "\"optimization\":{";
        json << "\"status\":\"" << json_escape(optimization_status_) << "\",";
        json << "\"results\":" << optimization_results_json_locked();
        json << "}";
        json << "}";
        return json.str();
    }

    std::string csv_list_json() {
        return "{\"ok\":true,\"files\":" + csv_files_array_json() + "}";
    }

    std::string csv_files_array_json() {
        std::string error;
        auto files = csv_catalog_.list(error);
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < files.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << csv_metadata_json(files[i]);
        }
        json << "]";
        return json.str();
    }

    std::string csv_metadata_json(const CsvMetadata& file) {
        std::ostringstream json;
        json << "{";
        json << "\"path\":\"" << json_escape(file.path) << "\",";
        json << "\"fileName\":\"" << json_escape(file.file_name) << "\",";
        json << "\"schema\":\"" << json_escape(file.schema) << "\",";
        json << "\"rows\":" << file.rows << ",";
        json << "\"startTimestamp\":" << file.start_timestamp << ",";
        json << "\"endTimestamp\":" << file.end_timestamp << ",";
        json << "\"columns\":[";
        for (std::size_t i = 0; i < file.columns.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << "\"" << json_escape(file.columns[i]) << "\"";
        }
        json << "]}";
        return json.str();
    }

    std::string levels_json(const std::vector<BookLevel>& levels) {
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < levels.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << "{\"price\":" << levels[i].price << ",\"quantity\":" << levels[i].quantity << "}";
        }
        json << "]";
        return json.str();
    }

    std::string trades_json() {
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < recent_trades_.size(); ++i) {
            const auto& trade = recent_trades_[i];
            if (i > 0) {
                json << ",";
            }
            json << "{";
            json << "\"buyOrderId\":" << trade.buy_order_id << ",";
            json << "\"sellOrderId\":" << trade.sell_order_id << ",";
            json << "\"price\":" << trade.price << ",";
            json << "\"quantity\":" << trade.quantity << ",";
            json << "\"timestamp\":" << trade.timestamp;
            json << "}";
        }
        json << "]";
        return json.str();
    }

    std::string ints_json(const std::vector<int>& values) {
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << values[i];
        }
        json << "]";
        return json.str();
    }

    std::string volume_json() {
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < volume_buckets_.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << "{\"timestamp\":" << volume_buckets_[i].timestamp
                 << ",\"volume\":" << volume_buckets_[i].volume << "}";
        }
        json << "]";
        return json.str();
    }

    std::string strategies_json() {
        std::ostringstream json;
        json << "[";
        const auto& performances = strategies_.performances();
        for (std::size_t i = 0; i < performances.size(); ++i) {
            const auto& perf = performances[i];
            if (i > 0) {
                json << ",";
            }
            json << "{";
            json << "\"name\":\"" << json_escape(perf.name) << "\",";
            json << "\"pnl\":" << perf.pnl << ",";
            json << "\"inventory\":" << perf.inventory << ",";
            json << "\"trades\":" << perf.trades << ",";
            json << "\"winRate\":" << perf.win_rate << ",";
            json << "\"avgFillPrice\":" << perf.avg_fill_price << ",";
            json << "\"drawdown\":" << perf.drawdown << ",";
            json << "\"maxDrawdown\":" << perf.max_drawdown << ",";
            json << "\"sharpeLike\":" << perf.sharpe_like << ",";
            json << "\"exposureTime\":" << perf.exposure_time << ",";
            json << "\"volume\":" << perf.volume << ",";
            json << "\"feesPaid\":" << perf.fees_paid << ",";
            json << "\"slippagePaid\":" << perf.slippage_paid << ",";
            json << "\"canceledOrders\":" << perf.canceled_orders << ",";
            json << "\"modifiedOrders\":" << perf.modified_orders << ",";
            json << "\"missedTradeOpportunities\":" << perf.missed_trade_opportunities << ",";
            json << "\"averageLatencyUs\":" << perf.average_latency_us << ",";
            json << "\"averageFillDelayMs\":" << perf.average_fill_delay_ms << ",";
            json << "\"slippageVsIntended\":" << perf.slippage_vs_intended << ",";
            json << "\"activeOrderCount\":" << perf.active_order_ids.size() << ",";
            json << "\"pnlCurve\":" << doubles_json(perf.pnl_curve) << ",";
            json << "\"drawdownCurve\":" << doubles_json(perf.drawdown_curve) << ",";
            json << "\"inventoryCurve\":" << ints_json(perf.inventory_curve);
            json << "}";
        }
        json << "]";
        return json.str();
    }

    std::string active_orders_json(const std::vector<ActiveOrderInfo>& orders) {
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < orders.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << "{";
            json << "\"id\":" << orders[i].id << ",";
            json << "\"side\":\"" << (orders[i].side == Side::Buy ? "Buy" : "Sell") << "\",";
            json << "\"price\":" << orders[i].price << ",";
            json << "\"quantity\":" << orders[i].quantity << ",";
            json << "\"timestamp\":" << orders[i].timestamp;
            json << "}";
        }
        json << "]";
        return json.str();
    }

    std::string lifecycle_json() {
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < lifecycle_events_.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << "{";
            json << "\"type\":\"" << json_escape(lifecycle_events_[i].type) << "\",";
            json << "\"orderId\":" << lifecycle_events_[i].order_id << ",";
            json << "\"timestamp\":" << lifecycle_events_[i].timestamp << ",";
            json << "\"price\":" << lifecycle_events_[i].price << ",";
            json << "\"quantity\":" << lifecycle_events_[i].quantity;
            json << "}";
        }
        json << "]";
        return json.str();
    }

    std::string doubles_json(const std::vector<double>& values) {
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << values[i];
        }
        json << "]";
        return json.str();
    }

    std::string optimization_results_json_locked() {
        std::ostringstream json;
        json << "[";
        for (std::size_t i = 0; i < optimization_results_.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            const auto& result = optimization_results_[i];
            json << "{";
            json << "\"strategy\":\"" << json_escape(result.strategy) << "\",";
            json << "\"rankScore\":" << result.rank_score << ",";
            json << "\"parameters\":{";
            std::size_t j = 0;
            for (const auto& [key, value] : result.parameters) {
                if (j++ > 0) {
                    json << ",";
                }
                json << "\"" << json_escape(key) << "\":" << value;
            }
            json << "},";
            json << "\"inSample\":" << performance_json(result.in_sample) << ",";
            json << "\"outOfSample\":" << performance_json(result.out_of_sample);
            json << "}";
        }
        json << "]";
        return json.str();
    }

    std::string performance_json(const StrategyPerformance& perf) {
        std::ostringstream json;
        json << "{";
        json << "\"pnl\":" << perf.pnl << ",";
        json << "\"inventory\":" << perf.inventory << ",";
        json << "\"trades\":" << perf.trades << ",";
        json << "\"winRate\":" << perf.win_rate << ",";
        json << "\"avgFillPrice\":" << perf.avg_fill_price << ",";
        json << "\"maxDrawdown\":" << perf.max_drawdown << ",";
        json << "\"sharpeLike\":" << perf.sharpe_like << ",";
        json << "\"exposureTime\":" << perf.exposure_time << ",";
        json << "\"feesPaid\":" << perf.fees_paid << ",";
        json << "\"slippagePaid\":" << perf.slippage_paid << ",";
        json << "\"volume\":" << perf.volume << ",";
        json << "\"pnlCurve\":" << doubles_json(perf.pnl_curve) << ",";
        json << "\"drawdownCurve\":" << doubles_json(perf.drawdown_curve) << ",";
        json << "\"inventoryCurve\":" << ints_json(perf.inventory_curve);
        json << "}";
        return json.str();
    }

    std::string optimization_csv() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        std::ostringstream csv;
        csv << "rank,strategy,rank_score,parameters,in_sample_pnl,out_of_sample_pnl,out_of_sample_trades,out_of_sample_drawdown,out_of_sample_volume\n";
        for (std::size_t i = 0; i < optimization_results_.size(); ++i) {
            const auto& result = optimization_results_[i];
            csv << (i + 1) << "," << result.strategy << "," << result.rank_score << ",\"";
            std::size_t j = 0;
            for (const auto& [key, value] : result.parameters) {
                if (j++ > 0) {
                    csv << "; ";
                }
                csv << key << "=" << value;
            }
            csv << "\"," << result.in_sample.pnl << "," << result.out_of_sample.pnl << ","
                << result.out_of_sample.trades << "," << result.out_of_sample.max_drawdown << ","
                << result.out_of_sample.volume << "\n";
        }
        return csv.str();
    }

    void send_csv(int client_fd, const std::string& csv) {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/csv\r\n"
                 << "Content-Disposition: attachment; filename=\"optimization_results.csv\"\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Content-Length: " << csv.size() << "\r\n\r\n"
                 << csv;
        write_all(client_fd, response.str());
    }

    void send_json(int client_fd, const std::string& json) {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Content-Length: " << json.size() << "\r\n\r\n"
                 << json;
        write_all(client_fd, response.str());
    }

    void serve_static(int client_fd, const std::string& path) {
        std::string safe_path = path;
        if (safe_path.find("..") != std::string::npos) {
            send_not_found(client_fd);
            return;
        }

        std::string file_path = "frontend" + safe_path;
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            send_not_found(client_fd);
            return;
        }

        std::ostringstream body;
        body << file.rdbuf();
        std::string content = body.str();

        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: " << content_type(file_path) << "\r\n"
                 << "Content-Length: " << content.size() << "\r\n\r\n"
                 << content;
        write_all(client_fd, response.str());
    }

    std::string content_type(const std::string& file_path) {
        if (ends_with(file_path, ".html")) return "text/html";
        if (ends_with(file_path, ".css")) return "text/css";
        if (ends_with(file_path, ".js")) return "application/javascript";
        return "text/plain";
    }

    bool ends_with(const std::string& text, const std::string& suffix) {
        return text.size() >= suffix.size() &&
               text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void send_not_found(int client_fd) {
        std::string body = "Not found";
        std::ostringstream response;
        response << "HTTP/1.1 404 Not Found\r\n"
                 << "Content-Type: text/plain\r\n"
                 << "Content-Length: " << body.size() << "\r\n\r\n"
                 << body;
        write_all(client_fd, response.str());
    }

    bool write_all(int fd, const std::string& text) {
        const char* data = text.c_str();
        std::size_t remaining = text.size();

        while (remaining > 0) {
            ssize_t sent = send(fd, data, remaining, 0);
            if (sent <= 0) {
                return false;
            }
            data += sent;
            remaining -= static_cast<std::size_t>(sent);
        }

        return true;
    }
};

} // namespace

int main() {
    try {
        LiveServer server;
        server.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }

    return 0;
}
