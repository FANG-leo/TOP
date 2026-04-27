#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROFILE_SCRIPT="${ROOT_DIR}/scripts/run_perf_profile.sh"

CONFIG_FILE="config.txt"
RUN_PREFIX="v12"
MAX_PRODUCT=16
VALUES=(1 2 4 8 16)

usage() {
  cat <<'EOF'
Usage:
  ./scripts/run_perf_matrix.sh [options]

Options:
  -c, --config <file>       Config file to pass to top.lbm-exe.
                            Default: config.txt
  -p, --prefix <name>       Run-name prefix.
                            Default: v12
  -m, --max-product <num>   Only run combinations where np * omp <= num.
                            Default: 16
  -h, --help                Show this help message.

Examples:
  ./scripts/run_perf_matrix.sh
  ./scripts/run_perf_matrix.sh --config config.txt --prefix v12
  ./scripts/run_perf_matrix.sh --config config.txt --prefix test --max-product 16
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -c|--config)
      CONFIG_FILE="$2"
      shift 2
      ;;
    -p|--prefix)
      RUN_PREFIX="$2"
      shift 2
      ;;
    -m|--max-product)
      MAX_PRODUCT="$2"
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

if [[ ! -x "${PROFILE_SCRIPT}" ]]; then
  echo "Profile script not found or not executable: ${PROFILE_SCRIPT}" >&2
  exit 1
fi

echo "Config file: ${CONFIG_FILE}"
echo "Run prefix: ${RUN_PREFIX}"
echo "Max np*omp: ${MAX_PRODUCT}"
echo

for np in "${VALUES[@]}"; do
  for omp in "${VALUES[@]}"; do
    if (( np * omp > MAX_PRODUCT )); then
      continue
    fi

    run_name="${RUN_PREFIX}-np${np}-omp${omp}"
    echo "=== Running ${run_name} ==="
    "${PROFILE_SCRIPT}" \
      --config "${CONFIG_FILE}" \
      --np "${np}" \
      --omp "${omp}" \
      --run-name "${run_name}"
    echo
  done
done
