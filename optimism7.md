# `-march=native` Optimization for the Current SoA Build

## Goal

Keep the current SoA layout and hotspot structure unchanged, then let the compiler target the local CPU more aggressively so the existing SIMD-friendly loops can use wider vector instructions.

## What Was Changed

Added `-march=native` to the current release build targets:

- `src/lbm/CMakeLists.txt`
- `src/bin/CMakeLists.txt`

After reconfiguring and rebuilding, the active library flags in `build-release` became:

```text
-O3 -DNDEBUG -fPIC -march=native -fopenmp
```

## Why This Helps

The latest version had already done most of the structural work needed for vectorization:

- SoA layout for contiguous per-direction planes
- `restrict` on the hotspot path
- `#pragma omp simd` on the inner collision sweep

With that foundation in place, `-march=native` lets the compiler emit instructions tuned for the local CPU instead of staying on a conservative generic target.

## Vectorization Report

Rechecking the compiler vectorization report after enabling `-march=native` shows the key loops widened from 128-bit style vectors to 256-bit style vectors:

- `src/lbm/physics.cpp:278`
  `collision()` inner loop is now reported as:

```text
loop vectorized using 32 byte vectors
```

- `src/lbm/physics.cpp:342`
  `propagation()` main contiguous loop is also reported as:

```text
loop vectorized using 32 byte vectors
```

This is the important outcome of this round: the existing SoA kernels are now using wider SIMD on this machine.

## Rebuild

The project was rebuilt in `build-release` after updating the CMake target options.

## 20000-Iteration Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results6-20000.txt`
- iterations: `20000`
- MPI ranks: `1`
- output file: `results6.raw`

Observed result from the non-`perf` benchmark run:

| Metric | Value |
| --- | ---: |
| Elapsed time | `28.67 s` |
| FOM | `91.17 MLUPS` |

## Summary

This optimization did not change the algorithm or data layout. It improved performance by letting the compiler fully exploit the local CPU's wider SIMD support on the already-optimized SoA collision and propagation loops.
