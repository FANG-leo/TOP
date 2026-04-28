#!/usr/bin/env python3
from __future__ import annotations

import csv
import os
import re
import subprocess
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/tmp/matplotlib")

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


ROOT = Path("/home/fzj/桌面/top/top-new/TOP")
OUT_DIR = ROOT / "report-assets"
PERF_DATA = ROOT / "perf-data" / "v1" / "perf.data"
HOTSPOT_TXT = ROOT / "perf-data" / "v1" / "hotspot.txt"

HOTSPOT_RE = re.compile(
    r"^\s*([0-9]+(?:\.[0-9]+)?)%\s+[0-9.]+%\s+\S+\s+\S+\s+\[\.\]\s+(.+?)\s*$"
)


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def simplify_symbol(symbol: str) -> str:
    symbol = symbol.replace("std::__cxx11::", "")
    if len(symbol) > 58:
        return symbol[:55] + "..."
    return symbol


def read_perf_report(perf_data: Path) -> str | None:
    try:
        result = subprocess.run(
            [
                "perf",
                "report",
                "--stdio",
                "-i",
                str(perf_data),
                "--percent-limit",
                "0.5",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return result.stdout


def read_hotspot_source() -> str:
    report = read_perf_report(PERF_DATA)
    if report:
        return report
    return HOTSPOT_TXT.read_text(encoding="utf-8", errors="replace")


def parse_hotspots(text: str, limit: int = 8) -> list[tuple[str, float]]:
    entries: list[tuple[str, float]] = []
    seen: set[str] = set()
    for line in text.splitlines():
        match = HOTSPOT_RE.match(line)
        if not match:
            continue
        pct = float(match.group(1))
        symbol = match.group(2).strip()
        if symbol.startswith("0x") or symbol in seen:
            continue
        seen.add(symbol)
        entries.append((symbol, pct))
        if len(entries) >= limit:
            break
    return entries


def write_csv(path: Path, headers: list[str], rows: list[list[object]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(headers)
        writer.writerows(rows)


def write_text(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def make_baseline_hotspot_assets() -> None:
    entries = parse_hotspots(read_hotspot_source())
    if not entries:
        raise RuntimeError("No hotspot entries could be parsed from perf.data/hotspot.txt")

    csv_rows = [[symbol, f"{pct:.2f}"] for symbol, pct in entries]
    write_csv(OUT_DIR / "baseline_hotspots.csv", ["symbol", "percent"], csv_rows)

    tex_lines = [
        r"\begin{tabular}{@{}lr@{}}",
        r"\toprule",
        r"Symbol & Self overhead (\%) \\",
        r"\midrule",
    ]
    for symbol, pct in entries:
        safe_symbol = simplify_symbol(symbol).replace("_", r"\_")
        tex_lines.append(f"{safe_symbol} & {pct:.2f} \\\\")
    tex_lines.extend([r"\bottomrule", r"\end{tabular}"])
    write_text(OUT_DIR / "baseline_hotspots.tex", "\n".join(tex_lines) + "\n")

    labels = [simplify_symbol(symbol) for symbol, _ in entries]
    values = [pct for _, pct in entries]
    labels = list(reversed(labels))
    values = list(reversed(values))

    fig, ax = plt.subplots(figsize=(8.6, 4.8))
    bars = ax.barh(range(len(labels)), values, color="#1f77b4")
    ax.set_yticks(range(len(labels)))
    ax.set_yticklabels(labels, fontsize=8)
    ax.set_xlabel("Self overhead (%)")
    ax.set_title("Baseline hotspot profile from perf.data")
    ax.grid(True, axis="x", alpha=0.25)
    for bar, value in zip(bars, values):
        ax.text(value + 0.4, bar.get_y() + bar.get_height() / 2, f"{value:.2f}", va="center", fontsize=8)
    fig.tight_layout()
    fig.savefig(OUT_DIR / "baseline_hotspots.png", dpi=180)
    plt.close(fig)


def make_comparison_tables() -> None:
    tables: dict[str, tuple[list[str], list[list[object]]]] = {
        "core_kernel_progress": (
            ["Stage", "FOM (MLUPS)", "Main bottleneck"],
            [
                ["Baseline", "10.57", r"\code{compute\_equilibrium\_profile(...)}"],
                ["After equilibrium specialization", "22.11", r"\code{compute\_cell\_collision(...)}"],
                ["After collision rewrite", "24.90", r"\code{propagation(...)}"],
            ],
        ),
        "layout_transition": (
            ["Version", "Layout", "FOM (MLUPS)"],
            [
                ["Pre-layout-refactor", "AoS", "46.85"],
                ["Post-layout-refactor", "SoA", "72.73"],
            ],
        ),
        "hardware_tuning_progress": (
            ["Version", "Key change", "FOM (MLUPS)"],
            [
                ["SoA baseline", "SoA only", "72.73"],
                ["Native build", r"\code{-march=native}", "91.17"],
                ["Final single-core", "alignment + pointer walk + propagation cleanup", "109.62"],
            ],
        ),
        "mpi_cleanup_comparison": (
            ["Configuration", "Before communication cleanup", "After communication cleanup"],
            [
                [r"\code{np=2, omp=1}", "256.85", "286.48"],
            ],
        ),
    }

    for name, (headers, rows) in tables.items():
        write_csv(OUT_DIR / f"{name}.csv", headers, rows)

        align = "l" * len(headers)
        tex_lines = [
            r"\begin{tabular}{@{}" + align + r"@{}}",
            r"\toprule",
            " & ".join(headers) + r" \\",
            r"\midrule",
        ]
        for row in rows:
            tex_lines.append(" & ".join(str(cell) for cell in row) + r" \\")
        tex_lines.extend([r"\bottomrule", r"\end{tabular}"])
        write_text(OUT_DIR / f"{name}.tex", "\n".join(tex_lines) + "\n")


def make_v9_v12_best_line() -> None:
    summary_csv = OUT_DIR / "all_runs_summary.csv"
    if not summary_csv.exists():
        raise RuntimeError("report-assets/all_runs_summary.csv not found; run generate_report_assets.py first")

    target_versions = {"v9", "v10", "v11", "v12"}
    best: dict[str, dict[str, object]] = {}

    with summary_csv.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            version = row["version"].strip()
            if version not in target_versions:
                continue
            fom = float(row["fom_mlups"])
            mpi = int(row["mpi_ranks"])
            omp = int(row["omp_threads"])
            prev = best.get(version)
            if prev is None or fom > float(prev["fom_mlups"]):
                best[version] = {
                    "version": version,
                    "mpi_ranks": mpi,
                    "omp_threads": omp,
                    "fom_mlups": fom,
                    "run_name": row["run_name"].strip(),
                }

    versions = sorted(best.keys(), key=lambda v: int(v[1:]))
    if not versions:
        raise RuntimeError("No v9-v12 runs found in all_runs_summary.csv")

    rows = []
    for version in versions:
        item = best[version]
        rows.append(
            [
                version,
                item["run_name"],
                item["mpi_ranks"],
                item["omp_threads"],
                f'{float(item["fom_mlups"]):.2f}',
            ]
        )

    write_csv(
        OUT_DIR / "v9_v12_best_fom.csv",
        ["version", "run_name", "mpi_ranks", "omp_threads", "fom_mlups"],
        rows,
    )

    tex_lines = [
        r"\begin{tabular}{@{}lccr@{}}",
        r"\toprule",
        r"Version & MPI ranks & OpenMP threads & Best FOM (MLUPS) \\",
        r"\midrule",
    ]
    for version, _run_name, mpi, omp, fom in rows:
        tex_lines.append(f"{version} & {mpi} & {omp} & {fom} \\\\")
    tex_lines.extend([r"\bottomrule", r"\end{tabular}"])
    write_text(OUT_DIR / "v9_v12_best_fom.tex", "\n".join(tex_lines) + "\n")

    x_labels = [row[0] for row in rows]
    y_values = [float(row[4]) for row in rows]
    annotations = [f'np={row[2]}, omp={row[3]}' for row in rows]

    fig, ax = plt.subplots(figsize=(7.8, 4.4))
    ax.plot(range(len(x_labels)), y_values, marker="o", linewidth=2.2, color="#0a9396")
    for i, (value, ann) in enumerate(zip(y_values, annotations)):
        ax.text(i, value + max(y_values) * 0.02, f"{value:.1f}\n{ann}", ha="center", va="bottom", fontsize=8)
    ax.set_xticks(range(len(x_labels)))
    ax.set_xticklabels(x_labels)
    ax.set_xlabel("Version")
    ax.set_ylabel("Best FOM (MLUPS)")
    ax.set_title("Best-performing configuration for versions v9-v12")
    ax.grid(True, axis="y", alpha=0.25)
    fig.tight_layout()
    fig.savefig(OUT_DIR / "v9_v12_best_fom.png", dpi=180)
    plt.close(fig)


def main() -> None:
    ensure_dir(OUT_DIR)
    make_baseline_hotspot_assets()
    make_comparison_tables()
    make_v9_v12_best_line()
    print("Generated baseline hotspot chart and comparison tables in report-assets/")


if __name__ == "__main__":
    main()
