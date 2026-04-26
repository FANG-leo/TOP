# Halo Buffer Reuse Attempt

## Goal

Run a fresh `np = 2`, `omp = 1` performance pass on top of the current [optimism17.md](/home/h/topnew/TOP-26/project/optimism17.md:1) code, identify the next concrete bottleneck, then try one focused optimization round against it.

The specific question for this round was:

- after the timestep / `MPI_Barrier` cleanup, what should be optimized next?

## Fresh Perf Analysis on `optimism17`

The decision pass was collected with:

- build: `Release`
- MPI ranks: `2`
- OpenMP: `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`
- hotspot config: `config.baseline-1000.txt`
- full-stat config: `config.parallel-baseline-20000.txt`

### `perf stat`

Collected with:

```bash
OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
perf stat -x, -e task-clock,cycles,instructions,L1-dcache-loads,L1-dcache-load-misses \
  mpirun -np 2 ./build-release/top.lbm-exe config.parallel-baseline-20000.txt
```

Observed result on the `optimism17` code:

| Metric | Value |
| --- | ---: |
| FOM | `254.37 MLUPS` |
| task-clock | `40410.14 ms` |
| cycles | `161,142,218,235` |
| instructions | `165,677,995,613` |
| IPC | `1.03` |
| L1 loads | `86,865,797,265` |
| L1 load misses | `14,087,722,215` |
| L1 miss rate | `16.22%` |

This value is lower than a plain benchmark run, which is expected because `perf` adds measurement overhead. The important use here is comparison and hotspot direction, not headline FOM.

### `perf record/report`

Collected with:

```bash
OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
perf record -F 199 -g -o /tmp/perf-opt17-np2-omp1.data -- \
  mpirun -np 2 ./build-release/top.lbm-exe config.baseline-1000.txt
```

and inspected with:

```bash
perf report --stdio -i /tmp/perf-opt17-np2-omp1.data
```

The important hotspots were:

- `collision(...)`: about `49.8%`
- `propagation(...)`: about `31.2%`
- `lbm_comm_halo_exchange(...)`: about `9.2%`
- `special_cells(...)`: about `3.5%`
- `__memmove_avx_unaligned_erms`: about `2.6%`

## Why `halo_exchange` Was Chosen

`collision()` was still the single largest function, but it had already gone through several profitable cleanup rounds.

By contrast, `lbm_comm_halo_exchange(...)` now stood out as:

- large enough to matter
- structurally simple
- and likely to contain fixed overhead rather than deep numerical dependence

The most obvious fixed overhead in the current implementation was repeated creation of temporary `std::vector<double>` buffers inside every halo exchange call.

## Optimization Idea

Move the row/column communication buffers out of the timestep hot path:

1. allocate reusable row/column send and receive buffers once during `lbm_comm_init(...)`
2. reuse them in every call to `lbm_comm_halo_exchange(...)`
3. avoid per-step `std::vector` construction, resize, and destruction

In principle, this should reduce the fixed communication-side cost exposed by the new `optimism17` profile.

## Code Changes Tried

The attempted implementation:

- added persistent row/column halo buffers to `lbm_comm_t`
- allocated them in `lbm_comm_init(...)`
- freed them in `lbm_comm_release(...)`
- changed `pack_row(...)`, `pack_column(...)`, `unpack_row(...)`, and `unpack_column(...)` to use raw reusable buffers
- removed the per-call `std::vector<double>` temporaries inside `lbm_comm_halo_exchange(...)`

The modified files during the attempt were:

- `include/lbm/communications.hpp`
- `src/lbm/communications.cpp`

## Validation

### Smoke Test

Measured with:

- `mpirun -np 2`
- `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`
- `config.baseline-1000.txt`

Observed result:

| Metric | Value |
| --- | ---: |
| FOM | `254.44 MLUPS` |

The communication path still ran correctly, so the idea was mechanically safe.

### Benchmarking Caution

An early pair of `20000`-iteration runs was accidentally launched concurrently:

- one plain benchmark
- one `perf stat` run

Both dropped to about `86 MLUPS`. Those numbers were discarded because they were contaminated by resource contention from running two heavy MPI jobs at the same time.

Only the standalone reruns below were used for the final decision.

### Clean Full 20000-Iteration Benchmark

Measured with:

- `mpirun -np 2`
- `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`
- `config.parallel-baseline-20000.txt`

Observed result:

| Metric | Value |
| --- | ---: |
| FOM | `277.85 MLUPS` |

For comparison, the retained baseline from `optimism17.md` was:

- `286.48 MLUPS`

So the reusable-buffer version regressed by about `8.6 MLUPS`.

### Clean Post-Change `perf stat`

Collected with the same `perf stat` command as above.

Observed result:

| Metric | Value |
| --- | ---: |
| FOM | `284.58 MLUPS` |
| task-clock | `36147.50 ms` |
| cycles | `143,155,049,698` |
| instructions | `159,743,979,702` |
| IPC | `1.12` |
| L1 loads | `83,641,040,633` |
| L1 load misses | `13,976,524,894` |
| L1 miss rate | `16.71%` |

### Clean Post-Change `perf record/report`

Collected again on `config.baseline-1000.txt` to see whether the communication share moved.

Important hotspots after the attempted change:

- `collision(...)`: about `45.0%`
- `propagation(...)`: about `36.8%`
- `lbm_comm_halo_exchange(...)`: about `7.7%`
- `special_cells(...)`: about `6.1%`
- `__memmove_avx_unaligned_erms`: about `4.0%`

## Interpretation

This round produced an interesting but ultimately negative result.

What improved:

- `lbm_comm_halo_exchange(...)` did appear to shrink in the sampled hotspot view
  from about `9.2%` to about `7.7%`

What did not improve:

- the actual standalone `20000`-iteration benchmark got worse

This means the change likely traded one small fixed overhead for other costs that were at least as important:

- worse cache behavior
- less favorable code generation
- or more time exposed elsewhere after the communication slice moved slightly

In other words, the idea was reasonable, but in this implementation it did not beat the simpler `optimism17` version.

## Final Decision

This optimization was **not kept**.

After collecting the data above, the communication code was reverted to the previous `optimism17` implementation so that the working tree stays on the faster baseline.

The retained code therefore remains:

- timestep cleanup from `optimism17`
- not the reusable halo-buffer attempt from this round

## Conclusion

The fresh `optimism17` perf pass was still useful, because it showed that communication had become visible enough to justify inspection. But the first concrete attempt on that target did not pay off in end-to-end performance.

So the practical outcome of this round is:

1. communication buffer reuse was a plausible idea
2. this specific implementation was a negative optimization
3. the codebase was returned to the `optimism17` baseline after measurement
