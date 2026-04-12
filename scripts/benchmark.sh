#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMMAND="${1:-run}"
if [[ $# -gt 0 ]]; then
  shift
fi

cpu_count() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
    return
  fi
  getconf _NPROCESSORS_ONLN
}

CPU_COUNT="$(cpu_count)"
BUILD_DIR="${BSRVCORE_BENCH_BUILD_DIR:-build-bench}"
VENV_DIR="${BSRVCORE_BENCH_VENV:-.venv-benchmark}"
OUTPUT_DIR="${BSRVCORE_BENCH_OUTPUT_DIR:-}"
RUN_ID="${BSRVCORE_BENCH_RUN_ID:-$(date -u +%Y%m%d-%H%M%SZ)}"
PACKAGE_DIR=""
TMP_DIR=""
PREFIX="${BSRVCORE_BENCH_TAG:-benchmark-report}"
SCENARIO="mainline"
SWEEP_DEPTH="${BSRVCORE_BENCH_SWEEP_DEPTH:-standard}"
LISTEN_HOST="0.0.0.0"
LISTEN_PORT="${BSRVCORE_BENCH_LISTEN_PORT:-18080}"
SERVER_URL=""
SERVER_IO_THREADS=""
SERVER_WORKER_THREADS=""
SCRIPT_RUN_MODE="local"
HTTP_METHOD=""
REQUEST_BODY_BYTES="${BSRVCORE_BENCH_REQUEST_BODY_BYTES:-0}"
RESPONSE_BODY_BYTES="${BSRVCORE_BENCH_RESPONSE_BODY_BYTES:-0}"
CLIENT_ONLY_CONCURRENCY=""
CLIENT_ONLY_PROCESSES=""
CLIENT_ONLY_WRK_THREADS=""
WARMUP_MS=""
DURATION_MS=""
COOLDOWN_MS=""
REPETITIONS=""
FINE_WARMUP_MS=""
FINE_DURATION_MS=""
FINE_COOLDOWN_MS=""
FINE_REPETITIONS=""
SSH_TARGET=""
SSH_PORT=""
SSH_KEY=""
SSH_REMOTE_ROOT="$ROOT_DIR"
SSH_SERVER_HOST=""
SSH_LOG_PATH="/tmp/bsrvcore-benchmark-server.log"
SERVER_ENV_JSON=""
PARALLELISM="${BSRVCORE_BUILD_PARALLEL:-${CPU_COUNT}}"
WRK_BIN=""
BENCH_BIN="${ROOT_DIR}/${BUILD_DIR}/benchmarks/bsrvcore_http_benchmark"
BUNDLED_WRK_BIN="${ROOT_DIR}/${BUILD_DIR}/_deps/bsrvcore_benchmark_wrk/src/bsrvcore_benchmark_wrk/wrk"
PLOT_PYTHON=""
RUN_CELL_INDEX=0
SOURCE_JSON=""
NEIGHBOR_COUNT=5
BODY_PHASE="get-response"
BODY_SIZE_START=0
BODY_SIZE_STOP=$((128 * 1024))
BODY_SIZE_STEP=$((8 * 1024))
BODY_REFINE_STEP=$((2 * 1024))
BODY_MAX_BYTES=$((1024 * 1024))
BODY_STOP_BEST_STABLE_CONCURRENCY=8
BODY_REQUEST_SIZES=""
BODY_RESPONSE_SIZES=""
BODY_CONCURRENCY_VALUES="1,2,4,8,16,32,48,64,96,128,160,192,256,320"
COARSE_ONLY=0

usage() {
  cat <<'EOF'
Usage:
  bash scripts/benchmark.sh prepare [options]
  bash scripts/benchmark.sh run [options]
  bash scripts/benchmark.sh server [options]
  bash scripts/benchmark.sh client [options]
  bash scripts/benchmark.sh ssh-run [options]
  bash scripts/benchmark.sh probe [options]
  bash scripts/benchmark.sh body-run [options]

Common options:
  --scenario <name|mainline|io|all>
  --sweep-depth <quick|standard|full>
  --build-dir <dir>
  --output-dir <dir>
  --venv-dir <dir>
  --tag <prefix>
  --warmup-ms <n>
  --duration-ms <n>
  --cooldown-ms <n>
  --repetitions <n>
  --fine-warmup-ms <n>
  --fine-duration-ms <n>
  --fine-cooldown-ms <n>
  --fine-repetitions <n>
  --coarse-only

Probe/body options:
  --mode <local|client>
  --http-method <get|post>
  --request-body-bytes <n>
  --response-body-bytes <n>
  --source-json <path>
  --neighbor-count <n>
  --body-phase <get-response|post-request|post-matrix>
  --body-size-start <n>
  --body-size-stop <n>
  --body-size-step <n>
  --body-refine-step-bytes <n>
  --body-max-bytes <n>
  --body-stop-best-stable-concurrency <n>
  --body-request-sizes <csv>
  --body-response-sizes <csv>
  --body-concurrency-values <csv>

Server/client options:
  --listen-host <ip>
  --listen-port <port>
  --server-url <http://host:port>
  --server-io-threads <n>
  --server-worker-threads <n>
  --server-env-json <path>

SSH options:
  --ssh-target <user@host>
  --ssh-port <port>
  --ssh-key <path>
  --ssh-remote-root <path>
  --server-host <host>
EOF
}

refresh_output_paths() {
  PACKAGE_DIR="${OUTPUT_DIR}/package"
  TMP_DIR="${OUTPUT_DIR}/.tmp-benchmark"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scenario)
      SCENARIO="$2"
      shift 2
      ;;
    --sweep-depth)
      SWEEP_DEPTH="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
      BENCH_BIN="${ROOT_DIR}/${BUILD_DIR}/benchmarks/bsrvcore_http_benchmark"
      BUNDLED_WRK_BIN="${ROOT_DIR}/${BUILD_DIR}/_deps/bsrvcore_benchmark_wrk/src/bsrvcore_benchmark_wrk/wrk"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --venv-dir)
      VENV_DIR="$2"
      shift 2
      ;;
    --tag)
      PREFIX="$2"
      shift 2
      ;;
    --listen-host)
      LISTEN_HOST="$2"
      shift 2
      ;;
    --listen-port)
      LISTEN_PORT="$2"
      shift 2
      ;;
    --server-url)
      SERVER_URL="$2"
      shift 2
      ;;
    --server-io-threads)
      SERVER_IO_THREADS="$2"
      shift 2
      ;;
    --server-worker-threads)
      SERVER_WORKER_THREADS="$2"
      shift 2
      ;;
    --mode)
      SCRIPT_RUN_MODE="$2"
      shift 2
      ;;
    --http-method)
      HTTP_METHOD="$2"
      shift 2
      ;;
    --request-body-bytes)
      REQUEST_BODY_BYTES="$2"
      shift 2
      ;;
    --response-body-bytes)
      RESPONSE_BODY_BYTES="$2"
      shift 2
      ;;
    --client-concurrency)
      CLIENT_ONLY_CONCURRENCY="$2"
      shift 2
      ;;
    --client-processes)
      CLIENT_ONLY_PROCESSES="$2"
      shift 2
      ;;
    --wrk-threads-per-process)
      CLIENT_ONLY_WRK_THREADS="$2"
      shift 2
      ;;
    --warmup-ms)
      WARMUP_MS="$2"
      shift 2
      ;;
    --duration-ms)
      DURATION_MS="$2"
      shift 2
      ;;
    --cooldown-ms)
      COOLDOWN_MS="$2"
      shift 2
      ;;
    --repetitions)
      REPETITIONS="$2"
      shift 2
      ;;
    --fine-warmup-ms)
      FINE_WARMUP_MS="$2"
      shift 2
      ;;
    --fine-duration-ms)
      FINE_DURATION_MS="$2"
      shift 2
      ;;
    --fine-cooldown-ms)
      FINE_COOLDOWN_MS="$2"
      shift 2
      ;;
    --fine-repetitions)
      FINE_REPETITIONS="$2"
      shift 2
      ;;
    --coarse-only)
      COARSE_ONLY=1
      shift 1
      ;;
    --server-env-json)
      SERVER_ENV_JSON="$2"
      shift 2
      ;;
    --source-json)
      SOURCE_JSON="$2"
      shift 2
      ;;
    --neighbor-count)
      NEIGHBOR_COUNT="$2"
      shift 2
      ;;
    --body-phase)
      BODY_PHASE="$2"
      shift 2
      ;;
    --body-size-start)
      BODY_SIZE_START="$2"
      shift 2
      ;;
    --body-size-stop)
      BODY_SIZE_STOP="$2"
      shift 2
      ;;
    --body-size-step)
      BODY_SIZE_STEP="$2"
      shift 2
      ;;
    --body-refine-step-bytes)
      BODY_REFINE_STEP="$2"
      shift 2
      ;;
    --body-max-bytes)
      BODY_MAX_BYTES="$2"
      shift 2
      ;;
    --body-stop-best-stable-concurrency)
      BODY_STOP_BEST_STABLE_CONCURRENCY="$2"
      shift 2
      ;;
    --body-request-sizes)
      BODY_REQUEST_SIZES="$2"
      shift 2
      ;;
    --body-response-sizes)
      BODY_RESPONSE_SIZES="$2"
      shift 2
      ;;
    --body-concurrency-values)
      BODY_CONCURRENCY_VALUES="$2"
      shift 2
      ;;
    --ssh-target)
      SSH_TARGET="$2"
      shift 2
      ;;
    --ssh-port)
      SSH_PORT="$2"
      shift 2
      ;;
    --ssh-key)
      SSH_KEY="$2"
      shift 2
      ;;
    --ssh-remote-root)
      SSH_REMOTE_ROOT="$2"
      shift 2
      ;;
    --server-host)
      SSH_SERVER_HOST="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

case "${SWEEP_DEPTH}" in
  quick)
    : "${WARMUP_MS:=800}"
    : "${DURATION_MS:=2000}"
    : "${COOLDOWN_MS:=400}"
    : "${REPETITIONS:=2}"
    : "${FINE_WARMUP_MS:=${WARMUP_MS}}"
    : "${FINE_DURATION_MS:=${DURATION_MS}}"
    : "${FINE_COOLDOWN_MS:=${COOLDOWN_MS}}"
    : "${FINE_REPETITIONS:=${REPETITIONS}}"
    ;;
  standard)
    : "${WARMUP_MS:=1000}"
    : "${DURATION_MS:=3000}"
    : "${COOLDOWN_MS:=500}"
    : "${REPETITIONS:=2}"
    : "${FINE_WARMUP_MS:=${WARMUP_MS}}"
    : "${FINE_DURATION_MS:=${DURATION_MS}}"
    : "${FINE_COOLDOWN_MS:=${COOLDOWN_MS}}"
    : "${FINE_REPETITIONS:=${REPETITIONS}}"
    ;;
  full)
    : "${WARMUP_MS:=1500}"
    : "${DURATION_MS:=5000}"
    : "${COOLDOWN_MS:=800}"
    : "${REPETITIONS:=3}"
    : "${FINE_WARMUP_MS:=${WARMUP_MS}}"
    : "${FINE_DURATION_MS:=${DURATION_MS}}"
    : "${FINE_COOLDOWN_MS:=${COOLDOWN_MS}}"
    : "${FINE_REPETITIONS:=${REPETITIONS}}"
    ;;
  *)
    echo "Unsupported sweep depth: ${SWEEP_DEPTH}" >&2
    exit 1
    ;;
esac

if [[ -z "${OUTPUT_DIR}" ]]; then
  OUTPUT_DIR="${ROOT_DIR}/.artifacts/benchmark-results/${RUN_ID}"
fi
refresh_output_paths

mkdir -p "${OUTPUT_DIR}"

dedupe_numbers() {
  printf '%s\n' "$@" | awk 'NF && !seen[$0]++' | sort -n
}

max1() {
  local value="$1"
  if (( value < 1 )); then
    echo 1
  else
    echo "${value}"
  fi
}

normalize_http_method() {
  local method="${1:-}"
  method="${method,,}"
  case "${method}" in
    ""|get|post)
      echo "${method}"
      ;;
    *)
      echo "Unsupported --http-method: ${1}" >&2
      exit 1
      ;;
  esac
}

probe_scenario_name() {
  local scenario_name="${SCENARIO}"
  local method
  method="$(normalize_http_method "${HTTP_METHOD}")"
  if [[ "${scenario_name}" == "body-matrix" || "${scenario_name}" == "http_body_matrix" ]]; then
    if [[ -z "${method}" ]]; then
      echo "probe for body-matrix requires --http-method get|post" >&2
      exit 1
    fi
    if [[ "${method}" == "get" ]]; then
      echo "http_body_matrix_get"
    else
      echo "http_body_matrix_post"
    fi
    return
  fi
  if [[ -n "${method}" && ( "${scenario_name}" == "mainline" || "${scenario_name}" == "io" || "${scenario_name}" == "all" ) ]]; then
    if [[ "${method}" == "get" ]]; then
      echo "http_body_matrix_get"
    else
      echo "http_body_matrix_post"
    fi
    return
  fi
  echo "${scenario_name}"
}

scenario_count() {
  case "${SCENARIO}" in
    mainline)
      echo 1
      ;;
    io)
      echo 3
      ;;
    all)
      "${BENCH_BIN}" --list-scenarios | wc -l | tr -d ' '
      ;;
    *)
      echo 1
      ;;
  esac
}

ensure_named_scenario() {
  if [[ "${SCENARIO}" == "mainline" || "${SCENARIO}" == "io" || "${SCENARIO}" == "all" ]]; then
    echo "${COMMAND} requires a single named benchmark scenario" >&2
    exit 1
  fi
}

build_benchmark() {
  BSRVCORE_BUILD_DIR="${BUILD_DIR}" \
  BSRVCORE_BUILD_TYPE=Release \
  BSRVCORE_BUILD_TESTS=OFF \
  BSRVCORE_BUILD_EXAMPLES=OFF \
  BSRVCORE_BUILD_BENCHMARKS=ON \
  BSRVCORE_BUILD_TOOLS=OFF \
  BSRVCORE_BENCHMARK_BUILD_BUNDLED_WRK=ON \
  BSRVCORE_BUILD_PARALLEL="${PARALLELISM}" \
    "${ROOT_DIR}/scripts/build.sh" bsrvcore_http_benchmark
}

resolve_wrk_bin() {
  if [[ -x "${BUNDLED_WRK_BIN}" ]]; then
    WRK_BIN="${BUNDLED_WRK_BIN}"
    return
  fi
  if command -v wrk >/dev/null 2>&1; then
    WRK_BIN="$(command -v wrk)"
    return
  fi
  echo "Unable to locate wrk binary" >&2
  exit 1
}

python_can_import_matplotlib() {
  local python_bin="$1"
  "${python_bin}" - <<'PY' >/dev/null 2>&1
import importlib.util
import sys
sys.exit(0 if importlib.util.find_spec("matplotlib") else 1)
PY
}

ensure_plot_python() {
  if [[ -n "${VIRTUAL_ENV:-}" && -x "${VIRTUAL_ENV}/bin/python" ]] && \
     python_can_import_matplotlib "${VIRTUAL_ENV}/bin/python"; then
    PLOT_PYTHON="${VIRTUAL_ENV}/bin/python"
    return
  fi

  if [[ -x "${ROOT_DIR}/${VENV_DIR}/bin/python" ]] && \
     python_can_import_matplotlib "${ROOT_DIR}/${VENV_DIR}/bin/python"; then
    PLOT_PYTHON="${ROOT_DIR}/${VENV_DIR}/bin/python"
    return
  fi

  if command -v python3 >/dev/null 2>&1; then
    if python3 -m venv --help >/dev/null 2>&1; then
      if [[ ! -d "${ROOT_DIR}/${VENV_DIR}" ]]; then
        python3 -m venv "${ROOT_DIR}/${VENV_DIR}"
      fi
      "${ROOT_DIR}/${VENV_DIR}/bin/python" -m pip install --upgrade pip >/dev/null
      "${ROOT_DIR}/${VENV_DIR}/bin/python" -m pip install matplotlib >/dev/null
      PLOT_PYTHON="${ROOT_DIR}/${VENV_DIR}/bin/python"
      return
    fi
    if python_can_import_matplotlib "$(command -v python3)"; then
      PLOT_PYTHON="$(command -v python3)"
      return
    fi
  fi

  echo "Unable to find a Python environment with matplotlib" >&2
  exit 1
}

collect_env_json() {
  local role="$1"
  local output="$2"
  python3 "${ROOT_DIR}/scripts/benchmark_collect_env.py" --role "${role}" --output "${output}"
}

prepare_outputs() {
  local -a targets=(
    "${OUTPUT_DIR}/${PREFIX}.json"
    "${OUTPUT_DIR}/${PREFIX}.md"
    "${OUTPUT_DIR}/${PREFIX}-capacity-overview.png"
    "${OUTPUT_DIR}/${PREFIX}-per-connection-throughput.png"
    "${OUTPUT_DIR}/${PREFIX}-peak-neighborhood.png"
    "${OUTPUT_DIR}/${PREFIX}-thread-sensitivity.png"
    "${OUTPUT_DIR}/${PREFIX}-loadgen-sensitivity.png"
  )
  local target
  for target in "${targets[@]}"; do
    if [[ -e "${target}" ]]; then
      echo "Refusing to overwrite existing benchmark artifact: ${target}" >&2
      echo "Use --output-dir or --tag to write a new report set." >&2
      exit 1
    fi
  done

  rm -rf "${PACKAGE_DIR}" "${TMP_DIR}"
  mkdir -p "${PACKAGE_DIR}" "${TMP_DIR}/cells"
}

server_pairs_for_depth() {
  local half_cpu quarter_cpu three_quarter_cpu boosted_io_cpu double_cpu
  half_cpu="$(max1 $(( CPU_COUNT / 2 )))"
  quarter_cpu="$(max1 $(( CPU_COUNT / 4 )))"
  three_quarter_cpu="$(max1 $(( (CPU_COUNT * 3 + 3) / 4 )))"
  boosted_io_cpu="$(max1 $(( (CPU_COUNT * 6 + 4) / 5 )))"
  double_cpu="$(max1 $(( CPU_COUNT * 2 )))"

  echo "1 1"
  echo "${half_cpu} 1"
  echo "${three_quarter_cpu} 1"
  echo "${CPU_COUNT} 1"
  echo "${boosted_io_cpu} 1"
  echo "${quarter_cpu} ${half_cpu}"
  echo "${half_cpu} ${CPU_COUNT}"
  if [[ "${SWEEP_DEPTH}" != "quick" ]]; then
    echo "${quarter_cpu} 1"
    echo "${half_cpu} ${double_cpu}"
  fi
}

client_shapes_for_depth() {
  echo "1 1"
  echo "2 1"
  if [[ "${SWEEP_DEPTH}" != "quick" ]]; then
    echo "4 1"
    echo "4 2"
  fi
}

emit_local_matrix() {
  local matrix_file="$1"
  local -A seen=()
  while read -r io_threads worker_threads; do
    [[ -z "${io_threads}" ]] && continue
    local pressure_threads="${worker_threads}"
    if (( io_threads > pressure_threads )); then
      pressure_threads="${io_threads}"
    fi
    local conc_values=()
    mapfile -t conc_values < <(dedupe_numbers \
      "$(max1 "${pressure_threads}")" \
      "$(max1 $(( pressure_threads * 2 )))" \
      "$(max1 $(( pressure_threads * 4 )))" \
      "$(max1 $(( pressure_threads * 8 )))")
    while read -r client_processes wrk_threads; do
      [[ -z "${client_processes}" ]] && continue
      local conc
      for conc in "${conc_values[@]}"; do
        if (( client_processes > conc )); then
          continue
        fi
        if (( wrk_threads > (( conc + client_processes - 1 ) / client_processes ) )); then
          continue
        fi
        local label="io${io_threads}-worker${worker_threads}-conc${conc}-proc${client_processes}-wrk${wrk_threads}"
        local key="${label}"
        if [[ -z "${seen[${key}]:-}" ]]; then
          printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            "${label}" "${io_threads}" "${worker_threads}" "${conc}" \
            "${client_processes}" "${wrk_threads}" >> "${matrix_file}"
          seen["${key}"]=1
        fi
      done
    done < <(client_shapes_for_depth)
  done < <(server_pairs_for_depth)
}

emit_client_matrix() {
  local matrix_file="$1"
  local worker_threads="$2"
  local io_threads="$3"
  local -A seen=()
  local conc_values=()
  if [[ -n "${CLIENT_ONLY_CONCURRENCY}" ]]; then
    conc_values=("${CLIENT_ONLY_CONCURRENCY}")
  else
    local pressure_threads="${worker_threads}"
    if (( io_threads > pressure_threads )); then
      pressure_threads="${io_threads}"
    fi
    mapfile -t conc_values < <(dedupe_numbers \
      "$(max1 "${pressure_threads}")" \
      "$(max1 $(( pressure_threads * 2 )))" \
      "$(max1 $(( pressure_threads * 4 )))" \
      "$(max1 $(( pressure_threads * 8 )))")
  fi

  if [[ -n "${CLIENT_ONLY_PROCESSES}" && -n "${CLIENT_ONLY_WRK_THREADS}" ]]; then
    while read -r conc; do
      [[ -z "${conc}" ]] && continue
      local label="io${io_threads}-worker${worker_threads}-conc${conc}-proc${CLIENT_ONLY_PROCESSES}-wrk${CLIENT_ONLY_WRK_THREADS}"
      printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${label}" "${io_threads}" "${worker_threads}" "${conc}" \
        "${CLIENT_ONLY_PROCESSES}" "${CLIENT_ONLY_WRK_THREADS}" >> "${matrix_file}"
    done < <(printf '%s\n' "${conc_values[@]}")
    return
  fi

  while read -r client_processes wrk_threads; do
    [[ -z "${client_processes}" ]] && continue
    local conc
    for conc in "${conc_values[@]}"; do
      if (( client_processes > conc )); then
        continue
      fi
      if (( wrk_threads > (( conc + client_processes - 1 ) / client_processes ) )); then
        continue
      fi
      local label="io${io_threads}-worker${worker_threads}-conc${conc}-proc${client_processes}-wrk${wrk_threads}"
      if [[ -z "${seen[${label}]:-}" ]]; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
          "${label}" "${io_threads}" "${worker_threads}" "${conc}" \
          "${client_processes}" "${wrk_threads}" >> "${matrix_file}"
        seen["${label}"]=1
      fi
    done
  done < <(client_shapes_for_depth)
}

run_cell() {
  local mode="$1"
  local scenario_name="$2"
  local label="$3"
  local io_threads="$4"
  local worker_threads="$5"
  local client_concurrency="$6"
  local client_processes="$7"
  local wrk_threads="$8"
  local request_body_bytes="$9"
  local response_body_bytes="${10}"
  local output_json="${11}"

  local -a cmd=(
    "${BENCH_BIN}"
    --mode "${mode}"
    --scenario "${scenario_name}"
    --pressure-label "${label}"
    --server-io-threads "${io_threads}"
    --server-worker-threads "${worker_threads}"
    --client-concurrency "${client_concurrency}"
    --client-processes "${client_processes}"
    --wrk-threads-per-process "${wrk_threads}"
    --request-body-bytes "${request_body_bytes}"
    --response-body-bytes "${response_body_bytes}"
    --warmup-ms "${WARMUP_MS}"
    --duration-ms "${DURATION_MS}"
    --cooldown-ms "${COOLDOWN_MS}"
    --repetitions "${REPETITIONS}"
    --output-json "${output_json}"
  )
  if [[ -n "${WRK_BIN}" ]]; then
    cmd+=(--wrk-bin "${WRK_BIN}")
  fi
  if [[ "${mode}" == "client" ]]; then
    cmd+=(--server-url "${SERVER_URL}")
  fi
  "${cmd[@]}"
}

build_interim_json() {
  local mode="$1"
  local topology="$2"
  local command_line="$3"
  local coarse_json="${TMP_DIR}/coarse.json"
  local coarse_summary="${TMP_DIR}/coarse-summary.md"
  local -a package_cmd=(
    python3 "${ROOT_DIR}/scripts/benchmark_package.py"
    --input-dir "${TMP_DIR}/cells"
    --output-json "${coarse_json}"
    --summary-md "${coarse_summary}"
    --mode "${mode}"
    --scenario "${SCENARIO}"
    --sweep-depth "${SWEEP_DEPTH}"
    --topology "${topology}"
    --command-line "${command_line}"
    --client-env-json "${TMP_DIR}/client-env.json"
    --package-dir "${TMP_DIR}"
    --prefix "${PREFIX}"
  )
  if [[ -n "${SERVER_ENV_JSON}" ]]; then
    package_cmd+=(--server-env-json "${SERVER_ENV_JSON}")
  fi
  if [[ -n "${SERVER_URL}" ]]; then
    package_cmd+=(--server-url "${SERVER_URL}")
  fi
  "${package_cmd[@]}"
}

emit_refined_matrix() {
  local coarse_json="${TMP_DIR}/coarse.json"
  local refine_matrix="${TMP_DIR}/refine.tsv"
  local refine_only="${TMP_DIR}/refine-only.tsv"
  python3 "${ROOT_DIR}/scripts/benchmark_refine.py" \
    --json "${coarse_json}" \
    --sweep-depth "${SWEEP_DEPTH}" \
    --output-tsv "${refine_matrix}"
  awk 'NR==FNR { seen[$0] = 1; next } !seen[$0]' \
    "${TMP_DIR}/matrix.tsv" "${refine_matrix}" > "${refine_only}"
}

run_matrix_file() {
  local mode="$1"
  local matrix_file="$2"
  local prefix="$3"
  local total
  total="$(wc -l < "${matrix_file}" | tr -d ' ')"
  local local_index=0
  while IFS=$'\t' read -r label io_threads worker_threads client_concurrency client_processes wrk_threads scenario_name request_body_bytes response_body_bytes; do
    [[ -z "${label}" ]] && continue
    if [[ -z "${scenario_name:-}" ]]; then
      scenario_name="${SCENARIO}"
    fi
    if [[ -z "${request_body_bytes:-}" ]]; then
      request_body_bytes="${REQUEST_BODY_BYTES}"
    fi
    if [[ -z "${response_body_bytes:-}" ]]; then
      response_body_bytes="${RESPONSE_BODY_BYTES}"
    fi
    local_index=$(( local_index + 1 ))
    RUN_CELL_INDEX=$(( RUN_CELL_INDEX + 1 ))
    printf '[%s %s/%s] %s\n' "${prefix}" "${local_index}" "${total}" "${label}"
    run_cell "${mode}" "${scenario_name}" "${label}" "${io_threads}" "${worker_threads}" \
      "${client_concurrency}" "${client_processes}" "${wrk_threads}" \
      "${request_body_bytes}" "${response_body_bytes}" \
      "${TMP_DIR}/cells/cell-${RUN_CELL_INDEX}.json"
  done < "${matrix_file}"
}

copy_package_artifacts() {
  cp "${TMP_DIR}/client-env.json" "${PACKAGE_DIR}/client-env.json"
  if [[ -f "${TMP_DIR}/server-env.json" ]]; then
    cp "${TMP_DIR}/server-env.json" "${PACKAGE_DIR}/server-env.json"
  fi
}

package_results() {
  local mode="$1"
  local topology="$2"
  local command_line="$3"
  local package_summary="${PACKAGE_DIR}/summary.md"
  local -a package_cmd=(
    python3 "${ROOT_DIR}/scripts/benchmark_package.py"
    --input-dir "${TMP_DIR}/cells"
    --output-json "${OUTPUT_DIR}/${PREFIX}.json"
    --summary-md "${package_summary}"
    --mode "${mode}"
    --scenario "${SCENARIO}"
    --sweep-depth "${SWEEP_DEPTH}"
    --topology "${topology}"
    --command-line "${command_line}"
    --client-env-json "${TMP_DIR}/client-env.json"
    --package-dir "${PACKAGE_DIR}"
    --prefix "${PREFIX}"
  )
  if [[ -n "${SERVER_ENV_JSON}" ]]; then
    package_cmd+=(--server-env-json "${SERVER_ENV_JSON}")
  fi
  if [[ -n "${SERVER_URL}" ]]; then
    package_cmd+=(--server-url "${SERVER_URL}")
  fi
  "${package_cmd[@]}"

  ensure_plot_python
  local plots_manifest="${TMP_DIR}/generated-plots.txt"
  "${PLOT_PYTHON}" "${ROOT_DIR}/scripts/benchmark_plot.py" \
    --json "${OUTPUT_DIR}/${PREFIX}.json" \
    --output-dir "${OUTPUT_DIR}" \
    --prefix "${PREFIX}" > "${plots_manifest}"
  mapfile -t generated_plots < "${plots_manifest}"
  copy_package_artifacts
  printf 'Generated plots:\n'
  printf '  %s\n' "${generated_plots[@]}"
  printf 'Benchmark artifacts: %s\n' "${OUTPUT_DIR}"
}

prepare_command() {
  build_benchmark
  resolve_wrk_bin
  ensure_plot_python
  printf 'benchmark binary: %s\n' "${BENCH_BIN}"
  printf 'wrk binary: %s\n' "${WRK_BIN}"
  printf 'plot python: %s\n' "${PLOT_PYTHON}"
  printf 'output dir: %s\n' "${OUTPUT_DIR}"
}

probe_command() {
  local scenario_name
  scenario_name="$(probe_scenario_name)"
  local mode="${SCRIPT_RUN_MODE}"
  local client_concurrency="${CLIENT_ONLY_CONCURRENCY:-1}"
  local client_processes="${CLIENT_ONLY_PROCESSES:-1}"
  local wrk_threads="${CLIENT_ONLY_WRK_THREADS:-1}"
  local io_threads="${SERVER_IO_THREADS:-$(max1 $(( CPU_COUNT / 2 )))}"
  local worker_threads="${SERVER_WORKER_THREADS:-${CPU_COUNT}}"

  if [[ "${mode}" == "client" && -z "${SERVER_URL}" ]]; then
    echo "probe --mode client requires --server-url" >&2
    exit 1
  fi

  mkdir -p "${OUTPUT_DIR}"
  build_benchmark
  resolve_wrk_bin
  local env_path="${OUTPUT_DIR}/${PREFIX}-env.json"
  collect_env_json client "${env_path}"

  local label="${scenario_name}-io${io_threads}-worker${worker_threads}-conc${client_concurrency}-proc${client_processes}-wrk${wrk_threads}-req${REQUEST_BODY_BYTES}-resp${RESPONSE_BODY_BYTES}"
  local output_json="${OUTPUT_DIR}/${PREFIX}.json"
  if [[ -e "${output_json}" ]]; then
    echo "Refusing to overwrite existing probe artifact: ${output_json}" >&2
    exit 1
  fi

  run_cell "${mode}" "${scenario_name}" "${label}" "${io_threads}" "${worker_threads}" \
    "${client_concurrency}" "${client_processes}" "${wrk_threads}" \
    "${REQUEST_BODY_BYTES}" "${RESPONSE_BODY_BYTES}" "${output_json}"
  printf 'Probe artifact: %s\n' "${output_json}"
}

emit_body_matrix() {
  local matrix_file="$1"
  if [[ -z "${SOURCE_JSON}" ]]; then
    echo "body-run requires --source-json pointing to a consolidated benchmark JSON" >&2
    exit 1
  fi
  local -a cmd=(
    python3 "${ROOT_DIR}/scripts/benchmark_body_matrix.py"
    --json "${SOURCE_JSON}"
    --output-tsv "${matrix_file}"
    --phase "${BODY_PHASE}"
    --neighbor-count "${NEIGHBOR_COUNT}"
    --concurrency-values "${BODY_CONCURRENCY_VALUES}"
    --body-start-bytes "${BODY_SIZE_START}"
    --body-stop-bytes "${BODY_SIZE_STOP}"
    --body-step-bytes "${BODY_SIZE_STEP}"
    --body-refine-step-bytes "${BODY_REFINE_STEP}"
    --body-max-bytes "${BODY_MAX_BYTES}"
    --body-stop-best-stable-concurrency "${BODY_STOP_BEST_STABLE_CONCURRENCY}"
  )
  if [[ -n "${BODY_REQUEST_SIZES}" ]]; then
    cmd+=(--request-sizes "${BODY_REQUEST_SIZES}")
  fi
  if [[ -n "${BODY_RESPONSE_SIZES}" ]]; then
    cmd+=(--response-sizes "${BODY_RESPONSE_SIZES}")
  fi
  "${cmd[@]}"
}

body_run_command() {
  local mode="${SCRIPT_RUN_MODE}"
  if [[ "${mode}" == "client" && -z "${SERVER_URL}" ]]; then
    echo "body-run --mode client requires --server-url" >&2
    exit 1
  fi

  prepare_outputs
  build_benchmark
  resolve_wrk_bin
  collect_env_json client "${TMP_DIR}/client-env.json"
  if [[ -n "${SERVER_ENV_JSON}" ]]; then
    cp "${SERVER_ENV_JSON}" "${TMP_DIR}/server-env.json"
    SERVER_ENV_JSON="${TMP_DIR}/server-env.json"
  fi

  local matrix_file="${TMP_DIR}/matrix.tsv"
  : > "${matrix_file}"
  emit_body_matrix "${matrix_file}"

  local matrix_total
  matrix_total="$(wc -l < "${matrix_file}" | tr -d ' ')"
  if (( matrix_total == 0 )); then
    echo "body-run matrix is empty" >&2
    exit 1
  fi

  local scenario_label="body-${BODY_PHASE}"
  local previous_scenario="${SCENARIO}"
  local topology="single-host"
  if [[ "${mode}" == "client" ]]; then
    topology="dual-host-manual"
  fi
  SCENARIO="${scenario_label}"
  RUN_CELL_INDEX=0
  printf 'Running body sweep phase %s with %s cells derived from %s\n' \
    "${BODY_PHASE}" "${matrix_total}" "${SOURCE_JSON}"
  run_matrix_file "${mode}" "${matrix_file}" body
  package_results "${mode}" "${topology}" \
    "bash scripts/benchmark.sh body-run --source-json ${SOURCE_JSON} --body-phase ${BODY_PHASE} --output-dir ${OUTPUT_DIR}"
  SCENARIO="${previous_scenario}"
}

run_local_command() {
  prepare_outputs
  build_benchmark
  resolve_wrk_bin
  collect_env_json client "${TMP_DIR}/client-env.json"

  local matrix_file="${TMP_DIR}/matrix.tsv"
  : > "${matrix_file}"
  emit_local_matrix "${matrix_file}"

  local coarse_total
  coarse_total="$(wc -l < "${matrix_file}" | tr -d ' ')"
  local scenario_multiplier
  scenario_multiplier="$(scenario_count)"
  printf 'Running coarse sweep with %s pressure cells across scenario selector %s (%s benchmark cells expected)\n' \
    "${coarse_total}" "${SCENARIO}" "$(( coarse_total * scenario_multiplier ))"

  RUN_CELL_INDEX=0
  run_matrix_file local "${matrix_file}" coarse
  build_interim_json local "single-host" \
    "bash scripts/benchmark.sh run --scenario ${SCENARIO} --sweep-depth ${SWEEP_DEPTH} --output-dir ${OUTPUT_DIR}"
  if (( COARSE_ONLY == 1 )); then
    SERVER_ENV_JSON=""
    package_results local "single-host" \
      "bash scripts/benchmark.sh run --scenario ${SCENARIO} --sweep-depth ${SWEEP_DEPTH} --coarse-only --output-dir ${OUTPUT_DIR}"
    return
  fi
  emit_refined_matrix

  local refine_total
  refine_total="$(wc -l < "${TMP_DIR}/refine-only.tsv" | tr -d ' ')"
  if (( refine_total > 0 )); then
    printf 'Running fine sweep with %s additional pressure cells near peak regions\n' "${refine_total}"
    local coarse_warmup_ms="${WARMUP_MS}"
    local coarse_duration_ms="${DURATION_MS}"
    local coarse_cooldown_ms="${COOLDOWN_MS}"
    local coarse_repetitions="${REPETITIONS}"
    WARMUP_MS="${FINE_WARMUP_MS}"
    DURATION_MS="${FINE_DURATION_MS}"
    COOLDOWN_MS="${FINE_COOLDOWN_MS}"
    REPETITIONS="${FINE_REPETITIONS}"
    run_matrix_file local "${TMP_DIR}/refine-only.tsv" fine
    WARMUP_MS="${coarse_warmup_ms}"
    DURATION_MS="${coarse_duration_ms}"
    COOLDOWN_MS="${coarse_cooldown_ms}"
    REPETITIONS="${coarse_repetitions}"
  fi

  SERVER_ENV_JSON=""
  package_results local "single-host" "bash scripts/benchmark.sh run --scenario ${SCENARIO} --sweep-depth ${SWEEP_DEPTH} --output-dir ${OUTPUT_DIR}"
}

server_command() {
  ensure_named_scenario
  build_benchmark
  collect_env_json server "${OUTPUT_DIR}/server-env.json"
  if [[ -z "${SERVER_IO_THREADS}" ]]; then
    SERVER_IO_THREADS="$(max1 $(( CPU_COUNT / 2 )))"
  fi
  if [[ -z "${SERVER_WORKER_THREADS}" ]]; then
    SERVER_WORKER_THREADS="${CPU_COUNT}"
  fi
  exec "${BENCH_BIN}" \
    --mode server \
    --scenario "${SCENARIO}" \
    --listen-host "${LISTEN_HOST}" \
    --listen-port "${LISTEN_PORT}" \
    --pressure-label "server-mode" \
    --server-io-threads "${SERVER_IO_THREADS}" \
    --server-worker-threads "${SERVER_WORKER_THREADS}" \
    --request-body-bytes "${REQUEST_BODY_BYTES}" \
    --response-body-bytes "${RESPONSE_BODY_BYTES}" \
    --client-concurrency 1
}

client_command() {
  ensure_named_scenario
  if [[ -z "${SERVER_URL}" ]]; then
    echo "client mode requires --server-url" >&2
    exit 1
  fi
  if [[ -z "${SERVER_IO_THREADS}" || -z "${SERVER_WORKER_THREADS}" ]]; then
    echo "client mode requires --server-io-threads and --server-worker-threads for result labeling" >&2
    exit 1
  fi

  prepare_outputs
  build_benchmark
  resolve_wrk_bin
  collect_env_json client "${TMP_DIR}/client-env.json"
  if [[ -n "${SERVER_ENV_JSON}" ]]; then
    cp "${SERVER_ENV_JSON}" "${TMP_DIR}/server-env.json"
    SERVER_ENV_JSON="${TMP_DIR}/server-env.json"
  fi

  local matrix_file="${TMP_DIR}/matrix.tsv"
  : > "${matrix_file}"
  emit_client_matrix "${matrix_file}" "${SERVER_WORKER_THREADS}" "${SERVER_IO_THREADS}"

  local coarse_total
  coarse_total="$(wc -l < "${matrix_file}" | tr -d ' ')"
  printf 'Running coarse client-side sweep with %s cells against %s\n' "${coarse_total}" "${SERVER_URL}"
  RUN_CELL_INDEX=0
  run_matrix_file client "${matrix_file}" coarse
  build_interim_json client "dual-host-manual" \
    "bash scripts/benchmark.sh client --scenario ${SCENARIO} --server-url ${SERVER_URL} --server-io-threads ${SERVER_IO_THREADS} --server-worker-threads ${SERVER_WORKER_THREADS} --sweep-depth ${SWEEP_DEPTH} --output-dir ${OUTPUT_DIR}"
  if (( COARSE_ONLY == 1 )); then
    package_results client "dual-host-manual" \
      "bash scripts/benchmark.sh client --scenario ${SCENARIO} --server-url ${SERVER_URL} --server-io-threads ${SERVER_IO_THREADS} --server-worker-threads ${SERVER_WORKER_THREADS} --sweep-depth ${SWEEP_DEPTH} --coarse-only --output-dir ${OUTPUT_DIR}"
    return
  fi
  emit_refined_matrix

  local refine_total
  refine_total="$(wc -l < "${TMP_DIR}/refine-only.tsv" | tr -d ' ')"
  if (( refine_total > 0 )); then
    printf 'Running fine client-side sweep with %s additional cells near peak regions\n' "${refine_total}"
    local coarse_warmup_ms="${WARMUP_MS}"
    local coarse_duration_ms="${DURATION_MS}"
    local coarse_cooldown_ms="${COOLDOWN_MS}"
    local coarse_repetitions="${REPETITIONS}"
    WARMUP_MS="${FINE_WARMUP_MS}"
    DURATION_MS="${FINE_DURATION_MS}"
    COOLDOWN_MS="${FINE_COOLDOWN_MS}"
    REPETITIONS="${FINE_REPETITIONS}"
    run_matrix_file client "${TMP_DIR}/refine-only.tsv" fine
    WARMUP_MS="${coarse_warmup_ms}"
    DURATION_MS="${coarse_duration_ms}"
    COOLDOWN_MS="${coarse_cooldown_ms}"
    REPETITIONS="${coarse_repetitions}"
  fi

  package_results client "dual-host-manual" \
    "bash scripts/benchmark.sh client --scenario ${SCENARIO} --server-url ${SERVER_URL} --server-io-threads ${SERVER_IO_THREADS} --server-worker-threads ${SERVER_WORKER_THREADS} --sweep-depth ${SWEEP_DEPTH} --output-dir ${OUTPUT_DIR}"
}

ssh_args() {
  local -a args=()
  [[ -n "${SSH_PORT}" ]] && args+=(-p "${SSH_PORT}")
  [[ -n "${SSH_KEY}" ]] && args+=(-i "${SSH_KEY}")
  printf '%q ' "${args[@]}"
}

wait_for_tcp() {
  local host="$1"
  local port="$2"
  python3 - "$host" "$port" <<'PY'
import socket
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
deadline = time.time() + 30
while time.time() < deadline:
    sock = socket.socket()
    sock.settimeout(1)
    try:
        sock.connect((host, port))
    except OSError:
        time.sleep(0.5)
    else:
        sock.close()
        sys.exit(0)
    finally:
        sock.close()
sys.exit(1)
PY
}

ssh_run_command() {
  ensure_named_scenario
  if [[ -z "${SSH_TARGET}" ]]; then
    echo "ssh-run requires --ssh-target" >&2
    exit 1
  fi

  build_benchmark
  resolve_wrk_bin
  prepare_outputs
  collect_env_json client "${TMP_DIR}/client-env.json"

  local server_host="${SSH_SERVER_HOST}"
  if [[ -z "${server_host}" ]]; then
    server_host="${SSH_TARGET##*@}"
    server_host="${server_host%%:*}"
  fi

  local ssh_extra=()
  [[ -n "${SSH_PORT}" ]] && ssh_extra+=(-p "${SSH_PORT}")
  [[ -n "${SSH_KEY}" ]] && ssh_extra+=(-i "${SSH_KEY}")

  ssh "${ssh_extra[@]}" "${SSH_TARGET}" \
    "cd '${SSH_REMOTE_ROOT}' && BSRVCORE_BUILD_DIR='${BUILD_DIR}' BSRVCORE_BUILD_TYPE=Release BSRVCORE_BUILD_TESTS=OFF BSRVCORE_BUILD_EXAMPLES=OFF BSRVCORE_BUILD_BENCHMARKS=ON BSRVCORE_BUILD_TOOLS=OFF BSRVCORE_BENCHMARK_BUILD_BUNDLED_WRK=ON BSRVCORE_BUILD_PARALLEL='${PARALLELISM}' bash scripts/build.sh bsrvcore_http_benchmark"

  ssh "${ssh_extra[@]}" "${SSH_TARGET}" \
    "cd '${SSH_REMOTE_ROOT}' && python3 scripts/benchmark_collect_env.py --role server --output /tmp/bsrvcore-benchmark-server-env.json && cat /tmp/bsrvcore-benchmark-server-env.json" \
    > "${TMP_DIR}/server-env.json"
  SERVER_ENV_JSON="${TMP_DIR}/server-env.json"

  local matrix_file="${TMP_DIR}/matrix.tsv"
  : > "${matrix_file}"
  emit_local_matrix "${matrix_file}"

  local -A group_seen=()
  local label io_threads worker_threads client_concurrency client_processes wrk_threads
  while IFS=$'\t' read -r label io_threads worker_threads client_concurrency client_processes wrk_threads; do
    [[ -z "${label}" ]] && continue
    local group_key="${io_threads}|${worker_threads}"
    if [[ -z "${group_seen[${group_key}]:-}" ]]; then
      group_seen["${group_key}"]=1
      local remote_pid
      remote_pid="$(
        ssh "${ssh_extra[@]}" "${SSH_TARGET}" \
          "cd '${SSH_REMOTE_ROOT}' && nohup bash scripts/benchmark.sh server --scenario '${SCENARIO}' --build-dir '${BUILD_DIR}' --listen-host '${LISTEN_HOST}' --listen-port '${LISTEN_PORT}' --server-io-threads '${io_threads}' --server-worker-threads '${worker_threads}' > '${SSH_LOG_PATH}' 2>&1 & echo \$!"
      )"
      wait_for_tcp "${server_host}" "${LISTEN_PORT}"
      SERVER_URL="http://${server_host}:${LISTEN_PORT}"
      local subgroup_file="${TMP_DIR}/subgroup-${io_threads}-${worker_threads}.tsv"
      awk -F'\t' -v io="${io_threads}" -v worker="${worker_threads}" '$2 == io && $3 == worker' \
        "${matrix_file}" > "${subgroup_file}"
      local index=0
      local subgroup_total
      subgroup_total="$(wc -l < "${subgroup_file}" | tr -d ' ')"
      while IFS=$'\t' read -r label io_threads worker_threads client_concurrency client_processes wrk_threads scenario_name request_body_bytes response_body_bytes; do
        [[ -z "${label}" ]] && continue
        index=$(( index + 1 ))
        if [[ -z "${scenario_name:-}" ]]; then
          scenario_name="${SCENARIO}"
        fi
        if [[ -z "${request_body_bytes:-}" ]]; then
          request_body_bytes="${REQUEST_BODY_BYTES}"
        fi
        if [[ -z "${response_body_bytes:-}" ]]; then
          response_body_bytes="${RESPONSE_BODY_BYTES}"
        fi
        printf '[ssh %s/%s] %s\n' "${index}" "${subgroup_total}" "${label}"
        run_cell client "${scenario_name}" "${label}" "${io_threads}" "${worker_threads}" \
          "${client_concurrency}" "${client_processes}" "${wrk_threads}" \
          "${request_body_bytes}" "${response_body_bytes}" \
          "${TMP_DIR}/cells/cell-${io_threads}-${worker_threads}-${index}.json"
      done < "${subgroup_file}"
      ssh "${ssh_extra[@]}" "${SSH_TARGET}" "kill '${remote_pid}' >/dev/null 2>&1 || true"
      sleep 1
    fi
  done < "${matrix_file}"

  package_results client "dual-host-ssh" \
    "bash scripts/benchmark.sh ssh-run --scenario ${SCENARIO} --ssh-target ${SSH_TARGET} --sweep-depth ${SWEEP_DEPTH} --output-dir ${OUTPUT_DIR}"
}

case "${COMMAND}" in
  prepare)
    prepare_command
    ;;
  run)
    run_local_command
    ;;
  server)
    server_command
    ;;
  client)
    client_command
    ;;
  ssh-run)
    ssh_run_command
    ;;
  probe)
    probe_command
    ;;
  body-run)
    body_run_command
    ;;
  *)
    echo "Unknown command: ${COMMAND}" >&2
    usage >&2
    exit 1
    ;;
esac
