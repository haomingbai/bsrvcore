#!/usr/bin/env python3
"""Render focused body-size benchmark charts and a compact report manifest."""

from __future__ import annotations

import argparse
import json
import math
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib import colors


LINE_COLORS = [
    "#1d4ed8",
    "#d97706",
    "#059669",
    "#dc2626",
    "#7c3aed",
    "#0891b2",
    "#4b5563",
    "#be123c",
    "#0f766e",
    "#7c2d12",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mainline-json", required=True)
    parser.add_argument("--get-json", required=True)
    parser.add_argument("--post-long-dir", required=True)
    parser.add_argument("--post-short-dir", required=True)
    parser.add_argument("--post-confirm-dir")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--prefix", default="benchmark-report")
    return parser.parse_args()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def kib_label(size_bytes: int) -> str:
    if size_bytes == 0:
        return "0"
    if size_bytes % 1024 == 0:
        return f"{size_bytes // 1024} KiB"
    return f"{size_bytes / 1024.0:.1f} KiB"


def stable_marker(stability: str) -> str:
    return "o" if stability == "stable" else "X"


def finalize_figure(figure: plt.Figure, top: float = 0.965) -> None:
    figure.tight_layout(rect=(0.0, 0.0, 1.0, top))


def load_consolidated_rows(json_path: Path) -> list[dict]:
    data = load_json(json_path)
    rows: list[dict] = []
    for cell in data.get("cells", []):
        aggregate = cell.get("aggregate", {})
        latency = aggregate.get("latency_us", {})
        rows.append(
            {
                "cell_key": cell.get("cell_key", ""),
                "client_concurrency": int(cell.get("client_concurrency", 0)),
                "request_body_bytes": int(cell.get("request_body_bytes", 0)),
                "response_body_bytes": int(cell.get("response_body_bytes", 0)),
                "mean_rps": float(aggregate.get("rps", {}).get("mean", 0.0)),
                "per_connection_rps": float(
                    cell.get("derived", {}).get("per_connection_rps", 0.0)
                ),
                "p95_us": float(latency.get("p95", {}).get("mean", 0.0)),
                "p99_us": float(latency.get("p99", {}).get("mean", 0.0)),
                "stability": str(aggregate.get("stability", "unknown")),
            }
        )
    return rows


def load_probe_rows(directory: Path) -> tuple[list[dict], set[tuple[int, int]]]:
    rows: list[dict] = []
    for path in sorted(directory.glob("*.json")):
        if path.name.endswith("-env.json"):
            continue
        payload = load_json(path)
        cells = payload.get("cells", [])
        if not cells:
            continue
        cell = cells[0]
        aggregate = cell.get("aggregate", {})
        latency = aggregate.get("latency_us", {})
        rows.append(
            {
                "path": str(path),
                "client_concurrency": int(cell.get("client_concurrency", 0)),
                "request_body_bytes": int(cell.get("request_body_bytes", 0)),
                "response_body_bytes": int(cell.get("response_body_bytes", 0)),
                "mean_rps": float(aggregate.get("rps", {}).get("mean", 0.0)),
                "per_connection_rps": float(aggregate.get("rps", {}).get("mean", 0.0))
                / max(1, int(cell.get("client_concurrency", 1))),
                "p95_us": float(latency.get("p95", {}).get("mean", 0.0)),
                "p99_us": float(latency.get("p99", {}).get("mean", 0.0)),
                "cv_rps": float(aggregate.get("rps", {}).get("cv", 0.0)),
                "cv_p95": float(latency.get("p95", {}).get("cv", 0.0)),
                "loadgen_failure_max": float(
                    aggregate.get("loadgen_failure_count", {}).get("max", 0.0)
                ),
                "timeout_error_max": float(
                    aggregate.get("socket_timeout_error_count", {}).get("max", 0.0)
                ),
                "stability": str(aggregate.get("stability", "unknown")),
            }
        )

    failures: set[tuple[int, int]] = set()
    failure_path = directory / "failures.tsv"
    if failure_path.exists():
        for line in failure_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) != 2:
                continue
            failures.add((int(parts[0]), int(parts[1])))

    return rows, failures


def rows_to_map(rows: list[dict]) -> dict[tuple[int, int], dict]:
    return {
        (row["client_concurrency"], row["request_body_bytes"]): row for row in rows
    }


def plot_get_curves(rows: list[dict], output: Path) -> None:
    by_size: dict[int, list[dict]] = defaultdict(list)
    for row in rows:
        by_size[row["response_body_bytes"]].append(row)
    for series in by_size.values():
        series.sort(key=lambda row: row["client_concurrency"])

    figure, axes = plt.subplots(2, 1, figsize=(12, 9), sharex=True)
    for index, size in enumerate(sorted(by_size)):
        series = by_size[size]
        x = [row["client_concurrency"] for row in series]
        y_rps = [row["mean_rps"] for row in series]
        y_per_conn = [row["per_connection_rps"] for row in series]
        color = LINE_COLORS[index % len(LINE_COLORS)]
        label = kib_label(size)
        axes[0].plot(x, y_rps, color=color, linewidth=1.7, marker="o", label=label)
        axes[1].plot(
            x,
            y_per_conn,
            color=color,
            linewidth=1.7,
            marker="o",
            label=label,
        )

    axes[0].set_title("GET Response Size Sweep: throughput vs concurrency", pad=12)
    axes[0].set_ylabel("Mean RPS")
    axes[0].grid(alpha=0.25)
    axes[0].legend(
        loc="center left",
        bbox_to_anchor=(1.02, 0.5),
        title="Response size",
    )

    axes[1].set_ylabel("Mean RPS / connection")
    axes[1].set_xlabel("Client concurrency")
    axes[1].grid(alpha=0.25)

    finalize_figure(figure, top=0.95)
    figure.savefig(output, bbox_inches="tight")
    plt.close(figure)


def plot_get_size_slices(rows: list[dict], output: Path) -> None:
    interesting_concurrency = [64, 96, 128, 160]
    by_concurrency: dict[int, list[dict]] = defaultdict(list)
    for row in rows:
        if row["client_concurrency"] in interesting_concurrency:
            by_concurrency[row["client_concurrency"]].append(row)
    for series in by_concurrency.values():
        series.sort(key=lambda row: row["response_body_bytes"])

    figure, axes = plt.subplots(2, 1, figsize=(11, 8.5), sharex=True)
    for index, conc in enumerate(sorted(by_concurrency)):
        series = by_concurrency[conc]
        x = [row["response_body_bytes"] / 1024.0 for row in series]
        y_rps = [row["mean_rps"] for row in series]
        y_p95 = [row["p95_us"] for row in series]
        color = LINE_COLORS[index % len(LINE_COLORS)]
        label = f"conc={conc}"
        axes[0].plot(x, y_rps, color=color, linewidth=1.8, marker="o", label=label)
        axes[1].plot(x, y_p95, color=color, linewidth=1.8, marker="o", label=label)

    axes[0].set_title("GET Response Size Sweep: fixed-concurrency slices", pad=12)
    axes[0].set_ylabel("Mean RPS")
    axes[0].grid(alpha=0.25)
    axes[0].legend(loc="best")

    axes[1].set_ylabel("p95 latency (us)")
    axes[1].set_xlabel("Response body size (KiB)")
    axes[1].grid(alpha=0.25)

    finalize_figure(figure)
    figure.savefig(output)
    plt.close(figure)


def annotate_status(
    ax: plt.Axes,
    x_values: list[int],
    y_values: list[int],
    row_map: dict[tuple[int, int], dict],
    failures: set[tuple[int, int]],
) -> None:
    for x_index, conc in enumerate(x_values):
        for y_index, req in enumerate(y_values):
            key = (conc, req)
            if key in failures:
                label = "T"
                color = "#ffffff"
            elif key in row_map:
                label = "S" if row_map[key]["stability"] == "stable" else "U"
                color = "#ffffff"
            else:
                label = ""
                color = "#111827"
            if label:
                ax.text(
                    x_index,
                    y_index,
                    label,
                    ha="center",
                    va="center",
                    fontsize=9,
                    fontweight="bold",
                    color=color,
                )


def build_matrix(
    x_values: list[int],
    y_values: list[int],
    row_map: dict[tuple[int, int], dict],
    failures: set[tuple[int, int]],
    field: str,
) -> list[list[float]]:
    matrix: list[list[float]] = []
    for req in y_values:
        row_values: list[float] = []
        for conc in x_values:
            key = (conc, req)
            if key in failures:
                row_values.append(math.nan)
            elif key in row_map:
                row_values.append(float(row_map[key][field]))
            else:
                row_values.append(math.nan)
        matrix.append(row_values)
    return matrix


def plot_post_heatmaps(
    rows: list[dict],
    failures: set[tuple[int, int]],
    output: Path,
    title: str,
) -> None:
    row_map = rows_to_map(rows)
    x_values = sorted({row["client_concurrency"] for row in rows} | {key[0] for key in failures})
    y_values = sorted({row["request_body_bytes"] for row in rows} | {key[1] for key in failures})
    if not x_values or not y_values:
        return

    rps_matrix = build_matrix(x_values, y_values, row_map, failures, "mean_rps")
    p95_matrix = build_matrix(x_values, y_values, row_map, failures, "p95_us")
    valid_rps = [value for row in rps_matrix for value in row if not math.isnan(value) and value > 0.0]
    valid_p95 = [value for row in p95_matrix for value in row if not math.isnan(value) and value > 0.0]
    if not valid_rps or not valid_p95:
        return

    figure, axes = plt.subplots(1, 2, figsize=(13, 5.2), sharey=True)
    figure.suptitle(title, y=0.99)

    rps_image = axes[0].imshow(
        rps_matrix,
        aspect="auto",
        cmap="viridis",
        norm=colors.LogNorm(vmin=min(valid_rps), vmax=max(valid_rps)),
    )
    axes[0].set_title("Mean RPS")
    axes[0].set_xticks(range(len(x_values)))
    axes[0].set_xticklabels(x_values)
    axes[0].set_yticks(range(len(y_values)))
    axes[0].set_yticklabels([kib_label(req) for req in y_values])
    axes[0].set_xlabel("Client concurrency")
    axes[0].set_ylabel("Request body size")
    annotate_status(axes[0], x_values, y_values, row_map, failures)
    figure.colorbar(rps_image, ax=axes[0], shrink=0.88)

    p95_image = axes[1].imshow(
        p95_matrix,
        aspect="auto",
        cmap="magma",
        norm=colors.Normalize(vmin=min(valid_p95), vmax=max(valid_p95)),
    )
    axes[1].set_title("p95 latency (us)")
    axes[1].set_xticks(range(len(x_values)))
    axes[1].set_xticklabels(x_values)
    axes[1].set_yticks(range(len(y_values)))
    axes[1].set_yticklabels([kib_label(req) for req in y_values])
    axes[1].set_xlabel("Client concurrency")
    annotate_status(axes[1], x_values, y_values, row_map, failures)
    figure.colorbar(p95_image, ax=axes[1], shrink=0.88)

    finalize_figure(figure, top=0.92)
    figure.savefig(output)
    plt.close(figure)


def plot_post_frontier(
    rows: list[dict],
    failures: set[tuple[int, int]],
    output: Path,
    title: str,
) -> None:
    by_request: dict[int, list[dict]] = defaultdict(list)
    for row in rows:
        by_request[row["request_body_bytes"]].append(row)

    request_sizes = sorted(set(by_request) | {req for _, req in failures})
    stable_points_x: list[int] = []
    stable_points_y: list[int] = []
    unstable_points_x: list[int] = []
    unstable_points_y: list[int] = []
    timeout_points_x: list[int] = []
    timeout_points_y: list[int] = []
    highest_stable: list[tuple[float, float]] = []

    for req in request_sizes:
        series = sorted(by_request.get(req, []), key=lambda row: row["client_concurrency"])
        stable_concurrency = [
            row["client_concurrency"] for row in series if row["stability"] == "stable"
        ]
        if stable_concurrency:
            highest_stable.append((req / 1024.0, max(stable_concurrency)))
        for row in series:
            if row["stability"] == "stable":
                stable_points_x.append(row["request_body_bytes"] / 1024.0)
                stable_points_y.append(row["client_concurrency"])
            else:
                unstable_points_x.append(row["request_body_bytes"] / 1024.0)
                unstable_points_y.append(row["client_concurrency"])

    for conc, req in sorted(failures):
        timeout_points_x.append(req / 1024.0)
        timeout_points_y.append(conc)

    figure, ax = plt.subplots(figsize=(10.5, 5.5))
    ax.set_title(title, pad=12)
    if stable_points_x:
        ax.scatter(
            stable_points_x,
            stable_points_y,
            s=80,
            color="#059669",
            marker="o",
            label="stable point",
        )
    if unstable_points_x:
        ax.scatter(
            unstable_points_x,
            unstable_points_y,
            s=90,
            color="#d97706",
            marker="X",
            label="unstable point",
        )
    if timeout_points_x:
        ax.scatter(
            timeout_points_x,
            timeout_points_y,
            s=90,
            color="#dc2626",
            marker="s",
            label="probe timeout / failed",
        )
    if highest_stable:
        highest_stable.sort()
        ax.plot(
            [point[0] for point in highest_stable],
            [point[1] for point in highest_stable],
            color="#111827",
            linewidth=1.4,
            linestyle="--",
            label="highest stable concurrency",
        )

    ax.set_xlabel("Request body size (KiB)")
    ax.set_ylabel("Client concurrency")
    ax.grid(alpha=0.25)
    ax.legend(loc="best")

    finalize_figure(figure)
    figure.savefig(output)
    plt.close(figure)


def write_manifest(
    output: Path,
    mainline_path: Path,
    get_path: Path,
    post_long_dir: Path,
    post_short_dir: Path,
    post_confirm_dir: Path | None,
    mainline_data: dict,
    get_rows: list[dict],
    post_long_rows: list[dict],
    post_long_failures: set[tuple[int, int]],
    post_short_rows: list[dict],
    post_short_failures: set[tuple[int, int]],
    post_confirm_rows: list[dict],
) -> None:
    get_at_160 = [
        {
            "response_body_bytes": row["response_body_bytes"],
            "mean_rps": row["mean_rps"],
            "per_connection_rps": row["per_connection_rps"],
            "p95_us": row["p95_us"],
            "stability": row["stability"],
        }
        for row in sorted(
            (
                row for row in get_rows if row["client_concurrency"] == 160
            ),
            key=lambda row: row["response_body_bytes"],
        )
    ]

    manifest = {
        "sources": {
            "mainline_json": str(mainline_path),
            "get_json": str(get_path),
            "post_long_dir": str(post_long_dir),
            "post_short_dir": str(post_short_dir),
            "post_confirm_dir": str(post_confirm_dir) if post_confirm_dir else "",
        },
        "mainline_winner": mainline_data.get("winner", {}),
        "get_response_at_conc_160": get_at_160,
        "post_long_probe_points": sorted(
            post_long_rows,
            key=lambda row: (row["client_concurrency"], row["request_body_bytes"]),
        ),
        "post_long_failures": [
            {"client_concurrency": conc, "request_body_bytes": req}
            for conc, req in sorted(post_long_failures)
        ],
        "post_short_probe_points": sorted(
            post_short_rows,
            key=lambda row: (row["client_concurrency"], row["request_body_bytes"]),
        ),
        "post_short_failures": [
            {"client_concurrency": conc, "request_body_bytes": req}
            for conc, req in sorted(post_short_failures)
        ],
        "post_confirm_probe_points": sorted(
            post_confirm_rows,
            key=lambda row: (row["client_concurrency"], row["request_body_bytes"]),
        ),
    }
    output.write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def main() -> None:
    args = parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    mainline_path = Path(args.mainline_json)
    get_path = Path(args.get_json)
    post_long_dir = Path(args.post_long_dir)
    post_short_dir = Path(args.post_short_dir)
    post_confirm_dir = Path(args.post_confirm_dir) if args.post_confirm_dir else None

    mainline_data = load_json(mainline_path)
    get_rows = load_consolidated_rows(get_path)
    post_long_rows, post_long_failures = load_probe_rows(post_long_dir)
    post_short_rows, post_short_failures = load_probe_rows(post_short_dir)
    post_confirm_rows, _ = (
        load_probe_rows(post_confirm_dir) if post_confirm_dir else ([], set())
    )

    plot_get_curves(
        get_rows, output_dir / f"{args.prefix}-body-get-response-curves.png"
    )
    plot_get_size_slices(
        get_rows, output_dir / f"{args.prefix}-body-get-response-slices.png"
    )
    plot_post_heatmaps(
        post_long_rows,
        post_long_failures,
        output_dir / f"{args.prefix}-body-post-long-heatmap.png",
        "POST request-size sweep: long-window probes",
    )
    plot_post_frontier(
        post_long_rows,
        post_long_failures,
        output_dir / f"{args.prefix}-body-post-long-frontier.png",
        "POST request-size sweep: long-window stability frontier",
    )
    plot_post_heatmaps(
        post_short_rows,
        post_short_failures,
        output_dir / f"{args.prefix}-body-post-short-heatmap.png",
        "POST request-size sweep: short-window map",
    )
    write_manifest(
        output_dir / f"{args.prefix}-body-summary.json",
        mainline_path,
        get_path,
        post_long_dir,
        post_short_dir,
        post_confirm_dir,
        mainline_data,
        get_rows,
        post_long_rows,
        post_long_failures,
        post_short_rows,
        post_short_failures,
        post_confirm_rows,
    )


if __name__ == "__main__":
    main()
