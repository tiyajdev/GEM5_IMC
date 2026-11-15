import pandas as pd
import matplotlib.pyplot as plt

# 1. Load Data (new 17-column CSV with header)
df = pd.read_csv('dse_results.csv')
df['Di'] = df['di'].astype(int)

# 2. Aggregate 8 layers -> 108 design points (same metrics as the published figure)
inf = df.groupby(['Config', 'Di']).agg(
    Latency=('latency_s', 'sum'),
    Bytes=('imc_bytes', 'sum')).reset_index()

# 3. Pareto frontier: STRICT dominance filter (fixes the mislabeled point)
pts = inf[['Latency', 'Bytes']].values
nd = []
for i, (L, B) in enumerate(pts):
    dominated = any((L2 <= L and B2 <= B and (L2 < L or B2 < B))
                    for j, (L2, B2) in enumerate(pts) if j != i)
    if not dominated:
        nd.append(i)
pareto_df = inf.iloc[nd].sort_values('Latency')

# 4. IEEE formatting (unchanged from your version)
plt.rcParams.update({'font.size': 10, 'font.family': 'serif',
    'axes.labelsize': 10, 'legend.fontsize': 9,
    'xtick.labelsize': 9, 'ytick.labelsize': 9, 'text.usetex': False})
fig, ax = plt.subplots(figsize=(7, 4.5))
colors = {32: '#4477AA', 64: '#EE6677', 128: '#228833'}
markers = {32: 'o', 64: '^', 128: 's'}
for di_val in sorted(inf['Di'].unique()):
    mask = inf['Di'] == di_val
    ax.scatter(inf.loc[mask, 'Latency'], inf.loc[mask, 'Bytes'],
               label=f'$D_i$ = {di_val}', color=colors[di_val],
               marker=markers[di_val], alpha=0.6, edgecolors='k',
               s=50, linewidths=0.5)
ax.plot(pareto_df['Latency'], pareto_df['Bytes'], color='#CC3311',
        linestyle='--', linewidth=1.2, label='Pareto Frontier', zorder=10)
ax.set_xscale('log')
ax.set_title('Design Space Exploration: ResNet-20 on IMC Architecture')
ax.set_xlabel('Total Inference Latency (s) [Log Scale]')
ax.set_ylabel('Total SRAM Data Movement (Bytes)')
ax.grid(True, which="both", linestyle=':', alpha=0.4)
ax.legend(title="$D_i$ Configurations", frameon=True, loc='best')
plt.tight_layout()
plt.savefig('pareto_resnetv1.png', format='png', dpi=300, bbox_inches='tight')
print(f"Pareto points: {len(pareto_df)}  -> saved pareto_resnetv1.png")