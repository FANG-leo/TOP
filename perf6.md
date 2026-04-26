# Perf Analysis for Current SoA Version

## Test Setup

- Date: `2026-04-26`
- Build: `Release`
- Executable: `./build-release/top.lbm-exe`
- Config: `config.results6-20000.txt`
- Iterations: `20000`
- MPI ranks: `1`
- Output file: `results6.raw`

This test used the latest SoA version and kept `show_progress = 0` to avoid terminal-output noise during profiling.

## Overall Metrics

Measured with `perf stat` on the current SoA version:

| Metric | Value |
| --- | ---: |
| Elapsed time | `35.84 s` |
| FOM | `72.73 MLUPS` |
| CPU utilization | `98.5%` |
| IPC | `2.32` |
| L1 miss rate | `10.69%` |

Raw counters:

- `task-clock`: `35302.69 ms`
- `cycles`: `142,171,114,713`
- `instructions`: `329,549,238,677`
- `L1-dcache-loads`: `121,082,572,765`
- `L1-dcache-load-misses`: `12,948,154,056`

## Function Time Breakdown

Measured with `perf record` + `perf report --stdio`.

The table below keeps the main resolved project functions from `libtop.lbm-lib.so`.

| Function | Self Overhead | Children Overhead | Notes |
| --- | ---: | ---: | --- |
| `collision(Mesh*, Mesh const*)` | `60.03%` | `60.83%` | Collision remains the dominant hotspot even after the SoA refactor |
| `propagation(Mesh*, Mesh const*)` | `29.66%` | `30.23%` | Propagation is still the second major hotspot |
| `special_cells(Mesh*, lbm_mesh_type_s*, lbm_comm_t_s const*)` | `5.50%` | `5.74%` | Boundary and obstacle handling became more visible relative to the faster core kernels |
| `save_frame(_IO_FILE*, Mesh const*)` | `0.41%` | `2.21%` | Output overhead is still small compared with the compute kernels |
| `get_vect_norm_2(double const*, double const*)` | `0.73%` | `0.90%` | Small helper cost mainly from save/render-side macroscopic calculations |
| `get_cell_velocity(double*, double const*, double)` | `0.43%` | `0.64%` | Also mostly from save/render-side postprocessing |

## Interpretation

1. The SoA refactor significantly improved total runtime and FOM, but it did not change the basic ordering of hotspots.
2. `collision(...)` is still the main bottleneck, now at about `60%` of resolved sampled time.
3. `propagation(...)` remains the second major hotspot at about `30%`, but both kernels are running much faster in absolute terms than before the layout change.
4. The lower L1 miss rate compared with earlier pre-SoA profiles is consistent with the improved data layout.

## Notes

- `perf stat` and `perf record` were run separately, so the recorded FOM values differ slightly due to normal runtime noise.
- The `perf stat` run produced `FOM = 72.73 MLUPS`.
- The `perf record` run produced `FOM = 72.40 MLUPS`.
- A small amount of kernel/user sample time still appears as `[unknown]`, but the resolved project-symbol breakdown is much cleaner than in some earlier runs.
