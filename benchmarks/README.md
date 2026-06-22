# Cache Benchmark

The benchmark binary compares LRU, LFU, Random, and Noeviction cache policies on
Uniform, Zipf, Sequential Loop, Read-Heavy, Write-Heavy, and Burst workloads.

## Build

```bash
cmake -S . -B build
cmake --build build --target cache_benchmarks -j 4
```

## Smoke Run

```bash
./build/benchmarks/cache_benchmarks \
  --policy=lru \
  --profile=uniform \
  --cache-size=64 \
  --key-space=256 \
  --value-size=32 \
  --requests=256 \
  --duration-ms=20 \
  --ttl-ms=0 \
  --read-threads=2 \
  --write-threads=1 \
  --runs=1 \
  --benchmark_min_time=0.001s
```

## Experiment Runner

```bash
benchmarks/run_experiments.sh smoke
benchmarks/run_experiments.sh quick
benchmarks/run_experiments.sh full
```

The runner writes JSON output to `benchmark-results/` by default. Override paths
with environment variables:

```bash
OUT_DIR=/tmp/cache-benchmarks BUILD_DIR=build JOBS=8 benchmarks/run_experiments.sh quick
```

## Main Options

- `--policy=lru|lfu|random|noeviction|all`
- `--profile=uniform|zipf|sequential-loop|read-heavy|write-heavy|burst|all`
- `--cache-size=N[,N...]`
- `--key-space=N[,N...]`
- `--value-size=N[,N...]`
- `--requests=N[,N...]`
- `--duration-ms=N[,N...]`, where `0` means fixed request-count mode
- `--get-ratio=N[,N...]`; when omitted, read-heavy defaults to `90` and write-heavy to `10`
- `--ttl-ms=N[,N...]`, where `0` disables TTL
- `--read-threads=N[,N...]`
- `--write-threads=N[,N...]`
- `--runs=N`
- `--seed=N`

## Report Metrics

The benchmark emits counters for hit ratio, throughput, p50/p99 Get latency,
evictions, memory estimate, final cache size, hits, misses, puts, expired items,
and put errors.
