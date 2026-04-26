# `np=2`, `omp=1` Propagation Optimization

## Goal

After the positive `collision()` round in [optimism15.md](/home/h/topnew/TOP-26/project/optimism15.md:1), target the next largest hotspot on the current best parallel baseline:

- `mpirun -np 2`
- `OMP_NUM_THREADS=1`

The aim of this round was to reduce overhead inside `propagation()` without changing the MPI communication structure.

## Why `propagation()` Was Next

The dedicated `np = 2`, `omp = 1` hotspot analysis recorded in `optimism15.md` showed:

- `collision(...)`: about `47%`
- `propagation(...)`: about `34%`
- `lbm_comm_halo_exchange(...)`: about `1%`

Since `collision()` had already been improved in the previous round, `propagation()` was the next natural target.

## Problem in the Previous Version

Even on the current best configuration, `propagation()` still entered:

- one full `#pragma omp parallel` region
- two `omp for` sections
- two `omp single` sections

That structure made sense for threaded runs, but it imposed unnecessary OpenMP runtime overhead on the `OMP_NUM_THREADS = 1` fast path that is currently giving the best overall performance.

## Optimization Idea

Keep the propagation logic unchanged, but restructure the implementation so that:

1. interior propagation, top/bottom border propagation, left/right border propagation, and corner handling are split into small helpers
2. when `omp_get_max_threads() <= 1`, those helpers run directly without entering an OpenMP parallel region
3. the interior hot loop uses per-column pointers instead of rebuilding full indices for every assignment

This mirrors the strategy that worked for `collision()` in the previous round.

## Changes Made

In [src/lbm/physics.cpp](/home/h/topnew/TOP-26/project/src/lbm/physics.cpp:350):

1. Split `propagation()` into local helper lambdas for:
   - interior columns
   - top/bottom edges
   - left/right edges
   - corners

2. Added a serial fast path:
   - if `omp_get_max_threads() <= 1`, call the helper loops directly

3. Kept the threaded path for future experiments:
   - if more than one OpenMP thread is available, preserve the existing `omp for` / `omp single` structure

4. Reworked the interior column loop to use plane-local pointers:
   - `in*_col`
   - `out*_col`

   so the hottest part of the loop becomes a simpler contiguous copy pattern.

## Files Changed

- `src/lbm/physics.cpp`

## Validation

### Smoke Test

Measured with:

- `mpirun -np 2`
- `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`
- `config.baseline-1000.txt`

Observed result:

| Metric | Value |
| --- | ---: |
| FOM | `264.29 MLUPS` |

The short run completed normally, so the propagation rewrite did not break the two-rank path.

### Benchmarking Caution

During this round, one invalid measurement was produced by launching:

- a full `20000`-iteration benchmark
- and a full `perf stat` run

at the same time.

Both runs collapsed to about `78 MLUPS`, which was not treated as a real algorithmic regression. That result came from running two heavy MPI jobs concurrently on the same machine, so it was discarded.

All final numbers below were re-run cleanly, one at a time.

## Clean Full 20000-Iteration Benchmark

Measured with:

- `mpirun -np 2`
- `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`
- `config.parallel-baseline-20000.txt`

Observed result:

| Metric | Value |
| --- | ---: |
| FOM | `276.30 MLUPS` |

For comparison, the retained `collision()`-optimized baseline from `optimism15.md` reported:

- `269.57 MLUPS`

So this propagation round gave a small positive gain over the previous best two-rank baseline.

## Clean Post-Optimization `perf stat`

Collected with:

```bash
OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
perf stat -x, -e task-clock,cycles,instructions,L1-dcache-loads,L1-dcache-load-misses \
  mpirun -np 2 ./build-release/top.lbm-exe config.parallel-baseline-20000.txt
```

Observed result:

| Metric | Value |
| --- | ---: |
| FOM | `275.13 MLUPS` |
| task-clock | `37352.46 ms` |
| cycles | `147,322,288,497` |
| instructions | `163,888,111,628` |
| IPC | `1.11` |
| L1 loads | `86,452,461,222` |
| L1 load misses | `14,074,604,310` |
| L1 miss rate | `16.28%` |

For comparison, `optimism15.md` reported:

| Metric | `optimism15` |
| --- | ---: |
| FOM | `278.45 MLUPS` |
| task-clock | `36877.93 ms` |
| IPC | `1.12` |
| L1 miss rate | `16.61%` |

## Interpretation

This round appears to be a modest but real improvement in the ordinary full benchmark, though not a dramatic one.

What changed:

- the clean standalone benchmark improved from `269.57 MLUPS` to `276.30 MLUPS`
- `propagation()` now has a proper single-thread fast path, matching the earlier `collision()` cleanup

What did not change much:

- IPC stayed very close to the previous round
- cache-miss behavior improved only slightly

So the main gain here is likely from reducing unnecessary OpenMP runtime overhead and simplifying the propagation interior loop, not from a major memory-system breakthrough.

## Conclusion

This was a small positive optimization and is worth keeping.

The main lesson is consistent with the previous round: for the current best parallel configuration, the most effective optimizations are still the ones that explicitly favor the real execution mode `np = 2`, `omp = 1`, rather than the more general threaded structure.
