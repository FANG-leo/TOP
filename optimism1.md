# Cleaned Baseline for 1000 Iterations

## Current Version Test for 20000 Iterations

Measured on `2026-04-25` with the `Release` build and a single MPI rank.

This run used the same parameters as `config.txt` (`20000` iterations, `800 x 160`, `results.raw`, `write_interval = 50`) and added only `show_progress = 0` in a temporary config to avoid terminal-print overhead during performance sampling.

| Metric | Value |
| --- | ---: |
| Elapsed time | `242.5 s` |
| FOM (MLUPS) | `10.57` |
| CPU utilization | `99.7%` |
| IPC | `2.52` |
| L1 miss rate | `3.20%` |

## Goal

Establish a baseline that minimizes non-algorithmic noise before further optimization.

## Benchmark Setup

- Build: `Release`
- Execution mode: singleton MPI process (`1 rank`)
- Config file: `config.baseline-1000.txt`
- Iterations: `1000`
- Mesh: `800 x 160`
- Obstacle: `x=100`, `y=80`, `r=12`
- Reynolds: `96`
- Inflow max velocity: `0.16`

## Interference Removed

The following sources of benchmark noise were removed or bypassed:

1. Result file output during the benchmark.
   `output_filename = none`

2. Per-step progress printing inside the timed loop.
   `show_progress = 0`

3. Unnecessary MPI synchronization in single-rank baseline runs.
   `MPI_Barrier` calls in `main.cpp` are skipped when `comm_size == 1`.

4. Unnecessary halo-exchange path in single-rank baseline runs.
   `lbm_comm_halo_exchange()` now returns immediately when there are no neighbors.

5. Invalid file-header write when output is disabled.
   Header writing is now guarded by `fp != NULL`.

## Measured Baseline

Measured on `2026-04-25` with the cleaned config and `Release` build:

| Run | FOM (MLUPS) |
| --- | ---: |
| 1 | 9.92 |
| 2 | 10.03 |
| 3 | 9.63 |
| Average | 9.86 |

Observed range: `9.63` to `10.03` MLUPS.

Using `800 * 160 * 1000 = 128,000,000` lattice updates, this corresponds to an elapsed time of about `12.8s` to `13.3s` per run.

## Files Changed

- `include/lbm/config.hpp`
  Added `SHOW_PROGRESS` macro and `show_progress` config field.

- `src/lbm/config.cpp`
  Added `show_progress` parsing and support for disabling output with `output_filename = none|null|off`.

- `src/bin/main.cpp`
  Guarded progress printing, guarded file-header writing, and skipped unnecessary `MPI_Barrier` calls for single-rank runs.

- `src/lbm/communications.cpp`
  Added an early return in `lbm_comm_halo_exchange()` when there are no neighboring ranks.

- `config.baseline-1000.txt`
  Added a dedicated 1000-iteration baseline config.

## Notes

- The topology print from `lbm_comm_print()` still appears, but it is outside the timed section and does not affect the measured FOM.
- This baseline is intended for step 1: removing system-level interference before touching the numerical kernel itself.
