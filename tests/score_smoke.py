import csv
import io
import json
import math
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


data = json.loads(run("--score-quick", "--memory-limit", "256M", "--json"))
check(data["schema_version"] == 2, "score schema")
check(data["score_version"] == 2, "score version")
check(data["profile"]["name"] == "memorymark-quick-v2", "quick profile name")
check(data["comparable"] is False, "quick profile must not be comparable")
check(data["profile"]["latency_stride"] == 64, "latency stride")
check(data["profile"]["stream_stride"] == 8, "stream stride")
check(data["profile"]["repeat"] == 2 and data["profile"]["size_per_thread"] == 16 * 1024 * 1024, "quick fixed fields")
check(len(data["components"]) == 6, "six score components")
check(data["verified"] is True and math.isfinite(data["score"]) and data["score"] > 0, "score validity")

hashes = {component["test"]: int(component["sequence_hash"], 16) for component in data["components"]}
check(hashes["seq-read"] == 0 and hashes["seq-write"] == 0 and hashes["mix"] == 0, "stream hashes")
check(hashes["random-read"] != 0 and hashes["random-write"] == hashes["random-read"], "random hash determinism")
check(hashes["latency"] != 0 and hashes["latency"] != hashes["random-read"], "latency hash")
aggregate = 0
rates = []
for component in data["components"]:
    check(component["gbps"] > 0 and component["million_ops_per_second"] > 0, "positive component rate")
    rates.append(component["million_ops_per_second"])
    aggregate ^= int(component["sequence_hash"], 16)
check(aggregate == int(data["sequence_hash"], 16), "aggregate sequence hash")
recomputed = math.exp(sum(math.log(rate) for rate in rates) / len(rates))
check(math.isclose(recomputed, data["score"], rel_tol=1e-7), "geometric mean")

rows = list(csv.DictReader(io.StringIO(run("--score-quick", "--csv"))))
check(len(rows) == 1, "score CSV row count")
row = rows[0]
check(row["comparable"] == "false" and row["latency_stride"] == "64", "CSV profile fields")
check(row["score_version"] == "2", "CSV score version")
check(row["sequence_hash"] == f"{aggregate:016x}", "CSV aggregate hash")
check(all(f"{test}_sequence_hash" in row for test in hashes), "CSV component hashes")
print("score smoke tests passed")
