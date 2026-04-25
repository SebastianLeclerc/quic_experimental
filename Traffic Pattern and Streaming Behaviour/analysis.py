#!/usr/bin/env python3
import sys
import csv
import re
import numpy as np
from pathlib import Path
from collections import defaultdict

# Filename format:
# transport_pattern_pMSGPS_sSIZE.log
FILENAME_RE = re.compile(
    r'(?P<transport>tcp|quic)_(?P<pattern>periodic|burst|sporadic)_p(?P<msgps>\d+)_s(?P<size>\d+)\.log'
)

def analyze_file(path):
    latencies_ns = []
    seqs = []

    with open(path, "r") as f:
        reader = csv.reader(f)
        next(reader, None)  # skip header

        for row in reader:
            try:
                recv_ts = int(row[1])
                seq     = int(row[2])
                send_ts = int(row[3])
            except (IndexError, ValueError):
                continue

            latencies_ns.append(recv_ts - send_ts)
            seqs.append(seq)

    if not latencies_ns:
        return None

    lat_ms = np.array(latencies_ns) / 1e6

    seqs.sort()
    expected = seqs[-1] - seqs[0] + 1
    received = len(seqs)
    lost = expected - received
    loss_rate = lost / expected if expected > 0 else 0.0

    return {
        "loss": loss_rate,
        "mean": lat_ms.mean(),
        "p50": np.percentile(lat_ms, 50),
        "p99": np.percentile(lat_ms, 99),
        "min": lat_ms.min(),
        "max": lat_ms.max(),
    }

def classify(pattern, msgps, size):
    # Baseline
    if pattern == "periodic" and msgps == 1 and size == 128:
        return "BASELINE"

    # Size Sweep
    if pattern == "periodic" and msgps == 1000 and size in (32, 64, 128, 512, 1024):
        return "SIZE SWEEP"

    # Rate Sweep
    if pattern == "periodic" and size == 64 and msgps in (10, 100, 1000, 5000, 10000):
        return "RATE SWEEP"

    # Bursty
    if pattern == "burst":
        return "BURST"

    # Sporadic
    if pattern == "sporadic":
        return "SPORADIC"

    return "OTHER"

def main(results_dir):
    groups = defaultdict(list)

    for path in sorted(Path(results_dir).glob("*.log")):
        m = FILENAME_RE.match(path.name)
        if not m:
            continue

        pattern = m.group("pattern")
        msgps   = int(m.group("msgps"))
        size    = int(m.group("size"))

        stats = analyze_file(path)
        if not stats:
            continue

        entry = {
            "run": path.stem,   # filename without .log
            "pattern": pattern,
            "msgps": msgps,
            "size": size,
            **stats,
        }

        group = classify(pattern, msgps, size)
        groups[group].append(entry)

    # Print in narrative order
    for group in ["BASELINE", "RATE SWEEP", "SIZE SWEEP", "BURST", "SPORADIC", "OTHER"]:
        if group not in groups:
            continue

        print(group)
        print("Run,Loss_Rate,Mean,P50,P99,Min,Max")

        for r in sorted(groups[group], key=lambda x: (x["msgps"], x["size"], x["run"])):
            print(
                f"{r['run']},"
                f"{r['loss']:.6f},"
                f"{r['mean']:.3f},"
                f"{r['p50']:.3f},"
                f"{r['p99']:.3f},"
                f"{r['min']:.3f},"
                f"{r['max']:.3f}"
            )

        print()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <results_folder>")
        sys.exit(1)

    main(sys.argv[1])
