# Perf Analysis for Current Version After Propagation Optimization

## Test Setup

- Date: `2026-04-26`
- Build: `Release`
- Executable: `./build-release/top.lbm-exe`
- Config: `config.results3-20000.txt`
- Iterations: `20000`
- MPI ranks: `1`
- Output file: `results3.raw`

This test used the latest version after the `propagation(...)` optimization and kept `show_progress = 0` to avoid terminal-output noise during profiling.

## Overall Metrics

Measured with `perf stat` on the current version:

| Metric | Value |
| --- | ---: |
| Elapsed time | `67.50 s` |
| FOM | `38.18 MLUPS` |
| CPU utilization | `99.3%` |
| IPC | `2.23` |
| L1 miss rate | `11.66%` |

Raw counters:

- `task-clock`: `67035.68 ms`
- `cycles`: `268,827,721,119`
- `instructions`: `599,638,734,262`
- `L1-dcache-loads`: `188,002,246,869`
- `L1-dcache-load-misses`: `21,922,643,905`

## Function Time Breakdown

Measured with `perf record` + `perf report --stdio`.

The table below keeps the main project functions from `libtop.lbm-lib.so`.

| Function | Self Overhead | Children Overhead | Notes |
| --- | ---: | ---: | --- |
| `compute_cell_collision(double*, double*)` | `66.10%` | `71.13%` | Collision is again the dominant hotspot in this profile |
| `propagation(Mesh*, Mesh const*)` | `23.90%` | `24.25%` | Propagation cost dropped significantly versus the previous profile |
| `collision(Mesh*, Mesh const*)` | `3.92%` | `5.74%` | Outer collision sweep wrapper |
| `special_cells(Mesh*, lbm_mesh_type_s*, lbm_comm_t_s const*)` | `2.97%` | `3.01%` | Boundary and obstacle handling |
| `compute_cell_collision(double*, double*)@plt` | `0.46%` | `1.04%` | Small remaining PLT/call overhead signal |
| `save_frame(_IO_FILE*, Mesh const*)` | `0.19%` | `0.99%` | Output overhead remains small |

## Interpretation

1. The `propagation(...)` optimization appears to have worked: its sampled self time dropped from about half of runtime in the prior profile to about one quarter in this one.
2. With propagation reduced, the main bottleneck has shifted back to `compute_cell_collision(...)`.
3. This is consistent with the runtime improvement as well: the code is faster overall, but now collision dominates the remaining execution time.
4. There is also a noticeable block of `[unknown]` / kernel-side samples in this run, which means not all non-user time could be symbolized by `perf`.

## Notes

- `perf stat` and `perf record` were run separately, so the recorded FOM values differ slightly due to normal runtime noise.
- The `perf stat` run produced `FOM = 38.18 MLUPS`.
- The `perf record` run produced `FOM = 37.99 MLUPS`.
- Kernel symbols were restricted by the system, so a portion of sampled time appears as `[unknown]` and is not merged into the project-function table above.
