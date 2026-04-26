# Why OpenMP on Both `collision()` and `propagation()` Still Lost to the Serial Best

## Goal

Explain why the combined OpenMP experiment from `optimism9.md` still performed worse than the current best serial version, even after parallelizing both major timestep kernels.

## Context

The previous OpenMP attempt in `wuxiao3.md` parallelized only:

- `collision(Mesh*, Mesh const*)`

and did not improve end-to-end runtime.

So the next experiment extended OpenMP to both:

- `collision(Mesh*, Mesh const*)`
- `propagation(Mesh*, Mesh const*)`

The expectation was that covering both dominant kernels would increase the parallel fraction enough to produce a net gain.

## Measured Result

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results9-20000.txt`
- iterations: `20000`
- output file: `results9.raw`
- MPI ranks: `1`
- OpenMP: `OMP_NUM_THREADS=8 OMP_PROC_BIND=close OMP_PLACES=cores`

Observed comparison:

| Version | Time | FOM |
| --- | ---: | ---: |
| Serial `results7` best | `23.70 s` | `109.62 MLUPS` |
| OpenMP `results9` combined-kernel run | `28.44 s` | `91.56 MLUPS` |

So the combined OpenMP version remained clearly slower than the best serial kernel version.

## Main Reasons

1. The code now pays OpenMP overhead in more than one hot phase per timestep.

   `collision()` already uses:

```cpp
#pragma omp parallel for schedule(static)
```

   and `propagation()` now also enters its own OpenMP parallel region.

   Since the benchmark runs for `20000` iterations, thread-team setup, teardown, worksharing, and synchronization costs are repeated many times and become visible in total runtime.

2. `propagation()` is strongly memory-traffic dominated.

   The propagation kernel mostly moves values between SoA planes with relatively little arithmetic per byte transferred.

   That means multithreading does not automatically translate into speedup:

- multiple threads compete for memory bandwidth
- cache pressure rises
- the serial SIMD version already uses a fairly efficient access pattern

   So the kernel can hit memory-system limits before it gets enough benefit from extra cores.

3. The new `propagation()` OpenMP structure still contains synchronization points.

   Inside the threaded propagation path, the work is split into:

- one workshared interior loop
- a `single` border section
- another workshared edge loop
- another `single` corner section

   These phases are safe, but they also introduce barriers and waiting between chunks of work. On a modest grid size like `800 x 160`, the synchronization cost is not negligible.

4. The full timestep is still not fully parallel.

   Even after threading both major kernels, the timestep still contains other work such as:

- `special_cells()`
- halo-exchange-related control flow
- output activity
- remaining serial orchestration

   So the total speedup is bounded by the serial fraction of the timestep, not just by the speedup of the two largest kernels.

5. The serial baseline is already unusually strong.

   Before the OpenMP attempt, the code had already accumulated:

- SoA layout
- `restrict`
- explicit SIMD hints
- `-march=native`
- propagation main-path cleanup

   That pushed the serial version to `109.62 MLUPS`, which means there is much less easy headroom left for a naive threaded speedup. In this regime, OpenMP overhead can outweigh the remaining parallel benefit.

## Interpretation

The negative result from `optimism9.md` suggests that the earlier explanation from `wuxiao3.md` was only part of the story.

It was true that parallelizing only `collision()` was too narrow, but adding `propagation()` as well still did not solve the core issue. The limiting factor is now more likely a combination of:

- OpenMP runtime overhead
- synchronization cost
- memory-bandwidth contention
- and the already high quality of the serial kernel implementation

## Conclusion

This experiment is best understood as a second useful negative result:

- the slowdown is not simply because `propagation()` had been left serial before
- the current problem size and kernel structure still do not make this OpenMP style profitable
- "parallelize both major loops" is not sufficient on its own to beat the current serial best

## Recommendation

If OpenMP work continues, the next direction should probably be one of these:

1. Move from multiple small per-kernel parallel regions toward a larger persistent parallel region across the timestep.
2. Reduce synchronization inside the threaded propagation path.
3. Re-test on a larger problem size where thread overhead is easier to amortize.
4. Pause OpenMP work and continue with single-core structural kernel optimization instead.
