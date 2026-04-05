#!/usr/bin/env python3

"""
Expected file name: ENCRYPTIONpatternSIZE-run.log
Example: RSA2048b100-1.log

Expected file content example:

Start: 1775411984526232484 ns
Start: 1775411984550124520 ns
Start: 1775411984551565901 ns
Total messages sent: 220
Average send rate: 3.67 msgs/s
Total messages sent: 220
Average send rate: 3.67 msgs/s
Total messages sent: 220
Average send rate: 3.67 msgs/s

recv_ts,seq,send_ts,rnd_len
1775411989986452740,0,1775411989968690037,84
...etc,
"""

import os
import re
import csv
import statistics
from collections import defaultdict

NS_TO_MS = 1_000_000.0


def analyze_log(filepath):
    start_ts = []
    seq0_send_ts = []
    latencies = []
    received = 0
    sent = 0

    with open(filepath, "r") as f:
        for line in f:
            line = line.strip()

            if line.startswith("Start:"):
                start_ts.append(int(line.split()[1]))
                continue

            if "Total messages sent" in line:
                sent += int(line.split()[-1])
                continue

            if not line or line.startswith("recv_ts"):
                continue

            parts = line.split(",")
            if len(parts) != 4:
                continue

            recv_ts, seq, send_ts, _ = parts
            recv_ts = int(recv_ts)
            send_ts = int(send_ts)
            seq = int(seq)

            latencies.append((recv_ts - send_ts) / NS_TO_MS)
            received += 1

            if seq == 0:
                seq0_send_ts.append(send_ts)

    latencies.sort()

    result = {
        "mean": statistics.mean(latencies),
        "median": statistics.median(latencies),
        "p99": latencies[int(0.99 * (len(latencies) - 1))],
        "min": latencies[0],
        "max": latencies[-1],
        "reliability": received / sent if sent > 0 else None,
    }

    if start_ts and seq0_send_ts:
        start_ts.sort()
        seq0_send_ts.sort()
        n = min(len(start_ts), len(seq0_send_ts))
        overheads = [(seq0_send_ts[i] - start_ts[i]) / NS_TO_MS for i in range(n)]
        result["security_overhead"] = statistics.mean(overheads)
    else:
        result["security_overhead"] = None

    return result


def main():
    results = defaultdict(list)

    filename_re = re.compile(
        r"(?P<enc>[A-Za-z0-9]+)"
        r"(?P<pattern>[psb])"
        r"(?P<size>\d+)-"
        r"(?P<run>\d+)\.log"
    )

    for fname in os.listdir("."):
        if not fname.endswith(".log"):
            continue

        m = filename_re.match(fname)
        if not m:
            continue

        key = (
            m.group("enc"),
            m.group("pattern"),
            int(m.group("size")),
        )

        metrics = analyze_log(fname)
        results[key].append(metrics)

    with open("aggregated_results.csv", "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "encryption",
            "pattern",
            "size",
            "median_of_medians",
            "min_median",
            "max_median",
            "median_p99",
            "max_p99",
            "median_security_overhead",
            "reliability",
        ])

        for (enc, pattern, size), runs in results.items():
            medians = [r["median"] for r in runs]
            p99s = [r["p99"] for r in runs]
            reliab = statistics.mean(r["reliability"] for r in runs)

            sec = [r["security_overhead"] for r in runs if r["security_overhead"]]
            sec_med = statistics.median(sec) if sec else None

            writer.writerow([
                enc,
                pattern,
                size,
                statistics.median(medians),
                min(medians),
                max(medians),
                statistics.median(p99s),
                max(p99s),
                sec_med,
                reliab,
            ])

    print("Wrote aggregated_results.csv")


if __name__ == "__main__":
    main()