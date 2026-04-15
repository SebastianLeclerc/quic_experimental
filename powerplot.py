#!/usr/bin/env python3

import csv
import matplotlib.pyplot as plt
import matplotlib as mpl

# ---- Load data ----
N = []
mins = []
p50 = []
p90 = []
maxs = []

with open("sensor_edge.csv") as f:
    reader = csv.DictReader(f)
    for row in reader:
        N.append(int(row["N"]))
        mins.append(float(row["min"]))
        p50.append(float(row["p50"]))
        p90.append(float(row["p90"]))
        if row["max"].upper() == "OOM":
            maxs.append(None)  # break line at OOM
        else:
            maxs.append(float(row["max"]))

mpl.rcParams["font.family"] = "serif"
mpl.rcParams["font.serif"] = ["Times New Roman", "Computer Modern", "DejaVu Serif"]

mpl.rcParams.update({
    "font.size": 18,        # base font size
    "axes.labelsize": 18,   # x/y label size
    "axes.titlesize": 18,
    "xtick.labelsize": 18,
    "ytick.labelsize": 18,
    "legend.fontsize": 18,
})

# ---- Plot ----
plt.figure(figsize=(7, 5))

plt.plot(N, maxs, marker="x", label="max", linewidth=2, color="#4C72B0")
plt.plot(N, p90, marker="^", label="p90", linewidth=2, color="#C44E52")
plt.plot(N, p50, marker="s", label="p50", linewidth=2, color="#8172B3")
plt.plot(N, mins, marker="o", label="min", linewidth=2, color="#55A868")

plt.axvline(x=20, color="red", linestyle="--", alpha=0.7)
plt.text(10.5, 2550, "Failure regime (OOM)", color="red") #x, y coordinate of text

plt.xlabel("Number of sensors (N)")
plt.ylabel("Latency (ms)")

plt.xticks(N)
plt.grid(True, which="both", linestyle="--", alpha=0.5)
plt.legend()
plt.tight_layout()

plt.savefig("powerstorm_latency.png", dpi=300)
plt.show()
