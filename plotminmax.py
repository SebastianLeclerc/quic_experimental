#!/usr/bin/env python3

import sys
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt


def main(csv_path, direction):
    df = pd.read_csv(csv_path)

    # Rename algo labels
    df["algo"] = df["algo"].replace({
    "RSA2048": "RSA-2048",
    "RSA4096": "RSA-4096",
    "ECDSAP256": "ECDSA P-256",
    "ECDSAP384": "ECDSA P-384"
    })

    # Sort algos
    algo_order = [
    "RSA-2048",
    "RSA-4096",
    "ECDSA P-256",
    "ECDSA P-384",
    "Ed25519",
    ]

    env_palette = {
    "QUIC-cloud":  "#4C72B0",  # muted blue
    "TCP-rpi5":  "#55A868",  # muted green
    "QUIC-rpi4": "#C44E52",  # muted red
    "QUIC-rpi5": "#8172B3",  # muted purple
    }

    df["algo"] = pd.Categorical(
        df["algo"],
        categories=algo_order,
        ordered=True
    )


    # Detect path column
    if "path" not in df.columns:
        raise ValueError(f"CSV must contain 'path' column, got: {df.columns.tolist()}")

    df = df[df["path"] == direction]
    if df.empty:
        raise ValueError(f"No data for path '{direction}'")

    # Combine environment label
    df["env"] = df["transport"] + "-" + df["platform"]

    # Aggregate statistics
    agg = (
        df.groupby(["algo", "env"])["latency_ms"]
        .agg(["min", "median", "max"])
        .reset_index()
    )

    sns.set_theme(style="whitegrid", context="paper")
    plt.figure(figsize=(8, 4))

    ax = sns.barplot(
        data=agg,
        x="algo",
        y="median",
        hue="env",
        palette=env_palette,
        edgecolor="black"
    )

    # Add vertical separators between categories
    xticks = ax.get_xticks()

    for x in xticks[:-1]:
        ax.axvline(
            x + 0.5,
            color="grey",
            linewidth=0.8,
            alpha=0.5,
            zorder=0
        )

    # Add min–max whiskers
    for i, row in agg.iterrows():
        x = ax.get_xticks()[list(agg["algo"].unique()).index(row["algo"])]
        hue_offset = (
            list(agg["env"].unique()).index(row["env"]) - 
            (len(agg["env"].unique()) - 1) / 2
        ) * 0.2

        ax.vlines(
            x + hue_offset,
            row["min"],
            row["max"],
            colors="black",
            linewidth=1.2
        )

        ax.hlines(
            [row["min"], row["max"]],
            x + hue_offset - 0.05,
            x + hue_offset + 0.05,
            colors="black",
            linewidth=1.2
        )

    #Arrows/Text
    # Pick the first algorithm + first environment as reference
    ref = agg.iloc[0]

    # X position of the first algorithm
    x_positions = ax.get_xticks()
    x = x_positions[0]

    # Horizontal offset to the RIGHT of the bar
    x_offset = -0.28#+0.35

    # --- Max arrow (right side) ---
    ax.annotate(
        "Max value",
        xy=(x + x_offset, ref["max"]),
        xytext=(x + x_offset + 0.35, ref["max"] * 1.15),
        arrowprops=dict(arrowstyle="->", linewidth=1, color="red"),
        fontsize=9,
        color="red",
        ha="left",
        va="bottom",
    )

    # --- Min arrow (right side) ---
    ax.annotate(
        "Min value",
        xy=(x + x_offset, ref["min"]),
        xytext=(x + x_offset + 0.35, ref["min"] * 0.85),
        arrowprops=dict(arrowstyle="->", linewidth=1, color="red"),
        fontsize=9,
        color="red",
        ha="left",
        va="top",
    )

    # Axis labels
    ax.set_xlabel("")
    ax.set_ylabel("Median latency (ms)")

    plt.xticks(rotation=30, ha="right")
    ax.legend(title="", frameon=True)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: plot_bars_minmax.py <result.csv> <path>")
        print("Example: plot_bars_minmax.py result.csv sensor-cloud")
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])