#!/usr/bin/env python3
import argparse
import csv
import json
import sys
from pathlib import Path


PARAM_COLUMNS = [
    "policy",
    "profile",
    "cache",
    "keys",
    "value",
    "requests",
    "duration_ms",
    "get",
    "ttl_ms",
    "threads",
    "miss_ns",
    "iterations_param",
    "repeats",
]

METRIC_COLUMNS = [
    "real_time_ns",
    "cpu_time_ns",
    "time_unit",
    "items_per_second",
    "hit_ratio",
    "get_p50_ns",
    "get_p99_ns",
    "evictions",
    "memory_estimate",
    "cache_size",
    "hits",
    "misses",
    "puts",
    "writes",
    "erases",
    "cache_store_errors",
    "database_hits",
    "database_misses",
    "expired",
    "put_errors",
    "set_errors",
    "get_null_results",
]


def parse_name(name):
    parts = name.split("/")
    params = {"benchmark": parts[0] if parts else name}

    for part in parts[1:]:
        key, separator, value = part.partition(":")
        if not separator:
            continue
        if key == "iterations":
            key = "iterations_param"
        params[key] = value

    return params


def row_from_benchmark(source, benchmark):
    name = benchmark.get("run_name") or benchmark.get("name", "")
    params = parse_name(name)

    row = {
        "source": source,
        "benchmark": params.get("benchmark", ""),
        "run_type": benchmark.get("run_type", ""),
        "aggregate_name": benchmark.get("aggregate_name", ""),
        "repetition_index": benchmark.get("repetition_index", ""),
    }

    for column in PARAM_COLUMNS:
        row[column] = params.get(column, "")

    row["real_time_ns"] = benchmark.get("real_time", "")
    row["cpu_time_ns"] = benchmark.get("cpu_time", "")
    row["time_unit"] = benchmark.get("time_unit", "")
    for column in METRIC_COLUMNS[3:]:
        row[column] = benchmark.get(column, "")

    return row


def load_rows(paths):
    rows = []

    for path in paths:
        with path.open("r", encoding="utf-8") as file:
            data = json.load(file)

        for benchmark in data.get("benchmarks", []):
            rows.append(row_from_benchmark(str(path), benchmark))

    return rows


def parse_args():
    parser = argparse.ArgumentParser(
        description="Convert Google Benchmark JSON output to a flat CSV table."
    )
    parser.add_argument("json_files", nargs="+", type=Path)
    parser.add_argument("-o", "--output", type=Path, help="CSV output path; defaults to stdout")
    return parser.parse_args()


def main():
    args = parse_args()
    rows = load_rows(args.json_files)
    columns = [
        "source",
        "benchmark",
        "run_type",
        "aggregate_name",
        "repetition_index",
        *PARAM_COLUMNS,
        *METRIC_COLUMNS,
    ]

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8", newline="") as file:
            writer = csv.DictWriter(file, fieldnames=columns)
            writer.writeheader()
            writer.writerows(rows)
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
