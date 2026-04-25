# Optimization of `compute_cell_collision(...)` via the `collision()` Hot Loop

## Goal

Further reduce the runtime cost attributed to `compute_cell_collision(...)`, which had become the dominant hotspot again after optimizing `propagation(...)`.

## Optimization Idea

At this stage, the bottleneck was no longer only the arithmetic inside one collision update. A large part of the remaining cost came from how the collision kernel was invoked:

- `collision()` iterated over cells using `Mesh_get_cell()`
- each cell update crossed a function boundary into `compute_cell_collision(...)`
- address computation was repeated for every cell

So the next useful optimization was to make the *outer collision sweep* cheaper, not just the per-cell formula.

## Changes Made

1. Introduced a raw-pointer D2Q9 collision helper.
   The collision math was moved into an internal `collide_cell_d2q9(...)` helper that works directly on:

```text
double* cell_out
const double* cell_in
```

   plus precomputed `omega` constants.

2. Rewrote `collision()` to scan the flat mesh storage directly.
   Instead of calling `Mesh_get_cell(...)` for every cell, the optimized loop now uses:

   - `cell_stride`
   - `col_stride`
   - direct `mesh->cells` indexing

   to reach each cell.

3. Removed repeated per-cell setup from the outer loop.
   The relaxation constants:

   - `omega`
   - `1 - omega`

   are now computed once per collision sweep and passed into the helper.

4. Kept the public function interface intact.
   `compute_cell_collision(...)` still exists, but it now forwards to the internal optimized helper. That preserves compatibility while letting the hot loop use the faster path.

## Why This Helps

- Less address-calculation overhead in the collision sweep
- No repeated `Mesh_get_cell()` calls in the hottest loop
- Better inlining opportunities for the compiler
- Cleaner raw-pointer kernel for instruction scheduling

## Files Changed

- `src/lbm/physics.cpp`

## Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results4-20000.txt`
- iterations: `20000`
- output file: `results4.raw`
- MPI ranks: `1`

Observed results:

| Metric | Value |
| --- | ---: |
| Elapsed time | `55.00 s` |
| FOM | `46.85 MLUPS` |
| Output file size | `392M` |
