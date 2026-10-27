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


data = json.loads(run("--score-quick", "--memory-limit", "384M", "--json"))
check(data["schema_version"] == 2, "score schema")
check(data["score_version"] == 3, "score version")
check(data["profile"]["name"] == "quick-diagnostic-v3", "quick profile name")
check(data["comparable"] is False, "quick profile must not be comparable")
check(data["profile"]["latency_stride"] == 64, "latency stride")
check(data["profile"]["stream_stride"] == 8, "stream stride")
check(data["profile"]["repeat"] == 2 and data["profile"]["size_per_thread"] == 16 * 1024 * 1024, "quick fixed fields")
check(len(data["components"]) == 8, "eight score components")
check(data["verified"] is True and math.isfinite(data["score"]) and data["score"] > 0, "score validity")

hashes = {component["test"]: int(component["sequence_hash"], 16) for component in data["components"]}
check(all(hashes[test] == 0 for test in ["seq-read", "seq-write", "mix", "copy", "triad"]), "stream hashes")
check(hashes["random-read"] != 0 and hashes["random-write"] == hashes["random-read"], "random hash determinism")
check(hashes["latency"] != 0 and hashes["latency"] != hashes["random-read"], "latency hash")
aggregate = 0
rates = []
weights = []
for component in data["components"]:
    check(component["gbps"] > 0 and component["million_ops_per_second"] > 0, "positive component rate")
    check(component["weight"] > 0, "positive component weight")
    rates.append(component["million_ops_per_second"])
    weights.append(component["weight"])
    aggregate ^= int(component["sequence_hash"], 16)
check(aggregate == int(data["sequence_hash"], 16), "aggregate sequence hash")
check(math.isclose(sum(weights), 1.0, abs_tol=1e-9), "component weights")
recomputed = math.exp(sum(weight * math.log(rate) for rate, weight in zip(rates, weights)) / sum(weights))
check(math.isclose(recomputed, data["score"], rel_tol=1e-7), "geometric mean")
groups = data["group_scores"]
stream_rates = rates[:5]
stream_weights = weights[:5]
random_rates = rates[5:7]
random_weights = weights[5:7]
check(math.isclose(groups["streaming_bandwidth"], math.exp(sum(w * math.log(r) for r, w in zip(stream_rates, stream_weights)) / sum(stream_weights)), rel_tol=1e-7), "streaming group")
check(math.isclose(groups["random_access"], math.exp(sum(w * math.log(r) for r, w in zip(random_rates, random_weights)) / sum(random_weights)), rel_tol=1e-7), "random group")
check(math.isclose(groups["dependent_latency"], rates[-1], rel_tol=1e-7), "latency group")

rows = list(csv.DictReader(io.StringIO(run("--score-quick", "--csv"))))
check(len(rows) == 1, "score CSV row count")
row = rows[0]
check(row["comparable"] == "false" and row["latency_stride"] == "64", "CSV profile fields")
check(row["score_version"] == "3", "CSV score version")
check(float(row["streaming_bandwidth_score"]) > 0 and float(row["random_access_score"]) > 0, "CSV group scores")
check(row["sequence_hash"] == f"{aggregate:016x}", "CSV aggregate hash")
check(all(f"{test}_sequence_hash" in row for test in hashes), "CSV component hashes")

text = run("--score-quick", "--memory-limit", "384M", "--details")
for label in ["Streaming bandwidth", "Random access", "Dependent latency", "Copy", "Triad"]:
    check(label in text, f"text details missing {label}")
print("score smoke tests passed")
