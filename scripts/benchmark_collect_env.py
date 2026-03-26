#!/usr/bin/env python3
"""Collect benchmark environment details via common CLI tools."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--role", default="client")
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def run_command(*args: str) -> str:
    executable = shutil.which(args[0])
    if executable is None:
        return ""
    try:
        completed = subprocess.run(
            [executable, *args[1:]],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return ""
    text = completed.stdout.strip() or completed.stderr.strip()
    return text


def read_text_file(path: str) -> str:
    file_path = Path(path)
    if not file_path.exists():
        return ""
    return file_path.read_text(encoding="utf-8", errors="replace").strip()


def parse_os_release() -> dict[str, str]:
    data: dict[str, str] = {}
    for line in read_text_file("/etc/os-release").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key] = value.strip().strip('"')
    return data


def tool_entry(command_name: str, *version_args: str) -> dict[str, str]:
    path = shutil.which(command_name) or ""
    version = run_command(command_name, *version_args) if path else ""
    return {"path": path, "version": version.splitlines()[0] if version else ""}


def main() -> None:
    args = parse_args()

    data = {
        "role": args.role,
        "timestamp_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "hostname": socket.gethostname(),
        "logical_cpu_count": os.cpu_count() or 1,
        "primary_ips": run_command("hostname", "-I").split(),
        "uname": run_command("uname", "-a"),
        "os_release": parse_os_release(),
        "tools": {
            "python3": tool_entry("python3", "--version"),
            "cmake": tool_entry("cmake", "--version"),
            "cxx": tool_entry("c++", "--version"),
            "gxx": tool_entry("g++", "--version"),
            "wrk": tool_entry("wrk"),
        },
        "snapshots": {
            "nproc": run_command("nproc"),
            "lscpu": run_command("lscpu"),
            "free_h": run_command("free", "-h"),
            "ip_addr": run_command("ip", "addr"),
            "hostnamectl": run_command("hostnamectl"),
            "proc_meminfo": read_text_file("/proc/meminfo"),
        },
    }

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
