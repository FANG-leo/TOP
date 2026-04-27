# GPU Offload Optimization Summary

## Goal

This round was a GPU-oriented optimization attempt built around **OpenMP target offload**, not a CUDA rewrite.  
The purpose was to keep the existing MPI + OpenMP solver structure, move the main per-step compute kernels onto a device when available, and preserve correctness across the host/device boundary.

The implementation landed in commit `v13` and is guarded by the new CMake option:

- `TOP_LBM_USE_OMP_TARGET` in [CMakeLists.txt](/home/h/topnew/TOP-26/project/CMakeLists.txt:15)

## Optimization Process

### 1. Add an optional GPU/offload build path

The first step was to make offload an **opt-in experimental path** rather than rewriting the default code path:

- `TOP_LBM_USE_OMP_TARGET` was added as a CMake option in [CMakeLists.txt](/home/h/topnew/TOP-26/project/CMakeLists.txt:15)
- the macro is propagated to the solver targets in [src/lbm/CMakeLists.txt](/home/h/topnew/TOP-26/project/src/lbm/CMakeLists.txt:23)

That keeps the CPU implementation as the safe baseline while allowing a separate offload-enabled build.

### 2. Extend mesh data structures with device-resident storage

To run kernels on a device, the code first needed a second memory space:

- `Mesh::device_cells`
- `Mesh::halo_row_buffer`
- `Mesh::device_halo_row_buffer`
- `lbm_mesh_type_t::device_types`

These were added in [include/lbm/structures.hpp](/home/h/topnew/TOP-26/project/include/lbm/structures.hpp:16).

Then [src/lbm/structures.cpp](/home/h/topnew/TOP-26/project/src/lbm/structures.cpp:115) implemented:

- device allocation with `omp_target_alloc(...)`
- device release with `omp_target_free(...)`
- full mesh synchronization:
  - `Mesh_sync_host_to_device(...)`
  - `Mesh_sync_device_to_host(...)`
- halo-only synchronization:
  - `Mesh_sync_halo_send_device_to_host(...)`
  - `Mesh_sync_halo_recv_host_to_device(...)`

This was the key enabling step: without explicit host/device data ownership, the MPI-based solver could not safely mix GPU compute with CPU-side communication.

### 3. Offload the compute kernels that dominate each timestep

After data management existed, the main timestep kernels were given OpenMP target implementations in [src/lbm/physics.cpp](/home/h/topnew/TOP-26/project/src/lbm/physics.cpp:42):

- `special_cells(...)`
- `collision(...)`
- `propagation(...)`

The offload path is runtime-guarded by:

- `omp_get_num_devices() > 0`
- `Mesh_has_device_data(...)`
- `lbm_mesh_type_has_device_data(...)`

and then uses:

- `#pragma omp target teams distribute parallel for`
- `#pragma omp target teams distribute parallel for collapse(2)`

The important design choice here was **not** to invent a separate GPU-only algorithm. The GPU path reuses the same solver phases and the same numerical formulas, which reduces the risk of a CPU/GPU divergence.

### 4. Bridge GPU compute with CPU MPI halo exchange

Because halo exchange still happens with MPI on the host, the offload work also had to solve the data handoff around communication.

The timestep loop in [src/bin/main.cpp](/home/h/topnew/TOP-26/project/src/bin/main.cpp:106) now does:

1. initial host-to-device upload after setup
2. `special_cells(...)` on device when available
3. `collision(...)` on device when available
4. `Mesh_sync_halo_send_device_to_host(...)`
5. `lbm_comm_halo_exchange(...)` on host
6. `Mesh_sync_halo_recv_host_to_device(...)`
7. `propagation(...)` on device when available
8. `Mesh_sync_device_to_host(...)` before frame output

This is the real substance of the GPU optimization effort. The hard part was not only offloading arithmetic kernels, but stitching them back into an MPI code that still exchanges ghost zones on the CPU side.

### 5. Keep a safe fallback path

The offload implementation was written as an extension, not a replacement:

- if no OpenMP target device exists, the solver stays on the CPU path
- the optimized CPU paths from earlier rounds remain intact

That makes the GPU work low-risk from a correctness and maintainability perspective.

## Results

### Confirmed functional results

The strongest validated result in the repository is **correctness parity** for the smoke test:

- [config.gpu-smoke.txt](/home/h/topnew/TOP-26/project/config.gpu-smoke.txt:1)
- [config.gpu-smoke-output.txt](/home/h/topnew/TOP-26/project/config.gpu-smoke-output.txt:1)
- `gpu-smoke.cpu.raw`
- `gpu-smoke.gpu.raw`

I verified that:

- `gpu-smoke.cpu.raw` and `gpu-smoke.gpu.raw` are byte-identical

So the GPU/offload path successfully reproduced the CPU result for the checked smoke case.

The current build tree also shows the experimental path is enabled in the release configuration:

- `TOP_LBM_USE_OMP_TARGET:BOOL=ON` in [build-release/CMakeCache.txt](/home/h/topnew/TOP-26/project/build-release/CMakeCache.txt:276)

### Performance result that can be stated safely

From the repository contents, the GPU work clearly achieved:

1. a buildable OpenMP-target offload path
2. device-aware mesh/type allocation and synchronization
3. kernel offload for `special_cells`, `collision`, and `propagation`
4. correct host/device halo handoff around MPI exchange
5. smoke-test output parity with the CPU path

### Performance result that should **not** be overstated

I did **not** find a dedicated in-repo benchmark report proving that the GPU/offload path beats the retained CPU baseline end-to-end.

In contrast, the CPU/MPI optimization path **does** have measured retained results, for example:

- [optimism17.md](/home/h/topnew/TOP-26/project/optimism17.md:101) reports `286.48 MLUPS` for the current best `np = 2`, `omp = 1` no-output baseline

So the honest summary is:

- the GPU work successfully established a correct experimental offload pipeline
- the repository does not yet show a final measured GPU speedup that clearly surpasses the best retained CPU path

## Final Takeaway

This GPU optimization round was successful as an **infrastructure and correctness milestone**.

What was achieved:

- the solver can now keep mesh data on a device
- the major timestep kernels have offload implementations
- MPI halo exchange is integrated with host/device synchronization
- CPU and GPU smoke outputs match

What remains open:

- a clean, reproducible end-to-end GPU performance study
- a comparison against the best retained CPU baseline
- further reduction of host/device synchronization overhead, which is likely the next main bottleneck

In short, this round did not merely “add a pragma”; it converted the solver into a hybrid codebase that can execute its core compute phases on a GPU-capable OpenMP target while preserving the existing MPI structure and numerical behavior.
