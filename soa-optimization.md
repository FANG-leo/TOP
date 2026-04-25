# AoS to SoA Refactor

## Goal

Refactor the mesh storage from Array of Structures (AoS) to Structure of Arrays (SoA) so the hottest numerical kernels operate on direction-major planes instead of per-cell interleaved data.

## Why Change the Layout

In the AoS layout, one cell stored all 9 D2Q9 populations contiguously:

```text
[f0, f1, ..., f8] for cell 0
[f0, f1, ..., f8] for cell 1
...
```

That is convenient for generic code, but not ideal for the optimized hot loops:

- `collision()` wants to sweep many adjacent cells and let the compiler vectorize across them
- `propagation()` often shifts one direction plane independently of the others
- interleaving all 9 directions inside each cell hurts contiguous per-direction access

The SoA layout stores one full plane per direction:

```text
all f0 values
all f1 values
...
all f8 values
```

This is much friendlier to:

- SIMD/vectorization across cells
- direction-wise streaming
- simpler contiguous memory access in the hottest kernels

## Changes Made

1. Switched the logical mesh layout to direction-major storage.
   `mesh->cells` is still one contiguous allocation, but it is now interpreted as 9 planes of size:

```text
width * height
```

2. Added explicit SoA access helpers in `include/lbm/structures.hpp`.
   New helpers include:

   - `Mesh_scalar_index(...)`
   - `Mesh_plane_size(...)`
   - `Mesh_direction_plane(...)`
   - `Mesh_direction_plane_const(...)`
   - `Mesh_get_value(...)`
   - `Mesh_set_value(...)`
   - `Mesh_load_cell(...)`
   - `Mesh_store_cell(...)`

3. Rewrote the hot kernels to use SoA directly.
   - `collision()` now reads and writes the 9 direction planes explicitly
   - `propagation()` now operates on direction planes directly instead of interleaved cell storage

4. Updated initialization, boundary handling, save/render logic, and halo exchange paths.
   Non-hot paths that still need whole-cell semantics now use `Mesh_load_cell(...)` / `Mesh_store_cell(...)` as a compatibility layer.

5. Preserved overall numerical semantics.
   The algorithm is unchanged; only the in-memory organization and access strategy were refactored.

## Why This Helps

- Better cache behavior for direction-wise kernels
- Improved vectorization opportunities in `collision()`
- Cleaner per-plane streaming in `propagation()`
- Reduced penalty from interleaved AoS access in the hottest loops

## Files Changed

- `include/lbm/structures.hpp`
- `src/lbm/initialization.cpp`
- `src/lbm/physics.cpp`
- `src/lbm/communications.cpp`

## Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results6-20000.txt`
- iterations: `20000`
- output file: `results6.raw`
- MPI ranks: `1`

Observed results:

| Metric | Value |
| --- | ---: |
| Elapsed time | `36.05 s` |
| FOM | `71.69 MLUPS` |
| Output file size | `392M` |
