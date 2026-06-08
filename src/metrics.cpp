#include "metrics.hpp"

void Metrics::record_order() {
    orders_processed_++;
}

void Metrics::record_trade(int quantity) {
    trades_executed_++;
    volume_executed_ += quantity;
}

void Metrics::record_cancel() {
    canceled_orders_++;
}

void Metrics::record_modify() {
    modified_orders_++;
}

void Metrics::record_latency(double latency_us) {
    total_latency_us_ += latency_us;
    latency_samples_++;
}

void Metrics::record_fill_delay(double delay_ms) {
    total_fill_delay_ms_ += delay_ms;
    fill_delay_samples_++;
}

void Metrics::record_missed_opportunity() {
    missed_trade_opportunities_++;
}

void Metrics::record_slippage(double slippage) {
    total_slippage_ += slippage;
}

std::uint64_t Metrics::orders_processed() const {
    return orders_processed_;
}

std::uint64_t Metrics::trades_executed() const {
    return trades_executed_;
}

std::uint64_t Metrics::volume_executed() const {
    return volume_executed_;
}

std::uint64_t Metrics::canceled_orders() const {
    return canceled_orders_;
}

std::uint64_t Metrics::modified_orders() const {
    return modified_orders_;
}

std::uint64_t Metrics::missed_trade_opportunities() const {
    return missed_trade_opportunities_;
}

double Metrics::average_latency_us() const {
    return latency_samples_ == 0 ? 0.0 : total_latency_us_ / static_cast<double>(latency_samples_);
}

double Metrics::average_fill_delay_ms() const {
    return fill_delay_samples_ == 0 ? 0.0 : total_fill_delay_ms_ / static_cast<double>(fill_delay_samples_);
}

double Metrics::total_slippage() const {
    return total_slippage_;
}
