#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

os.environ.setdefault("MPLCONFIGDIR", "/tmp/matplotlib")

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


FOM_RE = re.compile(r"FOM:\s*([0-9]+(?:\.[0-9]+)?)\s*MLUPS")
META_KV_RE = re.compile(r"^([a-zA-Z0-9_]+)=(.*)$")
RUN_NAME_RE = re.compile(r"^(v[0-9]+)-np([0-9]+)-omp([0-9]+)$")
HOTSPOT_RE = re.compile(r"^\s*([0-9]+(?:\.[0-9]+)?)%\s+[0-9.]+%\s+(.*)$")
HOTSPOT_TREE_RE = re.compile(r".*?([0-9]+(?:\.[0-9]+)?)%--(.+)$")
ROOT_VERSION_STDOUT_RE = re.compile(r"^(v[0-9]+|v[0-9]+-configtxt)$")

VERSION_LABELS = {
    "v1-configtxt": "Initial baseline",
    "v2-configtxt": "Collision helper cleanup",
    "v3": "Collision optimization",
    "v4": "Propagation optimization",
    "v5": "Further single-core tuning",
    "v6": "SoA refactor",
    "v7": "-march=native build",
    "v8": "High-performance serial baseline",
    "v9": "OpenMP-heavy attempt",
    "v10": "Aligned allocation and pointer walk",
    "v11": "MPI communication repair",
    "v12": "Post-MPI cleanup and scaling study",
}


@dataclass
class RunRecord:
    run_name: str
    version: str
    mpi_ranks: int
    omp_threads: int
    fom_mlups: float | None
    ipc: float | None
    l1_miss_pct: float | None
    cache_miss_pct: float | None
    cpu_utilization: float | None
    task_clock_seconds: float | None
    run_dir: Path
    stdout_log: Path | None
    perf_csv: Path | None
    hotspot_txt: Path | None


def parse_meta(meta_path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in meta_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = META_KV_RE.match(line.strip())
        if match:
            values[match.group(1)] = match.group(2)
    return values


def parse_fom(stdout_log: Path) -> float | None:
    for line in stdout_log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = FOM_RE.search(line)
        if match:
            return float(match.group(1))
    return None


def parse_perf_csv(perf_csv: Path) -> dict[str, float]:
    data: dict[str, float] = {}
    with perf_csv.open("r", encoding="utf-8", errors="replace") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or "," not in line or line.startswith("#"):
                continue
            row = next(csv.reader([line]))
            if len(row) < 3:
                continue
            event = row[2].strip()
            value_text = row[0].strip()
            metric_text = row[5].strip() if len(row) > 5 else ""
            metric_unit = row[6].strip() if len(row) > 6 else ""
            try:
                value = float(value_text)
            except ValueError:
                continue
            data[f"event:{event}"] = value
            if metric_text:
                try:
                    data[f"metric:{event}"] = float(metric_text)
                except ValueError:
                    pass
            if metric_unit:
                data[f"metric_unit:{event}"] = math.nan
    return data


def parse_hotspots(hotspot_txt: Path, limit: int = 8) -> list[tuple[str, float]]:
    rows: list[tuple[str, float]] = []
    seen: set[str] = set()
    for line in hotspot_txt.read_text(encoding="utf-8", errors="replace").splitlines():
        match = HOTSPOT_RE.match(line)
        if match:
            pct = float(match.group(1))
            symbol = match.group(2).strip()
            if not symbol.startswith("[") and not symbol.startswith("...") and symbol not in seen:
                rows.append((symbol, pct))
                seen.add(symbol)
                if len(rows) >= limit:
                    break
            continue

        tree_match = HOTSPOT_TREE_RE.match(line)
        if not tree_match:
            continue
        pct = float(tree_match.group(1))
        symbol = tree_match.group(2).strip()
        if symbol.startswith("0x") or symbol.startswith("[") or symbol in seen:
            continue
        rows.append((symbol, pct))
        seen.add(symbol)
        if len(rows) >= limit:
            break
    return rows


def collect_runs(perf_root: Path) -> list[RunRecord]:
    runs: list[RunRecord] = []
    for meta_path in sorted(perf_root.glob("v*/v*-np*-omp*/meta.txt")):
        values = parse_meta(meta_path)
        run_name = values.get("run_name", meta_path.parent.name)
        run_match = RUN_NAME_RE.match(run_name)
        if not run_match:
            continue
        version = run_match.group(1)
        mpi_ranks = int(run_match.group(2))
        omp_threads = int(run_match.group(3))
        run_dir = meta_path.parent
        stdout_log = run_dir / "stdout.log"
        perf_csv = run_dir / "perf.csv"
        hotspot_txt = run_dir / "hotspot.txt"
        perf_values = parse_perf_csv(perf_csv) if perf_csv.exists() else {}
        cycles = perf_values.get("event:cycles")
        instructions = perf_values.get("event:instructions")
        l1_loads = perf_values.get("event:L1-dcache-loads")
        l1_misses = perf_values.get("event:L1-dcache-load-misses")
        cache_refs = perf_values.get("event:cache-references")
        cache_misses = perf_values.get("event:cache-misses")
        task_clock = perf_values.get("event:task-clock")
        runs.append(
            RunRecord(
                run_name=run_name,
                version=version,
                mpi_ranks=mpi_ranks,
                omp_threads=omp_threads,
                fom_mlups=parse_fom(stdout_log) if stdout_log.exists() else None,
                ipc=(instructions / cycles) if cycles and instructions else None,
                l1_miss_pct=(100.0 * l1_misses / l1_loads) if l1_loads and l1_misses else None,
                cache_miss_pct=(100.0 * cache_misses / cache_refs) if cache_refs and cache_misses else None,
                cpu_utilization=perf_values.get("metric:task-clock"),
                task_clock_seconds=(task_clock / 1e9) if task_clock else None,
                run_dir=run_dir,
                stdout_log=stdout_log if stdout_log.exists() else None,
                perf_csv=perf_csv if perf_csv.exists() else None,
                hotspot_txt=hotspot_txt if hotspot_txt.exists() else None,
            )
        )
    return runs


def collect_root_versions(perf_root: Path) -> list[tuple[str, float]]:
    items: list[tuple[str, float]] = []
    for stdout_log in sorted(perf_root.glob("*/stdout.log")):
        parent_name = stdout_log.parent.name
        if not ROOT_VERSION_STDOUT_RE.match(parent_name):
            continue
        fom = parse_fom(stdout_log)
        if fom is None:
            continue
        items.append((parent_name, fom))

    v12_np1 = perf_root / "v12" / "v12-np1-omp1" / "stdout.log"
    if v12_np1.exists():
        fom = parse_fom(v12_np1)
        if fom is not None and not any(name == "v12" for name, _ in items):
            items.append(("v12", fom))

    return sorted(items, key=lambda item: version_sort_key_from_name(item[0]))


def version_sort_key_from_name(name: str) -> tuple[int, str]:
    match = re.match(r"^v([0-9]+)", name)
    if match:
        return (int(match.group(1)), name)
    return (10**9, name)


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def fmt(value: float | None, digits: int = 2) -> str:
    if value is None:
        return "n/a"
    return f"{value:.{digits}f}"


def write_csv(path: Path, headers: list[str], rows: Iterable[Iterable[object]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(headers)
        for row in rows:
            writer.writerow(list(row))


def write_text(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def filter_version(runs: list[RunRecord], version: str) -> list[RunRecord]:
    return sorted(
        [run for run in runs if run.version == version and run.fom_mlups is not None],
        key=lambda run: (run.mpi_ranks, run.omp_threads),
    )


def make_summary_table(runs: list[RunRecord], out_dir: Path) -> None:
    csv_path = out_dir / "all_runs_summary.csv"
    rows = []
    for run in sorted(runs, key=lambda item: (item.version, item.mpi_ranks, item.omp_threads)):
        rows.append(
            [
                run.version,
                run.run_name,
                run.mpi_ranks,
                run.omp_threads,
                fmt(run.fom_mlups),
                fmt(run.ipc),
                fmt(run.l1_miss_pct),
                fmt(run.cache_miss_pct),
                fmt(run.cpu_utilization),
                fmt(run.task_clock_seconds),
            ]
        )
    write_csv(
        csv_path,
        [
            "version",
            "run_name",
            "mpi_ranks",
            "omp_threads",
            "fom_mlups",
            "ipc",
            "l1_miss_pct",
            "cache_miss_pct",
            "cpu_utilization",
            "task_clock_seconds",
        ],
        rows,
    )


def make_version_evolution(perf_root: Path, out_dir: Path) -> None:
    versions = collect_root_versions(perf_root)
    if not versions:
        return

    rows = []
    for version_name, fom in versions:
        rows.append([version_name, VERSION_LABELS.get(version_name, ""), f"{fom:.2f}"])
    write_csv(out_dir / "version_evolution.csv", ["version", "label", "fom_mlups"], rows)

    tex_lines = [
        r"\begin{tabularx}{\textwidth}{@{}llr@{}}",
        r"\toprule",
        r"Version & Main change & FOM (MLUPS) \\",
        r"\midrule",
    ]
    for version_name, fom in versions:
        label = VERSION_LABELS.get(version_name, "").replace("_", r"\_")
        tex_lines.append(f"{version_name} & {label} & {fom:.2f} \\\\")
    tex_lines.extend([r"\bottomrule", r"\end{tabularx}"])
    write_text(out_dir / "version_evolution.tex", "\n".join(tex_lines) + "\n")

    x_labels = [name for name, _ in versions]
    y_values = [fom for _, fom in versions]
    fig, ax = plt.subplots(figsize=(8.5, 4.4))
    ax.plot(range(len(x_labels)), y_values, marker="o", linewidth=2.2, color="#005f73")
    for i, value in enumerate(y_values):
        ax.text(i, value + max(y_values) * 0.015, f"{value:.1f}", ha="center", fontsize=8)
    ax.set_xticks(range(len(x_labels)))
    ax.set_xticklabels(x_labels, rotation=35, ha="right")
    ax.set_ylabel("FOM (MLUPS)")
    ax.set_title("Version-by-version performance evolution")
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_dir / "version_evolution.png", dpi=180)
    plt.close(fig)


def make_perf_matrix(version_runs: list[RunRecord], version: str, out_dir: Path) -> None:
    mpi_values = sorted({run.mpi_ranks for run in version_runs})
    omp_values = sorted({run.omp_threads for run in version_runs})
    matrix: dict[tuple[int, int], float] = {
        (run.mpi_ranks, run.omp_threads): run.fom_mlups or float("nan") for run in version_runs
    }

    csv_rows: list[list[object]] = []
    for mpi in mpi_values:
        row: list[object] = [mpi]
        for omp in omp_values:
            value = matrix.get((mpi, omp))
            row.append("" if value is None else f"{value:.2f}")
        csv_rows.append(row)

    write_csv(
        out_dir / f"{version}_perf_matrix.csv",
        ["mpi_ranks"] + [f"omp_{omp}" for omp in omp_values],
        csv_rows,
    )

    tex_lines = [
        r"\begin{tabular}{@{}" + "l" + "r" * len(omp_values) + r"@{}}",
        r"\toprule",
        "MPI ranks & " + " & ".join([f"OMP={omp}" for omp in omp_values]) + r" \\",
        r"\midrule",
    ]
    for mpi in mpi_values:
        cells = [str(mpi)]
        for omp in omp_values:
            value = matrix.get((mpi, omp))
            cells.append("--" if value is None else f"{value:.2f}")
        tex_lines.append(" & ".join(cells) + r" \\")
    tex_lines.extend([r"\bottomrule", r"\end{tabular}"])
    write_text(out_dir / f"{version}_perf_matrix.tex", "\n".join(tex_lines) + "\n")

    heatmap_values = [[matrix.get((mpi, omp), float("nan")) for omp in omp_values] for mpi in mpi_values]
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    image = ax.imshow(heatmap_values, aspect="auto", cmap="YlOrRd")
    ax.set_xticks(range(len(omp_values)))
    ax.set_xticklabels([str(omp) for omp in omp_values])
    ax.set_yticks(range(len(mpi_values)))
    ax.set_yticklabels([str(mpi) for mpi in mpi_values])
    ax.set_xlabel("OpenMP threads")
    ax.set_ylabel("MPI ranks")
    ax.set_title(f"{version} MLUPS heatmap")
    for i, mpi in enumerate(mpi_values):
        for j, omp in enumerate(omp_values):
            value = matrix.get((mpi, omp))
            if value is not None:
                ax.text(j, i, f"{value:.0f}", ha="center", va="center", color="black", fontsize=8)
    fig.colorbar(image, ax=ax, label="MLUPS")
    fig.tight_layout()
    fig.savefig(out_dir / f"{version}_heatmap.png", dpi=180)
    plt.close(fig)


def make_scaling_plots(version_runs: list[RunRecord], version: str, out_dir: Path) -> None:
    mpi_omp1 = sorted(
        [run for run in version_runs if run.omp_threads == 1 and run.fom_mlups is not None],
        key=lambda run: run.mpi_ranks,
    )
    omp_np1 = sorted(
        [run for run in version_runs if run.mpi_ranks == 1 and run.fom_mlups is not None],
        key=lambda run: run.omp_threads,
    )

    if mpi_omp1:
        baseline = mpi_omp1[0].fom_mlups or 1.0
        rows = [
            [
                run.mpi_ranks,
                f"{run.fom_mlups:.2f}",
                f"{(run.fom_mlups or 0.0) / baseline:.3f}",
                f"{((run.fom_mlups or 0.0) / baseline) / run.mpi_ranks:.3f}",
            ]
            for run in mpi_omp1
        ]
        write_csv(
            out_dir / f"{version}_mpi_scaling.csv",
            ["mpi_ranks", "fom_mlups", "speedup", "parallel_efficiency"],
            rows,
        )

        fig, ax1 = plt.subplots(figsize=(7.0, 4.2))
        x = [run.mpi_ranks for run in mpi_omp1]
        y = [run.fom_mlups for run in mpi_omp1]
        ax1.plot(x, y, marker="o", linewidth=2, color="#005f73", label="MLUPS")
        ax1.set_xlabel("MPI ranks")
        ax1.set_ylabel("MLUPS")
        ax1.set_title(f"{version} MPI scaling with OMP=1")
        ax1.grid(True, alpha=0.3)
        ax2 = ax1.twinx()
        speedup = [(run.fom_mlups or 0.0) / baseline for run in mpi_omp1]
        ax2.plot(x, speedup, marker="s", linestyle="--", color="#bb3e03", label="Speedup")
        ax2.set_ylabel("Speedup vs np=1")
        fig.tight_layout()
        fig.savefig(out_dir / f"{version}_mpi_scaling.png", dpi=180)
        plt.close(fig)

        fig, ax = plt.subplots(figsize=(7.0, 4.2))
        efficiency = [((run.fom_mlups or 0.0) / baseline) / run.mpi_ranks for run in mpi_omp1]
        ax.plot(x, efficiency, marker="o", linewidth=2, color="#ae2012")
        ax.axhline(1.0, linestyle="--", linewidth=1.2, color="gray", alpha=0.7)
        ax.set_xlabel("MPI ranks")
        ax.set_ylabel("Parallel efficiency")
        ax.set_title(f"{version} strong-scaling efficiency with OMP=1")
        ax.set_ylim(0, max(1.05, max(efficiency) * 1.1))
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(out_dir / f"{version}_strong_scaling_efficiency.png", dpi=180)
        plt.close(fig)

    if omp_np1:
        rows = [[run.omp_threads, f"{run.fom_mlups:.2f}", f"{run.ipc:.2f}" if run.ipc else ""] for run in omp_np1]
        write_csv(out_dir / f"{version}_openmp_scaling.csv", ["omp_threads", "fom_mlups", "ipc"], rows)

        fig, ax1 = plt.subplots(figsize=(7.0, 4.2))
        x = [run.omp_threads for run in omp_np1]
        y = [run.fom_mlups for run in omp_np1]
        ax1.plot(x, y, marker="o", linewidth=2, color="#0a9396")
        ax1.set_xlabel("OpenMP threads")
        ax1.set_ylabel("MLUPS")
        ax1.set_title(f"{version} OpenMP scaling with np=1")
        ax1.grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(out_dir / f"{version}_openmp_scaling.png", dpi=180)
        plt.close(fig)


def make_counter_comparison(runs: list[RunRecord], run_names: list[str], out_dir: Path) -> None:
    selected = [run for run in runs if run.run_name in run_names]
    selected.sort(key=lambda run: run_names.index(run.run_name))
    if len(selected) < 2:
        return

    rows = []
    for run in selected:
        rows.append(
            [
                run.run_name,
                run.version,
                run.mpi_ranks,
                run.omp_threads,
                fmt(run.fom_mlups),
                fmt(run.ipc),
                fmt(run.l1_miss_pct),
                fmt(run.cache_miss_pct),
                fmt(run.cpu_utilization),
            ]
        )

    write_csv(
        out_dir / "counter_comparison.csv",
        [
            "run_name",
            "version",
            "mpi_ranks",
            "omp_threads",
            "fom_mlups",
            "ipc",
            "l1_miss_pct",
            "cache_miss_pct",
            "cpu_utilization",
        ],
        rows,
    )

    tex_lines = [
        r"\begin{tabular}{@{}lrrrrr@{}}",
        r"\toprule",
        r"Run & MLUPS & IPC & L1 miss (\%) & Cache miss (\%) & CPUs utilized \\",
        r"\midrule",
    ]
    for run in selected:
        tex_lines.append(
            f"{run.run_name} & {fmt(run.fom_mlups)} & {fmt(run.ipc)} & "
            f"{fmt(run.l1_miss_pct)} & {fmt(run.cache_miss_pct)} & {fmt(run.cpu_utilization)} \\\\"
        )
    tex_lines.extend([r"\bottomrule", r"\end{tabular}"])
    write_text(out_dir / "counter_comparison.tex", "\n".join(tex_lines) + "\n")


def simplify_symbol(symbol: str) -> str:
    symbol = symbol.replace("[.] ", "").replace("[k] ", "")
    if len(symbol) > 52:
        return symbol[:49] + "..."
    return symbol


def make_hotspot_assets(runs: list[RunRecord], run_name: str, out_dir: Path) -> None:
    selected = next((run for run in runs if run.run_name == run_name), None)
    if selected is None or selected.hotspot_txt is None:
        return
    entries = parse_hotspots(selected.hotspot_txt)
    if not entries:
        return

    write_csv(out_dir / f"{run_name}_hotspots.csv", ["symbol", "percent"], entries)

    tex_lines = [
        r"\begin{tabular}{@{}lr@{}}",
        r"\toprule",
        r"Symbol & Samples (\%) \\",
        r"\midrule",
    ]
    for symbol, pct in entries:
        safe_symbol = simplify_symbol(symbol).replace("_", r"\_")
        tex_lines.append(f"{safe_symbol} & {pct:.2f} \\\\")
    tex_lines.extend([r"\bottomrule", r"\end{tabular}"])
    write_text(out_dir / f"{run_name}_hotspots.tex", "\n".join(tex_lines) + "\n")

    labels = [simplify_symbol(symbol) for symbol, _ in entries]
    values = [pct for _, pct in entries]
    fig, ax = plt.subplots(figsize=(8.0, 4.6))
    ax.barh(labels[::-1], values[::-1], color="#94d2bd")
    ax.set_xlabel("Samples (%)")
    ax.set_title(f"Hotspot breakdown for {run_name}")
    fig.tight_layout()
    fig.savefig(out_dir / f"{run_name}_hotspots.png", dpi=180)
    plt.close(fig)


def write_readme(out_dir: Path, version: str, compare_runs: list[str]) -> None:
    content = f"""Generated report assets
=======================

This directory was generated by `scripts/generate_report_assets.py`.

Main outputs:
- `version_evolution.csv` / `.tex` / `.png`: version-by-version FOM evolution for the single-run chain.
- `all_runs_summary.csv`: flat summary of every parsed run.
- `{version}_perf_matrix.csv` / `.tex`: matrix table for the selected version.
- `{version}_heatmap.png`: MPI/OpenMP heatmap for the selected version.
- `{version}_mpi_scaling.png`: MPI scaling plot using OMP=1 runs.
- `{version}_openmp_scaling.png`: OpenMP scaling plot using np=1 runs.
- `counter_comparison.csv` / `.tex`: perf stat comparison for {", ".join(compare_runs)}.
- `*_hotspots.csv` / `.tex` / `.png`: hotspot tables and plots for selected runs.
"""
    write_text(out_dir / "README.txt", content)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate report-ready plots and tables from perf-data."
    )
    parser.add_argument(
        "--perf-root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "perf-data",
        help="Root directory containing perf-data.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "report-assets",
        help="Directory where figures and tables will be written.",
    )
    parser.add_argument(
        "--version",
        default="v12",
        help="Version used for the main matrix and scaling plots.",
    )
    parser.add_argument(
        "--compare-runs",
        nargs="+",
        default=["v9-np1-omp8", "v12-np2-omp1"],
        help="Runs to compare in the perf counter table.",
    )
    parser.add_argument(
        "--hotspot-runs",
        nargs="+",
        default=["v12-np2-omp1", "v9-np1-omp8"],
        help="Runs for which hotspot assets will be generated.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    ensure_dir(args.out_dir)
    runs = collect_runs(args.perf_root)
    if not runs:
        raise SystemExit(f"No runs found under {args.perf_root}")

    version_runs = filter_version(runs, args.version)
    if not version_runs:
        raise SystemExit(f"No runs found for version {args.version}")

    make_version_evolution(args.perf_root, args.out_dir)
    make_summary_table(runs, args.out_dir)
    make_perf_matrix(version_runs, args.version, args.out_dir)
    make_scaling_plots(version_runs, args.version, args.out_dir)
    make_counter_comparison(runs, args.compare_runs, args.out_dir)
    for run_name in args.hotspot_runs:
        make_hotspot_assets(runs, run_name, args.out_dir)
    write_readme(args.out_dir, args.version, args.compare_runs)
    print(f"Generated report assets in: {args.out_dir}")


if __name__ == "__main__":
    main()
