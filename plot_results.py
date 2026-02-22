import matplotlib.pyplot as plt
import numpy as np

# Data Labels
layers = ['WL-1', 'WL-2', 'WL-3', 'WL-4', 'WL-5', 'WL-6', 'WL-7', 'WL-8']

# Execution Time (seconds)
cpu_time = [0.1139, 0.9003, 0.4322, 0.6661, 0.0192, 0.2746, 0.5916, 0.0325]
dimc_time = [0.0125, 0.1437, 0.0755, 0.1715, 0.0035, 0.0681, 0.2155, 0.0028]

# Data Moved (Bytes -> Megabytes for readability)
cpu_mem = [17577153, 137313581, 65682145, 98027205, 3036993, 39278561, 86707689, 4630989]
dimc_mem = [2013305, 23504621, 12371777, 28140037, 575053, 11210601, 35643365, 461665]

# Convert Bytes to MB
cpu_mem_mb = [x / (1024*1024) for x in cpu_mem]
dimc_mem_mb = [x / (1024*1024) for x in dimc_mem]

x = np.arange(len(layers))  # the label locations
width = 0.35  # the width of the bars

# ----------------------------------------------------
# 1. Plot Execution Time Comparison
# ----------------------------------------------------
fig, ax = plt.subplots(figsize=(10, 6))
rects1 = ax.bar(x - width/2, cpu_time, width, label='CPU Baseline', color='#d62728')
rects2 = ax.bar(x + width/2, dimc_time, width, label='28nm DIMC Accelerator', color='#1f77b4')

ax.set_ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold')
ax.set_title('ResNet-20 Execution Time: CPU vs. DIMC Accelerator', fontsize=14, fontweight='bold')
ax.set_xticks(x)
ax.set_xticklabels(layers, rotation=45, fontsize=11)
ax.legend(fontsize=12)
ax.grid(axis='y', linestyle='--', alpha=0.7)

plt.tight_layout()
plt.savefig('execution_time_comparison.png', dpi=300) # dpi=300 is required for IEEE/Academic print quality
print("Saved: execution_time_comparison.png")

# ----------------------------------------------------
# 2. Plot Memory Traffic Comparison
# ----------------------------------------------------
fig, ax = plt.subplots(figsize=(10, 6))
rects1 = ax.bar(x - width/2, cpu_mem_mb, width, label='CPU Baseline', color='#ff7f0e')
rects2 = ax.bar(x + width/2, dimc_mem_mb, width, label='28nm DIMC Accelerator', color='#2ca02c')

ax.set_ylabel('Total Data Moved (MB)', fontsize=12, fontweight='bold')
ax.set_title('ResNet-20 Memory Traffic Reduction', fontsize=14, fontweight='bold')
ax.set_xticks(x)
ax.set_xticklabels(layers, rotation=45, fontsize=11)
ax.legend(fontsize=12)
ax.grid(axis='y', linestyle='--', alpha=0.7)

plt.tight_layout()
plt.savefig('memory_traffic_comparison.png', dpi=300)
print("Saved: memory_traffic_comparison.png")