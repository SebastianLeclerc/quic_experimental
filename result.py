import csv
import sys
import statistics
from collections import defaultdict

NS_TO_MS = 1_000_000.0

def percentile(sorted_vals, p):
    idx = int(p * (len(sorted_vals) - 1))
    return sorted_vals[idx]

def compute_stats(values):
    values.sort()
    q1 = percentile(values, 0.25)
    q3 = percentile(values, 0.75)

    return {
        "count": len(values),
        "min": values[0],
        "q1": q1,
        "median": statistics.median(values),
        "q3": q3,
        "iqr": q3 - q1,
        "max": values[-1],
        "p99": percentile(values, 0.99),
    }

def read_log(path):
    data = {}
    seqs_by_topic = defaultdict(set)

    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            topic = row["topic"]
            seq = int(row["seq"])
            recv_ts = int(row["recv_ts"])
            send_ts = int(row["send_ts"])

            data[(topic, seq)] = {
                "recv": recv_ts,
                "send": send_ts,
            }

            seqs_by_topic[topic].add(seq)

    return data, seqs_by_topic

def compute_reliability(seqs_by_topic):
    expected = 0
    missing = 0

    for topic, seqs in seqs_by_topic.items():
        lo, hi = min(seqs), max(seqs)
        exp = hi - lo + 1
        miss = exp - len(seqs)

        expected += exp
        missing += miss

    return 1.0 if expected == 0 else 1.0 - (missing / expected)

def main(edge_log, cloud_log):
    edge, edge_seqs = read_log(edge_log)
    cloud, cloud_seqs = read_log(cloud_log)

    # Use seqs observed at sensor side (edge log) as ground truth
    reliability = compute_reliability(edge_seqs)

    matched = edge.keys() & cloud.keys()

    A_ms = []  # sensor -> edge
    B_ms = []  # edge -> cloud
    C_ms = []  # end-to-end
    
    for key in matched:
        send = edge[key]["send"]
        edge_recv = edge[key]["recv"]
        cloud_recv = cloud[key]["recv"]

        # Sanity check
        if not (send <= edge_recv <= cloud_recv):
            print(
                f"Timestamp order violation for {key} | "
                f"sensor={send}, edge={edge_recv}, cloud={cloud_recv}",
                file=sys.stderr
            )
            exit(1)

        # Only valid samples
        A_ms.append((edge_recv - send) / NS_TO_MS)
        B_ms.append((cloud_recv - edge_recv) / NS_TO_MS)
        C_ms.append((cloud_recv - send) / NS_TO_MS)


    results = {
        "A_sensor_to_edge": compute_stats(A_ms),
        "B_edge_to_cloud": compute_stats(B_ms),
        "C_end_to_end": compute_stats(C_ms),
    }

    print(
        "measurement,count,min_ms,q1_ms,median_ms,q3_ms,iqr_ms,max_ms,p99_ms,reliability"
    )

    for name, stats in results.items():
        print(
            f"{name},"
            f"{stats['count']},"
            f"{stats['min']:.6f},"
            f"{stats['q1']:.6f},"
            f"{stats['median']:.6f},"
            f"{stats['q3']:.6f},"
            f"{stats['iqr']:.6f},"
            f"{stats['max']:.6f},"
            f"{stats['p99']:.6f},"
            f"{reliability:.6f}"
        )

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python multihop_latency_stats.py edge.log cloud.log")
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])