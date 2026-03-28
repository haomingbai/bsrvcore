#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR="${BSRVCORE_IWYU_BUILD_DIR:-${BSRVCORE_BUILD_DIR:-build-release}}"
BUILD_TYPE="${BSRVCORE_IWYU_BUILD_TYPE:-${BSRVCORE_BUILD_TYPE:-Release}}"
BUILD_TESTS="${BSRVCORE_BUILD_TESTS:-ON}"
BUILD_EXAMPLES="${BSRVCORE_BUILD_EXAMPLES:-ON}"
BUILD_BENCHMARKS="${BSRVCORE_BUILD_BENCHMARKS:-ON}"
BUILD_TOOLS="${BSRVCORE_BUILD_TOOLS:-ON}"
PARALLELISM="${BSRVCORE_IWYU_JOBS:-${BSRVCORE_BUILD_PARALLEL:-}}"
COMPILATION_DB="${BSRVCORE_IWYU_COMPILATION_DB:-}"
CUSTOM_MAPPINGS="${BSRVCORE_IWYU_MAPPING_FILES:-}"

DRY_RUN=0
RUN_FORMAT=1
VERBOSE=0
TARGETS=()
EXCLUDES=()
EXTRA_IWYU_ARGS=()
PROTECTED_SNIPPET_FILES=()

declare -A PROTECTED_SNIPPET_BEGIN_MARKERS=()
declare -A PROTECTED_SNIPPET_END_MARKERS=()

usage() {
  cat <<'EOF'
Usage: ./scripts/iwyu.sh [options] [path ...] [-- <extra iwyu args>]

Detect include-what-you-use tooling, then apply IWYU fixes to the repository.
If IWYU is unavailable, the script prints a message and exits successfully.

Options:
  -n, --dry-run         Show the diffs from fix_includes.py without editing files
  -p, --build-dir DIR   Build directory that owns compile_commands.json
      --compdb PATH     Path to compile_commands.json, or its parent directory
  -j, --jobs N          Concurrent IWYU jobs
  -e, --exclude PATH    Exclude a source file or directory (repeatable)
      --no-format       Skip the final clang-format pass
  -v, --verbose         Print the underlying IWYU commands
  -h, --help            Show this help

Environment:
  BSRVCORE_IWYU_BUILD_DIR        Override the default build dir
  BSRVCORE_IWYU_BUILD_TYPE       Override the CMake build type used for configure
  BSRVCORE_IWYU_COMPILATION_DB   Path to compile_commands.json or its parent dir
  BSRVCORE_IWYU_JOBS             Override the default job count
  BSRVCORE_IWYU_MAPPING_FILES    Colon-separated extra IWYU mapping files

If no path is provided, the script runs on: include src tests examples benchmarks
EOF
}

log() {
  printf '[iwyu] %s\n' "$*"
}

warn() {
  printf '[iwyu] %s\n' "$*" >&2
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

append_mapping_if_exists() {
  local file_path="$1"
  if [[ -f "${file_path}" ]]; then
    MAPPING_FILES+=("${file_path}")
  fi
}

collect_protected_snippet_markers() {
  local relative_path
  local absolute_path

  while IFS= read -r relative_path; do
    if [[ -z "${relative_path}" ]]; then
      continue
    fi

    absolute_path="${ROOT_DIR}/${relative_path}"
    PROTECTED_SNIPPET_FILES+=("${absolute_path}")
    PROTECTED_SNIPPET_BEGIN_MARKERS["${absolute_path}"]="$(grep -m1 '^// BEGIN README_SNIPPET:' "${absolute_path}" || true)"
    PROTECTED_SNIPPET_END_MARKERS["${absolute_path}"]="$(grep -m1 '^// END README_SNIPPET:' "${absolute_path}" || true)"
  done < <(
    cd "${ROOT_DIR}" &&
      rg -l '^// BEGIN README_SNIPPET:' "${TARGETS[@]}" 2>/dev/null || true
  )
}

restore_protected_snippet_markers() {
  local file_path
  local begin_marker
  local end_marker
  local tmp_file

  for file_path in "${PROTECTED_SNIPPET_FILES[@]}"; do
    if [[ ! -f "${file_path}" ]]; then
      continue
    fi

    begin_marker="${PROTECTED_SNIPPET_BEGIN_MARKERS[${file_path}]:-}"
    end_marker="${PROTECTED_SNIPPET_END_MARKERS[${file_path}]:-}"

    if [[ -n "${begin_marker}" ]] && ! grep -qFx "${begin_marker}" "${file_path}"; then
      tmp_file="$(mktemp "${TMPDIR:-/tmp}/bsrvcore-iwyu-marker.XXXXXX")"
      awk -v marker="${begin_marker}" '
        BEGIN {
          inserted = 0
        }
        !inserted && /^#include / {
          print marker
          inserted = 1
        }
        {
          print
        }
        END {
          if (!inserted) {
            print marker
          }
        }
      ' "${file_path}" > "${tmp_file}"
      mv "${tmp_file}" "${file_path}"
    fi

    if [[ -n "${end_marker}" ]] && ! grep -qFx "${end_marker}" "${file_path}"; then
      printf '%s\n' "${end_marker}" >> "${file_path}"
    fi
  done
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
    "${ROOT_DIR}/${BUILD_DIR}/compile_commands.json" \
    "${ROOT_DIR}/build-release/compile_commands.json" \
    "${ROOT_DIR}/build/compile_commands.json" \
    "${ROOT_DIR}/cmake-build-debug/compile_commands.json" \
    "${ROOT_DIR}/build-clang/compile_commands.json"; do
    if [[ -f "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done

  return 1
}

configure_compilation_db() {
  log "compile_commands.json not found; configuring CMake in ${BUILD_DIR}"

  cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBSRVCORE_BUILD_TESTS="${BUILD_TESTS}" \
    -DBSRVCORE_BUILD_EXAMPLES="${BUILD_EXAMPLES}" \
    -DBSRVCORE_BUILD_BENCHMARKS="${BUILD_BENCHMARKS}" \
    -DBSRVCORE_BUILD_TOOLS="${BUILD_TOOLS}"
}

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    -n|--dry-run)
      DRY_RUN=1
      ;;
    -p|--build-dir)
      BUILD_DIR="$2"
      shift
      ;;
    --compdb)
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
    --no-format)
      RUN_FORMAT=0
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
      EXTRA_IWYU_ARGS+=("$@")
      break
      ;;
    *)
      TARGETS+=("$1")
      ;;
  esac
  shift
done

PARALLELISM="$(default_parallelism)"

IWYU_BIN="$(find_command include-what-you-use iwyu || true)"
if [[ -z "${IWYU_BIN}" ]]; then
  log "include-what-you-use not found on PATH; skipping."
  exit 0
fi

IWYU_TOOL="$(find_command iwyu_tool.py iwyu-tool iwyu_tool || true)"
FIX_INCLUDES="$(find_command fix_includes.py fix_includes || true)"

if [[ -z "${IWYU_TOOL}" || -z "${FIX_INCLUDES}" ]]; then
  log "IWYU helper tools not found (need iwyu_tool.py and fix_includes.py); skipping."
  exit 0
fi

COMPDB_PATH="$(resolve_compilation_db || true)"
if [[ -z "${COMPDB_PATH}" ]]; then
  configure_compilation_db
  COMPDB_PATH="$(resolve_compilation_db)"
fi

BUILD_PATH="$(dirname "${COMPDB_PATH}")"

if [[ "${#TARGETS[@]}" -eq 0 ]]; then
  for path in include src tests examples benchmarks; do
    if [[ -e "${ROOT_DIR}/${path}" ]]; then
      TARGETS+=("${path}")
    fi
  done
fi

if [[ "${#TARGETS[@]}" -eq 0 ]]; then
  log "No source directories found; nothing to do."
  exit 0
fi

collect_protected_snippet_markers

declare -a MAPPING_FILES=()

append_mapping_if_exists "/usr/share/include-what-you-use/boost-1.75-all.imp"
append_mapping_if_exists "/usr/share/include-what-you-use/boost-1.75-all-private.imp"

if [[ "${#MAPPING_FILES[@]}" -eq 0 ]]; then
  append_mapping_if_exists "/usr/share/include-what-you-use/boost-all.imp"
  append_mapping_if_exists "/usr/share/include-what-you-use/boost-all-private.imp"
fi

if [[ -n "${CUSTOM_MAPPINGS}" ]]; then
  IFS=':' read -r -a extra_mapping_files <<< "${CUSTOM_MAPPINGS}"
  for mapping_file in "${extra_mapping_files[@]}"; do
    if [[ -f "${mapping_file}" ]]; then
      MAPPING_FILES+=("${mapping_file}")
    else
      warn "Skipping missing mapping file: ${mapping_file}"
    fi
  done
fi

declare -a IWYU_FORWARD_ARGS=()
for mapping_file in "${MAPPING_FILES[@]}"; do
  IWYU_FORWARD_ARGS+=("-Xiwyu" "--mapping_file=${mapping_file}")
done
IWYU_FORWARD_ARGS+=("-Xiwyu" "--quoted_includes_first")
IWYU_FORWARD_ARGS+=("${EXTRA_IWYU_ARGS[@]}")

declare -a IWYU_CMD=("${IWYU_TOOL}" -p "${BUILD_PATH}" -j "${PARALLELISM}")
if [[ "${VERBOSE}" -eq 1 ]]; then
  IWYU_CMD+=(-v)
fi
for excluded_path in "${EXCLUDES[@]}"; do
  IWYU_CMD+=(-e "${excluded_path}")
done
IWYU_CMD+=("${TARGETS[@]}")
IWYU_CMD+=(-- "${IWYU_FORWARD_ARGS[@]}")

TMP_OUTPUT="$(mktemp "${TMPDIR:-/tmp}/bsrvcore-iwyu.XXXXXX")"
trap 'rm -f "${TMP_OUTPUT}"' EXIT

log "Using compilation database: ${COMPDB_PATH}"
log "Using include-what-you-use: ${IWYU_BIN}"
if [[ "${#MAPPING_FILES[@]}" -gt 0 ]]; then
  log "Using ${#MAPPING_FILES[@]} IWYU mapping file(s)."
fi
if [[ "${#PROTECTED_SNIPPET_FILES[@]}" -gt 0 ]]; then
  log "Protecting README snippet markers in ${#PROTECTED_SNIPPET_FILES[@]} file(s)."
fi

(
  cd "${ROOT_DIR}"
  "${IWYU_CMD[@]}"
) | tee "${TMP_OUTPUT}"

declare -a FIX_CMD=(
  "${FIX_INCLUDES}"
  --nosafe_headers
  --reorder
  --quoted_includes_first
  --separate_project_includes
  bsrvcore
  --basedir
  "${ROOT_DIR}"
)

if [[ "${DRY_RUN}" -eq 1 ]]; then
  FIX_CMD+=(-n)
  log "Applying include fixes (dry run)."
else
  log "Applying include fixes."
fi
"${FIX_CMD[@]}" < "${TMP_OUTPUT}"

if [[ "${DRY_RUN}" -eq 1 ]]; then
  exit 0
fi

restore_protected_snippet_markers

if [[ "${RUN_FORMAT}" -eq 1 ]]; then
  if command -v clang-format >/dev/null 2>&1; then
    log "Running clang-format across tracked source files."
    bash "${ROOT_DIR}/scripts/format.sh"
  else
    warn "clang-format not found; skipping the final format pass."
  fi
fi
