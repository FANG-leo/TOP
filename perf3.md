# Perf Analysis for Current Latest Version

## Test Setup

- Date: `2026-04-25`
- Build: `Release`
- Executable: `./build-release/top.lbm-exe`
- Config: `config.results2-20000.txt`
- Iterations: `20000`
- MPI ranks: `1`
- Output file: `results2.raw`

This test used the latest optimized version and kept `show_progress = 0` to avoid terminal-output noise during profiling.

## Overall Metrics

Measured with `perf stat` on the current latest version:

| Metric | Value |
| --- | ---: |
| Elapsed time | `103.30 s` |
| FOM | `24.90 MLUPS` |
| CPU utilization | `99.4%` |
| IPC | `2.30` |
| L1 miss rate | `11.54%` |

Raw counters:

- `task-clock`: `102578.63 ms`
- `cycles`: `414,373,845,866`
- `instructions`: `951,081,515,149`
- `L1-dcache-loads`: `235,580,593,835`
- `L1-dcache-load-misses`: `27,175,964,327`

## Function Time Breakdown

Measured with `perf record` + `perf report --stdio`.

The table below keeps the main project functions from `libtop.lbm-lib.so`.

| Function | Self Overhead | Children Overhead | Notes |
| --- | ---: | ---: | --- |
| `propagation(Mesh*, Mesh const*)` | `50.05%` | `50.78%` | Streaming is now the largest hotspot |
| `compute_cell_collision(double*, double*)` | `43.05%` | `46.32%` | Collision remains large, but no longer the top hotspot |
| `collision(Mesh*, Mesh const*)` | `2.41%` | `3.60%` | Outer collision sweep wrapper |
| `special_cells(Mesh*, lbm_mesh_type_s*, lbm_comm_t_s const*)` | `1.72%` | `1.77%` | Boundary and obstacle handling |
| `save_frame(_IO_FILE*, Mesh const*)` | `0.21%` | `0.82%` | Output overhead remains small |
| `compute_cell_collision(double*, double*)@plt` | `0.38%` | `0.74%` | Small remaining PLT/call overhead signal |

## Interpretation

1. The latest `compute_cell_collision(...)` optimization reduced the collision kernel’s sampled self time from the previous profile.
2. The primary hotspot has now shifted to `propagation(...)`, which takes about half of the sampled runtime.
3. This is a good sign: the collision-side arithmetic work was reduced enough that streaming/memory movement is now the dominant bottleneck.
4. The next meaningful optimization target should therefore be `propagation()`, especially its memory-access pattern and write behavior.

## Notes

- `perf stat` and `perf record` were run separately, so the recorded FOM values differ slightly due to normal runtime noise.
- The `perf stat` run produced `FOM = 24.90 MLUPS`.
- The `perf record` run produced `FOM = 24.67 MLUPS`.
- Kernel symbols were restricted by the system, so a small amount of `[unknown]` kernel-side sample time is excluded from the main function table above.
