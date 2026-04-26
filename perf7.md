# Perf Analysis for the `-march=native` Build

## Test Setup

- Date: `2026-04-26`
- Build: `Release`
- Executable: `./build-release/top.lbm-exe`
- Config: `config.results6-20000.txt`
- Iterations: `20000`
- MPI ranks: `1`
- Output file: `results6.raw`

This profile was collected on the current SoA version after enabling `-march=native`.

## Overall Metrics

Measured with `perf stat`:

| Metric | Value |
| --- | ---: |
| Elapsed time | `74.60 s` |
| FOM | `34.54 MLUPS` |
| CPU utilization | `99.5%` |
| IPC | `0.68` |
| L1 miss rate | `15.75%` |

Raw counters:

- `task-clock`: `74225.04 ms`
- `cycles`: `297,628,480,378`
- `instructions`: `202,330,235,437`
- `L1-dcache-loads`: `91,013,397,588`
- `L1-dcache-load-misses`: `14,334,521,623`

## Function Time Breakdown

Measured with `perf record` + `perf report --stdio`.

| Function | Self Overhead | Children Overhead | Notes |
| --- | ---: | ---: | --- |
| `propagation(Mesh*, Mesh const*)` | `46.62%` | `47.18%` | Propagation became the largest resolved hotspot in this `perf` run |
| `collision(Mesh*, Mesh const*)` | `44.51%` | `45.02%` | Collision is still nearly the same size as propagation |
| `special_cells(Mesh*, lbm_mesh_type_s*, lbm_comm_t_s const*)` | `5.49%` | `5.72%` | Boundary and obstacle handling remains visible but much smaller than the two core kernels |
| `save_frame(_IO_FILE*, Mesh const*)` | `0.93%` | `1.65%` | Output work is still secondary |
| `get_cell_velocity(double*, double const*, double)` | `0.25%` | `0.50%` | Mostly save/render-side postprocessing |
| `get_vect_norm_2(double const*, double const*)` | `0.11%` | `0.36%` | Also mainly postprocessing-side helper work |

## Interpretation

1. With `-march=native`, the bare benchmark improved strongly, but the `perf` run itself adds substantial overhead, so the profiled elapsed time and FOM are much worse than the non-profiled benchmark.
2. The two main kernels are now very close in sampled cost, with `propagation(...)` slightly ahead of `collision(...)` in this run.
3. This profile suggests the next round of single-core optimization should probably look at both kernels together instead of assuming collision is still overwhelmingly dominant.

## Notes

- The `perf stat` run produced `FOM = 34.54 MLUPS`.
- The `perf record` run produced `FOM = 34.77 MLUPS`.
- The non-`perf` benchmark on the same build was much faster at `28.67 s` and `91.17 MLUPS`, so please compare optimization success using the non-`perf` benchmark first and use `perf` mainly for hotspot ordering.
