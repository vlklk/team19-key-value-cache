# Cache Request Simulator Benchmark

The benchmark binary compares LRU, LFU, Random, and Noeviction cache policies on
Uniform, Zipf, Sequential Loop, Read-Heavy, Write-Heavy, and Burst workloads.

The benchmark measures the `CacheRequestSimulator`, not raw `Cache::Get` / `Cache::Put` calls.
Each read operation calls `CacheRequestSimulator::Get`. On cache miss the simulator reads the
value from a temporary `TxtDatabase` and inserts it into the cache. Each write operation calls
`CacheRequestSimulator::Set`, which writes to the database and then updates the cache.

The temporary database is generated before timing starts and contains all synthetic keys from the
configured key space. Database creation and cache warmup are excluded from benchmark timing.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCACHE_BUILD_TESTS=OFF
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
  --duration-ms=0 \
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

The runner writes JSON and CSV output to `benchmark-results/` by default.
Override paths with environment variables:

```bash
OUT_DIR=/tmp/cache-benchmarks BUILD_DIR=build JOBS=8 benchmarks/run_experiments.sh quick
```

The runner uses fixed request-count mode by default:

```text
--duration-ms=0
```

That is intentional for the simulator benchmark. A simulator miss touches `TxtDatabase`, so
fixed-duration mode can execute very different numbers of operations for different profiles.
Fixed request-count mode makes policies and profiles easier to compare because each scenario gets
the same number of generated operations.

Convert existing Google Benchmark JSON output manually:

```bash
benchmarks/summarize_results.py benchmark-results/cache_benchmarks_quick.json \
  --output benchmark-results/cache_benchmarks_quick.csv
```

## Presets

The script presets are intentionally smaller than a raw cache benchmark because `TxtDatabase::Get`
and `TxtDatabase::Set` work through a text file.

- `smoke`: one tiny LRU/uniform scenario, for checking that build and output work.
- `quick`: all policies and profiles, one cache size, fixed 1000 operations per scenario.
- `full`: all policies and profiles, two cache sizes, fixed 3000 operations per scenario.

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

The benchmark emits simulator-level counters for cache hits, cache misses, simulator hit ratio,
database hits, database misses, successful writes, Set errors, and null Get results. It also reports
cache-level state required by the task: eviction count, expired item count, final cache size, and
memory estimate.

`get_p50_ns` and `get_p99_ns` are latencies of `CacheRequestSimulator::Get`, so a miss includes
simulator fill-on-miss work: cache lookup, database lookup, and cache insertion attempt.

## Notes on Noeviction and Burst

`Noeviction` may reject cache insertions after the cache is full. In the simulator this is treated as a
cache-store failure, not as an application-level read/write failure: if `TxtDatabase` returns a value,
`CacheRequestSimulator::Get` still returns that value even when the cache cannot store it. These
failures are reported as `cache_store_errors` / `put_errors`.

The Burst profile chooses the operation type and the hot/cold key independently. This avoids the
old artifact where all `Get` operations accidentally targeted only the hot set because both the
operation type and hot/cold selection were based on `index % 100`.
