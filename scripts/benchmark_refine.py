#!/usr/bin/env python3
"""Generate a fine-grained sweep matrix around peak cells."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True)
    parser.add_argument("--output-tsv", required=True)
    parser.add_argument(
        "--sweep-depth",
        choices=("quick", "standard", "full"),
        default="standard",
    )
    parser.add_argument("--granularity", type=int, default=1)
    parser.add_argument("--decline-threshold-ratio", type=float, default=0.90)
    parser.add_argument("--decline-min-points", type=int, default=2)
    parser.add_argument("--max-concurrency-multiplier", type=float, default=2.0)
    return parser.parse_args()


def metric_mean(cell: dict, metric: str) -> float:
    return float(cell["aggregate"][metric]["mean"])


def latency_mean(cell: dict, percentile: str) -> float:
    return float(cell["aggregate"]["latency_us"][percentile]["mean"])


def winner_sort_key(cell: dict) -> tuple:
    return (
        0 if cell["aggregate"].get("stability") == "stable" else 1,
        -metric_mean(cell, "rps"),
        latency_mean(cell, "p95"),
        latency_mean(cell, "p99"),
    )


def canonical_label(
    server_io_threads: int,
    server_worker_threads: int,
    client_concurrency: int,
    client_processes: int,
    wrk_threads_per_process: int,
) -> str:
    return (
        f"io{server_io_threads}-worker{server_worker_threads}"
        f"-conc{client_concurrency}-proc{client_processes}"
        f"-wrk{wrk_threads_per_process}"
    )


PROFILES = {
    "quick": {
        "conc_outer_span": 24,
        "conc_outer_step": 4,
        "conc_inner_span": 10,
        "conc_inner_step": 2,
        "worker_radius": 4,
        "worker_compare_radius": 2,
        "io_radius": 2,
        "io_compare_radius": 1,
        "compare_wrk_max": 2,
    },
    "standard": {
        "conc_outer_span": 32,
        "conc_outer_step": 4,
        "conc_inner_span": 12,
        "conc_inner_step": 2,
        "worker_radius": 5,
        "worker_compare_radius": 3,
        "io_radius": 2,
        "io_compare_radius": 1,
        "compare_wrk_max": 2,
    },
    "full": {
        "conc_outer_span": 40,
        "conc_outer_step": 4,
        "conc_inner_span": 16,
        "conc_inner_step": 2,
        "worker_radius": 6,
        "worker_compare_radius": 4,
        "io_radius": 3,
        "io_compare_radius": 2,
        "compare_wrk_max": 3,
    },
}


def append_row(
    rows: list[tuple[str, int, int, int, int, int]],
    seen: set[tuple[int, int, int, int, int]],
    io_threads: int,
    worker_threads: int,
    concurrency: int,
    client_processes: int,
    wrk_threads_per_process: int,
) -> None:
    io_threads = max(1, io_threads)
    worker_threads = max(1, worker_threads)
    concurrency = max(1, concurrency)
    client_processes = max(1, client_processes)
    wrk_threads_per_process = max(1, wrk_threads_per_process)
    if client_processes > concurrency:
        return
    if wrk_threads_per_process > math.ceil(concurrency / client_processes):
        return
    key = (io_threads, worker_threads, concurrency, client_processes, wrk_threads_per_process)
    if key in seen:
        return
    seen.add(key)
    rows.append(
        (
            canonical_label(
                io_threads,
                worker_threads,
                concurrency,
                client_processes,
                wrk_threads_per_process,
            ),
            io_threads,
            worker_threads,
            concurrency,
            client_processes,
            wrk_threads_per_process,
        )
    )


def best_cells_by_scenario(cells: list[dict]) -> list[dict]:
    picked: dict[str, dict] = {}
    for cell in sorted(cells, key=winner_sort_key):
        scenario = cell["scenario"]
        if scenario not in picked:
            picked[scenario] = cell
    return list(picked.values())


def normalize_granularity(granularity: int) -> int:
    return max(1, int(granularity))


def round_to_granularity(value: int, granularity: int) -> int:
    granularity = normalize_granularity(granularity)
    value = max(1, int(value))
    return int(math.ceil(value / granularity) * granularity)


def concurrency_candidates(base: int, profile: dict, granularity: int) -> list[int]:
    values = {base}
    outer_span = int(profile["conc_outer_span"])
    outer_step = int(profile["conc_outer_step"])
    inner_span = int(profile["conc_inner_span"])
    inner_step = int(profile["conc_inner_step"])

    for delta in range(-outer_span, outer_span + 1, outer_step):
        values.add(base + delta)
    for delta in range(-inner_span, inner_span + 1, inner_step):
        values.add(base + delta)
    values.add(max(1, base // 2))
    values.add(max(1, int(round(base * 0.75))))
    values.add(max(1, int(round(base * 1.25))))
    return sorted(round_to_granularity(value, granularity) for value in values if value >= 1)


def comparison_concurrency_candidates(base: int, granularity: int) -> list[int]:
    values = {
        max(1, base // 4),
        max(1, base // 2),
        max(1, int(round(base * 0.75))),
        max(1, base - 8),
        base,
        base + 8,
        base + 16,
    }
    return sorted(round_to_granularity(value, granularity) for value in values)


def nearby_loadgen_concurrency_candidates(base: int, granularity: int) -> list[int]:
    values = {
        max(1, base - 8),
        base,
        base + 8,
    }
    return sorted(round_to_granularity(value, granularity) for value in values)


def low_worker_candidates(base: int) -> list[int]:
    values = {
        1,
        2,
        4,
        8,
        max(1, base // 2),
        max(1, int(round(base * 0.75))),
        base,
    }
    return sorted(values)


def high_io_candidates(base: int) -> list[int]:
    values = {
        base,
        max(1, int(round(base * 1.25))),
        max(1, int(round(base * 1.5))),
        max(1, base * 2),
        max(1, int(round(base * 2.4))),
    }
    return sorted(values)


def reference_series_for_cell(cells: list[dict], anchor: dict) -> list[dict]:
    matched = [
        cell
        for cell in cells
        if cell["scenario"] == anchor["scenario"]
        and int(cell["server_io_threads"]) == int(anchor["server_io_threads"])
        and int(cell["server_worker_threads"]) == int(anchor["server_worker_threads"])
        and int(cell["client_processes"]) == int(anchor["client_processes"])
        and int(cell["wrk_threads_per_process"]) == int(anchor["wrk_threads_per_process"])
        and str(cell.get("http_method", "GET")) == str(anchor.get("http_method", "GET"))
        and int(cell.get("request_body_bytes", 0)) == int(anchor.get("request_body_bytes", 0))
        and int(cell.get("response_body_bytes", 0)) == int(anchor.get("response_body_bytes", 0))
    ]
    by_conc: dict[int, dict] = {}
    for cell in matched:
        conc = int(cell["client_concurrency"])
        current = by_conc.get(conc)
        if current is None or winner_sort_key(cell) < winner_sort_key(current):
            by_conc[conc] = cell
    return [by_conc[conc] for conc in sorted(by_conc)]


def decline_focus_concurrency(
    series: list[dict], threshold_ratio: float, min_points: int
) -> tuple[int | None, int | None]:
    if not series:
        return None, None

    peak_cell = max(series, key=lambda cell: metric_mean(cell, "rps"))
    peak_conc = int(peak_cell["client_concurrency"])
    peak_rps = metric_mean(peak_cell, "rps")
    decline_threshold = peak_rps * max(0.01, min(1.0, threshold_ratio))

    consecutive = 0
    decline_conc: int | None = None
    for cell in series:
        conc = int(cell["client_concurrency"])
        if conc <= peak_conc:
            continue
        if metric_mean(cell, "rps") <= decline_threshold:
            consecutive += 1
            if consecutive >= max(1, min_points):
                decline_conc = conc
                break
        else:
            consecutive = 0

    if decline_conc is None:
        tail = [int(cell["client_concurrency"]) for cell in series if int(cell["client_concurrency"]) > peak_conc]
        if tail:
            decline_conc = max(tail)

    return peak_conc, decline_conc


def decline_window_candidates(
    peak_conc: int,
    decline_conc: int | None,
    granularity: int,
    max_multiplier: float,
) -> list[int]:
    granularity = normalize_granularity(granularity)
    upper = max(
        peak_conc + granularity,
        int(math.ceil(peak_conc * max(1.05, max_multiplier))),
        int(decline_conc or peak_conc),
    )
    lower = max(1, peak_conc - 4 * granularity)

    values: set[int] = set()
    cursor = lower
    while cursor <= upper:
        values.add(cursor)
        cursor += granularity

    if decline_conc is not None and decline_conc > peak_conc:
        values.add((peak_conc + decline_conc) // 2)
    values.add(peak_conc)
    if decline_conc is not None:
        values.add(decline_conc)

    return sorted(round_to_granularity(value, granularity) for value in values if value >= 1)


def refine_rows(
    cells: list[dict],
    sweep_depth: str,
    granularity: int,
    decline_threshold_ratio: float,
    decline_min_points: int,
    max_concurrency_multiplier: float,
) -> list[tuple[str, int, int, int, int, int]]:
    rows: list[tuple[str, int, int, int, int, int]] = []
    seen: set[tuple[int, int, int, int, int]] = set()
    profile = PROFILES[sweep_depth]
    granularity = normalize_granularity(granularity)

    for cell in best_cells_by_scenario(cells):
        io_threads = int(cell["server_io_threads"])
        worker_threads = int(cell["server_worker_threads"])
        concurrency = int(cell["client_concurrency"])
        client_processes = int(cell["client_processes"])
        wrk_threads_per_process = int(cell["wrk_threads_per_process"])

        for conc in concurrency_candidates(concurrency, profile, granularity):
            append_row(
                rows,
                seen,
                io_threads,
                worker_threads,
                conc,
                client_processes,
                wrk_threads_per_process,
            )

        series = reference_series_for_cell(cells, cell)
        peak_conc, decline_conc = decline_focus_concurrency(
            series,
            threshold_ratio=decline_threshold_ratio,
            min_points=decline_min_points,
        )
        if peak_conc is not None:
            for conc in decline_window_candidates(
                peak_conc,
                decline_conc,
                granularity,
                max_concurrency_multiplier,
            ):
                append_row(
                    rows,
                    seen,
                    io_threads,
                    worker_threads,
                    conc,
                    client_processes,
                    wrk_threads_per_process,
                )

        for compare_proc in range(1, min(4, concurrency) + 1):
            wrk_cap = min(
                int(profile["compare_wrk_max"]),
                math.ceil(concurrency / compare_proc),
            )
            for compare_wrk in range(1, max(1, wrk_cap) + 1):
                for conc in comparison_concurrency_candidates(concurrency, granularity):
                    append_row(
                        rows,
                        seen,
                        io_threads,
                        worker_threads,
                        conc,
                        compare_proc,
                        compare_wrk,
                    )

        for worker_candidate in range(
            max(1, worker_threads - int(profile["worker_radius"])),
            worker_threads + int(profile["worker_radius"]) + 1,
        ):
            append_row(
                rows,
                seen,
                io_threads,
                worker_candidate,
                concurrency,
                client_processes,
                wrk_threads_per_process,
            )

        for worker_candidate in low_worker_candidates(worker_threads):
            append_row(
                rows,
                seen,
                io_threads,
                worker_candidate,
                concurrency,
                client_processes,
                wrk_threads_per_process,
            )

        for io_candidate in high_io_candidates(io_threads):
            for conc in comparison_concurrency_candidates(concurrency, granularity):
                append_row(
                    rows,
                    seen,
                    io_candidate,
                    1,
                    conc,
                    client_processes,
                    wrk_threads_per_process,
                )
            for conc in nearby_loadgen_concurrency_candidates(concurrency, granularity):
                for compare_proc in range(1, min(4, conc) + 1):
                    wrk_cap = min(
                        int(profile["compare_wrk_max"]),
                        math.ceil(conc / compare_proc),
                    )
                    for compare_wrk in range(1, max(1, wrk_cap) + 1):
                        append_row(
                            rows,
                            seen,
                            io_candidate,
                            1,
                            conc,
                            compare_proc,
                            compare_wrk,
                        )

        for conc in nearby_loadgen_concurrency_candidates(concurrency, granularity):
            for worker_candidate in low_worker_candidates(worker_threads):
                for compare_proc in range(1, min(4, conc) + 1):
                    wrk_cap = min(
                        int(profile["compare_wrk_max"]),
                        math.ceil(conc / compare_proc),
                    )
                    for compare_wrk in range(1, max(1, wrk_cap) + 1):
                        append_row(
                            rows,
                            seen,
                            io_threads,
                            worker_candidate,
                            conc,
                            compare_proc,
                            compare_wrk,
                        )

        for io_candidate in range(
            max(1, io_threads - int(profile["io_radius"])),
            io_threads + int(profile["io_radius"]) + 1,
        ):
            append_row(
                rows,
                seen,
                io_candidate,
                worker_threads,
                concurrency,
                client_processes,
                wrk_threads_per_process,
            )

        for io_candidate in range(
            max(1, io_threads - int(profile["io_compare_radius"])),
            io_threads + int(profile["io_compare_radius"]) + 1,
        ):
            for worker_candidate in range(
                max(1, worker_threads - int(profile["worker_compare_radius"])),
                worker_threads + int(profile["worker_compare_radius"]) + 1,
            ):
                append_row(
                    rows,
                    seen,
                    io_candidate,
                    worker_candidate,
                    concurrency,
                    client_processes,
                    wrk_threads_per_process,
                )

    rows.sort(key=lambda row: (row[1], row[2], row[3], row[4], row[5]))
    return rows


def main() -> None:
    args = parse_args()
    data = json.loads(Path(args.json).read_text(encoding="utf-8"))
    rows = refine_rows(
        data.get("cells", []),
        args.sweep_depth,
        granularity=args.granularity,
        decline_threshold_ratio=args.decline_threshold_ratio,
        decline_min_points=args.decline_min_points,
        max_concurrency_multiplier=args.max_concurrency_multiplier,
    )
    output = Path(args.output_tsv)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "".join(
            f"{label}\t{io_threads}\t{worker_threads}\t{concurrency}\t{client_processes}\t{wrk_threads}\n"
            for label, io_threads, worker_threads, concurrency, client_processes, wrk_threads in rows
        ),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
