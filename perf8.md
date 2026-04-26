# Perf Analysis Across MPI/OpenMP Baseline Configurations

## Goal

Collect a comparable `perf` snapshot across several parallel configurations after the halo-exchange rewrite, including:

- single-rank single-thread
- single-rank multithread
- two-rank single-thread
- two-rank multithread

The intent is to see whether the current limitations come more from:

- OpenMP scaling behavior
- MPI communication costs
- or MPI + OpenMP interaction

## Test Configurations

`perf stat` was collected with:

- build: `Release`
- config: `config.parallel-baseline-20000.txt`
- iterations: `20000`
- output disabled
- OpenMP binding: `OMP_PROC_BIND=close OMP_PLACES=cores`

Measured configurations:

1. `np=1`, `OMP_NUM_THREADS=1`
2. `np=1`, `OMP_NUM_THREADS=8`
3. `np=2`, `OMP_NUM_THREADS=1`
4. `np=2`, `OMP_NUM_THREADS=4`

`perf record/report` hotspot sampling was then collected on the same code using:

- config: `config.baseline-1000.txt`

to keep hotspot collection short while preserving the same mesh shape and execution structure.

## Perf Stat Results

| MPI ranks | OMP threads | Elapsed time | FOM | task-clock (ms) | cycles | instructions | IPC | L1 loads | L1 misses | L1 miss rate |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `1` | `1` | `24.386 s` | `106.49 MLUPS` | `24199.08` | `93,350,424,506` | `138,886,835,057` | `1.49` | `62,009,311,141` | `12,522,273,115` | `20.19%` |
| `1` | `8` | `30.849 s` | `83.94 MLUPS` | `54551.29` | `202,447,030,941` | `157,203,974,804` | `0.78` | `68,769,671,521` | `14,406,958,885` | `20.95%` |
| `2` | `1` | `18.561 s` | `280.96 MLUPS` | `36093.70` | `142,558,703,907` | `153,846,807,402` | `1.08` | `68,314,154,352` | `14,098,725,377` | `20.64%` |
| `2` | `4` | `23.961 s` | `216.92 MLUPS` | `83359.14` | `314,398,200,509` | `170,442,318,333` | `0.54` | `75,940,619,802` | `15,063,852,395` | `19.84%` |

## Immediate Interpretation

1. `np=1, omp=1` still beats `np=1, omp=8`.
   The multithreaded single-rank version takes longer, has much lower IPC, and slightly higher L1 miss pressure.

2. `np=2, omp=1` is currently the strongest configuration among the tested set.
   After the halo-exchange rewrite, the two-rank single-thread run clearly outperforms both single-rank cases and the `np=2, omp=4` hybrid case.

3. Adding OpenMP on top of `np=2` hurts again.
   `np=2, omp=4` has the lowest IPC of all tested configurations and much higher aggregate task-clock, which suggests the code is paying more synchronization and memory-system overhead than it gains from extra threads.

4. L1 miss rates stay in roughly the same band.
   The biggest visible signal is not a huge L1 miss explosion, but the strong IPC drop once more threads are involved.

## Perf Record / Report Highlights

### `np=1`, `omp=1`

Top resolved project hotspots:

- `collision(Mesh*, Mesh const*) [clone ._omp_fn.0]`: about `45%` self
- `propagation(Mesh*, Mesh const*) [clone ._omp_fn.0]`: about `36%` child share under `GOMP_parallel`
- `__memmove_avx_unaligned_erms`: visible from the `f0` plane copy path

Interpretation:

- even in the nominal single-thread run, the current code still enters OpenMP runtime paths
- collision and propagation remain the two dominant compute kernels

### `np=1`, `omp=8`

Top resolved project hotspots:

- `propagation(Mesh*, Mesh const*) [clone ._omp_fn.0]`
- `collision(Mesh*, Mesh const*) [clone ._omp_fn.0]`
- a large amount of time under thread-start/runtime paths such as `clone3` / worker-thread stacks

Interpretation:

- much more time is now showing up in thread/runtime context
- both major kernels remain dominant, but the lower IPC indicates that threading overhead and memory contention are pulling efficiency down

### `np=2`, `omp=1`

Top resolved project hotspots:

- `collision(Mesh*, Mesh const*) [clone ._omp_fn.0]`: about `46%` self
- `propagation(Mesh*, Mesh const*) [clone ._omp_fn.0]`: about `34%`
- `PMPI_Barrier`: visible but relatively small
- `lbm_comm_halo_exchange(...)`: visible around `1-2%` self/children in the sampled report

Interpretation:

- after the rewrite, halo exchange is no longer the dominant blocker
- the main cost center is back where we want it: the numerical kernels
- MPI communication is now visible but secondary in this two-rank run

### `np=2`, `omp=4`

Top resolved project hotspots:

- `propagation(Mesh*, Mesh const*) [clone ._omp_fn.0]`
- `collision(Mesh*, Mesh const*) [clone ._omp_fn.0]`
- substantial time again under thread-start/runtime stacks such as `clone3`

Interpretation:

- hybrid MPI+OpenMP currently compounds the same OpenMP inefficiency seen in the single-rank threaded runs
- the program does not appear compute-starved; instead it looks more limited by synchronization/runtime overhead and reduced per-thread efficiency

## Main Takeaways

1. The halo-exchange rewrite succeeded in making `np=2` practical and profileable.
2. The best tested configuration in this batch is `np=2`, `omp=1`.
3. OpenMP still degrades performance both at `np=1` and at `np=2`.
4. The current next-step bottleneck is not obviously MPI communication anymore, but rather poor OpenMP scaling on top of otherwise efficient collision/propagation kernels.

## Conclusion

This `perf` pass suggests that the next parallel optimization priority should be:

1. treat `np=2, omp=1` as the current best parallel baseline
2. investigate why the OpenMP versions lose so much IPC
3. only after that, continue expanding the MPI/OpenMP scaling matrix further
