#!/usr/bin/env python3

import sys
import csv
from pathlib import Path

def read_ns(path: Path):
    with path.open() as f:
        return [int(line.strip()) for line in f if line.strip()]


def infer_platform(dir_name: str):
    if "rpi4" in dir_name.lower():
        return "rpi4"
    if "rpi5" in dir_name.lower():
        return "rpi5"
    return dir_name

def extract_latency_samples(a, b):
    n = min(len(a), len(b))
    samples = []

    for i in range(n):
        x, y = a[i], b[i]
        if x < y:
            samples.append((y - x) / 1e6)  # ns → ms
    return samples

def main(root_dir: Path, output_csv: Path):
    rows = []

    # Expect structure: root / TRANSPORT / PLATFORMDIR / *.log
    for transport_dir in root_dir.iterdir():
        if not transport_dir.is_dir():
            continue

        transport = transport_dir.name

        for platform_dir in transport_dir.iterdir():
            if not platform_dir.is_dir():
                continue

            platform = infer_platform(platform_dir.name)

            # Group files by algorithm
            files = list(platform_dir.glob("*.log"))
            algo_map = {}

            for f in files:
                name = f.stem.lower()

                if name.endswith("sensor"):
                    algo = f.stem[:-6]
                    algo_map.setdefault(algo, {})["sensor"] = f
                elif name.endswith("edge"):
                    algo = f.stem[:-4]
                    algo_map.setdefault(algo, {})["edge"] = f
                elif name.endswith("cloud"):
                    algo = f.stem[:-5]
                    algo_map.setdefault(algo, {})["cloud"] = f

            # Process each algorithm
            for algo, roles in algo_map.items():
                if not {"sensor", "edge", "cloud"} <= roles.keys():
                    continue

                sensor_ts = read_ns(roles["sensor"])
                edge_ts   = read_ns(roles["edge"])
                cloud_ts  = read_ns(roles["cloud"])

                se = extract_latency_samples(sensor_ts, edge_ts)
                ec = extract_latency_samples(edge_ts, cloud_ts)
                sc = extract_latency_samples(sensor_ts, cloud_ts)

                for v in se:
                    rows.append([transport, platform, algo, "sensor-edge", v])

                for v in ec:
                    rows.append([transport, platform, algo, "edge-cloud", v])

                for v in sc:
                    rows.append([transport, platform, algo, "sensor-cloud", v])

    # Write CSV
    with output_csv.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["transport", "platform", "algo", "path", "latency_ms"])
        writer.writerows(rows)

    print(f"Wrote {len(rows)} latency samples to {output_csv}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: extract_latencies.py <results_root_dir> <output_csv>")
        sys.exit(1)

    main(Path(sys.argv[1]), Path(sys.argv[2]))