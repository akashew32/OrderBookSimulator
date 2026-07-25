#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-bench"
STAMP="$(date +%Y-%m-%dT%H%M%S)"
REPORT="${ROOT_DIR}/benchmarks/results/benchmark-report-${STAMP}.txt"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target order_book_benchmark test_benchmark_suite
"${BUILD_DIR}/test_benchmark_suite"
"${BUILD_DIR}/order_book_benchmark" --all | tee "${REPORT}"

echo "Saved stdout report to ${REPORT}"
