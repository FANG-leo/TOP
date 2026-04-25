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

## Rerun Log

Measured again on `2026-04-25` from the existing `build-release` directory:

- Command:
  `LD_LIBRARY_PATH=/home/fzj/桌面/top/top-new/TOP/build-release/lib OMP_NUM_THREADS=1 mpirun -np 1 /home/fzj/桌面/top/top-new/TOP/build-release/top.lbm-exe /home/fzj/桌面/top/top-new/TOP/config.baseline-1000.txt`
- Result:
  `FOM = 13.33 MLUPS`

This rerun is faster than the earlier `9.86 MLUPS` average recorded above, which means the current `build-release` binary or runtime environment differs from the one used for the first baseline table.

## Perf Collection

Collected on `2026-04-25` for the same `config.baseline-1000.txt` single-rank baseline.

- Raw stdout log:
  [stdout.log](/home/fzj/桌面/top/top-new/TOP/perf-data/baseline-1000/stdout.log)
- Raw perf CSV:
  [perf.csv](/home/fzj/桌面/top/top-new/TOP/perf-data/baseline-1000/perf.csv)
- Collection script:
  [run_perf_baseline.sh](/home/fzj/桌面/top/top-new/TOP/scripts/run_perf_baseline.sh)
- Parser:
  [parse_perf_stat.py](/home/fzj/桌面/top/top-new/TOP/scripts/parse_perf_stat.py)

Parsed result:

| Run | FOM (MLUPS) | IPC | CPU Util | Cache Hit Rate | Branch Pred Hit | L1 Hit Rate | Task Clock (s) | Memory Accesses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `baseline-1000` | `18.57` | `3.63` | `0.923` | `94.09%` | `99.92%` | `96.81%` | `7.179` | `42,123,778,649` L1 loads |

Generated analysis files:

- Hotspot report:
  [hotspot.txt](/home/fzj/桌面/top/top-new/TOP/perf-data/baseline-1000/hotspot.txt)
- Per-thread report:
  [per-thread.txt](/home/fzj/桌面/top/top-new/TOP/perf-data/baseline-1000/per-thread.txt)
- Per-core report:
  [per-core.txt](/home/fzj/桌面/top/top-new/TOP/perf-data/baseline-1000/per-core.txt)

Top hotspot symbols in this run:

1. `compute_equilibrium_profile(double*, double, int)`
2. `compute_cell_collision(double*, double*)`
3. `get_vect_norm_2(double const*, double const*)`
4. `propagation(Mesh*, Mesh const*)`
5. `get_cell_velocity(double*, double*, double)`

Recommended collection command:

```bash
cd /home/fzj/桌面/top/top-new/TOP
./scripts/run_perf_suite.sh
```

Recommended parsing command for one or more runs:

```bash
python3 scripts/parse_perf_stat.py perf-data/*/stdout.log perf-data/*/perf.csv
```
