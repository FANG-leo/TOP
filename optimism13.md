# MPI Halo-Exchange Rewrite and Multi-Rank Recovery

## Goal

Fix the stalled `np > 1` execution path so that multi-rank runs can complete again, then re-measure a full `20000`-iteration benchmark on the repaired MPI version.

## Problem in the Previous Version

The parallel baseline experiment revealed that:

- `np = 1` runs completed normally
- `np = 2` runs did not finish and appeared to hang

Inspection of the communication path showed two major issues in `lbm_comm_halo_exchange(...)`:

1. A custom `MPI_Syncall(...)` call at the end of halo exchange resolved to a generated sync stub that effectively inserted a one-second sleep on the multi-rank path.
2. Halo exchange itself used many small blocking `MPI_Send` / `MPI_Recv` operations separated by repeated `MPI_Barrier(...)` calls.

That combination made the multi-rank path extremely fragile and much slower than it needed to be.

## Optimization Idea

Replace the fragmented halo exchange with a simpler and safer bulk-exchange scheme:

- pack each boundary into a contiguous buffer
- exchange opposite faces with `MPI_Sendrecv(...)`
- exchange each corner with a matching `MPI_Sendrecv(...)`
- remove the custom `MPI_Syncall(...)` path entirely

This keeps the same halo semantics while removing the artificial per-step sleep and greatly reducing synchronization overhead.

## Changes Made

1. Removed the generated sync-wrapper path from `src/lbm/communications.cpp`.

   The old `MPI_Syncall(...)` tail call is no longer used, so the multi-rank path no longer pays the hidden one-second delay per timestep.

2. Replaced fragmented horizontal / vertical / diagonal ghost exchange helpers with bulk packing helpers.

   Added internal packing/unpacking helpers for:

- boundary columns
- boundary rows
- corner cells

3. Rewrote `lbm_comm_halo_exchange(...)` around `MPI_Sendrecv(...)`.

   The new exchange pattern now performs:

- left ghost exchange
- right ghost exchange
- top ghost exchange
- bottom ghost exchange
- four corner exchanges

   without the long sequence of blocking pointwise sends plus global barriers.

4. Added a dedicated full benchmark config:

- `config.results11-20000.txt`

   with output written to:

- `results11.raw`

## Why This Helps

- Removes the effective per-step sleep from the multi-rank path
- Reduces synchronization overhead
- Replaces many tiny blocking MPI messages with larger bulk transfers
- Makes the two-rank path progress normally again

## Files Changed

- `src/lbm/communications.cpp`
- `config.results11-20000.txt`

## Validation

A short smoke test with:

- `mpirun -np 2`
- `OMP_NUM_THREADS=1`
- `config.baseline-1000.txt`

completed successfully after the rewrite, confirming that the previous multi-rank stall was resolved.

Observed smoke-test result:

| Metric | Value |
| --- | ---: |
| Elapsed time | `1.29 s` |
| FOM | `269.84 MLUPS` |

## 20000-Iteration Benchmark

Measured on `2026-04-26` with:

- build: `Release`
- config: `config.results11-20000.txt`
- iterations: `20000`
- output file: `results11.raw`
- MPI ranks: `2`
- OpenMP: `OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores`

Observed result:

| Metric | Value |
| --- | ---: |
| Elapsed time | `20.27 s` |
| FOM | `256.85 MLUPS` |

## Conclusion

This round restored the usability of the multi-rank MPI path.

The important outcome is not only the measured `256.85 MLUPS`, but the fact that the `np = 2` run now completes reliably instead of stalling. That reopens the path for the larger MPI/OpenMP scaling study that had been blocked before.
