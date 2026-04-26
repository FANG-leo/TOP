# OpenMP Attempt on `collision()` with No Positive Gain

## Goal

Start the OpenMP phase from the lowest-risk entry point by parallelizing only the outer column loop of `collision(Mesh*, Mesh const*)`.

## What Was Changed

Added an OpenMP parallel-for directive to the outer loop of `collision()` in `src/lbm/physics.cpp`:

```cpp
#pragma omp parallel for schedule(static)
```

The inner loop kept the existing SIMD directive:

```cpp
#pragma omp simd
```

So this version used thread-level parallelism across interior columns and SIMD within each column.

## Why This Was Tried First

This was the most conservative way to start OpenMP because:

- each interior column is independent in `collision()`
- it avoids changing the numerical algorithm
- it keeps write ownership disjoint across threads
- it is easy to compare directly with the current serial best version

## Benchmark Setup

Tests were run on `2026-04-26` with:

- build: `Release`
- executable: `./build-release/top.lbm-exe`
- config: `config.results7-20000.txt`
- iterations: `20000`
- MPI ranks: `1`
- machine topology:
  - `16` logical CPUs
  - `8` physical cores
  - `1` socket

## Results

Current serial best reference before this OpenMP attempt:

| Version | Time | FOM |
| --- | ---: | ---: |
| Serial `results7` version | `23.70 s` | `109.62 MLUPS` |

OpenMP attempt results:

| OpenMP setting | Time | FOM |
| --- | ---: | ---: |
| `OMP_NUM_THREADS=16` | `27.21 s` | `96.15 MLUPS` |
| `OMP_NUM_THREADS=8 OMP_PROC_BIND=close OMP_PLACES=cores` | `26.28 s` | `99.68 MLUPS` |

## Analysis

This first OpenMP version did not provide a positive gain.

The most likely reasons are:

1. Only `collision()` was parallelized.
   The full timestep still contains:

- `special_cells()`
- halo exchange
- `propagation()`
- output work

   so the parallel fraction of the full step is still limited.

2. The current problem size is not large enough to hide the OpenMP overhead cleanly.
   Thread startup, scheduling, and synchronization costs are now visible.

3. SMT did not help here.
   `16` threads performed worse than the `8` core-bound run, which suggests the kernel is not benefiting from hyper-threading in this form.

4. The serial code is already highly optimized.
   After SoA, `restrict`, `-march=native`, SIMD, and propagation-path cleanup, the baseline single-process version is already strong, so naive parallelization has less room to win.

## Conclusion

This OpenMP attempt should be treated as a useful negative result:

- OpenMP is still a reasonable direction overall
- but parallelizing only `collision()` is not enough on its own
- the next meaningful OpenMP step should parallelize `collision()` and `propagation()` as a coordinated pair, then re-measure

## Recommendation

If OpenMP work continues, the next step should be:

1. either extend OpenMP to `propagation()` as well
2. or revert this isolated `collision()` OpenMP change and only keep a version that parallelizes the major kernels together
