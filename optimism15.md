# `np=2`, `omp=1` Collision Optimization

## Goal

Use a dedicated `np = 2`, `OMP_NUM_THREADS = 1` performance pass to decide whether the next optimization target should be `collision()` or `propagation()`, then implement one focused `collision()` optimization round on top of the current `optimism13`-style MPI baseline.

## Dedicated Perf Analysis Process

The decision pass was collected on the current two-rank synchronous-halo version with:

- build: `Release`
- MPI ranks: `2`
- OpenMP: `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`
- hotspot config: `config.baseline-1000.txt`
- full-stat config: `config.parallel-baseline-20000.txt`

### `perf record/report`

Collected with:

```bash
OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
perf record -F 199 -g -o /tmp/perf-np2-omp1.data -- \
  mpirun -np 2 ./build-release/top.lbm-exe config.baseline-1000.txt
```

and inspected with:

```bash
perf report --stdio -i /tmp/perf-np2-omp1.data
```

The important hotspots were:

- `collision(...)`: about `46.6%` self, about `47%` total
- `propagation(...)`: about `33.8%` self, about `34%` total
- `PMPI_Barrier`: about `8.4%`
- `special_cells(...)`: about `2.8%`
- `lbm_comm_halo_exchange(...)`: about `1%`

This made the next target clear: communication was no longer the main issue, and `collision()` was now the single largest cost center.

### Pre-Optimization `perf stat`

Collected with:

```bash
OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
perf stat -x, -e task-clock,cycles,instructions,L1-dcache-loads,L1-dcache-load-misses \
  mpirun -np 2 ./build-release/top.lbm-exe config.parallel-baseline-20000.txt
```

Observed result before the new `collision()` change:

| Metric | Value |
| --- | ---: |
| FOM | `246.87 MLUPS` |
| task-clock | `41566.12 ms` |
| cycles | `164,732,308,433` |
| instructions | `158,987,410,083` |
| IPC | `0.97` |
| L1 loads | `70,687,762,660` |
| L1 load misses | `14,110,853,600` |
| L1 miss rate | `19.96%` |

## Optimization Idea

The current best parallel configuration is still effectively single-threaded inside each rank. In that setup, the old `collision()` implementation still entered an OpenMP `parallel for` region every timestep even when only one OpenMP thread was active.

That suggested a low-risk optimization:

1. keep the existing multi-threaded path available for future experiments
2. add a real serial fast path for `OMP_NUM_THREADS = 1`
3. simplify the hot interior loop addressing by switching from repeated `idx` formation to per-column base pointers

## Optimization Attempts

### Attempt 1: `mx/my` Arithmetic Rewrite

The first draft combined the serial fast path with a more aggressive arithmetic rewrite that expanded the loop around raw momenta (`mx`, `my`) and `inv_density^2`.

This version looked fine on a short smoke test:

- `np = 2`
- `OMP_NUM_THREADS = 1`
- `config.baseline-1000.txt`
- observed FOM: about `270.88 MLUPS`

But it collapsed badly on the full `20000`-iteration benchmark:

- observed FOM: about `82 MLUPS`

That strongly suggested a numerical-stability or long-run correctness problem, so this arithmetic rewrite was discarded immediately.

### Attempt 2: Keep Only the Safe Structural Part

The final retained version keeps the safer part of the idea:

- preserve the original `vx` / `vy` collision formula
- keep the SIMD-friendly SoA layout
- avoid entering the OpenMP runtime when `omp_get_max_threads() <= 1`
- use per-column plane pointers inside the interior loop

The implementation lives in [src/lbm/physics.cpp](/home/h/topnew/TOP-26/project/src/lbm/physics.cpp:247).

## Final Code Change

The final `collision()` structure now works like this:

1. build a local `collide_columns(...)` helper around the interior column sweep
2. if `omp_get_max_threads() <= 1`, execute the full interior range directly
3. otherwise keep the existing outer `#pragma omp parallel for` path

This targets the exact configuration that `perf8.md` identified as the current best one: `np = 2`, `omp = 1`.

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
| FOM | `269.79 MLUPS` |

### Full 20000-Iteration Benchmark

Measured with:

- `mpirun -np 2`
- `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`
- `config.parallel-baseline-20000.txt`

Observed result:

| Metric | Value |
| --- | ---: |
| FOM | `269.57 MLUPS` |

For reference, the earlier two-rank synchronous-halo baseline in [optimism13.md](/home/h/topnew/TOP-26/project/optimism13.md:1) reported:

- `256.85 MLUPS` on the full `20000`-iteration run with output enabled to `results11.raw`

So this round improved the current two-rank, one-thread execution path while leaving the MPI communication structure unchanged.

## Post-Optimization `perf stat`

Collected again with the same `perf stat` command used above.

Observed result after the retained `collision()` change:

| Metric | Value |
| --- | ---: |
| FOM | `278.45 MLUPS` |
| task-clock | `36877.93 ms` |
| cycles | `145,874,927,642` |
| instructions | `163,284,328,589` |
| IPC | `1.12` |
| L1 loads | `85,248,626,056` |
| L1 load misses | `14,159,886,814` |
| L1 miss rate | `16.61%` |

## Interpretation

Three things stand out:

1. The hotspot-based decision was correct.
   `collision()` was the largest remaining kernel in the dedicated `np = 2`, `omp = 1` profile, and it was the right place to spend the next optimization attempt.

2. The safe structural change helped, while the aggressive arithmetic rewrite did not.
   This is a useful reminder that not every operation-count reduction is numerically safe in this solver.

3. The retained change improved the execution profile.
   The post-change `perf stat` run shows:

- higher FOM
- lower task-clock
- higher IPC
- lower L1 miss rate

## Conclusion

This round produced a usable positive optimization for the current best parallel configuration.

The key lesson is that the best next step was not another communication rewrite or another OpenMP scaling attempt, but a `collision()`-specific cleanup tailored to the actual `np = 2`, `omp = 1` execution mode. The next optimization target after this should still be `propagation()`, but only after treating this `collision()` result as the new two-rank baseline.
