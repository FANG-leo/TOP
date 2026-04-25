# Perf Analysis for Current Version After Collision-Loop Optimization

## Test Setup

- Date: `2026-04-26`
- Build: `Release`
- Executable: `./build-release/top.lbm-exe`
- Config: `config.results4-20000.txt`
- Iterations: `20000`
- MPI ranks: `1`
- Output file: `results4.raw`

This test used the latest version after the `collision()` hot-loop optimization and kept `show_progress = 0` to avoid terminal-output noise during profiling.

## Overall Metrics

Measured with `perf stat` on the current version:

| Metric | Value |
| --- | ---: |
| Elapsed time | `55.75 s` |
| FOM | `46.41 MLUPS` |
| CPU utilization | `99.1%` |
| IPC | `2.17` |
| L1 miss rate | `11.22%` |

Raw counters:

- `task-clock`: `55216.54 ms`
- `cycles`: `222,241,806,846`
- `instructions`: `481,946,130,894`
- `L1-dcache-loads`: `155,950,804,883`
- `L1-dcache-load-misses`: `17,495,394,800`

## Function Time Breakdown

Measured with `perf record` + `perf report --stdio`.

The table below keeps the main resolved project functions from `libtop.lbm-lib.so`.

| Function | Self Overhead | Children Overhead | Notes |
| --- | ---: | ---: | --- |
| `collision(Mesh*, Mesh const*)` | `65.16%` | `66.17%` | Collision sweep remains the dominant resolved hotspot |
| `propagation(Mesh*, Mesh const*)` | `27.50%` | `27.85%` | Propagation is the second major hotspot |
| `special_cells(Mesh*, lbm_mesh_type_s*, lbm_comm_t_s const*)` | `3.93%` | `4.01%` | Boundary and obstacle handling |
| `save_frame(_IO_FILE*, Mesh const*)` | `0.41%` | `1.46%` | Output overhead remains small |

## Interpretation

1. The current version is substantially faster overall than earlier profiles, with elapsed time now around `55.75 s`.
2. Among the resolved project symbols, `collision(...)` is still the largest hotspot, while `propagation(...)` remains the second largest.
3. This aligns with the recent optimization history: propagation was reduced significantly, and the remaining dominant work is now concentrated in the collision sweep.

## Symbolization Note

This specific `perf record` run contains a large amount of `[unknown]` sample time, including an unresolved user-space symbol and restricted kernel symbols. Because of that, the resolved project-function percentages above should be read as:

- reliable for relative comparison among resolved project functions
- not a complete partition of all runtime

The most visible unresolved entries were:

- `[unknown] 0x3fbc71c71c71c71c`
- `[unknown] [k] 0x000000a200000322`

## Notes

- `perf stat` and `perf record` were run separately, so the recorded FOM values differ slightly due to normal runtime noise.
- The `perf stat` run produced `FOM = 46.41 MLUPS`.
- The `perf record` run produced `FOM = 46.85 MLUPS`.
- Kernel symbols were restricted by the system, and this run also had unresolved user-space samples, so part of the sampled time appears as `[unknown]` and is not merged into the project-function table above.
