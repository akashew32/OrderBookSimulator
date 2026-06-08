#include <iostream>
#include <string>

#include "csv_replay.hpp"
#include "matching_engine.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: validate_replay_file <csv-path>\n";
        return 2;
    }

    CsvReplayEngine replay;
    std::string error;
    if (!replay.load(argv[1], error)) {
        std::cerr << error << "\n";
        return 1;
    }

    MatchingEngine engine;
    std::uint64_t orders = 0;
    std::uint64_t trades = 0;

    while (replay.has_next()) {
        const ReplayEvent* event = replay.next();
        if (!event || event->event_type != ReplayEventType::Order) {
            continue;
        }

        auto fills = engine.process_order(event->order);
        orders++;
        trades += fills.size();
    }

    std::cout << argv[1]
              << " | rows=" << replay.size()
              << " | orders=" << orders
              << " | trades=" << trades
              << " | engine_orders=" << engine.metrics().orders_processed()
              << " | engine_volume=" << engine.metrics().volume_executed()
              << "\n";

    return 0;
}
