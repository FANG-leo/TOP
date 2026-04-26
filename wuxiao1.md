# Restrict and Vectorization Optimization for the Collision Path

## Goal

Push the current collision hotspot further by doing two related improvements:

1. extend `restrict` from the hotspot implementation up into the public header-level interfaces
2. make the inner collision sweep easier for the compiler to vectorize

## Optimization Idea

The latest version had already reduced a lot of abstraction overhead inside the collision path, but there were still two remaining limitations:

1. The compiler could not see aliasing guarantees consistently at the interface level.
   Even though the hottest implementation path already used `__restrict` locally, the public function signatures still did not communicate those no-alias assumptions clearly.

2. The collision sweep was structurally close to vectorizable, but the compiler still benefited from a more explicit SIMD hint on the inner contiguous loop.

So this optimization focused on making aliasing information explicit everywhere it matters and exposing the collision loop more clearly as a SIMD-friendly contiguous kernel.

## Changes Made

1. Added a const cell-pointer type in the shared structures header.

```text
typedef const double* lbm_mesh_const_cell_t;
```

2. Extended `restrict` to header-level interfaces in `include/lbm/physics.hpp`.
   This was applied to the main hotspot-related interfaces such as:

   - `get_cell_density(...)`
   - `get_cell_velocity(...)`
   - `compute_cell_collision(...)`
   - `compute_bounce_back(...)`
   - `compute_inflow_zou_he_poiseuille_distr(...)`
   - `compute_outflow_zou_he_const_density(...)`
   - `special_cells(...)`
   - `collision(...)`
   - `propagation(...)`

3. Aligned the implementation signatures in `src/lbm/physics.cpp` with the new interface-level aliasing guarantees.

4. Added an explicit SIMD hint on the inner `collision()` loop.
   The contiguous per-column loop over interior cells now carries:

```text
#pragma omp simd
```

   so the compiler is encouraged to vectorize the collision sweep over adjacent cells.

## Why This Helps

- Better alias analysis for the compiler across function boundaries
- Clearer intent that input and output cell pointers do not overlap
- Improved chance of automatic SIMD vectorization in the hottest contiguous loop
- Low-risk optimization that does not change the numerical algorithm

## Files Changed

- `include/lbm/structures.hpp`
- `include/lbm/physics.hpp`
- `src/lbm/physics.cpp`

## Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results5-20000.txt`
- iterations: `20000`
- output file: `results5.raw`
- MPI ranks: `1`

Observed results:

| Metric | Value |
| --- | ---: |
| Elapsed time | `55.20 s` |
| FOM | `46.68 MLUPS` |
| Output file size | `392M` |
