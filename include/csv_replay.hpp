#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "order.hpp"
#include "trade.hpp"

enum class ReplayEventType {
    Order,
    Trade,
    Note
};

struct ReplayEvent {
    ReplayEventType event_type = ReplayEventType::Order;
    std::uint64_t timestamp = 0;
    Order order{};
    Trade trade{};
    std::string note;
};

struct CsvMetadata {
    std::string path;
    std::string file_name;
    std::string schema;
    std::size_t rows = 0;
    std::uint64_t start_timestamp = 0;
    std::uint64_t end_timestamp = 0;
    std::vector<std::string> columns;
};

class CsvReplayEngine {
public:
    bool load(const std::string& path, std::string& error);
    static bool inspect(const std::string& path, CsvMetadata& metadata, std::string& error);
    bool has_next() const;
    const ReplayEvent* next();
    const ReplayEvent* step_to_timestamp(std::uint64_t timestamp);
    std::vector<ReplayEvent> take_until(std::uint64_t timestamp);
    void reset();

    std::size_t position() const;
    std::size_t size() const;
    std::uint64_t current_timestamp() const;
    const std::string& source_path() const;
    const std::vector<ReplayEvent>& events() const;

private:
    std::vector<ReplayEvent> events_;
    std::size_t index_ = 0;
    std::uint64_t current_timestamp_ = 0;
    std::string source_path_;
};
