# Optimization of `propagation(Mesh*, Mesh const*)`

## Goal

Reduce the runtime cost of `propagation(Mesh*, Mesh const*)`, which had become the largest hotspot in the latest `perf` profile.

## Optimization Idea

The old implementation used a generic triple loop:

- loop over all cells
- loop over all 9 directions
- compute destination coordinates
- check whether the destination is still inside the local mesh
- write the propagated value

That is correct, but it is expensive in the hotspot because it repeats:

- 9 boundary checks per cell
- repeated direction lookups
- repeated `Mesh_get_cell()` address calculations
- a branch inside the innermost loop

The optimized version rewrites propagation as a D2Q9-specific streaming kernel.

## Changes Made

1. Added a branchless fast path for interior cells.
   The large interior region now uses a specialized D2Q9 pull-style kernel that writes all 9 populations of one destination cell in one shot.

2. Kept a small generic fallback only for border cells.
   Borders are a tiny fraction of the mesh, so the old safe generic logic is now confined there instead of being used everywhere.

3. Replaced repeated `Mesh_get_cell()` calls in the hot path with direct flat-array indexing.
   The optimized interior loop works directly on:

```text
mesh->cells
```

   using precomputed:

   - `cell_stride`
   - `col_stride`

   to reduce address recomputation.

4. Kept the same propagation semantics.
   The numerical behavior is unchanged; only the implementation strategy of the hot path was rewritten to improve locality and reduce branch overhead.

## Why This Helps

- Fewer branches in the dominant interior region
- Less per-element index arithmetic
- Contiguous writes to one destination cell at a time
- Better cache locality than repeatedly sweeping the whole mesh by direction

## Files Changed

- `src/lbm/physics.cpp`

## Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results3-20000.txt`
- iterations: `20000`
- output file: `results3.raw`
- MPI ranks: `1`

Observed results:

| Metric | Value |
| --- | ---: |
| Elapsed time | `65.52 s` |
| FOM | `39.37 MLUPS` |
| Output file size | `392M` |
