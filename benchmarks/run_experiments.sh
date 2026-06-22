#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-"$ROOT_DIR/build"}"
OUT_DIR="${OUT_DIR:-"$ROOT_DIR/benchmark-results"}"
JOBS="${JOBS:-4}"
MODE="${1:-quick}"

usage() {
    cat <<USAGE
Usage: benchmarks/run_experiments.sh [smoke|quick|full]

Environment:
  BUILD_DIR  CMake build directory, default: $ROOT_DIR/build
  OUT_DIR    Benchmark output directory, default: $ROOT_DIR/benchmark-results
  JOBS       Parallel build jobs, default: 4
USAGE
}

case "$MODE" in
    smoke)
        ARGS=(
            --policy=lru
            --profile=uniform
            --cache-size=64
            --key-space=256
            --value-size=32
            --requests=256
            --duration-ms=20
            --ttl-ms=0
            --read-threads=2
            --write-threads=1
            --runs=1
            --benchmark_min_time=0.001s
        )
        ;;
    quick)
        ARGS=(
            --policy=all
            --profile=all
            --cache-size=256,1024
            --key-space=10000
            --value-size=64
            --requests=5000
            --duration-ms=200
            --ttl-ms=0
            --read-threads=2
            --write-threads=1
            --runs=3
            --benchmark_min_time=0.01s
        )
        ;;
    full)
        ARGS=(
            --policy=all
            --profile=all
            --cache-size=256,1024,4096
            --key-space=10000
            --value-size=64
            --requests=20000
            --duration-ms=500
            --ttl-ms=0,5000
            --read-threads=1,4
            --write-threads=1,2
            --runs=3
            --benchmark_min_time=0.01s
        )
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

mkdir -p "$OUT_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target cache_benchmarks -j "$JOBS"

"$BUILD_DIR/benchmarks/cache_benchmarks" \
    "${ARGS[@]}" \
    --benchmark_out="$OUT_DIR/cache_benchmarks_${MODE}.json" \
    --benchmark_out_format=json

python3 "$ROOT_DIR/benchmarks/summarize_results.py" \
    "$OUT_DIR/cache_benchmarks_${MODE}.json" \
    --output "$OUT_DIR/cache_benchmarks_${MODE}.csv"
