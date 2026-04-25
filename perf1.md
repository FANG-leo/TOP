# Perf Hotspot Analysis for Current Version

## Test Setup

- Date: `2026-04-25`
- Build: `Release`
- Executable: `./build-release/top.lbm-exe`
- Config base: `config.txt`
- Analysis config: same as `config.txt`, plus `show_progress = 0`
- MPI ranks: `1`
- Perf command:

```bash
perf record -F 199 -g -o /tmp/top26-perf-20000.data -- \
  mpirun -np 1 ./build-release/top.lbm-exe /tmp/config-20000-perf.txt
```

The progress bar was disabled only to avoid terminal-output noise during sampling. Other runtime parameters were kept the same as the current `config.txt`.

## Run Result

- FOM: `10.98 MLUPS`
- Samples captured: `46392`
- Event: `cycles:P`

## Function Time Breakdown

The table below keeps only project functions from `libtop.lbm-lib.so`.

| Function | Self Overhead | Children Overhead | Notes |
| --- | ---: | ---: | --- |
| `compute_equilibrium_profile(double*, double, int)` | `34.00%` | `50.10%` | Largest hotspot, dominates the collision path |
| `propagation(Mesh*, Mesh const*)` | `22.90%` | `23.24%` | Pure streaming step, mostly self time |
| `get_vect_norm_2(double const*, double const*)` | `12.62%` | `19.76%` | Frequently called math helper |
| `compute_cell_collision(double*, double*)` | `11.68%` | `27.82%` | Main per-cell collision routine |
| `get_cell_velocity(double*, double*, double)` | `2.53%` | `9.42%` | Helper used by collision/equilibrium code |
| `get_cell_density(double*)` | `1.93%` | `4.28%` | Helper used in collision/equilibrium code |
| `collision(Mesh*, Mesh const*)` | `1.12%` | `1.98%` | Wrapper around the collision sweep |
| `special_cells(Mesh*, lbm_mesh_type_s*, lbm_comm_t_s const*)` | `0.92%` | `0.95%` | Boundary/obstacle handling |

## Additional Perf Signals

`perf report` also showed noticeable samples on PLT entries:

| Symbol | Self Overhead |
| --- | ---: |
| `get_vect_norm_2(...)@plt` | `6.15%` |
| `compute_equilibrium_profile(...)@plt` | `3.48%` |
| `get_cell_density(...)@plt` | `0.65%` |

This usually means the code spends a meaningful amount of time in many small helper calls, and the compiler did not fully inline them across the current build boundaries.

## Summary

1. The dominant cost is the collision-side call chain, especially `compute_equilibrium_profile`, `compute_cell_collision`, `get_vect_norm_2`, `get_cell_velocity`, and `get_cell_density`.
2. `propagation()` is the second major hotspot and already appears as mostly direct self time, so improvements there likely need memory-access optimization rather than just reducing call overhead.
3. The PLT samples suggest there is likely room to reduce function-call overhead by inlining or restructuring tiny math helpers inside the hot collision path.

## Raw Perf Extract

```text
Children      Self  Shared Object      Symbol
50.10%    34.00%  libtop.lbm-lib.so  compute_equilibrium_profile(double*, double, int)
27.82%    11.68%  libtop.lbm-lib.so  compute_cell_collision(double*, double*)
23.24%    22.90%  libtop.lbm-lib.so  propagation(Mesh*, Mesh const*)
19.76%    12.62%  libtop.lbm-lib.so  get_vect_norm_2(double const*, double const*)
 9.43%     6.15%  libtop.lbm-lib.so  get_vect_norm_2(double const*, double const*)@plt
 9.42%     2.53%  libtop.lbm-lib.so  get_cell_velocity(double*, double*, double)
 6.73%     3.48%  libtop.lbm-lib.so  compute_equilibrium_profile(double*, double, int)@plt
 4.28%     1.93%  libtop.lbm-lib.so  get_cell_density(double*)
 1.98%     1.12%  libtop.lbm-lib.so  collision(Mesh*, Mesh const*)
 0.95%     0.92%  libtop.lbm-lib.so  special_cells(Mesh*, lbm_mesh_type_s*, lbm_comm_t_s const*)
 0.92%     0.65%  libtop.lbm-lib.so  get_cell_density(double*)@plt
```

## Notes

- Percentages are sampling-based and approximate.
- A small amount of kernel/unknown samples was present, but kernel symbols were restricted by the system and are not included in the hotspot table above.
