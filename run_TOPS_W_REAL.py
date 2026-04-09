import matplotlib.pyplot as plt
import numpy as np

# ==========================================
# 1. RESNET-V1 WORKLOAD DATA
# ==========================================
# Total MACs across the 8 ResNet layers (Extracted from gem5 logs)
total_macs = 10141696
# 1 MAC = 1 Multiply + 1 Add = 2 Operations
total_ops = total_macs * 2

# Total System Energy from your Table 6.x
# Converted from micro-Joules (µJ) to Joules
baseline_energy_j = 2235.9 * 1e-6
dimc_energy_j = 559.6 * 1e-6

# ==========================================
# 2. CALCULATE TOPS/W
# ==========================================
# Formula: TOPS/W = Operations / (Energy in Joules * 10^12)
baseline_topsw = total_ops / (baseline_energy_j * 1e12)
dimc_topsw = total_ops / (dimc_energy_j * 1e12)

print(f"--- ResNet-v1 System Efficiency ---")
print(f"CPU Baseline: {baseline_topsw:.6f} TOPS/W")
print(f"DIMC System:  {dimc_topsw:.6f} TOPS/W")
print(f"Multiplier:   {dimc_topsw / baseline_topsw:.2f}x\n")

# ==========================================
# 3. GENERATE PUBLICATION-READY PLOT
# ==========================================
fig, ax = plt.subplots(figsize=(7, 5))
categories = ["CPU Baseline\n(ResNet-v1)", "DIMC System\n(ResNet-v1)"]
values = [baseline_topsw, dimc_topsw]
colors = ["#1f77b4", "#ff7f0e"]

# Create the bars
bars = ax.bar(categories, values, color=colors, width=0.5, edgecolor="black")

# Format the axes and grid
ax.set_ylabel("Energy Efficiency (TOPS/W)", fontsize=12, fontweight="bold")
ax.set_title(
    "ResNet-v1 System Energy Efficiency Comparison",
    fontsize=14,
    fontweight="bold",
    pad=15,
)
ax.grid(axis="y", linestyle="--", alpha=0.7)

# Add exact value labels on top of each bar
for bar in bars:
    height = bar.get_height()
    ax.annotate(
        f"{height:.4f}\nTOPS/W",
        xy=(bar.get_x() + bar.get_width() / 2, height),
        xytext=(0, 5),
        textcoords="offset points",
        ha="center",
        va="bottom",
        fontsize=11,
        fontweight="bold",
    )

# Y-axis scaling adjusts automatically to leave room for the labels
ax.set_ylim(0, max(values) * 1.3)

# Clean up layout and save to file
plt.tight_layout()
plt.savefig("resnet_efficiency_comparison.png", dpi=300, bbox_inches="tight")

print(
    "Plot successfully generated and saved as 'resnet_efficiency_comparison.png'"
)
