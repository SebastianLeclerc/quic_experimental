#!/usr/bin/env python3
import glob
import os
import statistics

def read_log(path):
    data = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            topic, ts = line.split(",")
            data[topic] = int(ts)
    return data

# Collect all latencies across ALL tests
se_all = []   # sensor -> edge (ms)
ec_all = []   # edge -> cloud (ms)
sc_all = []   # sensor -> cloud (ms)

sensor_logs = sorted(glob.glob("sensor*.log"))

if not sensor_logs:
    print("No sensor*.log files found")
    exit(1)

for sensor_path in sensor_logs:
    test_id = sensor_path.replace("sensor", "").replace(".log", "")
    edge_path = f"edge{test_id}.log"
    cloud_path = f"cloud{test_id}.log"

    if not (os.path.exists(edge_path) and os.path.exists(cloud_path)):
        print(f"WARNING: skipping test {test_id}, missing edge/cloud log")
        continue

    sensor = read_log(sensor_path)
    edge   = read_log(edge_path)
    cloud  = read_log(cloud_path)

    for topic, s_ts in sensor.items():
        if topic not in edge or topic not in cloud:
            print(f"WARNING [{test_id}]: topic {topic} missing in edge/cloud")
            continue

        e_ts = edge[topic]
        c_ts = cloud[topic]

        if not (s_ts < e_ts < c_ts):
            print(
                f"WARNING [{test_id}] order violation for {topic}: "
                f"sensor={s_ts}, edge={e_ts}, cloud={c_ts}"
            )
            continue

        se_all.append((e_ts - s_ts) / 1e6)
        ec_all.append((c_ts - e_ts) / 1e6)
        sc_all.append((c_ts - s_ts) / 1e6)

def print_stats(label, data):
    if not data:
        print(f"{label}: no valid samples")
        return

    data_sorted = sorted(data)

    print(f"{label}:")
    p90 = statistics.quantiles(data_sorted, n=10)[8]
    print(f"N,min,p50,p90,max")
    print(f"N,{min(data_sorted):.3f},{statistics.median(data_sorted):.3f},{p90:.3f},{max(data_sorted):.3f}\n")

print("\n=== Aggregated results over ALL tests ===\n")
print_stats("Sensor → Edge", se_all)
print_stats("Edge → Cloud", ec_all)
print_stats("Sensor → Cloud", sc_all)