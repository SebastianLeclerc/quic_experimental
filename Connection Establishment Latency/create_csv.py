import os
import csv

BASE_DIR = "."   # adjust if needed

PATH_FOLDERS = { #Adjust to name of source folder
    #"sensor_to_edge": "sensor-edge",
    #"cloud_to_edge": "cloud-edge"
    #"sensor_HW_Acc_Off": "sensor_HW_Acc_Off"
    "sensor_rpi4": "sensor_rpi4" 
}

def extract_algo(folder_name):
    for part in ["RSA2048", "RSA4096", "ECDSAP256", "ECDSAP384", "Ed25519"]:
        if part in folder_name:
            return part
    return folder_name

def transport_from_filename(filename):
    if "tls1.2" in filename:
        return "TCP-tls1.2"
    if "tls1.3" in filename:
        return "TCP-tls1.3"
    if filename in ("edge.log", "sensor.log"):
        return "QUIC"
    return "UNKNOWN"

def read_all_timestamps(path):
    if not os.path.exists(path):
        return []
    ts = []
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if line.isdigit():
                ts.append(int(line))
    return ts

OUTPUT_FILE = "results.csv"
rows = []

for path_folder, path_label in PATH_FOLDERS.items():
    full_path = os.path.join(BASE_DIR, path_folder)

    for algo_folder in os.listdir(full_path):
        algo_path = os.path.join(full_path, algo_folder)
        if not os.path.isdir(algo_path):
            continue

        algo = extract_algo(algo_folder)

        file_pairs = [
            ("edge.log", "sensor.log"),
            ("edge_tls1.2.log", "sensor_tls1.2.log"),
            ("edge_tls1.3.log", "sensor_tls1.3.log"),
        ]

        for edge_file, sensor_file in file_pairs:
            edge_path = os.path.join(algo_path, edge_file)
            sensor_path = os.path.join(algo_path, sensor_file)

            edge_ts = read_all_timestamps(edge_path)
            sensor_ts = read_all_timestamps(sensor_path)

            if not edge_ts or not sensor_ts:
                continue

            # Pair line-by-line (truncate to shortest)
            for e, s in zip(edge_ts, sensor_ts):
                latency_ms = (e - s) / 1_000_000.0

                if latency_ms < 0:
                    print("WARNING: Negative latency detected!")
                    print(f"  Path: {path_folder}")
                    print(f"  Algo: {algo_folder}")
                    print(f"  Files: {edge_file} / {sensor_file}")
                    print(f"  Edge timestamp:   {e}")
                    print(f"  Sensor timestamp: {s}")
                    print(f"  Latency (ms): {latency_ms}")
                    print("-" * 60)

                transport = transport_from_filename(edge_file)
                platform = path_folder.replace("_to_", "-")

                rows.append([
                    transport,
                    platform,
                    algo,
                    path_label,
                    latency_ms
                ])

with open(OUTPUT_FILE, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["transport", "platform", "algo", "path", "latency_ms"])
    writer.writerows(rows)

print(f"Done. Wrote {len(rows)} rows to {OUTPUT_FILE}")
