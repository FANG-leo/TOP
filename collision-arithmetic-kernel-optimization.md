# Collision Arithmetic Kernel Optimization

## Goal

Further optimize `collision(Mesh*, Mesh const*)` after the SoA refactor and `-march=native` work by making the inner collision sweep more arithmetic-focused and easier for the compiler to schedule efficiently.

## Problem in the Previous Version

The previous collision kernel was already fast, but it still had two avoidable costs in the hottest inner loop:

1. It recomputed the equilibrium terms from normalized velocities `vx` / `vy`, which lengthened the dependency chain from the density reciprocal through several derived quantities.
2. It kept the loop in an indexed style, which left some address-generation work in the critical path.

## Optimization Idea

Rewrite the collision kernel around conserved moments instead of around fully materialized velocities:

- compute `mx` and `my` first
- reuse `inv_density` and `inv_density^2`
- build the axis and diagonal equilibrium terms directly from those moments
- scan the SoA planes with incrementing pointers inside the inner loop

This keeps the algorithm identical while reducing arithmetic and addressing overhead in the hottest path.

## Changes Made

1. Rewrote the inner loop of `collision()` to use:

- momentum in `x`: `mx`
- momentum in `y`: `my`
- `inv_density`
- `inv_density2`

instead of first expanding everything through explicit `vx`, `vy`, `sum`, and `diff` values.

2. Replaced repeated indexed plane accesses with per-column pointers that advance through the interior sweep.

3. Reused common terms more aggressively:

- `axis_x`
- `axis_y`
- `diag_sum`
- `diag_diff`
- `momentum_x`
- `momentum_y`

4. Kept the SIMD-friendly inner loop structure intact.

## Why This Helps

- Shorter arithmetic dependency chain after the reciprocal
- Fewer repeated address calculations in the hot loop
- Better fit for the current SoA layout and compiler vectorization strategy
- No algorithmic change and no layout change

## Files Changed

- `src/lbm/physics.cpp`
- `config.results8-20000.txt`

## Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results8-20000.txt`
- iterations: `20000`
- output file: `results8.raw`
- MPI ranks: `1`

Observed results:

| Metric | Value |
| --- | ---: |
| Elapsed time | `24.17 s` |
| FOM | `107.47 MLUPS` |
| Output file size | `392M` |
