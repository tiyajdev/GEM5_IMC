import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# 1. Load Data
cols = ["Config", "WL", "di_s", "do_s", "m_s", "nm_s", "bc_s", "Latency", "SRAM", "Bytes"]
df = pd.read_csv('dse_results.csv', skiprows=1, names=cols, on_bad_lines='skip')

# 2. Extract truth from Config string (e.g., R128_C32_M4_N2_B1)
# R refers to the Di parameter in your architecture
df['Di_raw'] = df['Config'].str.extract(r'R(\d+)')[0]
df['M_raw'] = df['Config'].str.extract(r'_M(\d+)')[0]

# Clean any non-matching rows to prevent the IntCastingNaNError
df = df.dropna(subset=['Di_raw', 'M_raw'])
df['Di'] = df['Di_raw'].astype(int)
df['M'] = df['M_raw'].astype(int)

# 3. Aggregate 8 layers into 1 model-level result (Full ResNet-20)
# This results in the final 108 design points
inf = df.groupby(['Config', 'Di', 'M']).agg({'Latency': 'sum', 'Bytes': 'sum'}).reset_index()

# 4. Calculate Pareto Frontier (Using ALL points)
inf_sorted = inf.sort_values(by='Latency')
pareto_points = []
min_y = float('inf')
for _, row in inf_sorted.iterrows():
    if row['Bytes'] < min_y:
        pareto_points.append(row)
        min_y = row['Bytes']
pareto_df = pd.DataFrame(pareto_points)

# 5. IEEE Professional Formatting
plt.rcParams.update({
    'font.size': 10,
    'font.family': 'serif',
    'axes.labelsize': 10,
    'legend.fontsize': 9,
    'xtick.labelsize': 9,
    'ytick.labelsize': 9,
    'text.usetex': False  # Set to True if you have a local TeX distribution installed
})

fig, ax = plt.subplots(figsize=(7, 4.5)) 

# Professional Color-blind friendly palette
colors = {32: '#4477AA', 64: '#EE6677', 128: '#228833'}
markers = {32: 'o', 64: '^', 128: 's'}

# Plot all points
unique_di = sorted(inf['Di'].unique())
for di_val in unique_di:
    mask = inf['Di'] == di_val
    ax.scatter(inf.loc[mask, 'Latency'], inf.loc[mask, 'Bytes'], 
               label=f'$D_i$ = {di_val}', 
               color=colors.get(di_val, 'black'), 
               marker=markers.get(di_val, 'D'),
               alpha=0.6, edgecolors='k', s=50, linewidths=0.5)

# Add the dashed Pareto Frontier line
if not pareto_df.empty:
    ax.plot(pareto_df['Latency'], pareto_df['Bytes'], color='#CC3311', 
            linestyle='--', linewidth=1.2, label='Pareto Frontier', zorder=10)

# 6. Logarithmic scale for Latency
ax.set_xscale('log')

# Final Polish for Publication
ax.set_title('Design Space Exploration: ResNet-20 on IMC Architecture')
ax.set_xlabel('Total Inference Latency (s) [Log Scale]')
ax.set_ylabel('Total SRAM Data Movement (Bytes)')
ax.grid(True, which="both", linestyle=':', alpha=0.4)

# Legend updated to Di
ax.legend(title="$D_i$ Configurations", frameon=True, loc='best')

plt.tight_layout()

# --- THE SAVING STEP ---
plt.savefig('ResNet20_DSE_Pareto.pdf', format='pdf', bbox_inches='tight')
plt.savefig('ResNet20_DSE_Pareto.png', format='png', dpi=300, bbox_inches='tight')

print("Plot stored successfully: ResNet20_DSE_Pareto.pdf and ResNet20_DSE_Pareto.png")
plt.show()