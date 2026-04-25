import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import matplotlib as mpl

# -----------------------------
# Load data
# -----------------------------
df = pd.read_csv("from_sensor.csv") #change according to path
#df = pd.read_csv("from_cloud.csv")

df = df[df["path"] == "sensor-edge"] #change according to path
#df = df[df["path"] == "cloud-edge"]

mpl.rcParams.update({
    "font.size": 12,
    "axes.titlesize": 12,
    "axes.labelsize": 12,
    "xtick.labelsize": 12,
    "ytick.labelsize": 12,
    "legend.fontsize": 11,
})

# -----------------------------
# Define configurations (bars), change according to path
# -----------------------------

configs = {
    "QUIC-rpi4": (
        (df["transport"] == "QUIC") & 
        (df["platform"] == "rpi4")
    ),
    "TCP-TLS1.3-rpi4": (
        (df["transport"] == "TCP-TLS1.3") &
        (df["platform"] == "rpi4")
    ),
    "QUIC-rpi5": (
        (df["transport"] == "QUIC") &
        (df["platform"] == "rpi5")
    ),
    "TCP-TLS1.3-rpi5": (
        (df["transport"] == "TCP-TLS1.3") &
        (df["platform"] == "rpi5")
    )
}
"""
configs = {
    "QUIC-cloud": (
        (df["transport"] == "QUIC") &
        (df["platform"] == "cloud")
    ),
    "TCP-TLS1.3-cloud": (
        (df["transport"] == "TCP-TLS1.3") &
        (df["platform"] == "cloud")
    )
}
"""
for k, m in configs.items():
    print(k, df[m].shape[0])

algos = ["RSA2048", "RSA4096", "ECDSAP256", "ECDSAP384", "Ed25519"]

# -----------------------------
# Compute statistics
# -----------------------------
stats = {}

for label, mask in configs.items():
    med, ymin, ymax = [], [], []

    for algo in algos:
        values = df[mask & (df["algo"] == algo)]["latency_ms"]

        med.append(values.median())
        ymin.append(values.min())
        ymax.append(values.max())

    stats[label] = {
        "median": np.array(med),
        "yerr": np.vstack([
            np.array(med) - np.array(ymin),
            np.array(ymax) - np.array(med)
        ])
    }

# -----------------------------
# Plot
# -----------------------------
x = np.arange(len(algos))

fig, ax = plt.subplots(figsize=(5, 5))

colors = [
    "#4C72B0",  # blue
    "#DD8452",  # orange
    "#55A868",  # green
    "#C44E52",  # red
    "#8172B3",  # purple
    "#937860",  # brown
]

n_cfg = len(configs)
width = 0.20 #bar width

for i, (label, color) in enumerate(zip(stats.keys(), colors)):
    ax.bar(
        x + (i - (n_cfg - 1)/2)*width, #x + i * width,
        stats[label]["median"],
        width,
        yerr=stats[label]["yerr"],
        capsize=4,
        label=label,
        color=color,
        edgecolor="black",
        linewidth=0.8
    )


# -----------------------------
# Formatting
# -----------------------------
ax.set_ylabel("Latency (ms)", fontsize=12)
ax.set_xticks(x)
ax.set_xticklabels(
    ["RSA-2048", "RSA-4096", "ECDSA P-256", "ECDSA P-384", "Ed25519"],
    rotation=30,
    ha="right"
)

# Vertical separators between cryptos
for xpos in range(len(algos) - 1):
    ax.axvline(
        xpos + 0.5,
        color="black",
        linewidth=1.0,
        alpha=0.4,
        zorder=0
    )

# -----------------------------
# Textual explanation/legend
# -----------------------------
ax.text(
    0.01, 0.98,
    "Bars: median\nWhiskers: min–max",
    transform=ax.transAxes,
    fontsize=12,
    verticalalignment="top",
    bbox=dict(boxstyle="round", facecolor="white", alpha=0.8)
)

ax.set_ylim(0, 449) #Force y-axis
ax.margins(x=0)
ax.set_xlim(-0.5, len(algos) - 0.5)
#fig.set_size_inches(5, fig.get_size_inches()[1])

ax.legend(frameon=True)
ax.legend(loc="upper right")
ax.grid(axis="y", linestyle="--", alpha=0.6)

plt.tight_layout()
plt.show()