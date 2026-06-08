#include <cassert>
#include <iostream>
#include <string>

#include "csv_replay.hpp"

int main() {
    CsvReplayEngine replay;
    std::string error;

    assert(replay.load("data/sample_replay.csv", error));
    assert(replay.size() == 16);
    assert(replay.position() == 0);

    const ReplayEvent* first = replay.next();
    assert(first != nullptr);
    assert(first->event_type == ReplayEventType::Order);
    assert(first->timestamp == 1);
    assert(first->order.id == 101);
    assert(first->order.side == Side::Buy);
    assert(first->order.type == OrderType::Limit);
    assert(first->order.price == 100);
    assert(first->order.quantity == 12);

    auto batch = replay.take_until(5);
    assert(batch.size() == 4);
    assert(replay.current_timestamp() == 5);
    assert(replay.position() == 5);

    replay.reset();
    assert(replay.position() == 0);
    assert(replay.current_timestamp() == 0);

    std::cout << "CSV replay tests passed!" << std::endl;
    return 0;
}
