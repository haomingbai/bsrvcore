#!/usr/bin/env python3
"""Merge per-cell benchmark JSON files into one package-friendly result set."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--summary-md", required=True)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--sweep-depth", required=True)
    parser.add_argument("--topology", required=True)
    parser.add_argument("--command-line", required=True)
    parser.add_argument("--client-env-json", required=True)
    parser.add_argument("--server-env-json")
    parser.add_argument("--server-url", default="")
    parser.add_argument("--package-dir", required=True)
    parser.add_argument("--prefix", default="benchmark-report")
    return parser.parse_args()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def metric_mean(cell: dict, metric: str) -> float:
    return float(cell["aggregate"][metric]["mean"])


def latency_mean(cell: dict, percentile: str) -> float:
    return float(cell["aggregate"]["latency_us"][percentile]["mean"])


def failure_max(cell: dict) -> float:
    return float(cell["aggregate"]["failure_ratio"]["max"])


def cell_key(cell: dict) -> str:
    return (
        f"{cell['scenario']}|{cell['pressure']}|io={cell['server_io_threads']}"
        f"|worker={cell['server_worker_threads']}|conc={cell['client_concurrency']}"
        f"|proc={cell['client_processes']}|wrk={cell['wrk_threads_per_process']}"
    )


def winner_sort_key(cell: dict) -> tuple:
    return (
        0 if cell["aggregate"].get("stability") == "stable" else 1,
        -metric_mean(cell, "rps"),
        latency_mean(cell, "p95"),
        latency_mean(cell, "p99"),
    )


def collect_cells(input_dir: Path) -> tuple[list[dict], dict]:
    cells: list[dict] = []
    first_run_config: dict[str, Any] = {}
    first_environment: dict[str, Any] = {}

    for path in sorted(input_dir.glob("cell-*.json")):
        data = load_json(path)
        run_config = data.get("run_config", {})
        if not first_run_config:
            first_run_config = run_config
        if not first_environment:
            first_environment = data.get("environment", {})

        for cell in data.get("cells", []):
            cell["client_processes"] = int(run_config.get("client_processes", 0))
            cell["wrk_threads_per_process"] = int(
                run_config.get("wrk_threads_per_process", 0)
            )
            cell["mode"] = run_config.get("mode", "local")
            cell["server_url"] = run_config.get("server_url", "")
            cell["cell_key"] = cell_key(cell)
            cells.append(cell)

    cells.sort(key=winner_sort_key)
    for rank, cell in enumerate(cells, start=1):
        cell["rank"] = rank
    return cells, {
        "run_config": first_run_config,
        "benchmark_binary_environment": first_environment,
    }


def build_sweep_space(cells: list[dict]) -> dict:
    def unique_values(key: str) -> list[Any]:
        return sorted({cell[key] for cell in cells})

    return {
        "cell_count": len(cells),
        "dimensions": {
            "scenario": unique_values("scenario"),
            "server_io_threads": unique_values("server_io_threads"),
            "server_worker_threads": unique_values("server_worker_threads"),
            "client_concurrency": unique_values("client_concurrency"),
            "client_processes": unique_values("client_processes"),
            "wrk_threads_per_process": unique_values("wrk_threads_per_process"),
        },
    }


def pick_winner(cells: list[dict]) -> dict:
    if not cells:
        raise SystemExit("no benchmark cells found")
    winner = min(cells, key=winner_sort_key)
    return {
        "cell_key": winner["cell_key"],
        "scenario": winner["scenario"],
        "pressure": winner["pressure"],
        "stability": winner["aggregate"].get("stability", "unknown"),
        "mean_rps": metric_mean(winner, "rps"),
        "p95_us": latency_mean(winner, "p95"),
        "p99_us": latency_mean(winner, "p99"),
        "failure_ratio_max": failure_max(winner),
        "server_io_threads": winner["server_io_threads"],
        "server_worker_threads": winner["server_worker_threads"],
        "client_concurrency": winner["client_concurrency"],
        "client_processes": winner["client_processes"],
        "wrk_threads_per_process": winner["wrk_threads_per_process"],
        "selection_mode": (
            "stable-first" if any(
                cell["aggregate"].get("stability") == "stable" for cell in cells
            ) else "best-effort-unstable"
        ),
    }


def markdown_table(headers: list[str], rows: list[list[str]]) -> list[str]:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    return lines


def build_summary(
    args: argparse.Namespace,
    cells: list[dict],
    winner: dict,
    client_env: dict,
    server_env: dict | None,
) -> str:
    top_rows = []
    for cell in cells[: min(10, len(cells))]:
        top_rows.append(
            [
                str(cell["rank"]),
                cell["scenario"],
                cell["pressure"],
                cell["aggregate"].get("stability", "unknown"),
                f"{metric_mean(cell, 'rps'):.2f}",
                f"{latency_mean(cell, 'p95'):.2f}",
                str(cell["server_io_threads"]),
                str(cell["server_worker_threads"]),
                str(cell["client_concurrency"]),
                str(cell["client_processes"]),
                str(cell["wrk_threads_per_process"]),
            ]
        )

    dimension_rows = [
        ["scenario", ", ".join(sorted({cell["scenario"] for cell in cells}))],
        [
            "server_io_threads",
            ", ".join(str(value) for value in sorted({cell["server_io_threads"] for cell in cells})),
        ],
        [
            "server_worker_threads",
            ", ".join(
                str(value) for value in sorted({cell["server_worker_threads"] for cell in cells})
            ),
        ],
        [
            "client_concurrency",
            ", ".join(
                str(value) for value in sorted({cell["client_concurrency"] for cell in cells})
            ),
        ],
        [
            "client_processes",
            ", ".join(
                str(value) for value in sorted({cell["client_processes"] for cell in cells})
            ),
        ],
        [
            "wrk_threads_per_process",
            ", ".join(
                str(value)
                for value in sorted({cell["wrk_threads_per_process"] for cell in cells})
            ),
        ],
    ]

    lines = [
        "# Benchmark Package Summary",
        "",
        "This package is intentionally concise. It records the benchmark facts, the winner, and the chart index.",
        "",
        "## Run",
        "",
        f"- mode: `{args.mode}`",
        f"- topology: `{args.topology}`",
        f"- scenario: `{args.scenario}`",
        f"- sweep_depth: `{args.sweep_depth}`",
        f"- server_url: `{args.server_url or 'local-started-per-cell'}`",
        f"- benchmark_command: `{args.command_line}`",
        f"- cell_count: `{len(cells)}`",
        "",
        "## Winner",
        "",
        *markdown_table(
            [
                "scenario",
                "pressure",
                "stability",
                "mean_rps",
                "p95_us",
                "p99_us",
                "server_io_threads",
                "server_worker_threads",
                "client_concurrency",
                "client_processes",
                "wrk_threads_per_process",
            ],
            [[
                winner["scenario"],
                winner["pressure"],
                winner["stability"],
                f"{winner['mean_rps']:.2f}",
                f"{winner['p95_us']:.2f}",
                f"{winner['p99_us']:.2f}",
                str(winner["server_io_threads"]),
                str(winner["server_worker_threads"]),
                str(winner["client_concurrency"]),
                str(winner["client_processes"]),
                str(winner["wrk_threads_per_process"]),
            ]],
        ),
        "",
        "## Top Cells",
        "",
        *markdown_table(
            [
                "rank",
                "scenario",
                "pressure",
                "stability",
                "mean_rps",
                "p95_us",
                "server_io_threads",
                "server_worker_threads",
                "client_concurrency",
                "client_processes",
                "wrk_threads_per_process",
            ],
            top_rows,
        ),
        "",
        "## Sweep Space",
        "",
        *markdown_table(["dimension", "values"], dimension_rows),
        "",
        "## Environment",
        "",
        f"- client_hostname: `{client_env.get('hostname', 'unknown')}`",
        f"- client_cpus: `{client_env.get('logical_cpu_count', 'unknown')}`",
        f"- client_uname: `{client_env.get('uname', 'unknown')}`",
    ]

    if server_env:
        lines.extend(
            [
                f"- server_hostname: `{server_env.get('hostname', 'unknown')}`",
                f"- server_cpus: `{server_env.get('logical_cpu_count', 'unknown')}`",
                f"- server_uname: `{server_env.get('uname', 'unknown')}`",
            ]
        )

    lines.extend(
        [
            "",
            "## Artifacts",
            "",
            f"- consolidated_json: [../{Path(args.output_json).name}](../{Path(args.output_json).name})",
            f"- technical_report: [../{args.prefix}.md](../{args.prefix}.md)",
            "- charts: read the embedded images from the technical report in the parent directory",
            "",
        ]
    )
    return "\n".join(lines) + "\n"


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir)
    package_dir = Path(args.package_dir)
    package_dir.mkdir(parents=True, exist_ok=True)

    cells, metadata = collect_cells(input_dir)
    client_env = load_json(Path(args.client_env_json))
    server_env = load_json(Path(args.server_env_json)) if args.server_env_json else None
    winner = pick_winner(cells)
    consolidated = {
        "format_version": 2,
        "environment": {
            "benchmark_binary": metadata["benchmark_binary_environment"],
            "client": client_env,
            "server": server_env or client_env,
        },
        "run_config": {
            **metadata["run_config"],
            "mode": args.mode,
            "scenario": args.scenario,
            "server_url": args.server_url,
            "topology": args.topology,
            "sweep_depth": args.sweep_depth,
            "benchmark_command": args.command_line,
        },
        "sweep_space": build_sweep_space(cells),
        "winner": winner,
        "cells": cells,
    }

    output_json = Path(args.output_json)
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(
        json.dumps(consolidated, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    Path(args.summary_md).write_text(
        build_summary(args, cells, winner, client_env, server_env),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
