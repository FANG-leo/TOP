# Optimization of `compute_equilibrium_profile(double*, double, int)`

## Goal

Reduce the runtime cost of the hottest collision-side equilibrium computation path identified by `perf`.

## Changes Made

1. Replaced the generic equilibrium computation internals with a D2Q9-specific component-based helper.
   Instead of recomputing vector dot products through generic helper functions, the new path uses direct `vx` / `vy` arithmetic and the closed-form D2Q9 expression.

2. Removed repeated work inside the hot collision loop.
   In `compute_cell_collision()`, the optimized path now computes:
   - `density`
   - `1 / density`
   - `vx`
   - `vy`
   - `v^2`

   only once per cell, then reuses them for all 9 directions.

3. Avoided repeated helper-function calls on the hotspot path.
   The old implementation repeatedly called:
   - `get_cell_density()`
   - `get_cell_velocity()`
   - `get_vect_norm_2()`
   - `compute_equilibrium_profile()`

   inside the collision flow.

   The new implementation keeps `compute_equilibrium_profile()` available for non-hot code paths such as initialization, but the collision kernel now uses a tighter specialized path.

4. Kept the numerical formula unchanged.
   The equilibrium expression is still:

```text
f_eq = w_i * rho * (1 + 3p + 4.5p^2 - 1.5|v|^2)
```

   where `p = e_i · v`.

## Expected Effect

- Lower function-call overhead in the collision kernel
- Less repeated arithmetic per cell
- Better chances for compiler inlining and instruction scheduling
- Reduced pressure on the hotspot previously dominated by `compute_equilibrium_profile(...)`

## Files Changed

- `src/lbm/physics.cpp`

## Benchmark

Measured on `2026-04-25` with:

- build: `Release`
- config: `config.results1-20000.txt`
- iterations: `20000`
- output file: `results1.raw`
- MPI ranks: `1`

Observed results:

| Metric | Value |
| --- | ---: |
| Elapsed time | `113.40 s` |
| FOM | `22.64 MLUPS` |
| Output file size | `392M` |
