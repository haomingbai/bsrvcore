#!/usr/bin/env python3
"""Generate benchmark plots and a short Markdown summary from benchmark JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True)
    parser.add_argument("--markdown", required=True)
    parser.add_argument("--rps-png", required=True)
    parser.add_argument("--latency-png", required=True)
    parser.add_argument("--failure-png", required=True)
    return parser.parse_args()


def load_cells(json_path: Path) -> tuple[dict, list[dict]]:
    data = json.loads(json_path.read_text(encoding="utf-8"))
    return data, data.get("cells", [])


def cell_label(cell: dict) -> str:
    return f"{cell['scenario']}/{cell['pressure']}"


def scenario_labels(cells: list[dict]) -> list[str]:
    return [cell_label(cell) for cell in cells]


def aggregate_stat(cell: dict, metric: str, stat: str = "mean") -> float:
    return float(cell["aggregate"][metric][stat])


def latency_stat(cell: dict, percentile: str, stat: str = "mean") -> float:
    return float(cell["aggregate"]["latency_us"][percentile][stat])


def run_latency(run: dict, percentile: str) -> float:
    return float(run["latency_us"][percentile])


def markdown_table(headers: list[str], rows: list[list[str]]) -> list[str]:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    return lines


def summary_counts(cells: list[dict]) -> tuple[int, int]:
    stable = sum(1 for cell in cells if cell["aggregate"].get("stability") == "stable")
    unstable = len(cells) - stable
    return stable, unstable


def overview_lines(cells: list[dict]) -> list[str]:
    stable_count, unstable_count = summary_counts(cells)
    best_rps_cell = max(cells, key=lambda cell: aggregate_stat(cell, "rps"))
    lowest_p95_cell = min(cells, key=lambda cell: latency_stat(cell, "p95"))
    highest_p95_cell = max(cells, key=lambda cell: latency_stat(cell, "p95"))

    return [
        "## Executive Summary",
        "",
        f"- total_cells: `{len(cells)}`",
        f"- stable_cells: `{stable_count}`",
        f"- unstable_cells: `{unstable_count}`",
        (
            f"- best_throughput: `{cell_label(best_rps_cell)}` at "
            f"`{aggregate_stat(best_rps_cell, 'rps'):.2f} rps`"
        ),
        (
            f"- lowest_p95: `{cell_label(lowest_p95_cell)}` at "
            f"`{latency_stat(lowest_p95_cell, 'p95'):.2f} us`"
        ),
        (
            f"- highest_p95: `{cell_label(highest_p95_cell)}` at "
            f"`{latency_stat(highest_p95_cell, 'p95'):.2f} us`"
        ),
        "",
    ]


def stability_rule_lines() -> list[str]:
    return [
        "## Stability Rule",
        "",
        "A cell is marked `stable` only when all conditions are met:",
        "",
        "- `rps_cv <= 10%`",
        "- `p95_cv <= 15%`",
        "- `failure_ratio.max <= 5%`",
        "- `loadgen_failure_count.max == 0`",
        "",
    ]


def cell_configuration_lines(cells: list[dict]) -> list[str]:
    rows = []
    for cell in cells:
        rows.append(
            [
                cell["scenario"],
                cell["pressure"],
                str(cell["client_concurrency"]),
                str(cell["server_io_threads"]),
                str(cell["server_worker_threads"]),
                str(cell["warmup_ms"]),
                str(cell["duration_ms"]),
                str(cell["cooldown_ms"]),
                str(cell["repetitions"]),
            ]
        )

    return [
        "## Cell Configuration",
        "",
        *markdown_table(
            [
                "scenario",
                "pressure",
                "client_concurrency",
                "server_io_threads",
                "server_worker_threads",
                "warmup_ms",
                "duration_ms",
                "cooldown_ms",
                "repetitions",
            ],
            rows,
        ),
        "",
    ]


def scenario_summary_lines(cells: list[dict]) -> list[str]:
    rows = []
    for cell in cells:
        aggregate = cell["aggregate"]
        rows.append(
            [
                cell["scenario"],
                cell["pressure"],
                f"{aggregate_stat(cell, 'rps'):.2f}",
                f"{aggregate['rps']['cv']:.3f}",
                f"{latency_stat(cell, 'p50'):.2f}",
                f"{latency_stat(cell, 'p95'):.2f}",
                f"{latency_stat(cell, 'p99'):.2f}",
                f"{latency_stat(cell, 'max'):.2f}",
                f"{aggregate['failure_ratio']['max']:.4f}",
                aggregate.get("stability", "unknown"),
            ]
        )

    return [
        "## Scenario Summary",
        "",
        *markdown_table(
            [
                "scenario",
                "pressure",
                "mean_rps",
                "rps_cv",
                "p50_us",
                "p95_us",
                "p99_us",
                "max_us",
                "failure_ratio_max",
                "stability",
            ],
            rows,
        ),
        "",
    ]


def detailed_result_lines(cells: list[dict]) -> list[str]:
    lines = ["## Detailed Results", ""]

    for cell in cells:
        aggregate = cell["aggregate"]
        lines.extend(
            [
                f"### {cell_label(cell)}",
                "",
                (
                    f"- load_shape: `client_concurrency={cell['client_concurrency']}`, "
                    f"`server_io_threads={cell['server_io_threads']}`, "
                    f"`server_worker_threads={cell['server_worker_threads']}`"
                ),
                (
                    f"- throughput: mean `{aggregate_stat(cell, 'rps'):.2f} rps`, "
                    f"min `{aggregate['rps']['min']:.2f}`, max `{aggregate['rps']['max']:.2f}`, "
                    f"cv `{aggregate['rps']['cv']:.3f}`"
                ),
                (
                    f"- latency: p50 `{latency_stat(cell, 'p50'):.2f} us`, "
                    f"p95 `{latency_stat(cell, 'p95'):.2f} us`, "
                    f"p99 `{latency_stat(cell, 'p99'):.2f} us`, "
                    f"max `{latency_stat(cell, 'max'):.2f} us`"
                ),
                (
                    f"- success_path: mean_success `{aggregate_stat(cell, 'success_count'):.2f}`, "
                    f"mean_errors `{aggregate_stat(cell, 'error_count'):.2f}`, "
                    f"failure_ratio_max `{aggregate['failure_ratio']['max']:.4f}`"
                ),
                (
                    f"- bandwidth: mean `{aggregate_stat(cell, 'mib_per_sec'):.3f} MiB/s`, "
                    f"bytes_sent_mean `{aggregate_stat(cell, 'bytes_sent'):.2f}`, "
                    f"bytes_received_mean `{aggregate_stat(cell, 'bytes_received'):.2f}`"
                ),
                f"- stability: `{aggregate.get('stability', 'unknown')}`",
                "",
                *markdown_table(
                    [
                        "repetition",
                        "rps",
                        "p50_us",
                        "p95_us",
                        "p99_us",
                        "max_us",
                        "failure_ratio",
                        "success_count",
                        "error_count",
                    ],
                    [
                        [
                            str(run["repetition"]),
                            f"{float(run['rps']):.2f}",
                            f"{run_latency(run, 'p50'):.2f}",
                            f"{run_latency(run, 'p95'):.2f}",
                            f"{run_latency(run, 'p99'):.2f}",
                            f"{run_latency(run, 'max'):.2f}",
                            f"{float(run['failure_ratio']):.4f}",
                            str(run["success_count"]),
                            str(run["error_count"]),
                        ]
                        for run in cell.get("runs", [])
                    ],
                ),
                "",
            ]
        )

    return lines


def save_bar_chart(
    labels: list[str],
    values: list[float],
    title: str,
    ylabel: str,
    output: Path,
    color: str,
) -> None:
    plt.figure(figsize=(10, 5))
    plt.bar(labels, values, color=color)
    plt.title(title)
    plt.ylabel(ylabel)
    plt.xticks(rotation=25, ha="right")
    plt.tight_layout()
    plt.savefig(output)
    plt.close()


def write_markdown_report(
    data: dict,
    cells: list[dict],
    json_path: Path,
    markdown_path: Path,
    rps_png: Path,
    latency_png: Path,
    failure_png: Path,
) -> None:
    environment = data.get("environment", {})
    run_config = data.get("run_config", {})

    lines = [
        "# Benchmark Report",
        "",
        *overview_lines(cells),
        "## Environment",
        "",
        f"- timestamp_utc: `{environment.get('timestamp_utc', 'unknown')}`",
        f"- os: `{environment.get('os', 'unknown')}`",
        f"- compiler: `{environment.get('compiler', 'unknown')}`",
        f"- build_type: `{environment.get('build_type', 'unknown')}`",
        f"- logical_cpu_count: `{environment.get('logical_cpu_count', 'unknown')}`",
        "",
        "## Run Config",
        "",
        f"- scenario: `{run_config.get('scenario', 'unknown')}`",
        f"- profile: `{run_config.get('profile', 'unknown')}`",
        f"- pressure: `{run_config.get('pressure', 'unknown')}`",
        f"- warmup_ms: `{run_config.get('warmup_ms', 'unknown')}`",
        f"- duration_ms: `{run_config.get('duration_ms', 'unknown')}`",
        f"- cooldown_ms: `{run_config.get('cooldown_ms', 'unknown')}`",
        f"- client_processes: `{run_config.get('client_processes', 'unknown')}`",
        f"- wrk_threads_per_process: `{run_config.get('wrk_threads_per_process', 'unknown')}`",
        f"- repetitions: `{run_config.get('repetitions', 'unknown')}`",
        f"- wrk_bin: `{run_config.get('wrk_bin', 'unknown')}`",
        "",
        "## Artifacts",
        "",
        f"- raw_json: [{json_path.name}]({json_path.name})",
        "",
    ]

    lines.extend(
        [
            *stability_rule_lines(),
            *cell_configuration_lines(cells),
            *scenario_summary_lines(cells),
            *detailed_result_lines(cells),
            "",
            "## Plots",
            "",
            f"![Throughput]({rps_png.name})",
            "",
            f"![Latency]({latency_png.name})",
            "",
            f"![Failure Ratio]({failure_png.name})",
            "",
        ]
    )

    markdown_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    args = parse_args()
    json_path = Path(args.json)
    markdown_path = Path(args.markdown)
    rps_png = Path(args.rps_png)
    latency_png = Path(args.latency_png)
    failure_png = Path(args.failure_png)

    data, cells = load_cells(json_path)
    labels = scenario_labels(cells)

    rps_values = [cell["aggregate"]["rps"]["mean"] for cell in cells]
    p95_values = [cell["aggregate"]["latency_us"]["p95"]["mean"] for cell in cells]
    p99_values = [cell["aggregate"]["latency_us"]["p99"]["mean"] for cell in cells]
    failure_values = [
        cell["aggregate"]["failure_ratio"]["max"] for cell in cells
    ]

    save_bar_chart(
        labels,
        rps_values,
        "Mean Requests Per Second",
        "RPS",
        rps_png,
        "#1f77b4",
    )

    plt.figure(figsize=(10, 5))
    positions = list(range(len(labels)))
    plt.bar(positions, p95_values, width=0.4, label="p95", color="#ff7f0e")
    plt.bar(
        [position + 0.4 for position in positions],
        p99_values,
        width=0.4,
        label="p99",
        color="#d62728",
    )
    plt.title("Latency Percentiles")
    plt.ylabel("Microseconds")
    plt.xticks([position + 0.2 for position in positions], labels, rotation=25, ha="right")
    plt.legend()
    plt.tight_layout()
    plt.savefig(latency_png)
    plt.close()

    save_bar_chart(
        labels,
        failure_values,
        "Failure Ratio Max",
        "Failure Ratio",
        failure_png,
        "#2ca02c",
    )

    write_markdown_report(
        data,
        cells,
        json_path,
        markdown_path,
        rps_png,
        latency_png,
        failure_png,
    )


if __name__ == "__main__":
    main()
