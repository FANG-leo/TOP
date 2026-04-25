#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-release"
OUT_DIR="${ROOT_DIR}/perf-data"
CONFIG_FILE="${1:-${ROOT_DIR}/config.baseline-1000.txt}"
RUN_NAME="${2:-baseline-1000}"
MPI_RANKS="${MPI_RANKS:-1}"
OMP_THREADS="${OMP_THREADS:-1}"

if [[ "${CONFIG_FILE}" != /* ]]; then
  CONFIG_FILE="${ROOT_DIR}/${CONFIG_FILE}"
fi

RUN_DIR="${OUT_DIR}/${RUN_NAME}"

mkdir -p "${RUN_DIR}"

STDOUT_LOG="${RUN_DIR}/stdout.log"
PERF_LOG="${RUN_DIR}/perf.csv"
PERF_DATA="${RUN_DIR}/perf.data"
HOTSPOT_LOG="${RUN_DIR}/hotspot.txt"
PER_THREAD_REPORT="${RUN_DIR}/per-thread.txt"
PER_CORE_REPORT="${RUN_DIR}/per-core.txt"

CMD="LD_LIBRARY_PATH='${BUILD_DIR}/lib' OMP_NUM_THREADS=${OMP_THREADS} mpirun -np ${MPI_RANKS} '${BUILD_DIR}/top.lbm-exe' '${CONFIG_FILE}'"

echo "Program output: ${STDOUT_LOG}"
echo "Overall counters: ${PERF_LOG}"
echo "Hotspot data: ${PERF_DATA}"
echo "Hotspot report: ${HOTSPOT_LOG}"
echo "Per-thread report: ${PER_THREAD_REPORT}"
echo "Per-core report: ${PER_CORE_REPORT}"

cd "${BUILD_DIR}"

perf stat -x, -o "${PERF_LOG}" \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses \
  bash -lc "${CMD}" \
  > "${STDOUT_LOG}" 2>&1

perf record -q -g --sample-cpu -o "${PERF_DATA}" \
  bash -lc "${CMD}" \
  >/dev/null 2>&1

perf report --stdio --input "${PERF_DATA}" --percent-limit 1 --sort symbol \
  > "${HOTSPOT_LOG}"

perf report --stdio --input "${PERF_DATA}" --percent-limit 1 --sort comm,pid,symbol \
  > "${PER_THREAD_REPORT}"

perf report --stdio --input "${PERF_DATA}" --percent-limit 1 --sort cpu,symbol \
  > "${PER_CORE_REPORT}"

python3 "${ROOT_DIR}/scripts/parse_perf_stat.py" "${STDOUT_LOG}" "${PERF_LOG}"
