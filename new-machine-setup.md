# New Machine Setup Guide

## Goal

This guide explains how to check a new machine, configure the required environment, and run this project successfully.

It is written for two cases:

1. **CPU-only run**
2. **GPU/offload run** using the project's OpenMP target path

The safest workflow is:

1. make the CPU version build and run first
2. then enable GPU/offload
3. finally verify correctness and performance

## 1. What This Project Needs

At minimum, a new machine should have:

- CMake `>= 3.25`
- a working C++ compiler
- MPI
- OpenMP

For visualization and validation helpers, it may also need:

- Python `>= 3.10`
- `uv`
- `gnuplot`

The main project build instructions already live in [README.md](/home/h/topnew/TOP-26/project/README.md:1).

## 2. Quick Environment Check

Run these commands first:

```bash
cmake --version
c++ --version
mpirun --version
python3 --version
```

Then check whether the compiler can see OpenMP:

```bash
echo | c++ -fopenmp -dM -E - | head
```

If this fails, OpenMP is not configured correctly for that compiler.

## 3. CPU Path: First Bring-Up

Always start with the CPU path, because it has the fewest moving parts.

### Configure

```bash
cmake -S . -B build-cpu -DTOP_LBM_USE_OMP_TARGET=OFF
```

### Build

```bash
cmake --build build-cpu -j -t top.lbm-exe
```

### Smoke Test

Single-rank:

```bash
OMP_NUM_THREADS=1 ./build-cpu/top.lbm-exe config.gpu-smoke.txt
```

Two-rank:

```bash
mpirun -np 2 env OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
  ./build-cpu/top.lbm-exe config.gpu-smoke.txt
```

If these succeed, the machine is already able to run the project on CPU.

## 4. GPU/Offload Path: What Is Different

This project does **not** use a CUDA-specific kernel path.  
The GPU path is implemented with the CMake switch:

- `TOP_LBM_USE_OMP_TARGET`

defined in [CMakeLists.txt](/home/h/topnew/TOP-26/project/CMakeLists.txt:15).

That means GPU execution depends on:

- compiler support for OpenMP target offload
- a working OpenMP offload runtime
- a visible target device on that machine

So a machine with a GPU may still fail to run this path if its OpenMP offload toolchain is missing or incompatible.

## 5. GPU/Offload Environment Check

### Step 1: Configure the offload build

```bash
cmake -S . -B build-gpu -DTOP_LBM_USE_OMP_TARGET=ON
```

### Step 2: Build it

```bash
cmake --build build-gpu -j -t top.lbm-exe
```

### Step 3: Confirm the option is really enabled

```bash
rg -n "TOP_LBM_USE_OMP_TARGET" build-gpu/CMakeCache.txt build-gpu/CMakeFiles -S
```

You want to see:

```text
TOP_LBM_USE_OMP_TARGET:BOOL=ON
```

### Step 4: Check whether the runtime can use a device

Try this strict test:

```bash
OMP_TARGET_OFFLOAD=MANDATORY OMP_NUM_THREADS=1 \
  ./build-gpu/top.lbm-exe config.gpu-smoke.txt
```

Interpretation:

- if it runs successfully, the machine can use the offload path
- if it fails immediately, the machine probably has no usable OpenMP target device or no working offload runtime

## 6. Recommended Run Matrix

Once the project builds, use this order.

### A. CPU baseline

```bash
OMP_NUM_THREADS=1 ./build-cpu/top.lbm-exe config.gpu-smoke.txt
```

### B. GPU build, but CPU fallback disabled

```bash
OMP_TARGET_OFFLOAD=MANDATORY OMP_NUM_THREADS=1 \
  ./build-gpu/top.lbm-exe config.gpu-smoke.txt
```

### C. GPU build with normal runtime behavior

```bash
OMP_TARGET_OFFLOAD=DEFAULT OMP_NUM_THREADS=1 \
  ./build-gpu/top.lbm-exe config.gpu-smoke.txt
```

### D. MPI check on the current best project baseline style

```bash
mpirun -np 2 env OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
  ./build-cpu/top.lbm-exe config.parallel-baseline-20000.txt
```

and, if GPU/offload works:

```bash
mpirun -np 2 env OMP_TARGET_OFFLOAD=MANDATORY OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
  ./build-gpu/top.lbm-exe config.parallel-baseline-20000.txt
```

## 7. How To Verify Correctness

For an output-producing smoke run:

CPU:

```bash
OMP_TARGET_OFFLOAD=DISABLED OMP_NUM_THREADS=1 \
  ./build-gpu/top.lbm-exe config.gpu-smoke-output.txt
mv gpu-smoke.raw gpu-smoke.cpu.raw
```

GPU:

```bash
OMP_TARGET_OFFLOAD=MANDATORY OMP_NUM_THREADS=1 \
  ./build-gpu/top.lbm-exe config.gpu-smoke-output.txt
mv gpu-smoke.raw gpu-smoke.gpu.raw
```

Compare them:

```bash
cmp gpu-smoke.cpu.raw gpu-smoke.gpu.raw
```

If `cmp` prints nothing and returns success, the files are identical.

You can also use the visualization helper from [README.md](/home/h/topnew/TOP-26/project/README.md:67):

```bash
lbm-viz --check ref_results.raw gpu-smoke.cpu.raw
lbm-viz --check ref_results.raw gpu-smoke.gpu.raw
```

## 8. How To Collect Basic Performance Numbers

For a simple CPU measurement:

```bash
OMP_NUM_THREADS=1 ./build-cpu/top.lbm-exe config.parallel-baseline-20000.txt
```

For the project's currently most relevant MPI configuration:

```bash
mpirun -np 2 env OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
  ./build-cpu/top.lbm-exe config.parallel-baseline-20000.txt
```

For GPU/offload comparison:

```bash
mpirun -np 2 env OMP_TARGET_OFFLOAD=MANDATORY OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores \
  ./build-gpu/top.lbm-exe config.parallel-baseline-20000.txt
```

The program prints:

- `FOM: ... MLUPS`

which is the main performance number used throughout this repository.

## 9. Common Failure Modes

### Build succeeds, but GPU run behaves like CPU

Possible causes:

- `TOP_LBM_USE_OMP_TARGET=OFF`
- `OMP_TARGET_OFFLOAD=DISABLED`
- the runtime silently fell back to the host

What to do:

- rebuild with `-DTOP_LBM_USE_OMP_TARGET=ON`
- rerun with `OMP_TARGET_OFFLOAD=MANDATORY`

### `MANDATORY` fails immediately

Possible causes:

- no OpenMP target device is available
- compiler supports `-fopenmp`, but not usable target offload on that machine
- driver/runtime mismatch

What to do:

- keep using the CPU path on that machine
- or fix the OpenMP offload toolchain before retrying

### `mpirun` fails even though single-rank works

Possible causes:

- MPI installation problem
- local launcher restrictions
- host firewall / interface / permissions issue

What to do:

- verify `mpirun --version`
- test a trivial `mpirun -np 2 hostname`
- confirm the MPI implementation is correctly installed

### GPU path runs, but is not faster

That is possible with this project. The current offload path still pays:

- host/device synchronization cost
- CPU-side MPI halo exchange cost
- runtime overhead from OpenMP offload

So success should be judged in this order:

1. builds correctly
2. runs correctly
3. matches CPU results
4. then compare performance

## 10. Recommended Checklist For A New Machine

Use this exact checklist:

1. `cmake --version`
2. `c++ --version`
3. `mpirun --version`
4. build CPU with `TOP_LBM_USE_OMP_TARGET=OFF`
5. run `config.gpu-smoke.txt` on CPU
6. run `mpirun -np 2` CPU smoke test
7. build GPU with `TOP_LBM_USE_OMP_TARGET=ON`
8. run `OMP_TARGET_OFFLOAD=MANDATORY` single-rank smoke test
9. compare CPU and GPU smoke outputs
10. run the `np=2`, `OMP_NUM_THREADS=1` benchmark if everything above passes

## Final Advice

On a new machine, do **not** start from the assumption that the GPU path will work automatically.

Treat CPU bring-up and GPU bring-up as two separate milestones:

1. CPU build and run
2. GPU/offload build and run

If CPU works but GPU `MANDATORY` fails, the machine is still usable for this project; it just is not yet ready for the experimental OpenMP offload path.
