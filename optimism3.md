# Optimization of `compute_cell_collision(double*, double*)`

## Goal

Reduce the runtime cost of the current top hotspot `compute_cell_collision(double*, double*)` identified by `perf`.

## Optimization Idea

After the earlier `compute_equilibrium_profile(...)` optimization, the profile showed that the dominant cost had moved upward into the full collision kernel itself. That meant the next useful step was to reduce arithmetic and control overhead inside `compute_cell_collision()`, not just inside a helper it calls.

The new optimization focuses on three things:

1. Inline the D2Q9 equilibrium formulas directly in the collision kernel.
   Instead of calling a per-direction helper 9 times, the function now computes the 9 equilibrium values directly from closed-form expressions.

2. Precompute shared scalar terms once per cell.
   The new version computes these only once:
   - `density`
   - `1 / density`
   - `vx`, `vy`
   - `vx^2`, `vy^2`
   - `|v|^2`
   - `vx + vy`, `vx - vy`
   - `(vx + vy)^2`, `(vx - vy)^2`
   - weighted density factors for center, axis, and diagonal directions

3. Rewrite the BGK update in blend form.
   Instead of:

```text
f_out = f_in - omega * (f_in - f_eq)
```

   the code now uses:

```text
f_out = (1 - omega) * f_in + omega * f_eq
```

   which is algebraically identical and easier for the compiler to schedule as a fused weighted blend.

## Code Changes

- Removed the remaining per-direction helper-call overhead from `compute_cell_collision()`.
- Expanded the 9 D2Q9 equilibrium distributions directly in `src/lbm/physics.cpp`.
- Reused common terms across axis and diagonal directions.

## Why This Helps

- Fewer repeated expressions per cell
- No helper dispatch in the hottest loop body
- Better instruction-level optimization opportunities
- Clearer mapping to the D2Q9 stencil structure

## Files Changed

- `src/lbm/physics.cpp`

## Benchmark

Measured on `2026-04-25` with:

- build: `Release`
- config: `config.results2-20000.txt`
- iterations: `20000`
- output file: `results2.raw`
- MPI ranks: `1`

Observed results:

| Metric | Value |
| --- | ---: |
| Elapsed time | `109.96 s` |
| FOM | `23.36 MLUPS` |
| Output file size | `392M` |
