import pandas as pd

# Load the data
df = pd.read_csv('dse_results.csv')

# Sum up latency and bytes for every 8 rows (one full inference)
# This turns 864 rows into 108 inference totals
inference_totals = df.groupby('Config').agg({
    'Latency': 'sum',
    'IMC_Bytes': 'sum'
}).reset_index()

# Sort by Latency to find the 'Speed King'
speed_king = inference_totals.sort_values('Latency').iloc[0]
# Sort by Bytes to find the 'Efficiency King'
efficiency_king = inference_totals.sort_values('IMC_Bytes').iloc[0]

print(f"Speed King: {speed_king['Config']} with {speed_king['Latency']:.4f}s")
print(f"Efficiency King: {efficiency_king['Config']} with {efficiency_king['IMC_Bytes']:.0f} bytes")