import csv
import io
import json
import math
import os
import subprocess
import sys

exe = sys.argv[1]


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def run(*args):
    proc = subprocess.run([exe, *args], text=True, capture_output=True, timeout=90)
    check(proc.returncode == 0, f"{args}: exit {proc.returncode}: {proc.stderr}")
    check(not proc.stderr, f"{args}: unexpected stderr: {proc.stderr}")
    return proc.stdout


if not sys.platform.startswith("linux"):
    print("process smoke skipped: Linux-only benchmark")
    raise SystemExit(0)

symbols = subprocess.run(["nm", exe], capture_output=True, text=True).stdout
if any(name in symbols for name in ["__asan_", "__tsan_", "__ubsan_"]):
    print("process smoke skipped for sanitizer runtime")
    raise SystemExit(0)

base = [
    "--process-bench", "--processes", "2", "--threads-per-process", "2",
    "--size", "64K", "--shared-size", "256K", "--thread-stack", "256K",
    "--iterations", "1", "--memory-limit", "64M",
]
data = json.loads(run(*base, "--json"))
check(data["schema_version"] == 1 and data["test"] == "process", "process schema")
check(len(data["levels"]) == 1, "single process level")
level = data["levels"][0]
check(level["requested"] == 2 and level["created"] == 2, "process count")
check(level["capacity_limited"] is False and level["footprint_available"] is True, "capacity/footprint")
check(level["idle"]["pss"] > 0 and level["idle"]["rss"] > 0, "idle footprint")
check(level["loaded"]["pss"] >= level["idle"]["pss"], "loaded footprint")
check(level["idle"]["fixed_overhead_pss"] >= 0, "fixed overhead")
check(level["thread_stacks"]["reserved"] > 0 and level["thread_stacks"]["resident_pss"] > 0, "thread stacks")
check(level["work"]["total_accesses"] > 0 and math.isfinite(level["work"]["elapsed_seconds"]), "memory work")
check(level["ipc_wakeup_ms"]["median"] >= 0 and math.isfinite(level["ipc_wakeup_ms"]["median"]), "IPC wake")
check(len(level["children"]) == 2, "child results")
check(all(child["ok"] and child["pid"] > 0 and child["workers"] == 2 for child in level["children"]), "children")

rows = list(csv.DictReader(io.StringIO(run(*base, "--csv"))))
check(len(rows) == 1 and rows[0]["created_processes"] == "2", "process CSV")

sweep_args = list(base)
sweep_args.insert(1, "--process-sweep")
sweep_args[sweep_args.index("--processes") + 1] = "4"
sweep = json.loads(run(*sweep_args, "--json"))
check([level["requested"] for level in sweep["levels"]] == [1, 2, 4], "process sweep")
print("process smoke tests passed")
