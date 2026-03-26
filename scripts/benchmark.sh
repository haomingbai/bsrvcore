#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BSRVCORE_BENCH_BUILD_DIR:-build-bench}"
VENV_DIR="${BSRVCORE_BENCH_VENV:-.venv-benchmark}"
PROFILE="${BSRVCORE_BENCH_PROFILE:-quick}"
SCENARIO="${BSRVCORE_BENCH_SCENARIO:-all}"
# Keep one canonical benchmark report unless the caller opts into a custom tag.
TAG="${BSRVCORE_BENCH_TAG:-benchmark-report}"
PARALLELISM="${BSRVCORE_BUILD_PARALLEL:-$(nproc)}"
CPU_COUNT="$(nproc)"

if (( CPU_COUNT >= 16 )); then
  CLIENT_PROCESSES=4
elif (( CPU_COUNT >= 8 )); then
  CLIENT_PROCESSES=2
else
  CLIENT_PROCESSES=1
fi

if (( CPU_COUNT >= 24 )); then
  WRK_THREADS_PER_PROCESS=2
else
  WRK_THREADS_PER_PROCESS=1
fi

OUTPUT_DIR="${ROOT_DIR}/docs/benchmark-results"
JSON_OUT="${OUTPUT_DIR}/${TAG}.json"
MD_OUT="${OUTPUT_DIR}/${TAG}.md"
RPS_PNG="${OUTPUT_DIR}/${TAG}-rps.png"
LATENCY_PNG="${OUTPUT_DIR}/${TAG}-latency.png"
FAILURE_PNG="${OUTPUT_DIR}/${TAG}-failure.png"
BENCH_BIN="${ROOT_DIR}/${BUILD_DIR}/benchmarks/bsrvcore_http_benchmark"
WRK_BIN="${ROOT_DIR}/${BUILD_DIR}/_deps/bsrvcore_benchmark_wrk/src/bsrvcore_benchmark_wrk/wrk"
VENV_PYTHON="${ROOT_DIR}/${VENV_DIR}/bin/python"
MPLCONFIGDIR="${BSRVCORE_BENCH_MPLCONFIGDIR:-/tmp/bsrvcore-matplotlib}"

mkdir -p "${OUTPUT_DIR}"

BSRVCORE_BUILD_DIR="${BUILD_DIR}" \
BSRVCORE_BUILD_TYPE=Release \
BSRVCORE_BUILD_TESTS=OFF \
BSRVCORE_BUILD_EXAMPLES=OFF \
BSRVCORE_BUILD_BENCHMARKS=ON \
BSRVCORE_BENCHMARK_BUILD_BUNDLED_WRK=ON \
BSRVCORE_BUILD_PARALLEL="${PARALLELISM}" \
  "${ROOT_DIR}/scripts/build.sh" bsrvcore_http_benchmark

BENCH_CMD=(
  "${BENCH_BIN}"
  --scenario "${SCENARIO}"
  --profile "${PROFILE}"
  --client-processes "${CLIENT_PROCESSES}"
  --wrk-threads-per-process "${WRK_THREADS_PER_PROCESS}"
  --output-json "${JSON_OUT}"
)

if [[ -x "${WRK_BIN}" ]]; then
  BENCH_CMD+=(--wrk-bin "${WRK_BIN}")
fi

"${BENCH_CMD[@]}"

if [[ ! -d "${ROOT_DIR}/${VENV_DIR}" ]]; then
  python3 -m venv "${ROOT_DIR}/${VENV_DIR}"
fi

"${VENV_PYTHON}" -m pip install --upgrade pip >/dev/null
"${VENV_PYTHON}" -m pip install matplotlib >/dev/null

MPLCONFIGDIR="${MPLCONFIGDIR}" \
  "${VENV_PYTHON}" "${ROOT_DIR}/scripts/benchmark_plot.py" \
  --json "${JSON_OUT}" \
  --markdown "${MD_OUT}" \
  --rps-png "${RPS_PNG}" \
  --latency-png "${LATENCY_PNG}" \
  --failure-png "${FAILURE_PNG}"

echo "Benchmark JSON: ${JSON_OUT}"
echo "Benchmark report: ${MD_OUT}"
echo "Benchmark plots:"
echo "  ${RPS_PNG}"
echo "  ${LATENCY_PNG}"
echo "  ${FAILURE_PNG}"
