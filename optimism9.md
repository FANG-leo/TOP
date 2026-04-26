# OpenMP on `collision()` and `propagation()` Together

## Goal

Extend the current OpenMP experiment from `collision(Mesh*, Mesh const*)` alone to both major timestep kernels:

- `collision(Mesh*, Mesh const*)`
- `propagation(Mesh*, Mesh const*)`

Then re-measure the full `20000`-iteration run on the current optimized SoA code path.

## Why This Was Tried

The latest single-rank profile and benchmark history showed:

- the best serial version had reached `109.62 MLUPS`
- `collision(...)` and `propagation(...)` had become the two dominant kernels
- parallelizing only `collision()` did not improve end-to-end runtime

So the next reasonable OpenMP step was to parallelize both heavy kernels instead of only one of them.

## Changes Made

1. Kept the existing OpenMP outer-loop parallelization in `collision()`.

2. Added OpenMP thread-level parallelism to `propagation()`.
   The dominant interior sweep now runs under:

```cpp
#pragma omp parallel
```

with workshared outer loops:

```cpp
#pragma omp for schedule(static)
```

while the contiguous inner loops keep their SIMD structure.

3. Kept the branch-free interior fast path intact.
   The OpenMP changes were added around the existing SoA propagation structure rather than rewriting the numerical logic again.

4. Left the small edge and corner handling on explicit low-frequency paths.
   Border work still stays separate from the main interior sweep so the hot path remains clean.

5. Added a dedicated benchmark config:

- `config.results9-20000.txt`

with output directed to:

- `results9.raw`

## Why This Might Help

- Both dominant kernels now expose thread-level parallel work
- The interior SoA loops already have contiguous access and SIMD hints
- The full timestep should have a larger parallel fraction than the earlier collision-only OpenMP attempt

## Files Changed

- `src/lbm/physics.cpp`
- `config.results9-20000.txt`

## Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results9-20000.txt`
- iterations: `20000`
- output file: `results9.raw`
- MPI ranks: `1`
- OpenMP: `OMP_NUM_THREADS=8 OMP_PROC_BIND=close OMP_PLACES=cores`

Observed timed run:

| Metric | Value |
| --- | ---: |
| Elapsed time | `28.44 s` |
| FOM | `91.56 MLUPS` |

An earlier run on the same configuration in the same session produced:

| Metric | Value |
| --- | ---: |
| FOM | `92.13 MLUPS` |

## Interpretation

This combined OpenMP version still did not beat the current best serial result:

| Version | Time | FOM |
| --- | ---: | ---: |
| Serial `results7` best | `23.70 s` | `109.62 MLUPS` |
| OpenMP `results9` combined-kernel run | `28.44 s` | `91.56 MLUPS` |

So, on this machine and problem size, adding OpenMP to both `collision()` and `propagation()` together is still a net regression relative to the best serial kernel version.

## Conclusion

This is another useful negative result:

- the main issue was not just that `propagation()` had been left serial
- even with both major kernels parallelized, OpenMP overhead and/or memory-system contention still outweigh the benefit here
- the next likely opportunities are not "add more OpenMP in the same style", but either:
  - a different parallelization strategy
  - a larger problem size
  - or more structural kernel changes before retrying threaded scaling
