# Perf Analysis for Current Optimized Version

## Test Setup

- Date: `2026-04-25`
- Build: `Release`
- Executable: `./build-release/top.lbm-exe`
- Config: `config.results1-20000.txt`
- Iterations: `20000`
- MPI ranks: `1`
- Output file: `results1.raw`

This test used the current optimized version and kept `show_progress = 0` to avoid terminal-output noise during profiling.

## Overall Metrics

Measured with `perf stat` on the current optimized version:

| Metric | Value |
| --- | ---: |
| Elapsed time | `116.24 s` |
| FOM | `22.11 MLUPS` |
| CPU utilization | `99.4%` |
| IPC | `2.30` |
| L1 miss rate | `11.08%` |

Raw counters:

- `task-clock`: `115544.43 ms`
- `cycles`: `464,633,792,108`
- `instructions`: `1,069,069,365,969`
- `L1-dcache-loads`: `245,065,546,785`
- `L1-dcache-load-misses`: `27,145,031,153`

## Function Time Breakdown

Measured with `perf record` + `perf report --stdio`.

The table below keeps the main project functions from `libtop.lbm-lib.so`.

| Function | Self Overhead | Children Overhead | Notes |
| --- | ---: | ---: | --- |
| `compute_cell_collision(double*, double*)` | `48.61%` | `51.95%` | Main collision hotspot after the equilibrium-path optimization |
| `propagation(Mesh*, Mesh const*)` | `44.68%` | `45.26%` | Streaming step is now nearly tied with collision in total cost |
| `collision(Mesh*, Mesh const*)` | `2.50%` | `3.65%` | Outer collision sweep wrapper |
| `special_cells(Mesh*, lbm_mesh_type_s*, lbm_comm_t_s const*)` | `1.72%` | `1.75%` | Boundary and obstacle handling |
| `save_frame(_IO_FILE*, Mesh const*)` | `0.22%` | `0.71%` | File-output overhead remains small in the sampled runtime |

## Interpretation

1. The previous top hotspot `compute_equilibrium_profile(...)` no longer appears as a dominant standalone function in the profile.
2. The runtime is now concentrated in two large kernels:
   - `compute_cell_collision(...)`
   - `propagation(...)`
3. This means the earlier optimization successfully removed a large amount of helper-call overhead, but the collision kernel itself is still the largest single cost center.
4. `propagation()` is now almost as expensive as collision, so the next major gains will likely come from memory-access optimization and data-layout-friendly streaming improvements.

## Notes

- `perf stat` and `perf record` were run separately, so the recorded FOM values differ slightly due to normal sampling/runtime noise.
- The `perf stat` run produced `FOM = 22.11 MLUPS`.
- The `perf record` run produced `FOM = 21.76 MLUPS`.
- Kernel symbols were restricted by the system, so a small amount of `[unknown]` kernel-side sample time is excluded from the main function table above.
