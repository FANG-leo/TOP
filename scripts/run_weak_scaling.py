#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import os
import re
import shlex
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/tmp/matplotlib")

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


FOM_RE = re.compile(r"FOM:\s*([0-9]+(?:\.[0-9]+)?)\s*MLUPS")


@dataclass
class RunResult:
    mpi_ranks: int
    omp_threads: int
    width: int
    height: int
    local_width: int
    local_height: int
    elapsed_seconds: float
    fom_mlups: float | None
    task_clock_seconds: float | None
    cpu_utilization: float | None
    weak_efficiency: float | None = None


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(
        description="Run and plot weak-scaling experiments for the LBM solver."
    )
    parser.add_argument("--build-dir", type=Path, default=root / "build-release")
    parser.add_argument("--out-dir", type=Path, default=root / "perf-data" / "weak-scaling")
    parser.add_argument(
        "--base-config",
        type=Path,
        default=root / "config.txt",
        help="Base config file used as the template for weak-scaling cases.",
    )
    parser.add_argument("--label", default="default")
    parser.add_argument("--np-list", default="1,2,4,8", help="Comma-separated MPI ranks.")
    parser.add_argument("--omp-threads", type=int, default=1)
    parser.add_argument("--iterations", type=int, default=None)
    parser.add_argument("--base-width", type=int, default=None)
    parser.add_argument("--base-height", type=int, default=None)
    parser.add_argument("--obstacle-x", type=float, default=None)
    parser.add_argument("--obstacle-r", type=float, default=None)
    parser.add_argument("--reynolds", type=int, default=None)
    parser.add_argument("--inflow-max-velocity", type=float, default=None)
    parser.add_argument(
        "--scale-axis",
        choices=("height", "width"),
        default="height",
        help="Which global dimension scales with MPI ranks to keep local work constant.",
    )
    parser.add_argument(
        "--skip-perf",
        action="store_true",
        help="Run without perf stat if perf is unavailable or undesired.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only generate configs and planned commands, without executing benchmarks.",
    )
    return parser.parse_args()


def parse_np_list(text: str) -> list[int]:
    values = []
    for item in text.split(","):
        item = item.strip()
        if item:
            values.append(int(item))
    if not values:
        raise ValueError("Empty --np-list.")
    return values


def parse_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = [part.strip() for part in line.split("=", 1)]
        values[key] = value
    return values


def get_int_arg(cli_value: int | None, config: dict[str, str], key: str) -> int:
    if cli_value is not None:
        return cli_value
    return int(config[key])


def get_float_arg(cli_value: float | None, config: dict[str, str], key: str) -> float:
    if cli_value is not None:
        return cli_value
    return float(config[key])


def compute_dimensions(base_width: int, base_height: int, ranks: int, scale_axis: str) -> tuple[int, int]:
    if scale_axis == "height":
        return base_width, base_height * ranks
    return base_width * ranks, base_height


def write_config(
    path: Path,
    iterations: int,
    width: int,
    height: int,
    obstacle_x: float,
    obstacle_r: float,
    reynolds: int,
    inflow_max_velocity: float,
) -> None:
    obstacle_y = height / 2.0
    content = f"""iterations           = {iterations}
width                = {width}
height               = {height}
obstacle_x           = {obstacle_x}
obstacle_y           = {obstacle_y:.1f}
obstacle_r           = {obstacle_r}
reynolds             = {reynolds}
inflow_max_velocity  = {inflow_max_velocity}
show_progress        = 0
"""
    path.write_text(content, encoding="utf-8")


def parse_fom(stdout_text: str) -> float | None:
    match = FOM_RE.search(stdout_text)
    return float(match.group(1)) if match else None


def parse_perf_csv(path: Path) -> tuple[float | None, float | None]:
    task_clock = None
    cpu_util = None
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or line.startswith("#") or "," not in line:
                continue
            row = next(csv.reader([line]))
            if len(row) < 7:
                continue
            event = row[2].strip()
            if event != "task-clock":
                continue
            try:
                task_clock = float(row[0].strip()) / 1e9
                cpu_util = float(row[5].strip())
            except ValueError:
                pass
            break
    return task_clock, cpu_util


def run_one(
    build_dir: Path,
    run_dir: Path,
    config_path: Path,
    mpi_ranks: int,
    omp_threads: int,
    skip_perf: bool,
) -> tuple[str, float, float | None, float | None]:
    exe_path = build_dir / "top.lbm-exe"
    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = str(omp_threads)
    env["LD_LIBRARY_PATH"] = str(build_dir / "lib")
    cmd = ["mpirun", "-np", str(mpi_ranks), str(exe_path), str(config_path)]

    stdout_path = run_dir / "stdout.log"
    perf_path = run_dir / "perf.csv"
    command_path = run_dir / "command.txt"
    command_path.write_text(shlex.join(cmd) + "\n", encoding="utf-8")

    start = time.monotonic()
    if skip_perf:
        proc = subprocess.run(cmd, cwd=build_dir, env=env, capture_output=True, text=True, check=True)
        stdout_text = proc.stdout + proc.stderr
        task_clock = None
        cpu_util = None
    else:
        perf_cmd = [
            "perf",
            "stat",
            "-x,",
            "-o",
            str(perf_path),
            "-e",
            "task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses",
        ] + cmd
        proc = subprocess.run(perf_cmd, cwd=build_dir, env=env, capture_output=True, text=True, check=True)
        stdout_text = proc.stdout + proc.stderr
        task_clock, cpu_util = parse_perf_csv(perf_path)
    elapsed = time.monotonic() - start
    stdout_path.write_text(stdout_text, encoding="utf-8")
    return stdout_text, elapsed, task_clock, cpu_util


def write_summary(results: list[RunResult], run_root: Path) -> None:
    csv_path = run_root / "weak_scaling_results.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "mpi_ranks",
                "omp_threads",
                "width",
                "height",
                "local_width",
                "local_height",
                "elapsed_seconds",
                "fom_mlups",
                "task_clock_seconds",
                "cpu_utilization",
                "weak_efficiency",
            ]
        )
        for result in results:
            writer.writerow(
                [
                    result.mpi_ranks,
                    result.omp_threads,
                    result.width,
                    result.height,
                    result.local_width,
                    result.local_height,
                    f"{result.elapsed_seconds:.4f}",
                    "" if result.fom_mlups is None else f"{result.fom_mlups:.2f}",
                    "" if result.task_clock_seconds is None else f"{result.task_clock_seconds:.4f}",
                    "" if result.cpu_utilization is None else f"{result.cpu_utilization:.3f}",
                    "" if result.weak_efficiency is None else f"{result.weak_efficiency:.4f}",
                ]
            )

    tex_path = run_root / "weak_scaling_results.tex"
    lines = [
        r"\begin{tabular}{@{}rrrrrrr@{}}",
        r"\toprule",
        r"MPI & Width & Height & Local W & Local H & Time (s) & Efficiency \\",
        r"\midrule",
    ]
    for result in results:
        eff = "n/a" if result.weak_efficiency is None else f"{result.weak_efficiency:.3f}"
        lines.append(
            f"{result.mpi_ranks} & {result.width} & {result.height} & {result.local_width} & "
            f"{result.local_height} & {result.elapsed_seconds:.2f} & {eff} \\\\"
        )
    lines.extend([r"\bottomrule", r"\end{tabular}"])
    tex_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_plots(results: list[RunResult], run_root: Path) -> None:
    mpi = [r.mpi_ranks for r in results]
    elapsed = [r.elapsed_seconds for r in results]
    eff = [r.weak_efficiency if r.weak_efficiency is not None else 0.0 for r in results]
    fom = [r.fom_mlups if r.fom_mlups is not None else 0.0 for r in results]

    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    ax.plot(mpi, elapsed, marker="o", linewidth=2, color="#005f73")
    ax.set_xlabel("MPI ranks")
    ax.set_ylabel("Elapsed time (s)")
    ax.set_title("Weak-scaling elapsed time")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(run_root / "weak_scaling_elapsed.png", dpi=180)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    ax.plot(mpi, eff, marker="o", linewidth=2, color="#ae2012")
    ax.axhline(1.0, linestyle="--", linewidth=1.1, color="gray", alpha=0.7)
    ax.set_xlabel("MPI ranks")
    ax.set_ylabel("Weak-scaling efficiency")
    ax.set_ylim(0, max(1.05, max(eff) * 1.1 if eff else 1.05))
    ax.set_title("Weak-scaling efficiency")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(run_root / "weak_scaling_efficiency.png", dpi=180)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    ax.plot(mpi, fom, marker="o", linewidth=2, color="#0a9396")
    ax.set_xlabel("MPI ranks")
    ax.set_ylabel("MLUPS")
    ax.set_title("Weak-scaling throughput")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(run_root / "weak_scaling_fom.png", dpi=180)
    plt.close(fig)


def main() -> None:
    args = parse_args()
    config_values = parse_config(args.base_config)
    mpi_list = parse_np_list(args.np_list)
    run_root = args.out_dir / args.label
    run_root.mkdir(parents=True, exist_ok=True)

    iterations = get_int_arg(args.iterations, config_values, "iterations")
    base_width = get_int_arg(args.base_width, config_values, "width")
    base_height = get_int_arg(args.base_height, config_values, "height")
    obstacle_x = get_float_arg(args.obstacle_x, config_values, "obstacle_x")
    obstacle_r = get_float_arg(args.obstacle_r, config_values, "obstacle_r")
    reynolds = get_int_arg(args.reynolds, config_values, "reynolds")
    inflow_max_velocity = get_float_arg(args.inflow_max_velocity, config_values, "inflow_max_velocity")

    results: list[RunResult] = []

    for ranks in mpi_list:
        width, height = compute_dimensions(base_width, base_height, ranks, args.scale_axis)
        local_width = base_width if args.scale_axis == "width" else width
        local_height = base_height if args.scale_axis == "height" else height

        run_dir = run_root / f"np{ranks}"
        run_dir.mkdir(parents=True, exist_ok=True)
        config_path = run_dir / "config.txt"
        write_config(
            config_path,
            iterations=iterations,
            width=width,
            height=height,
            obstacle_x=obstacle_x,
            obstacle_r=obstacle_r,
            reynolds=reynolds,
            inflow_max_velocity=inflow_max_velocity,
        )

        if args.dry_run:
            print(
                f"[dry-run] np={ranks} omp={args.omp_threads} width={width} height={height} "
                f"config={config_path}"
            )
            continue

        print(f"Running weak-scaling case: np={ranks}, omp={args.omp_threads}, width={width}, height={height}")
        stdout_text, elapsed, task_clock, cpu_util = run_one(
            build_dir=args.build_dir,
            run_dir=run_dir,
            config_path=config_path,
            mpi_ranks=ranks,
            omp_threads=args.omp_threads,
            skip_perf=args.skip_perf,
        )
        results.append(
            RunResult(
                mpi_ranks=ranks,
                omp_threads=args.omp_threads,
                width=width,
                height=height,
                local_width=local_width,
                local_height=local_height,
                elapsed_seconds=elapsed,
                fom_mlups=parse_fom(stdout_text),
                task_clock_seconds=task_clock,
                cpu_utilization=cpu_util,
            )
        )

    if args.dry_run:
        return

    if not results:
        raise SystemExit("No weak-scaling results were collected.")

    baseline_elapsed = results[0].elapsed_seconds
    for result in results:
        result.weak_efficiency = baseline_elapsed / result.elapsed_seconds if result.elapsed_seconds > 0 else None

    write_summary(results, run_root)
    make_plots(results, run_root)
    print(f"Weak-scaling results written to: {run_root}")


if __name__ == "__main__":
    main()
