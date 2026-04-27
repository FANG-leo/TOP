#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/perf-data"

CONFIG_FILE="config.baseline-1000.txt"
RUN_NAME="baseline-1000"
MPI_RANKS=1
OMP_THREADS=1

usage() {
  cat <<'EOF'
Usage:
  ./scripts/run_perf_profile.sh [options]

Options:
  -c, --config <file>      Config file to pass to top.lbm-exe.
                           Default: config.baseline-1000.txt
  -n, --np <count>         MPI rank count.
                           Default: 1
  -t, --omp <count>        OMP thread count.
                           Default: 1
  -r, --run-name <name>    Output folder name under perf-data/.
                           Default: baseline-1000
  -h, --help               Show this help message.

Examples:
  ./scripts/run_perf_profile.sh --config config.txt --np 2 --omp 1 --run-name v9-np2-omp1
  ./scripts/run_perf_profile.sh -c config.results9-20000.txt -n 1 -t 8 -r v9-omp8
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -c|--config)
      CONFIG_FILE="$2"
      shift 2
      ;;
    -n|--np)
      MPI_RANKS="$2"
      shift 2
      ;;
    -t|--omp)
      OMP_THREADS="$2"
      shift 2
      ;;
    -r|--run-name)
      RUN_NAME="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ "${CONFIG_FILE}" != /* ]]; then
  CONFIG_PATH="${ROOT_DIR}/${CONFIG_FILE}"
else
  CONFIG_PATH="${CONFIG_FILE}"
fi

if [[ ! -f "${CONFIG_PATH}" ]]; then
  echo "Config file not found: ${CONFIG_PATH}" >&2
  exit 1
fi

RUN_DIR="${OUT_DIR}/${RUN_NAME}"
mkdir -p "${RUN_DIR}"

META_FILE="${RUN_DIR}/meta.txt"
cat > "${META_FILE}" <<EOF
run_name=${RUN_NAME}
config=${CONFIG_PATH}
mpi_ranks=${MPI_RANKS}
omp_threads=${OMP_THREADS}
started_at=$(date --iso-8601=seconds)
command=OMP_NUM_THREADS=${OMP_THREADS} mpirun -np ${MPI_RANKS} ./build-release/top.lbm-exe ${CONFIG_PATH}
EOF

echo "Run directory: ${RUN_DIR}"
echo "Config file: ${CONFIG_PATH}"
echo "MPI ranks: ${MPI_RANKS}"
echo "OMP threads: ${OMP_THREADS}"
echo "Metadata: ${META_FILE}"

MPI_RANKS="${MPI_RANKS}" \
OMP_THREADS="${OMP_THREADS}" \
  "${ROOT_DIR}/scripts/run_perf_suite.sh" "${CONFIG_PATH}" "${RUN_NAME}"
