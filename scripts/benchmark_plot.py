#!/usr/bin/env python3
"""Generate curated benchmark charts from a consolidated benchmark JSON file."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.axes import Axes


COLORS = [
    "#1d4ed8",
    "#d97706",
    "#059669",
    "#dc2626",
    "#7c3aed",
    "#0891b2",
]


def finalize_figure(figure: plt.Figure, top: float = 0.965) -> None:
    """Reserve extra headroom so long chart titles do not overlap plot text."""
    figure.tight_layout(rect=(0.0, 0.0, 1.0, top))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--prefix", default="benchmark-report")
    return parser.parse_args()


def load_data(json_path: Path) -> dict:
    return json.loads(json_path.read_text(encoding="utf-8"))


def build_cell_key(cell: dict) -> str:
    return (
        f"{cell['scenario']}|{cell['pressure']}|io={cell['server_io_threads']}"
        f"|worker={cell['server_worker_threads']}|conc={cell['client_concurrency']}"
        f"|proc={cell['client_processes']}|wrk={cell['wrk_threads_per_process']}"
        f"|method={cell.get('http_method', 'GET')}"
        f"|req={int(cell.get('request_body_bytes', 0))}"
        f"|resp={int(cell.get('response_body_bytes', 0))}"
    )


def derive_rows(data: dict) -> list[dict]:
    rows: list[dict] = []
    for cell in data.get("cells", []):
        aggregate = cell.get("aggregate", {})
        latency = aggregate.get("latency_us", {})
        rows.append(
            {
                "cell_key": cell.get("cell_key", build_cell_key(cell)),
                "scenario": cell.get("scenario", "unknown"),
                "pressure": cell.get("pressure", "unknown"),
                "server_io_threads": int(cell.get("server_io_threads", 0)),
                "server_worker_threads": int(cell.get("server_worker_threads", 0)),
                "client_concurrency": int(cell.get("client_concurrency", 0)),
                "client_processes": int(cell.get("client_processes", 0)),
                "wrk_threads_per_process": int(
                    cell.get("wrk_threads_per_process", 0)
                ),
                "http_method": str(cell.get("http_method", "GET")),
                "request_body_bytes": int(cell.get("request_body_bytes", 0)),
                "response_body_bytes": int(cell.get("response_body_bytes", 0)),
                "mean_rps": float(aggregate.get("rps", {}).get("mean", 0.0)),
                "per_connection_rps": float(aggregate.get("rps", {}).get("mean", 0.0))
                / max(1, int(cell.get("client_concurrency", 0) or 1)),
                "p95_us": float(latency.get("p95", {}).get("mean", 0.0)),
                "p99_us": float(latency.get("p99", {}).get("mean", 0.0)),
                "failure_ratio_max": float(
                    aggregate.get("failure_ratio", {}).get("max", 0.0)
                ),
                "stability": aggregate.get("stability", "unknown"),
            }
        )
    rows.sort(
        key=lambda row: (
            row["scenario"],
            row["server_io_threads"],
            row["server_worker_threads"],
            row["client_concurrency"],
            row["client_processes"],
            row["wrk_threads_per_process"],
            row["http_method"],
            row["request_body_bytes"],
            row["response_body_bytes"],
        )
    )
    return rows


def group_key(row: dict) -> tuple:
    if row["scenario"].startswith("http_body_matrix_"):
        return (
            row["scenario"],
            row["server_io_threads"],
            row["server_worker_threads"],
            -1,
            -1,
            row["http_method"],
            row["request_body_bytes"],
            row["response_body_bytes"],
        )
    return (
        row["scenario"],
        row["server_io_threads"],
        row["server_worker_threads"],
        row["client_processes"],
        row["wrk_threads_per_process"],
        row["http_method"],
        row["request_body_bytes"],
        row["response_body_bytes"],
    )


def line_label(key: tuple, note: str = "") -> str:
    (
        scenario,
        io_threads,
        worker_threads,
        client_processes,
        wrk_threads,
        http_method,
        request_body_bytes,
        response_body_bytes,
    ) = key
    loadgen_label = (
        "adaptive loadgen"
        if client_processes < 0 or wrk_threads < 0
        else f"proc={client_processes} wrk={wrk_threads}"
    )
    label = (
        f"io={io_threads} worker={worker_threads} "
        f"{loadgen_label} {http_method} "
        f"req={request_body_bytes} resp={response_body_bytes}"
    )
    if note:
        return f"{label} ({note})"
    return label


def winner_row(rows: list[dict], data: dict) -> dict:
    winner_key = data.get("winner", {}).get("cell_key")
    for row in rows:
        if row["cell_key"] == winner_key:
            return row
    raise SystemExit("winner cell is missing from the consolidated JSON")


def group_rows(rows: list[dict]) -> dict[tuple, list[dict]]:
    groups: dict[tuple, list[dict]] = defaultdict(list)
    for row in rows:
        groups[group_key(row)].append(row)
    for series in groups.values():
        series.sort(key=lambda row: row["client_concurrency"])
    return groups


def max_rps(series: list[dict]) -> float:
    return max(row["mean_rps"] for row in series)


def unique_concurrency_count(series: list[dict]) -> int:
    return len({row["client_concurrency"] for row in series})


def stable_marker(row: dict) -> str:
    return "o" if row["stability"] == "stable" else "X"


def plot_metric_series(
    ax: Axes,
    series: list[dict],
    y_key: str,
    color: str,
    label: str,
    winner_cell_key: str,
) -> None:
    ordered = sorted(series, key=lambda row: row["client_concurrency"])
    x = [row["client_concurrency"] for row in ordered]
    y = [row[y_key] for row in ordered]
    ax.plot(x, y, color=color, linewidth=1.6, marker="o", label=label)
    for row in ordered:
        marker_size = 70 if row["cell_key"] == winner_cell_key else 36
        edge = "#111827" if row["cell_key"] == winner_cell_key else color
        ax.scatter(
            row["client_concurrency"],
            row[y_key],
            color=color,
            marker=stable_marker(row),
            s=marker_size,
            edgecolors=edge,
            linewidths=1.0,
            zorder=4,
        )


def representative_groups(
    rows: list[dict], winner: dict
) -> list[tuple[tuple, list[dict], str]]:
    groups = group_rows(rows)
    winner_group_key = group_key(winner)
    reference_key = reference_family_key(rows, winner)

    picked: list[tuple[tuple, list[dict], str]] = []
    used: set[tuple] = set()

    def add_group(key: tuple | None, note: str) -> None:
        if key is None or key in used:
            return
        series = groups.get(key)
        if not series or unique_concurrency_count(series) < 3:
            return
        picked.append((key, series, note))
        used.add(key)

    baseline_candidates = [
        key
        for key, series in groups.items()
        if unique_concurrency_count(series) >= 3
    ]
    baseline_key = min(
        baseline_candidates,
        key=lambda key: (key[1], key[2], key[3], key[4], -max_rps(groups[key])),
        default=None,
    )
    add_group(baseline_key, "baseline")

    if reference_key == winner_group_key:
        add_group(winner_group_key, "winner family")
    else:
        add_group(reference_key, "near-peak family")

    same_server_candidates = sorted(
        (
            key
            for key, series in groups.items()
            if key[0] == winner["scenario"]
            and key[1] == winner["server_io_threads"]
            and key[2] == winner["server_worker_threads"]
            and key != winner_group_key
            and unique_concurrency_count(series) >= 3
        ),
        key=lambda key: max_rps(groups[key]),
        reverse=True,
    )
    add_group(same_server_candidates[0] if same_server_candidates else None, "same server")

    high_thread_candidates = sorted(
        (
            key
            for key, series in groups.items()
            if key[0] == winner["scenario"]
            and key != winner_group_key
            and unique_concurrency_count(series) >= 3
            and (
                key[1] > winner["server_io_threads"]
                or key[2] > winner["server_worker_threads"]
            )
        ),
        key=lambda key: (max_rps(groups[key]), key[2], key[1]),
        reverse=True,
    )
    add_group(high_thread_candidates[0] if high_thread_candidates else None, "higher threads")

    return picked[:4]


def reference_family_key(rows: list[dict], winner: dict) -> tuple | None:
    groups = group_rows(rows)
    winner_key = group_key(winner)
    if unique_concurrency_count(groups.get(winner_key, [])) >= 4:
        return winner_key

    same_shape_candidates = [
        key
        for key, series in groups.items()
        if key[0] == winner["scenario"]
        and key[3] == winner["client_processes"]
        and key[4] == winner["wrk_threads_per_process"]
        and unique_concurrency_count(series) >= 4
    ]
    if same_shape_candidates:
        return max(same_shape_candidates, key=lambda key: max_rps(groups[key]))

    full_curve_candidates = [
        key
        for key, series in groups.items()
        if key[0] == winner["scenario"] and unique_concurrency_count(series) >= 4
    ]
    if full_curve_candidates:
        return max(full_curve_candidates, key=lambda key: max_rps(groups[key]))
    return None


def reference_server_pair(rows: list[dict], winner: dict) -> tuple[int, int] | None:
    counts: dict[tuple[int, int], list[dict]] = defaultdict(list)
    for row in rows:
        if row["scenario"] != winner["scenario"]:
            continue
        counts[(row["server_io_threads"], row["server_worker_threads"])].append(row)

    def shape_count(pair: tuple[int, int]) -> int:
        pair_rows = counts[pair]
        by_shape: dict[tuple[int, int], set[int]] = defaultdict(set)
        for row in pair_rows:
            by_shape[(row["client_processes"], row["wrk_threads_per_process"])].add(
                row["client_concurrency"]
            )
        return sum(1 for points in by_shape.values() if len(points) >= 3)

    winner_pair = (winner["server_io_threads"], winner["server_worker_threads"])
    if shape_count(winner_pair) >= 2:
        return winner_pair

    candidates = [
        pair for pair in counts if shape_count(pair) >= 2
    ]
    if not candidates:
        return None
    return max(
        candidates,
        key=lambda pair: (
            shape_count(pair),
            max(row["mean_rps"] for row in counts[pair]),
        ),
    )


def save_capacity_overview(
    rows: list[dict], winner: dict, output: Path
) -> Path | None:
    selections = representative_groups(rows, winner)
    if len(selections) < 2:
        return None

    figure, axes = plt.subplots(2, 1, figsize=(11, 9), sharex=True)
    for index, (key, series, note) in enumerate(selections):
        color = COLORS[index % len(COLORS)]
        plot_metric_series(
            axes[0],
            series,
            "mean_rps",
            color,
            line_label(key, note),
            winner["cell_key"],
        )
        plot_metric_series(
            axes[1],
            series,
            "p95_us",
            color,
            line_label(key, note),
            winner["cell_key"],
        )

    axes[0].set_title(
        f"Capacity Overview: {winner['scenario']} representative families",
        pad=12,
        wrap=True,
    )
    axes[0].set_ylabel("Mean RPS")
    axes[0].grid(alpha=0.25)
    axes[0].legend(loc="best")

    axes[1].set_ylabel("p95 latency (us)")
    axes[1].set_xlabel("Client concurrency")
    axes[1].grid(alpha=0.25)

    finalize_figure(figure)
    figure.savefig(output)
    plt.close(figure)
    return output


def save_per_connection_sensitivity(
    rows: list[dict], winner: dict, output: Path
) -> Path | None:
    selections = representative_groups(rows, winner)
    if len(selections) < 2:
        return None

    figure, axes = plt.subplots(2, 1, figsize=(11, 9), sharex=True)
    for index, (key, series, note) in enumerate(selections):
        color = COLORS[index % len(COLORS)]
        plot_metric_series(
            axes[0],
            series,
            "per_connection_rps",
            color,
            line_label(key, note),
            winner["cell_key"],
        )
        plot_metric_series(
            axes[1],
            series,
            "p95_us",
            color,
            line_label(key, note),
            winner["cell_key"],
        )

    axes[0].set_title(
        f"Per-Connection Throughput: {winner['scenario']} representative families",
        pad=12,
        wrap=True,
    )
    axes[0].set_ylabel("Mean RPS / connection")
    axes[0].grid(alpha=0.25)
    axes[0].legend(loc="best")

    axes[1].set_ylabel("p95 latency (us)")
    axes[1].set_xlabel("Client concurrency")
    axes[1].grid(alpha=0.25)

    finalize_figure(figure)
    figure.savefig(output)
    plt.close(figure)
    return output


def save_peak_neighborhood(rows: list[dict], winner: dict, output: Path) -> Path | None:
    reference_key = reference_family_key(rows, winner)
    if reference_key is None:
        return None
    winner_series = [row for row in rows if group_key(row) == reference_key]

    conc_min = max(1, winner["client_concurrency"] - 20)
    conc_max = winner["client_concurrency"] + 20
    focus = [
        row
        for row in winner_series
        if conc_min <= row["client_concurrency"] <= conc_max
    ]
    if len(focus) < 4:
        focus = winner_series

    ordered = sorted(focus, key=lambda row: row["client_concurrency"])
    x = [row["client_concurrency"] for row in ordered]
    rps = [row["mean_rps"] for row in ordered]
    p95 = [row["p95_us"] for row in ordered]
    p99 = [row["p99_us"] for row in ordered]

    (
        ref_scenario,
        ref_io,
        ref_worker,
        ref_proc,
        ref_wrk,
        ref_http_method,
        ref_request_body_bytes,
        ref_response_body_bytes,
    ) = reference_key
    detail_bits = [f"{ref_http_method}", f"req={ref_request_body_bytes}", f"resp={ref_response_body_bytes}"]
    figure, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    figure.suptitle(
        f"Peak Neighborhood: io={ref_io} worker={ref_worker} "
        f"proc={ref_proc} wrk={ref_wrk} {' '.join(detail_bits)}",
        y=0.985,
    )
    axes[0].plot(x, rps, color="#d97706", linewidth=1.8, marker="o")
    axes[0].scatter(
        winner["client_concurrency"],
        winner["mean_rps"],
        color="#111827",
        s=90,
        zorder=5,
    )
    top_of_band = max(max(rps), winner["mean_rps"])
    bottom_of_band = min(min(rps), winner["mean_rps"])
    span = max(top_of_band - bottom_of_band, 1.0)
    annotate_below = (winner["mean_rps"] - bottom_of_band) / span > 0.8
    offset = (8, -14) if annotate_below else (8, 10)
    vertical_align = "top" if annotate_below else "bottom"
    axes[0].annotate(
        f"winner: conc={winner['client_concurrency']}\n{winner['mean_rps']:.0f} rps",
        xy=(winner["client_concurrency"], winner["mean_rps"]),
        xytext=offset,
        textcoords="offset points",
        va=vertical_align,
        bbox={"boxstyle": "round,pad=0.2", "fc": "white", "ec": "none", "alpha": 0.85},
    )
    axes[0].set_ylabel("Mean RPS")
    axes[0].grid(alpha=0.25)
    axes[0].margins(y=0.10)

    axes[1].plot(x, p95, color="#1d4ed8", linewidth=1.6, marker="o", label="p95")
    axes[1].plot(x, p99, color="#7c3aed", linewidth=1.6, marker="o", label="p99")
    axes[1].set_ylabel("Latency (us)")
    axes[1].set_xlabel("Client concurrency")
    axes[1].grid(alpha=0.25)
    axes[1].margins(y=0.08)
    axes[1].legend(loc="best")

    finalize_figure(figure, top=0.95)
    figure.savefig(output)
    plt.close(figure)
    return output


def save_thread_sensitivity(
    rows: list[dict], winner: dict, output: Path
) -> Path | None:
    focus = [
        row
        for row in rows
        if row["scenario"] == winner["scenario"]
        and row["client_concurrency"] == winner["client_concurrency"]
        and row["client_processes"] == winner["client_processes"]
        and row["wrk_threads_per_process"] == winner["wrk_threads_per_process"]
    ]
    by_io: dict[int, list[dict]] = defaultdict(list)
    for row in focus:
        by_io[row["server_io_threads"]].append(row)

    ranked_series = sorted(
        (
            (io_threads, sorted(series, key=lambda row: row["server_worker_threads"]))
            for io_threads, series in by_io.items()
            if len({row["server_worker_threads"] for row in series}) >= 2
        ),
        key=lambda item: max_rps(item[1]),
        reverse=True,
    )[:4]
    if not ranked_series:
        return None

    figure, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    figure.suptitle(
        f"Server Thread Sensitivity: conc={winner['client_concurrency']} "
        f"proc={winner['client_processes']} wrk={winner['wrk_threads_per_process']}",
        y=0.985,
    )
    for index, (io_threads, series) in enumerate(ranked_series):
        color = COLORS[index % len(COLORS)]
        x = [row["server_worker_threads"] for row in series]
        rps = [row["mean_rps"] for row in series]
        p95 = [row["p95_us"] for row in series]
        label = f"io={io_threads}"
        axes[0].plot(x, rps, color=color, linewidth=1.6, marker="o", label=label)
        axes[1].plot(x, p95, color=color, linewidth=1.6, marker="o", label=label)

    axes[0].scatter(
        winner["server_worker_threads"],
        winner["mean_rps"],
        color="#111827",
        s=90,
        zorder=5,
    )
    axes[0].set_ylabel("Mean RPS")
    axes[0].grid(alpha=0.25)
    axes[0].margins(y=0.10)
    axes[0].legend(loc="best")

    axes[1].set_ylabel("p95 latency (us)")
    axes[1].set_xlabel("Server worker threads")
    axes[1].grid(alpha=0.25)
    axes[1].margins(y=0.08)

    finalize_figure(figure, top=0.95)
    figure.savefig(output)
    plt.close(figure)
    return output


def save_loadgen_sensitivity(
    rows: list[dict], winner: dict, output: Path
) -> Path | None:
    server_pair = reference_server_pair(rows, winner)
    if server_pair is None:
        return None
    server_io_threads, server_worker_threads = server_pair
    focus = [
        row
        for row in rows
        if row["scenario"] == winner["scenario"]
        and row["server_io_threads"] == server_io_threads
        and row["server_worker_threads"] == server_worker_threads
    ]
    by_shape: dict[tuple[int, int], list[dict]] = defaultdict(list)
    for row in focus:
        by_shape[(row["client_processes"], row["wrk_threads_per_process"])].append(row)

    ranked_shapes = sorted(
        (
            (shape, sorted(series, key=lambda row: row["client_concurrency"]))
            for shape, series in by_shape.items()
            if unique_concurrency_count(series) >= 3
        ),
        key=lambda item: (
            item[0] != (
                winner["client_processes"],
                winner["wrk_threads_per_process"],
            ),
            -max_rps(item[1]),
        ),
    )[:4]
    if len(ranked_shapes) < 2:
        return None

    figure, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    for index, (shape, series) in enumerate(ranked_shapes):
        color = COLORS[index % len(COLORS)]
        proc, wrk_threads = shape
        label = f"proc={proc} wrk={wrk_threads}"
        plot_metric_series(
            axes[0], series, "mean_rps", color, label, winner["cell_key"]
        )
        plot_metric_series(
            axes[1], series, "p95_us", color, label, winner["cell_key"]
        )

    axes[0].set_title(
        f"Load Generator Sensitivity: io={server_io_threads} "
        f"worker={server_worker_threads}",
        pad=12,
        wrap=True,
    )
    axes[0].set_ylabel("Mean RPS")
    axes[0].grid(alpha=0.25)
    axes[0].legend(loc="best")

    axes[1].set_ylabel("p95 latency (us)")
    axes[1].set_xlabel("Client concurrency")
    axes[1].grid(alpha=0.25)

    finalize_figure(figure)
    figure.savefig(output)
    plt.close(figure)
    return output


def main() -> None:
    args = parse_args()
    data = load_data(Path(args.json))
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    rows = derive_rows(data)
    if not rows:
        raise SystemExit("benchmark JSON has no cells to plot")

    winner = winner_row(rows, data)
    outputs = [
        save_capacity_overview(
            rows, winner, output_dir / f"{args.prefix}-capacity-overview.png"
        ),
        save_per_connection_sensitivity(
            rows,
            winner,
            output_dir / f"{args.prefix}-per-connection-throughput.png",
        ),
        save_peak_neighborhood(
            rows, winner, output_dir / f"{args.prefix}-peak-neighborhood.png"
        ),
        save_thread_sensitivity(
            rows, winner, output_dir / f"{args.prefix}-thread-sensitivity.png"
        ),
        save_loadgen_sensitivity(
            rows, winner, output_dir / f"{args.prefix}-loadgen-sensitivity.png"
        ),
    ]

    for output in outputs:
        if output is not None:
            print(output)


if __name__ == "__main__":
    main()
