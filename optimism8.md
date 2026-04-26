# Propagation Main-Path and Boundary-Path Separation Optimization

## Goal

Further optimize `propagation(Mesh*, Mesh const*)` by keeping the dominant interior sweep as clean as possible and moving border handling onto a separate, low-frequency path.

## Problem in the Previous Version

The previous propagation kernel already had a fast interior loop, but the boundary work still fell back to a generic border traversal using:

- `Mesh_get_value(...)`
- `Mesh_set_value(...)`
- per-cell border checks
- per-direction validity checks

That code was only touching a small fraction of the mesh, but it still reintroduced abstraction overhead and extra control flow into the propagation stage.

## Optimization Idea

Split the propagation step into two clearly different regions:

1. A branch-free interior fast path for the vast majority of cells.
2. A separate boundary path that handles only the four edges and four corners with direct SoA plane accesses.

This keeps the common case simple for the compiler while still preserving the original border semantics.

## Changes Made

1. Replaced the `f0` plane copy loop with `std::memcpy(...)`.
   The rest population is copied plane-to-plane directly because it does not shift during propagation.

2. Kept the interior pull-style SoA kernel as the dominant path and added an explicit SIMD hint on the inner contiguous loop.

3. Removed the old generic border double-loop from the normal path.
   Instead, the new implementation now handles:

- left edge
- right edge
- bottom edge
- top edge
- four corners

with direct per-plane assignments.

4. Kept a tiny generic fallback only for degenerate mesh sizes.
   This preserves correctness for unusual edge-case dimensions without polluting the main optimized path.

## Why This Helps

- Less control flow in the hot propagation stage
- No `Mesh_get_value(...)` / `Mesh_set_value(...)` overhead on ordinary border handling
- Better separation between the common interior case and the uncommon edge case
- More predictable memory access on both the interior sweep and the explicit edge code

## Files Changed

- `src/lbm/physics.cpp`
- `config.results7-20000.txt`

## Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results7-20000.txt`
- iterations: `20000`
- output file: `results7.raw`
- MPI ranks: `1`

Observed results:

| Metric | Value |
| --- | ---: |
| Elapsed time | `23.70 s` |
| FOM | `109.62 MLUPS` |
| Output file size | `392M` |
