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
OUTPUT_DIR="${ROOT_DIR}/docs/benchmark-results"
PACKAGE_DIR="${OUTPUT_DIR}/package"
TMP_DIR="${OUTPUT_DIR}/.tmp-benchmark"
PREFIX="${BSRVCORE_BENCH_TAG:-benchmark-report}"
SCENARIO="io"
SWEEP_DEPTH="${BSRVCORE_BENCH_SWEEP_DEPTH:-standard}"
LISTEN_HOST="0.0.0.0"
LISTEN_PORT="${BSRVCORE_BENCH_LISTEN_PORT:-18080}"
SERVER_URL=""
SERVER_IO_THREADS=""
SERVER_WORKER_THREADS=""
CLIENT_ONLY_CONCURRENCY=""
CLIENT_ONLY_PROCESSES=""
CLIENT_ONLY_WRK_THREADS=""
WARMUP_MS=""
DURATION_MS=""
COOLDOWN_MS=""
REPETITIONS=""
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

usage() {
  cat <<'EOF'
Usage:
  bash scripts/benchmark.sh prepare [options]
  bash scripts/benchmark.sh run [options]
  bash scripts/benchmark.sh server [options]
  bash scripts/benchmark.sh client [options]
  bash scripts/benchmark.sh ssh-run [options]

Common options:
  --scenario <name|io|all>
  --sweep-depth <quick|standard|full>
  --build-dir <dir>
  --venv-dir <dir>
  --tag <prefix>
  --warmup-ms <n>
  --duration-ms <n>
  --cooldown-ms <n>
  --repetitions <n>

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
    --server-env-json)
      SERVER_ENV_JSON="$2"
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
    ;;
  standard)
    : "${WARMUP_MS:=1000}"
    : "${DURATION_MS:=3000}"
    : "${COOLDOWN_MS:=500}"
    : "${REPETITIONS:=2}"
    ;;
  full)
    : "${WARMUP_MS:=1500}"
    : "${DURATION_MS:=5000}"
    : "${COOLDOWN_MS:=800}"
    : "${REPETITIONS:=3}"
    ;;
  *)
    echo "Unsupported sweep depth: ${SWEEP_DEPTH}" >&2
    exit 1
    ;;
esac

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

scenario_count() {
  case "${SCENARIO}" in
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
  if [[ "${SCENARIO}" == "io" || "${SCENARIO}" == "all" ]]; then
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
  rm -f "${OUTPUT_DIR}/${PREFIX}.json"
  rm -f "${OUTPUT_DIR}/${PREFIX}.md"
  rm -f "${OUTPUT_DIR}/${PREFIX}"*.png
  rm -rf "${PACKAGE_DIR}" "${TMP_DIR}"
  mkdir -p "${PACKAGE_DIR}" "${TMP_DIR}/cells"
}

server_pairs_for_depth() {
  local half_cpu quarter_cpu double_cpu
  half_cpu="$(max1 $(( CPU_COUNT / 2 )))"
  quarter_cpu="$(max1 $(( CPU_COUNT / 4 )))"
  double_cpu="$(max1 $(( CPU_COUNT * 2 )))"

  echo "1 1"
  echo "${quarter_cpu} ${half_cpu}"
  echo "${half_cpu} ${CPU_COUNT}"
  if [[ "${SWEEP_DEPTH}" != "quick" ]]; then
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
    local conc_values=()
    mapfile -t conc_values < <(dedupe_numbers \
      "$(max1 "${worker_threads}")" \
      "$(max1 $(( worker_threads * 2 )))" \
      "$(max1 $(( worker_threads * 4 )))" \
      "$(max1 $(( worker_threads * 8 )))")
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
    mapfile -t conc_values < <(dedupe_numbers \
      "$(max1 "${worker_threads}")" \
      "$(max1 $(( worker_threads * 2 )))" \
      "$(max1 $(( worker_threads * 4 )))" \
      "$(max1 $(( worker_threads * 8 )))")
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
  local label="$2"
  local io_threads="$3"
  local worker_threads="$4"
  local client_concurrency="$5"
  local client_processes="$6"
  local wrk_threads="$7"
  local output_json="$8"

  local -a cmd=(
    "${BENCH_BIN}"
    --mode "${mode}"
    --scenario "${SCENARIO}"
    --pressure-label "${label}"
    --server-io-threads "${io_threads}"
    --server-worker-threads "${worker_threads}"
    --client-concurrency "${client_concurrency}"
    --client-processes "${client_processes}"
    --wrk-threads-per-process "${wrk_threads}"
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
  while IFS=$'\t' read -r label io_threads worker_threads client_concurrency client_processes wrk_threads; do
    [[ -z "${label}" ]] && continue
    local_index=$(( local_index + 1 ))
    RUN_CELL_INDEX=$(( RUN_CELL_INDEX + 1 ))
    printf '[%s %s/%s] %s\n' "${prefix}" "${local_index}" "${total}" "${label}"
    run_cell "${mode}" "${label}" "${io_threads}" "${worker_threads}" \
      "${client_concurrency}" "${client_processes}" "${wrk_threads}" \
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
  mapfile -t generated_plots < <(
    "${PLOT_PYTHON}" "${ROOT_DIR}/scripts/benchmark_plot.py" \
      --json "${OUTPUT_DIR}/${PREFIX}.json" \
      --output-dir "${OUTPUT_DIR}" \
      --prefix "${PREFIX}"
  )
  copy_package_artifacts
  printf 'Generated plots:\n'
  printf '  %s\n' "${generated_plots[@]}"
}

prepare_command() {
  build_benchmark
  resolve_wrk_bin
  ensure_plot_python
  printf 'benchmark binary: %s\n' "${BENCH_BIN}"
  printf 'wrk binary: %s\n' "${WRK_BIN}"
  printf 'plot python: %s\n' "${PLOT_PYTHON}"
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
    "bash scripts/benchmark.sh run --scenario ${SCENARIO} --sweep-depth ${SWEEP_DEPTH}"
  emit_refined_matrix

  local refine_total
  refine_total="$(wc -l < "${TMP_DIR}/refine-only.tsv" | tr -d ' ')"
  if (( refine_total > 0 )); then
    printf 'Running fine sweep with %s additional pressure cells near peak regions\n' "${refine_total}"
    run_matrix_file local "${TMP_DIR}/refine-only.tsv" fine
  fi

  SERVER_ENV_JSON=""
  package_results local "single-host" "bash scripts/benchmark.sh run --scenario ${SCENARIO} --sweep-depth ${SWEEP_DEPTH}"
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
    "bash scripts/benchmark.sh client --scenario ${SCENARIO} --server-url ${SERVER_URL} --server-io-threads ${SERVER_IO_THREADS} --server-worker-threads ${SERVER_WORKER_THREADS} --sweep-depth ${SWEEP_DEPTH}"
  emit_refined_matrix

  local refine_total
  refine_total="$(wc -l < "${TMP_DIR}/refine-only.tsv" | tr -d ' ')"
  if (( refine_total > 0 )); then
    printf 'Running fine client-side sweep with %s additional cells near peak regions\n' "${refine_total}"
    run_matrix_file client "${TMP_DIR}/refine-only.tsv" fine
  fi

  package_results client "dual-host-manual" \
    "bash scripts/benchmark.sh client --scenario ${SCENARIO} --server-url ${SERVER_URL} --server-io-threads ${SERVER_IO_THREADS} --server-worker-threads ${SERVER_WORKER_THREADS} --sweep-depth ${SWEEP_DEPTH}"
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
      while IFS=$'\t' read -r label io_threads worker_threads client_concurrency client_processes wrk_threads; do
        [[ -z "${label}" ]] && continue
        index=$(( index + 1 ))
        printf '[ssh %s/%s] %s\n' "${index}" "${subgroup_total}" "${label}"
        run_cell client "${label}" "${io_threads}" "${worker_threads}" \
          "${client_concurrency}" "${client_processes}" "${wrk_threads}" \
          "${TMP_DIR}/cells/cell-${io_threads}-${worker_threads}-${index}.json"
      done < "${subgroup_file}"
      ssh "${ssh_extra[@]}" "${SSH_TARGET}" "kill '${remote_pid}' >/dev/null 2>&1 || true"
      sleep 1
    fi
  done < "${matrix_file}"

  package_results client "dual-host-ssh" \
    "bash scripts/benchmark.sh ssh-run --scenario ${SCENARIO} --ssh-target ${SSH_TARGET} --sweep-depth ${SWEEP_DEPTH}"
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
  *)
    echo "Unknown command: ${COMMAND}" >&2
    usage >&2
    exit 1
    ;;
esac
