#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR="${BSRVCORE_TIDY_BUILD_DIR:-${BSRVCORE_BUILD_DIR:-build}}"
COMPILATION_DB="${BSRVCORE_TIDY_COMPILATION_DB:-}"
PARALLELISM="${BSRVCORE_TIDY_JOBS:-${BSRVCORE_BUILD_PARALLEL:-}}"

VERBOSE=0
TARGETS=()
EXCLUDES=()
EXTRA_TIDY_ARGS=()

usage() {
  cat <<'EOF'
Usage: ./scripts/tidy.sh [options] [path ...] [-- <extra clang-tidy args>]

Run clang-tidy for repository-owned code only.
If clang-tidy is unavailable, the script prints a message and exits
successfully without failing the build.

Default source roots:
  src tests examples benchmarks

Header diagnostics are limited to:
  include src tests examples benchmarks

Third-party code under build/_deps is always excluded.

Options:
  -p, --build-dir DIR   Build directory that owns compile_commands.json
      --json PATH       Path to compile_commands.json, or its parent directory
      --compdb PATH     Path to compile_commands.json, or its parent directory
  -j, --jobs N          Concurrent jobs when run-clang-tidy is available
  -e, --exclude PATH    Exclude a source file or directory (repeatable)
  -v, --verbose         Print the underlying tidy command
  -h, --help            Show this help

Environment:
  BSRVCORE_TIDY_BUILD_DIR        Override the default build dir
  BSRVCORE_TIDY_COMPILATION_DB   Path to compile_commands.json or its parent dir
  BSRVCORE_TIDY_JOBS             Override the default job count

Default compile_commands.json lookup order:
  ./compile_commands.json
  ./build/compile_commands.json
  ./${BSRVCORE_TIDY_BUILD_DIR:-build}/compile_commands.json
EOF
}

log() {
  printf '[tidy] %s\n' "$*"
}

warn() {
  printf '[tidy] %s\n' "$*" >&2
}

find_command() {
  local candidate
  for candidate in "$@"; do
    if command -v "$candidate" >/dev/null 2>&1; then
      command -v "$candidate"
      return 0
    fi
  done
  return 1
}

default_parallelism() {
  if [[ -n "${PARALLELISM}" ]]; then
    printf '%s\n' "${PARALLELISM}"
    return 0
  fi

  if command -v nproc >/dev/null 2>&1; then
    nproc
    return 0
  fi

  if command -v getconf >/dev/null 2>&1; then
    getconf _NPROCESSORS_ONLN
    return 0
  fi

  printf '1\n'
}

resolve_compilation_db() {
  local candidate

  if [[ -n "${COMPILATION_DB}" ]]; then
    candidate="${COMPILATION_DB}"
    if [[ -d "${candidate}" ]]; then
      candidate="${candidate}/compile_commands.json"
    fi
    if [[ -f "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
    warn "compile_commands.json not found at ${candidate}"
    return 1
  fi

  for candidate in \
    "${ROOT_DIR}/compile_commands.json" \
    "${ROOT_DIR}/build/compile_commands.json" \
    "${ROOT_DIR}/${BUILD_DIR}/compile_commands.json" \
    ; do
    if [[ -f "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done

  return 1
}

normalize_path() {
  local path="$1"
  if [[ "${path}" = /* ]]; then
    printf '%s\n' "${path%/}"
  else
    path="${path#./}"
    printf '%s\n' "${ROOT_DIR}/${path%/}"
  fi
}

regex_escape() {
  printf '%s\n' "$1" | sed 's/[][(){}.^$*+?|\\]/\\&/g'
}

append_prefix_pattern() {
  local -n patterns_ref="$1"
  local raw_path="$2"
  local absolute_path

  absolute_path="$(normalize_path "${raw_path}")"
  patterns_ref+=("$(regex_escape "${absolute_path}")(/|$)")
}

join_patterns() {
  local -n patterns_ref="$1"
  local joined=''
  local index

  for index in "${!patterns_ref[@]}"; do
    if [[ "${index}" -gt 0 ]]; then
      joined+='|'
    fi
    joined+="${patterns_ref[$index]}"
  done

  printf '%s\n' "${joined}"
}

is_excluded_path() {
  local absolute_path="$1"
  local excluded

  if [[ "${absolute_path}" == "${ROOT_DIR}/build/_deps/"* ]]; then
    return 0
  fi

  for excluded in "${EXCLUDES[@]}"; do
    excluded="$(normalize_path "${excluded}")"
    if [[ "${absolute_path}" == "${excluded}" || "${absolute_path}" == "${excluded}/"* ]]; then
      return 0
    fi
  done

  return 1
}

collect_source_files() {
  local search_path
  local absolute_path
  local relative_path

  for search_path in "${TARGETS[@]}"; do
    absolute_path="$(normalize_path "${search_path}")"

    if is_excluded_path "${absolute_path}"; then
      continue
    fi

    if [[ -d "${absolute_path}" ]]; then
      while IFS= read -r relative_path; do
        if [[ -n "${relative_path}" ]]; then
          printf '%s\n' "${ROOT_DIR}/${relative_path}"
        fi
      done < <(
        cd "${ROOT_DIR}" &&
          rg --files "${search_path}" \
            -g '*.c' -g '*.cc' -g '*.cpp' -g '*.cxx'
      )
      continue
    fi

    if [[ -f "${absolute_path}" ]]; then
      case "${absolute_path}" in
        *.c|*.cc|*.cpp|*.cxx)
          printf '%s\n' "${absolute_path}"
          ;;
      esac
      continue
    fi

    warn "path not found: ${search_path}"
  done
}

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    -p|--build-dir)
      BUILD_DIR="$2"
      shift
      ;;
    --compdb)
      COMPILATION_DB="$2"
      shift
      ;;
    --json)
      COMPILATION_DB="$2"
      shift
      ;;
    -j|--jobs)
      PARALLELISM="$2"
      shift
      ;;
    -e|--exclude)
      EXCLUDES+=("$2")
      shift
      ;;
    -v|--verbose)
      VERBOSE=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      EXTRA_TIDY_ARGS+=("$@")
      break
      ;;
    *)
      TARGETS+=("$1")
      ;;
  esac
  shift
done

CLANG_TIDY_BIN="$(find_command clang-tidy || true)"
if [[ -z "${CLANG_TIDY_BIN}" ]]; then
  log "clang-tidy not found on PATH; skipping."
  exit 0
fi

COMPILATION_DB_PATH="$(resolve_compilation_db || true)"
if [[ -z "${COMPILATION_DB_PATH}" ]]; then
  warn "compile_commands.json not found; checked ./, ./build and ${BUILD_DIR}."
  exit 1
fi

COMPILATION_DB_DIR="$(cd "$(dirname "${COMPILATION_DB_PATH}")" && pwd)"
PARALLELISM="$(default_parallelism)"

if [[ "${#TARGETS[@]}" -eq 0 ]]; then
  TARGETS=(src tests examples benchmarks)
fi

ROOT_REGEX="$(regex_escape "${ROOT_DIR}")"
HEADER_FILTER="^${ROOT_REGEX}/(include|src|tests|examples|benchmarks)/"
EXCLUDE_HEADER_FILTER="^${ROOT_REGEX}/build/_deps/"

SOURCE_PATTERNS=()
EXCLUDE_PATTERNS=("$(regex_escape "${ROOT_DIR}/build/_deps")(/|$)")

for target in "${TARGETS[@]}"; do
  append_prefix_pattern SOURCE_PATTERNS "${target}"
done

for excluded in "${EXCLUDES[@]}"; do
  append_prefix_pattern EXCLUDE_PATTERNS "${excluded}"
done

if [[ "${#SOURCE_PATTERNS[@]}" -eq 0 ]]; then
  log "no repository source roots selected; nothing to do."
  exit 0
fi

SOURCE_REGEX="^($(join_patterns SOURCE_PATTERNS))"
if [[ "${#EXCLUDE_PATTERNS[@]}" -gt 0 ]]; then
  SOURCE_REGEX="^(?!($(join_patterns EXCLUDE_PATTERNS)))($(join_patterns SOURCE_PATTERNS))"
fi

RUN_CLANG_TIDY_BIN="$(find_command run-clang-tidy run-clang-tidy-21 || true)"
if [[ -n "${RUN_CLANG_TIDY_BIN}" ]]; then
  CMD=(
    "${RUN_CLANG_TIDY_BIN}"
    -clang-tidy-binary "${CLANG_TIDY_BIN}"
    -p "${COMPILATION_DB_DIR}"
    -j "${PARALLELISM}"
    -header-filter "${HEADER_FILTER}"
    -exclude-header-filter "${EXCLUDE_HEADER_FILTER}"
  )
  if [[ "${#EXTRA_TIDY_ARGS[@]}" -gt 0 ]]; then
    CMD+=("${EXTRA_TIDY_ARGS[@]}")
  fi
  CMD+=("${SOURCE_REGEX}")

  if [[ "${VERBOSE}" -eq 1 ]]; then
    printf '[tidy] running:'
    printf ' %q' "${CMD[@]}"
    printf '\n'
  fi

  "${CMD[@]}"
  exit 0
fi

mapfile -t SOURCE_FILES < <(collect_source_files | awk '!seen[$0]++')

if [[ "${#SOURCE_FILES[@]}" -eq 0 ]]; then
  log "no matching source files found; nothing to do."
  exit 0
fi

for source_file in "${SOURCE_FILES[@]}"; do
  if [[ "${VERBOSE}" -eq 1 ]]; then
    printf '[tidy] running: %q %q %q %q %q' \
      "${CLANG_TIDY_BIN}" \
      -p "${COMPILATION_DB_DIR}" \
      -header-filter "${HEADER_FILTER}" \
      -exclude-header-filter "${EXCLUDE_HEADER_FILTER}" \
      "${source_file}"
    if [[ "${#EXTRA_TIDY_ARGS[@]}" -gt 0 ]]; then
      printf ' %q' "${EXTRA_TIDY_ARGS[@]}"
    fi
    printf '\n'
  fi

  "${CLANG_TIDY_BIN}" \
    -p "${COMPILATION_DB_DIR}" \
    -header-filter "${HEADER_FILTER}" \
    -exclude-header-filter "${EXCLUDE_HEADER_FILTER}" \
    "${EXTRA_TIDY_ARGS[@]}" \
    "${source_file}"
done
