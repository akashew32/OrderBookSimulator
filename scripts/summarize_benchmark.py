#!/usr/bin/env python3
import json
import sys
from pathlib import Path


def fmt_ops(value):
    value = float(value)
    if value >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if value >= 1_000:
        return f"{value / 1_000:.1f}K"
    return str(int(value))


def main():
    if len(sys.argv) != 2:
        print("Usage: summarize_benchmark.py benchmarks/results/file.json", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    data = json.loads(path.read_text())
    aggregate = data.get("aggregate", {})
    runs = data.get("runs", [])
    if not runs:
        print("No benchmark runs found.", file=sys.stderr)
        return 1

    run = runs[0]
    scenario = aggregate.get("scenario") or run.get("scenario", "unknown")
    operations = aggregate.get("operations") or run.get("requested_operations", 0)
    throughput = aggregate.get("median_throughput", 0)
    median_latency = aggregate.get("aggregate_median_latency_us", 0)
    p99_latency = aggregate.get("aggregate_p99_latency_us", 0)
    cpu = run.get("cpu") or "unknown hardware"
    build_mode = run.get("build_mode", "unknown")
    iterations = aggregate.get("measured_iterations", len(runs))
    interval = run.get("latency_sample_interval", 1)

    print(
        f"Processed {fmt_ops(operations)} {scenario} order-book operations at a median "
        f"throughput of {fmt_ops(throughput)} operations/sec with {median_latency:.2f} us "
        f"median and {p99_latency:.2f} us p99 sampled operation latency on {cpu}."
    )
    if interval > 1:
        print(f"Note: latency was sampled every {interval} operations.")
    if build_mode != "Release":
        print("Warning: result came from a Debug build; do not use it as a performance claim.")
    if iterations < 3:
        print("Warning: fewer than three measured iterations; rerun with --iterations 3 or more.")
    print("Use this as a conservative summary; do not compare it to production exchange latency.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
