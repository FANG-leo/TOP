#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
from pathlib import Path


FOM_RE = re.compile(r"FOM:\s*([0-9]+(?:\.[0-9]+)?)\s*MLUPS")

# perf stat -x, commonly emits:
# value,unit,event,runtime,percent_running,metric,metric_unit
KNOWN_EVENTS = {
    "task-clock",
    "cycles",
    "instructions",
    "branches",
    "branch-misses",
    "cache-references",
    "cache-misses",
    "L1-dcache-loads",
    "L1-dcache-load-misses",
}


def parse_perf_file(path: Path) -> dict[str, object]:
    metrics: dict[str, dict[str, object]] = {}
    fom_mlups: float | None = None

    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line:
                continue

            fom_match = FOM_RE.search(line)
            if fom_match:
                fom_mlups = float(fom_match.group(1))
                continue

            if "," not in line:
                continue

            row = next(csv.reader([line]))
            if len(row) < 3:
                continue

            event = row[2].strip()
            if event not in KNOWN_EVENTS:
                continue

            value_text = row[0].strip()
            metric_text = row[5].strip() if len(row) > 5 else ""
            metric_unit_text = row[6].strip() if len(row) > 6 else ""

            try:
                value = float(value_text)
            except ValueError:
                continue

            metrics[event] = {
                "value": value,
                "unit": row[1].strip() if len(row) > 1 else "",
                "runtime": row[3].strip() if len(row) > 3 else "",
                "percent_running": row[4].strip() if len(row) > 4 else "",
                "metric": metric_text,
                "metric_unit": metric_unit_text,
            }

    summary: dict[str, object] = {
        "source": str(path),
        "fom_mlups": fom_mlups,
        "events": metrics,
    }

    if "instructions" in metrics and "cycles" in metrics:
        cycles = float(metrics["cycles"]["value"])
        instructions = float(metrics["instructions"]["value"])
        if cycles > 0:
            summary["ipc_computed"] = instructions / cycles

    if "branch-misses" in metrics and "branches" in metrics:
        misses = float(metrics["branch-misses"]["value"])
        total = float(metrics["branches"]["value"])
        if total > 0:
            summary["branch_miss_rate_pct"] = 100.0 * misses / total

    if "cache-misses" in metrics and "cache-references" in metrics:
        misses = float(metrics["cache-misses"]["value"])
        total = float(metrics["cache-references"]["value"])
        if total > 0:
            summary["cache_miss_rate_pct"] = 100.0 * misses / total
            summary["cache_hit_rate_pct"] = 100.0 - (100.0 * misses / total)

    if "L1-dcache-load-misses" in metrics and "L1-dcache-loads" in metrics:
        misses = float(metrics["L1-dcache-load-misses"]["value"])
        total = float(metrics["L1-dcache-loads"]["value"])
        if total > 0:
            summary["l1_dcache_miss_rate_pct"] = 100.0 * misses / total
            summary["l1_dcache_hit_rate_pct"] = 100.0 - (100.0 * misses / total)

    if "task-clock" in metrics:
        # perf's CSV value for task-clock is in ns on this machine.
        summary["task_clock_seconds"] = float(metrics["task-clock"]["value"]) / 1e9
        metric_value = metrics["task-clock"].get("metric")
        metric_unit = metrics["task-clock"].get("metric_unit")
        if metric_unit == "CPUs utilized":
            try:
                summary["cpu_utilization"] = float(metric_value)
            except (TypeError, ValueError):
                pass

    if "L1-dcache-loads" in metrics:
        summary["l1_dcache_loads"] = float(metrics["L1-dcache-loads"]["value"])

    if "cache-references" in metrics:
        summary["cache_references"] = float(metrics["cache-references"]["value"])

    if "branch-miss_rate_pct" in summary:
        pass

    if "branch_miss_rate_pct" in summary:
        summary["branch_prediction_hit_rate_pct"] = 100.0 - float(summary["branch_miss_rate_pct"])

    return summary


def normalize_run_name(path: Path) -> str:
    name = path.name
    if name in {"stdout.log", "perf.csv", "perf.data", "hotspot.txt", "per-thread.txt", "per-core.txt"}:
        return path.parent.name
    for suffix in (".stdout.log", ".perf.csv", ".log", ".csv", ".txt"):
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return path.stem


def merge_runs(items: list[dict[str, object]]) -> list[dict[str, object]]:
    merged: dict[str, dict[str, object]] = {}

    for item in items:
        source = Path(str(item["source"]))
        run_name = normalize_run_name(source)
        current = merged.setdefault(
            run_name,
            {
                "source": run_name,
                "files": [],
                "events": {},
            },
        )
        current["files"].append(str(source))

        if item.get("fom_mlups") is not None:
            current["fom_mlups"] = item["fom_mlups"]

        for key in (
            "ipc_computed",
            "cpu_utilization",
            "branch_miss_rate_pct",
            "branch_prediction_hit_rate_pct",
            "cache_miss_rate_pct",
            "cache_hit_rate_pct",
            "l1_dcache_miss_rate_pct",
            "l1_dcache_hit_rate_pct",
            "task_clock_seconds",
            "l1_dcache_loads",
            "cache_references",
        ):
            if item.get(key) is not None:
                current[key] = item[key]

        events = current.setdefault("events", {})
        events.update(item.get("events", {}))

    return list(merged.values())


def aggregate_runs(runs: list[dict[str, object]]) -> dict[str, object]:
    fom_values = [run["fom_mlups"] for run in runs if isinstance(run.get("fom_mlups"), float)]
    ipc_values = [run["ipc_computed"] for run in runs if isinstance(run.get("ipc_computed"), float)]
    l1_values = [
        run["l1_dcache_miss_rate_pct"]
        for run in runs
        if isinstance(run.get("l1_dcache_miss_rate_pct"), float)
    ]
    cache_hit_values = [
        run["cache_hit_rate_pct"] for run in runs if isinstance(run.get("cache_hit_rate_pct"), float)
    ]
    branch_hit_values = [
        run["branch_prediction_hit_rate_pct"]
        for run in runs
        if isinstance(run.get("branch_prediction_hit_rate_pct"), float)
    ]
    cpu_util_values = [
        run["cpu_utilization"] for run in runs if isinstance(run.get("cpu_utilization"), float)
    ]
    task_clock_values = [
        run["task_clock_seconds"] for run in runs if isinstance(run.get("task_clock_seconds"), float)
    ]

    aggregate: dict[str, object] = {
        "runs": runs,
        "run_count": len(runs),
    }

    if fom_values:
        aggregate["fom_mlups_avg"] = statistics.mean(fom_values)
        aggregate["fom_mlups_min"] = min(fom_values)
        aggregate["fom_mlups_max"] = max(fom_values)

    if ipc_values:
        aggregate["ipc_avg"] = statistics.mean(ipc_values)

    if l1_values:
        aggregate["l1_dcache_miss_rate_pct_avg"] = statistics.mean(l1_values)
    if cache_hit_values:
        aggregate["cache_hit_rate_pct_avg"] = statistics.mean(cache_hit_values)
    if branch_hit_values:
        aggregate["branch_prediction_hit_rate_pct_avg"] = statistics.mean(branch_hit_values)
    if cpu_util_values:
        aggregate["cpu_utilization_avg"] = statistics.mean(cpu_util_values)

    if task_clock_values:
        aggregate["task_clock_seconds_avg"] = statistics.mean(task_clock_values)

    return aggregate


def format_float(value: object, digits: int = 2) -> str:
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return "n/a"


def print_markdown(aggregate: dict[str, object]) -> None:
    print(
        "| File | FOM (MLUPS) | IPC | CPU Util | Cache Hit Rate | Branch Pred Hit | "
        "L1 Hit Rate | Task Clock (s) | Memory Accesses | |"
    )
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |")
    for run in aggregate["runs"]:
        run = dict(run)
        memory_access = "n/a"
        if isinstance(run.get("l1_dcache_loads"), float):
            memory_access = f"L1 loads={run['l1_dcache_loads']:.0f}"
        elif isinstance(run.get("cache_references"), float):
            memory_access = f"cache refs={run['cache_references']:.0f}"
        print(
            f"| {Path(str(run['source'])).name} | "
            f"{format_float(run.get('fom_mlups'))} | "
            f"{format_float(run.get('ipc_computed'))} | "
            f"{format_float(run.get('cpu_utilization'), 3)} | "
            f"{format_float(run.get('cache_hit_rate_pct'))}% | "
            f"{format_float(run.get('branch_prediction_hit_rate_pct'))}% | "
            f"{format_float(run.get('l1_dcache_hit_rate_pct'))}% | "
            f"{format_float(run.get('task_clock_seconds'), 3)} |"
            f" {memory_access} |"
        )

    if aggregate.get("run_count", 0) > 1:
        print(
            f"| Average | {format_float(aggregate.get('fom_mlups_avg'))} | "
            f"{format_float(aggregate.get('ipc_avg'))} | "
            f"{format_float(aggregate.get('cpu_utilization_avg'), 3)} | "
            f"{format_float(aggregate.get('cache_hit_rate_pct_avg'))}% | "
            f"{format_float(aggregate.get('branch_prediction_hit_rate_pct_avg'))}% | "
            f"{100.0 - float(aggregate['l1_dcache_miss_rate_pct_avg']):.2f}% | "
            f"{format_float(aggregate.get('task_clock_seconds_avg'), 3)} | n/a |"
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Parse perf stat -x, output files and summarize baseline metrics."
    )
    parser.add_argument("inputs", nargs="+", help="One or more perf output text files.")
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print the parsed summary as JSON instead of a Markdown table.",
    )
    args = parser.parse_args()

    parsed_items = [parse_perf_file(Path(item)) for item in args.inputs]
    runs = merge_runs(parsed_items)
    aggregate = aggregate_runs(runs)

    if args.json:
        print(json.dumps(aggregate, indent=2, ensure_ascii=False))
        return

    print_markdown(aggregate)


if __name__ == "__main__":
    main()
