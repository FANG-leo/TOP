# Parallel Baseline Experiment Start: MPI/OpenMP Scaling Sweep

## Goal

Start a parallel baseline experiment to understand whether the next optimization effort should focus on:

- OpenMP threading structure
- MPI communication behavior
- or MPI + OpenMP interaction

The planned test matrix was:

- `MPI np = 1, 2, 4`
- `OMP_NUM_THREADS = 1, 2, 4, 8`

using a common `20000`-iteration baseline config with output disabled so the results reflect compute and parallel overhead more directly.

## Benchmark Config

Added:

- `config.parallel-baseline-20000.txt`

with:

- `iterations = 20000`
- `output_filename = none`
- `show_progress = 0`

## Completed Results

The full matrix did not complete, but the single-rank OpenMP sweep finished successfully:

| MPI ranks | OMP threads | Time | FOM |
| ---: | ---: | ---: | ---: |
| `1` | `1` | `22.89 s` | `113.53 MLUPS` |
| `1` | `2` | `24.68 s` | `105.28 MLUPS` |
| `1` | `4` | `24.40 s` | `106.49 MLUPS` |
| `1` | `8` | `26.50 s` | `97.93 MLUPS` |

## Immediate Interpretation

For the current single-rank path:

- `OMP_NUM_THREADS=1` was the best result
- increasing OpenMP threads reduced performance
- `2`, `4`, and `8` threads all underperformed the single-thread baseline

This is consistent with the earlier observation that the current kernel structure does not benefit from naive OpenMP parallelization on this machine and problem size.

## Blocker Found

The experiment then moved on to:

- `np = 2`
- `omp = 1`

and the run did not complete. The two-rank job blocked for several minutes and had to be stopped manually.

At the point of interruption:

- both `top.lbm-exe` ranks were still alive
- the `mpirun -np 2` launcher was still active
- the test had not produced a FOM line

## Meaning of This Result

This means the next parallel optimization step is not just "tune OpenMP more."

Before a full MPI/OpenMP scaling study can proceed, the multi-rank execution path itself needs investigation. The current evidence suggests there is likely a blocking or synchronization problem in the `np > 1` path, which could involve:

- halo exchange ordering
- MPI send/recv pairing
- a barrier/communication interaction
- or MPI + current OpenMP behavior when moving beyond the single-rank case

## Conclusion

This baseline experiment already produced two useful outcomes:

1. On `np = 1`, OpenMP scaling is negative across `2/4/8` threads.
2. On `np = 2`, the program appears to block before completing the benchmark.

So the next parallel work should probably shift from "performance tuning" to "multi-rank correctness and progress debugging" before continuing the full scaling matrix.
