# Changelog

## 0.5.1 - 2026-03-16

Urgent packaging hotfix release, especially for users installing from generated packages.

### Fixed

- Fixed broken `bsrvcore.pc` generation in packages:
  - switched to template-based generation (`src/bsrvcore.pc.in`)
  - corrected `prefix/libdir/includedir` fields
  - exported complete link flags (`-lbsrvcore -lssl -lcrypto`)
- Fixed component packaging layout:
  - ensured top-level headers are in `devel`
  - ensured `bsrvcoreConfig.cmake` and `bsrvcoreConfigVersion.cmake` are included in `devel`
- Added build/runtime guidance in README for pkg-config usage and `ldconfig`
- Made ASan opt-in for Debug builds (`BSRVCORE_ENABLE_ASAN=ON`) instead of default-on,
  to avoid ASan runtime load-order failures for downstream packaged consumers
