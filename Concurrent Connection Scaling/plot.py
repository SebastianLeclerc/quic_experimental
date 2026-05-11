import os
import glob
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib as mpl

BASE_DIR = os.getcwd()
TRANSPORTS = ["TCP", "QUIC"]
SENSOR_COUNTS = [1, 5, 10, 15, 20]

mpl.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman"],
    "font.size": 18,
    "axes.titlesize": 18,
    "axes.labelsize": 18,
    "xtick.labelsize": 15,
    "ytick.labelsize": 15,
    "legend.fontsize": 15,
})

plt.rcParams["pdf.fonttype"] = 42   # TrueType
plt.rcParams["ps.fonttype"] = 42

COLORS = [
    "#4C72B0",  # blue
    "#DD8452",  # orange
    "#55A868",  # green
    "#C44E52",  # red
]

fig, ax = plt.subplots(figsize=(7, 5))

def load_stats(transport):
    rows = []

    for n in SENSOR_COUNTS:
        folder = os.path.join(BASE_DIR, transport, str(n))
        files = glob.glob(os.path.join(folder, "*.log"))

        if not files:
            print(f"WARNING: no logs in {folder}")
            continue

        latencies = []

        for f in files:
            df = pd.read_csv(f)
            latency_ns = df["recv_ts"] - df["send_ts"]
            latencies.append(latency_ns.values)

        latencies = np.concatenate(latencies)

        rows.append({
            "sensors": n,
            "p50": np.percentile(latencies, 50) / 1e6,
            "p99": np.percentile(latencies, 99) / 1e6,
        })

    return pd.DataFrame(rows).sort_values("sensors")


# Load stats
tcp = load_stats("TCP plaintext results")
quic = load_stats("QUIC results")

print("\nTCP stats:\n", tcp)
print("\nQUIC stats:\n", quic)



# ---- QUIC ----
plt.plot(quic["sensors"], quic["p99"],
         marker="o", linestyle="--", linewidth=2,
         color=COLORS[0], label="QUIC RSA-2048 p99")

plt.plot(quic["sensors"], quic["p50"],
         marker="o", linestyle="--", linewidth=2,
         color=COLORS[1], label="QUIC RSA-2048 median")

# --- TCP ---
plt.plot(tcp["sensors"], tcp["p99"],
         marker="s", linewidth=2,
         color=COLORS[2], label="TCP plaintext p99")

plt.plot(tcp["sensors"], tcp["p50"],
         marker="s", linewidth=2,
         color=COLORS[3], label="TCP plaintext median")

leg = plt.legend(title="MQTT over", frameon=True)
leg.get_title().set_fontweight("bold")

plt.xticks([1, 5, 10, 15, 20])

plt.xlabel("Number of concurrent sensors")
plt.ylabel("Latency (ms)")
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.show()
