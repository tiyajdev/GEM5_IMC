import matplotlib.pyplot as plt
import numpy as np

# ==========================================
# 1. ENTER GEM5 SIMULATION METRICS
# ==========================================
# Total MACs extracted from your DIMC stats.txt
total_macs = 196608
# 1 MAC = 1 Multiply + 1 Add = 2 Operations
total_ops = total_macs * 2

# Total System Energy from your LaTeX table
# Converted to Joules for the formula
baseline_energy_j = 41.87 * 1e-6  # 41.87 uJ
dimc_energy_j = 167.76 * 1e-9  # 167.76 nJ

# ==========================================
# 2. CALCULATE TOPS/W
# ==========================================
# Formula: TOPS/W = Operations / (Energy in Joules * 10^12)
baseline_topsw = total_ops / (baseline_energy_j * 1e12)
dimc_topsw = total_ops / (dimc_energy_j * 1e12)

# Print exact numbers to the console
print(f"--- System Efficiency (TOPS/W) ---")
print(f"RISC-V Baseline (Software): {baseline_topsw:.6f} TOPS/W")
print(f"28nm DIMC SoC (Hardware):   {dimc_topsw:.4f} TOPS/W")
print(f"Efficiency Multiplier:      {dimc_topsw / baseline_topsw:.1f}x\n")

# ==========================================
# 3. GENERATE PUBLICATION-READY PLOT
# ==========================================
# Set up the figure styling
fig, ax = plt.subplots(figsize=(7, 5))
categories = ["RISC-V CPU\n(Baseline)", "28nm DIMC SoC\n(Proposed)"]
values = [baseline_topsw, dimc_topsw]
colors = ["#1f77b4", "#ff7f0e"]  # Professional Blue and Orange

# Create the bars
bars = ax.bar(categories, values, color=colors, width=0.5, edgecolor="black")

# Format the axes and grid
ax.set_ylabel("Energy Efficiency (TOPS/W)", fontsize=12, fontweight="bold")
ax.set_title(
    "System-Level Energy Efficiency Comparison",
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
        xytext=(0, 5),  # 5 points vertical offset
        textcoords="offset points",
        ha="center",
        va="bottom",
        fontsize=11,
        fontweight="bold",
    )

# Use a logarithmic scale on the Y-axis to properly show the 249x difference
ax.set_yscale("log")
# Set Y-axis limits slightly wider than the data so labels fit perfectly
ax.set_ylim(0.005, 10.0)

# Clean up layout and save to file
plt.tight_layout()
plt.savefig("topsw_efficiency_comparison.png", dpi=300, bbox_inches="tight")

print(
    "Plot successfully generated and saved as 'topsw_efficiency_comparison.png'"
)
