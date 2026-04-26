# Cache-Line Alignment and Pointer-Walk Hot-Loop Optimization

## Goal

Try a lower-risk memory-bandwidth and cache-behavior optimization pass without changing timestep semantics:

- align the main mesh allocations to cache-line boundaries
- make the hottest collision and propagation loops walk contiguous pointers directly
- give the compiler stronger alignment information on the SoA planes

The intent was to reduce address-generation overhead, improve cache-line friendliness, and help the compiler schedule the memory-heavy kernels more efficiently.

## Optimization Idea

Instead of attempting another aggressive timestep fusion, this round kept the existing algorithmic structure unchanged and focused on three safer improvements:

1. Aligned storage for the major arrays.
2. Pointer-increment scans in the hot inner loops instead of repeated indexed loads/stores.
3. Explicit 64-byte alignment assumptions on the SoA planes in the collision and propagation kernels.

This targets memory-system efficiency and compiler code generation while preserving the same collision / halo / propagation flow.

## Changes Made

1. Switched the main mesh and communication-buffer allocations to `posix_memalign(...)`.

   Updated:

- `Mesh::cells`
- `lbm_mesh_type_t::types`
- `lbm_comm_t::buffer`

   so these major arrays are now allocated on `64`-byte boundaries.

2. Added a small alignment helper in `src/lbm/physics.cpp`.

   The collision and propagation kernels now use `__builtin_assume_aligned(..., 64)` on their SoA direction-plane pointers.

3. Rewrote the hot inner loop of `collision()` to use advancing per-column pointers.

   This removes repeated `idx`-based plane accesses from the innermost path and combines that with the moment-based equilibrium form:

- `mx`
- `my`
- `inv_density`
- `inv_density^2`

4. Rewrote the dominant interior path of `propagation()` to use pointer walks as well.

   The direction planes are still streamed in the same pull-style pattern, but the inner loop now advances aligned pointers instead of re-forming each address from `idx` on every iteration.

## Why This Might Help

- Better alignment for cache lines and SIMD loads/stores
- Less address-generation work in the hottest loops
- Cleaner contiguous access patterns in both major kernels
- No algorithmic or MPI-semantics change

## Files Changed

- `src/lbm/structures.cpp`
- `src/lbm/communications.cpp`
- `src/lbm/physics.cpp`
- `config.results10-20000.txt`

## Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results10-20000.txt`
- iterations: `20000`
- output file: `results10.raw`
- MPI ranks: `1`
- OpenMP: `OMP_NUM_THREADS=1`

Observed result:

| Metric | Value |
| --- | ---: |
| Elapsed time | `24.60 s` |
| FOM | `105.54 MLUPS` |

## Comparison

Compared with the previous best serial kernel version:

| Version | Time | FOM |
| --- | ---: | ---: |
| `optimism8` serial best | `23.70 s` | `109.62 MLUPS` |
| this alignment + pointer-walk version | `24.60 s` | `105.54 MLUPS` |

## Interpretation

This round did not improve end-to-end performance.

The likely reason is that these changes reduced some local hot-loop overhead, but not enough to outweigh the total runtime costs that still dominate the benchmark. In particular:

- the baseline was already highly optimized
- output and non-hot-path costs were unchanged
- alignment and pointer-walk cleanups appear to have smaller impact than the earlier larger structural wins

## Conclusion

This is a useful negative result:

- memory-layout cleanup alone still has some theoretical appeal
- but this specific alignment-and-pointer-walk pass did not beat the current best serial version
- the next meaningful gains will probably need either:
  - a more effective structural reduction in memory traffic
  - or a correctness-preserving partial fusion strategy with tighter scope than the earlier failed full fusion
