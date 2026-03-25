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


def scenario_labels(cells: list[dict]) -> list[str]:
    return [f"{cell['scenario']}/{cell['pressure']}" for cell in cells]


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
        f"- client_processes: `{run_config.get('client_processes', 'unknown')}`",
        f"- wrk_threads_per_process: `{run_config.get('wrk_threads_per_process', 'unknown')}`",
        f"- repetitions: `{run_config.get('repetitions', 'unknown')}`",
        "",
        "## Scenario Summary",
        "",
        "| scenario | mean_rps | p95_us | p99_us | failure_ratio_max | stability |",
        "| --- | ---: | ---: | ---: | ---: | --- |",
    ]

    for cell in cells:
        aggregate = cell.get("aggregate", {})
        latency = aggregate.get("latency_us", {})
        failure_ratio = aggregate.get("failure_ratio", {})
        lines.append(
            "| {scenario}/{pressure} | {mean_rps:.2f} | {p95:.2f} | {p99:.2f} | {failure:.4f} | {stability} |".format(
                scenario=cell.get("scenario", "unknown"),
                pressure=cell.get("pressure", "unknown"),
                mean_rps=aggregate.get("rps", {}).get("mean", 0.0),
                p95=latency.get("p95", {}).get("mean", 0.0),
                p99=latency.get("p99", {}).get("mean", 0.0),
                failure=failure_ratio.get("max", 0.0),
                stability=aggregate.get("stability", "unknown"),
            )
        )

    lines.extend(
        [
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

    write_markdown_report(data, cells, markdown_path, rps_png, latency_png, failure_png)


if __name__ == "__main__":
    main()
