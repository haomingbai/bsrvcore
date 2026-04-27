#!/usr/bin/env python3
"""Generate body sweep matrices from a consolidated benchmark JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True)
    parser.add_argument("--output-tsv", required=True)
    parser.add_argument(
        "--phase",
        choices=("get-response", "post-request", "post-matrix"),
        required=True,
    )
    parser.add_argument("--neighbor-count", type=int, default=5)
    parser.add_argument("--concurrency-values", default="")
    parser.add_argument("--concurrency-granularity", type=int, default=8)
    parser.add_argument("--throughput-threshold-ratio", type=float, default=0.80)
    parser.add_argument("--min-concurrency", type=int, default=1)
    parser.add_argument("--max-concurrency", type=int, default=640)
    parser.add_argument("--body-start-bytes", type=int, default=0)
    parser.add_argument("--body-stop-bytes", type=int, default=128 * 1024)
    parser.add_argument("--body-step-bytes", type=int, default=8 * 1024)
    parser.add_argument("--body-refine-step-bytes", type=int, default=2 * 1024)
    parser.add_argument("--body-max-bytes", type=int, default=1024 * 1024)
    parser.add_argument("--body-stop-best-stable-concurrency", type=int, default=8)
    parser.add_argument("--request-sizes", default="")
    parser.add_argument("--response-sizes", default="")
    return parser.parse_args()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


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


def parse_csv_numbers(raw: str) -> list[int]:
    values = set()
    for token in raw.split(","):
        token = token.strip()
        if not token:
            continue
        values.add(max(0, int(token)))
    return sorted(values)


def normalize_granularity(granularity: int) -> int:
    return max(1, int(granularity))


def round_to_granularity(value: int, granularity: int) -> int:
    granularity = normalize_granularity(granularity)
    value = max(1, int(value))
    return ((value + granularity - 1) // granularity) * granularity


def dedupe_sorted(values: list[int]) -> list[int]:
    return sorted({int(value) for value in values if int(value) > 0})


def range_sizes(start: int, stop: int, step: int, max_bytes: int) -> list[int]:
    start = max(0, start)
    stop = min(max_bytes, max(start, stop))
    step = max(1, step)
    values = {0, start, stop}
    current = start
    while current <= stop:
      values.add(current)
      current += step
    return sorted(values)


def effective_sizes(explicit_csv: str, start: int, stop: int, step: int, max_bytes: int) -> list[int]:
    explicit = parse_csv_numbers(explicit_csv)
    if explicit:
        return sorted({value for value in explicit if value <= max_bytes})
    return range_sizes(start, stop, step, max_bytes)


def select_thread_groups(data: dict, neighbor_count: int) -> tuple[dict, list[tuple[int, int]], int, int]:
    cells = data.get("cells", [])
    winner = data.get("winner") or min(cells, key=winner_sort_key)
    winner_io = int(winner["server_io_threads"])
    winner_worker = int(winner["server_worker_threads"])
    winner_proc = int(winner["client_processes"])
    winner_wrk = int(winner["wrk_threads_per_process"])
    winner_scenario = winner["scenario"]

    same_scenario = [cell for cell in cells if cell["scenario"] == winner_scenario]
    same_scenario.sort(
        key=lambda cell: (
            abs(int(cell["server_io_threads"]) - winner_io),
            abs(int(cell["server_worker_threads"]) - winner_worker),
            winner_sort_key(cell),
        )
    )

    groups: list[tuple[int, int]] = []
    seen: set[tuple[int, int]] = set()
    for cell in same_scenario:
        group = (int(cell["server_io_threads"]), int(cell["server_worker_threads"]))
        if group in seen:
            continue
        seen.add(group)
        groups.append(group)
        if len(groups) >= max(1, neighbor_count):
            break

    return winner, groups, winner_proc, winner_wrk


def best_cells_by_concurrency(cells: list[dict]) -> dict[int, dict]:
    grouped: dict[int, list[dict]] = {}
    for cell in cells:
        conc = int(cell["client_concurrency"])
        grouped.setdefault(conc, []).append(cell)

    selected: dict[int, dict] = {}
    for conc, bucket in grouped.items():
        selected[conc] = min(bucket, key=winner_sort_key)
    return selected


def derive_concurrency_values(
    args: argparse.Namespace,
    data: dict,
    winner: dict,
    thread_groups: list[tuple[int, int]],
) -> list[int]:
    granularity = normalize_granularity(args.concurrency_granularity)
    min_conc = max(1, int(args.min_concurrency))
    max_conc = max(min_conc, int(args.max_concurrency))

    explicit = parse_csv_numbers(args.concurrency_values)
    if explicit:
        return dedupe_sorted(
            [
                round_to_granularity(value, granularity)
                for value in explicit
                if min_conc <= value <= max_conc
            ]
        )

    winner_scenario = str(winner["scenario"])
    winner_method = str(winner.get("http_method", "GET"))
    group_set = set(thread_groups)

    candidates = [
        cell
        for cell in data.get("cells", [])
        if str(cell.get("scenario")) == winner_scenario
        and (int(cell.get("server_io_threads", 0)), int(cell.get("server_worker_threads", 0))) in group_set
        and int(cell.get("request_body_bytes", 0)) == 0
        and int(cell.get("response_body_bytes", 0)) == 0
        and str(cell.get("http_method", "GET")) == winner_method
    ]
    if not candidates:
        candidates = [
            cell
            for cell in data.get("cells", [])
            if str(cell.get("scenario")) == winner_scenario
            and int(cell.get("request_body_bytes", 0)) == 0
            and int(cell.get("response_body_bytes", 0)) == 0
        ]

    best_by_conc = best_cells_by_concurrency(candidates)
    if not best_by_conc:
        fallback = [1, 2, 4, 8, 16, 32, 64, 96, 128, 160, 192, 256, 320]
        return dedupe_sorted(
            [
                round_to_granularity(value, granularity)
                for value in fallback
                if min_conc <= value <= max_conc
            ]
        )

    ordered = sorted(best_by_conc.items(), key=lambda item: item[0])
    peak_conc, peak_cell = max(
        ordered,
        key=lambda item: metric_mean(item[1], "rps"),
    )
    peak_rps = metric_mean(peak_cell, "rps")
    threshold_ratio = max(0.05, min(0.99, float(args.throughput_threshold_ratio)))
    threshold_rps = peak_rps * threshold_ratio

    decline_conc = None
    for conc, cell in ordered:
        if conc <= peak_conc:
            continue
        if metric_mean(cell, "rps") <= threshold_rps:
            decline_conc = conc
            break

    if decline_conc is None:
        decline_conc = ordered[-1][0]

    upper = min(max_conc, max(decline_conc, peak_conc + granularity * 2))
    lower = max(min_conc, min(ordered[0][0], peak_conc))
    lower = round_to_granularity(lower, granularity)

    values = list(range(lower, upper + granularity, granularity))
    for conc, _ in ordered:
        if min_conc <= conc <= upper:
            values.append(conc)
    values.extend([peak_conc, decline_conc])

    return dedupe_sorted(
        [
            round_to_granularity(value, granularity)
            for value in values
            if min_conc <= value <= max_conc
        ]
    )


def append_row(
    rows: list[str],
    scenario_name: str,
    io_threads: int,
    worker_threads: int,
    concurrency: int,
    winner_client_processes: int,
    winner_wrk_threads_per_process: int,
    request_body_bytes: int,
    response_body_bytes: int,
) -> None:
    client_processes = max(1, min(winner_client_processes, concurrency))
    wrk_threads_per_process = max(
        1,
        min(
            winner_wrk_threads_per_process,
            (concurrency + client_processes - 1) // client_processes,
        ),
    )
    if client_processes > concurrency:
        return
    if wrk_threads_per_process > (concurrency + client_processes - 1) // client_processes:
        return
    label = (
        f"io{io_threads}-worker{worker_threads}-conc{concurrency}"
        f"-proc{client_processes}-wrk{wrk_threads_per_process}"
        f"-req{request_body_bytes}-resp{response_body_bytes}"
    )
    rows.append(
        "\t".join(
            [
                label,
                str(io_threads),
                str(worker_threads),
                str(concurrency),
                str(client_processes),
                str(wrk_threads_per_process),
                scenario_name,
                str(request_body_bytes),
                str(response_body_bytes),
            ]
        )
    )


def main() -> None:
    args = parse_args()
    data = load_json(Path(args.json))
    winner, thread_groups, winner_proc, winner_wrk = select_thread_groups(
        data, args.neighbor_count
    )
    concurrencies = derive_concurrency_values(args, data, winner, thread_groups)
    if not concurrencies:
        raise SystemExit("no concurrency values were derived")

    request_sizes = effective_sizes(
        args.request_sizes,
        args.body_start_bytes,
        args.body_stop_bytes,
        args.body_step_bytes,
        args.body_max_bytes,
    )
    response_sizes = effective_sizes(
        args.response_sizes,
        args.body_start_bytes,
        args.body_stop_bytes,
        args.body_step_bytes,
        args.body_max_bytes,
    )

    rows: list[str] = []
    for io_threads, worker_threads in thread_groups:
        for concurrency in concurrencies:
            if args.phase == "get-response":
                for response_body_bytes in response_sizes:
                    append_row(
                        rows,
                        "http_body_matrix_get",
                        io_threads,
                        worker_threads,
                        concurrency,
                        winner_proc,
                        winner_wrk,
                        0,
                        response_body_bytes,
                    )
            elif args.phase == "post-request":
                for request_body_bytes in request_sizes:
                    append_row(
                        rows,
                        "http_body_matrix_post",
                        io_threads,
                        worker_threads,
                        concurrency,
                        winner_proc,
                        winner_wrk,
                        request_body_bytes,
                        0,
                    )
            else:
                for request_body_bytes in request_sizes:
                    for response_body_bytes in response_sizes:
                        append_row(
                            rows,
                            "http_body_matrix_post",
                            io_threads,
                            worker_threads,
                            concurrency,
                            winner_proc,
                            winner_wrk,
                            request_body_bytes,
                            response_body_bytes,
                        )

    output = Path(args.output_tsv)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")

    print(
        f"winner={winner['pressure']} groups={thread_groups} "
        f"winner_proc={winner_proc} winner_wrk={winner_wrk} "
        f"concurrency={concurrencies} rows={len(rows)}"
    )


if __name__ == "__main__":
    main()
