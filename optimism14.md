# Non-Blocking Halo Exchange with Interior/Boundary Overlap

## Goal

Take the repaired two-rank MPI path from `optimism13.md` and push it one step further by overlapping communication with useful computation.

The idea was to replace the synchronous halo-exchange stage with a two-phase non-blocking version:

1. start halo transfers
2. compute the propagation region that does not depend on remote ghost cells
3. finish halo transfers
4. compute the remaining near-boundary and ghost-dependent propagation work

## Optimization Idea

The current two-rank path still performed communication as a distinct blocking stage before propagation.

This experiment tried to hide part of that latency by:

- splitting halo exchange into `begin` / `end`
- splitting propagation into:
  - a deep interior kernel
  - a boundary/strip completion kernel

This keeps the same overall algorithm while trying to overlap the communication window with interior propagation work.

## Changes Made

1. Extended the communication interface in `include/lbm/communications.hpp` with:

- `lbm_comm_halo_exchange_begin(...)`
- `lbm_comm_halo_exchange_end(...)`

2. Reworked `src/lbm/communications.cpp` so halo exchange can be posted non-blockingly.

   The new path uses:

- `MPI_Irecv(...)`
- `MPI_Isend(...)`
- `MPI_Waitall(...)`

   on the already packed face/corner buffers.

3. Split propagation in `include/lbm/physics.hpp` / `src/lbm/physics.cpp` into:

- `propagation_interior_core(...)`
- `propagation_finish_boundary(...)`

4. Updated the multi-rank timestep in `src/bin/main.cpp`.

   When `comm_size > 1`, the timestep now does:

- `collision(&temp, &mesh)`
- `lbm_comm_halo_exchange_begin(&mesh_comm, &temp)`
- copy `f0`
- `propagation_interior_core(&mesh, &temp)`
- `lbm_comm_halo_exchange_end(&mesh_comm, &temp)`
- `propagation_finish_boundary(&mesh, &temp)`

   The single-rank path still keeps the ordinary `propagation(...)` call.

5. Added a dedicated full benchmark config:

- `config.results12-20000.txt`

   with output written to:

- `results12.raw`

## Validation

Short smoke test:

- command shape: `mpirun -np 2`
- config: `config.baseline-1000.txt`
- OpenMP: `OMP_NUM_THREADS=1`

Observed smoke-test result:

| Metric | Value |
| --- | ---: |
| Elapsed time | `1.30 s` |
| FOM | `264.14 MLUPS` |

This confirmed that the overlap path runs correctly enough to complete a short two-rank benchmark.

## 20000-Iteration Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results12-20000.txt`
- iterations: `20000`
- output file: `results12.raw`
- MPI ranks: `2`
- OpenMP: `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`

Observed result:

| Metric | Value |
| --- | ---: |
| Elapsed time | `20.79 s` |
| FOM | `250.36 MLUPS` |

## Comparison

Compared with the previous synchronous bulk `Sendrecv` version from `optimism13.md`:

| Version | Time | FOM |
| --- | ---: | ---: |
| synchronous halo bulk exchange | `20.27 s` | `256.85 MLUPS` |
| non-blocking overlap attempt | `20.79 s` | `250.36 MLUPS` |

## Interpretation

This overlap attempt did not improve end-to-end performance.

The likely reason is that, on the current local problem size:

- the available interior work was not large enough to hide communication effectively
- splitting propagation added extra structural complexity and overhead
- the already repaired synchronous bulk-exchange version was efficient enough that the overlap opportunity was smaller than expected

## Conclusion

This is a useful negative result:

- the non-blocking overlap structure is now implemented and runnable
- but for the current `np=2`, `OMP=1`, `800x160` benchmark, it does not beat the simpler synchronous bulk halo-exchange version

So the best known two-rank baseline remains the `optimism13` version rather than this overlap attempt.
