#include "csv_replay.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

std::string trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }

    return value.substr(start, end - start);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> cells;
    std::string cell;
    bool quoted = false;

    for (char ch : line) {
        if (ch == '"') {
            quoted = !quoted;
        } else if (ch == ',' && !quoted) {
            cells.push_back(trim(cell));
            cell.clear();
        } else {
            cell.push_back(ch);
        }
    }

    cells.push_back(trim(cell));
    return cells;
}

std::uint64_t parse_u64(const std::string& value) {
    return static_cast<std::uint64_t>(std::stoull(value));
}

int parse_int(const std::string& value) {
    return std::stoi(value);
}

std::string cell(const std::vector<std::string>& row,
                 const std::unordered_map<std::string, std::size_t>& columns,
                 const std::string& name,
                 const std::string& fallback = "") {
    auto it = columns.find(name);
    if (it == columns.end() || it->second >= row.size() || row[it->second].empty()) {
        return fallback;
    }
    return row[it->second];
}

ReplayEvent parse_row(const std::vector<std::string>& row,
                      const std::unordered_map<std::string, std::size_t>& columns,
                      std::uint64_t fallback_id) {
    std::string kind = lower(cell(row, columns, "event_type", "order"));
    if (kind.empty()) {
        kind = lower(cell(row, columns, "event", "order"));
    }

    ReplayEvent event;
    event.timestamp = parse_u64(cell(row, columns, "timestamp", "0"));

    if (kind == "trade" || kind == "execution") {
        event.event_type = ReplayEventType::Trade;
        event.trade = Trade{
            parse_u64(cell(row, columns, "buy_order_id", "0")),
            parse_u64(cell(row, columns, "sell_order_id", "0")),
            parse_int(cell(row, columns, "price", "0")),
            parse_int(cell(row, columns, "quantity", "0")),
            event.timestamp
        };
        return event;
    }

    if (kind == "note" || kind == "halt") {
        event.event_type = ReplayEventType::Note;
        event.note = cell(row, columns, "note", kind);
        return event;
    }

    std::string side_text = lower(cell(row, columns, "side", "Buy"));
    std::string type_text = lower(cell(row, columns, "type", "Limit"));
    std::uint64_t id = parse_u64(cell(row, columns, "order_id", std::to_string(fallback_id)));

    event.event_type = ReplayEventType::Order;
    event.order = Order{
        id,
        side_text == "sell" || side_text == "ask" ? Side::Sell : Side::Buy,
        type_text == "market" ? OrderType::Market : OrderType::Limit,
        type_text == "market" ? 0 : parse_int(cell(row, columns, "price", "0")),
        parse_int(cell(row, columns, "quantity", cell(row, columns, "size", "1"))),
        event.timestamp
    };

    return event;
}

} // namespace

bool CsvReplayEngine::load(const std::string& path, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "Could not open CSV file: " + path;
        return false;
    }

    std::string header_line;
    if (!std::getline(file, header_line)) {
        error = "CSV file is empty";
        return false;
    }

    auto headers = split_csv_line(header_line);
    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        columns[lower(headers[i])] = i;
    }

    if (columns.find("timestamp") == columns.end()) {
        error = "CSV must include a timestamp column";
        return false;
    }

    std::vector<ReplayEvent> parsed;
    std::string line;
    std::uint64_t fallback_id = 1;
    std::size_t line_number = 1;

    try {
        while (std::getline(file, line)) {
            line_number++;
            if (trim(line).empty()) {
                continue;
            }

            auto row = split_csv_line(line);
            parsed.push_back(parse_row(row, columns, fallback_id++));
        }
    } catch (const std::exception& ex) {
        error = "CSV parse error on line " + std::to_string(line_number) + ": " + ex.what();
        return false;
    }

    std::sort(parsed.begin(), parsed.end(), [](const ReplayEvent& a, const ReplayEvent& b) {
        return a.timestamp < b.timestamp;
    });

    events_ = std::move(parsed);
    source_path_ = path;
    reset();
    return true;
}

bool CsvReplayEngine::inspect(const std::string& path, CsvMetadata& metadata, std::string& error) {
    CsvReplayEngine engine;
    if (!engine.load(path, error)) {
        return false;
    }

    std::ifstream file(path);
    std::string header_line;
    if (!std::getline(file, header_line)) {
        error = "CSV file is empty";
        return false;
    }

    metadata = CsvMetadata{};
    metadata.path = path;
    metadata.file_name = std::filesystem::path(path).filename().string();
    metadata.columns = split_csv_line(header_line);
    metadata.rows = engine.events_.size();
    if (!engine.events_.empty()) {
        metadata.start_timestamp = engine.events_.front().timestamp;
        metadata.end_timestamp = engine.events_.back().timestamp;
    }

    bool has_event_type = false;
    bool has_order = false;
    bool has_trade = false;
    for (const auto& column : metadata.columns) {
        std::string name = lower(column);
        has_event_type = has_event_type || name == "event_type" || name == "event";
        has_order = has_order || name == "order_id";
        has_trade = has_trade || name == "buy_order_id" || name == "sell_order_id";
    }
    if (has_event_type && has_order && has_trade) {
        metadata.schema = "mixed order/trade replay";
    } else if (has_event_type && has_order) {
        metadata.schema = "order replay";
    } else if (has_event_type && has_trade) {
        metadata.schema = "trade tape replay";
    } else {
        metadata.schema = "timestamped replay";
    }

    return true;
}

bool CsvReplayEngine::has_next() const {
    return index_ < events_.size();
}

const ReplayEvent* CsvReplayEngine::next() {
    if (!has_next()) {
        return nullptr;
    }

    const ReplayEvent* event = &events_[index_++];
    current_timestamp_ = event->timestamp;
    return event;
}

const ReplayEvent* CsvReplayEngine::step_to_timestamp(std::uint64_t timestamp) {
    const ReplayEvent* last = nullptr;
    while (has_next() && events_[index_].timestamp <= timestamp) {
        last = next();
    }
    return last;
}

std::vector<ReplayEvent> CsvReplayEngine::take_until(std::uint64_t timestamp) {
    std::vector<ReplayEvent> batch;
    while (has_next() && events_[index_].timestamp <= timestamp) {
        batch.push_back(*next());
    }
    return batch;
}

void CsvReplayEngine::reset() {
    index_ = 0;
    current_timestamp_ = 0;
}

std::size_t CsvReplayEngine::position() const {
    return index_;
}

std::size_t CsvReplayEngine::size() const {
    return events_.size();
}

std::uint64_t CsvReplayEngine::current_timestamp() const {
    return current_timestamp_;
}

const std::string& CsvReplayEngine::source_path() const {
    return source_path_;
}

const std::vector<ReplayEvent>& CsvReplayEngine::events() const {
    return events_;
}
