import os
import subprocess
import csv # <--- Essential Import

# --- DSE SEARCH SPACE ---
rows_sweep = [32, 64, 128]      # di
cols_sweep = [16, 24, 32]      # do_dim
m_factors = [1, 2, 4]          # Weight reuse factor
num_macros = [1, 2]         # Parallel macros
b_cycles_sweep = [1, 2]        # Bit-serial cycles

# rows_sweep = [32]      # di
# cols_sweep = [24]      # do_dim
# m_factors = [1]          # Weight reuse factor
# num_macros = [1]         # Parallel macros
# b_cycles_sweep = [2]        # Bit-serial cycles

# The 8 unique layers of ResNet-20 mapped to full 2D spatial parameters
resnet_workloads = [
    {"id": "WL-1 (Input Conv)",     "in_h": 32, "in_w": 32, "in_c": 3,  "k_h": 3, "k_w": 3, "filters": 16, "stride": 1, "pad": 1},
    {"id": "WL-2 (Stack 1 Conv)",   "in_h": 32, "in_w": 32, "in_c": 16, "k_h": 3, "k_w": 3, "filters": 16, "stride": 1, "pad": 1},
    {"id": "WL-3 (Stack 2 Conv 1)", "in_h": 32, "in_w": 32, "in_c": 16, "k_h": 3, "k_w": 3, "filters": 32, "stride": 2, "pad": 1},
    {"id": "WL-4 (Stack 2 Conv 2)", "in_h": 16, "in_w": 16, "in_c": 32, "k_h": 3, "k_w": 3, "filters": 32, "stride": 1, "pad": 1},
    {"id": "WL-5 (Stack 2 Proj)",   "in_h": 32, "in_w": 32, "in_c": 16, "k_h": 1, "k_w": 1, "filters": 32, "stride": 2, "pad": 0},
    {"id": "WL-6 (Stack 3 Conv 1)", "in_h": 16, "in_w": 16, "in_c": 32, "k_h": 3, "k_w": 3, "filters": 64, "stride": 2, "pad": 1},
    {"id": "WL-7 (Stack 3 Conv 2)", "in_h": 8,  "in_w": 8,  "in_c": 64, "k_h": 3, "k_w": 3, "filters": 64, "stride": 1, "pad": 1},
    {"id": "WL-8 (Stack 3 Proj)",   "in_h": 16, "in_w": 16, "in_c": 32, "k_h": 1, "k_w": 1, "filters": 64, "stride": 2, "pad": 0}
]

# FIX: Template keys must match the format arguments exactly
compiler_cmd_template = (
    "riscv64-linux-gnu-gcc -static -O3 "
    "-DIN_H={in_h} -DIN_W={in_w} -DIN_C={in_c} "
    "-DK_H={k_h} -DK_W={k_w} -DNUM_FILTERS={filters} "
    "-DSTRIDE={stride} -DPADDING={pad} "
    "-DHW_MAX_ROWS={hw_rows} -DHW_MAX_COLS={hw_cols} "
    "tests/test-progs/imc_acc_model/cnn_layer-o2.c "
    "-o tests/test-progs/imc_acc_model/cnn_layer-o2"
)

gem5_cmd_template = (
    "build/RISCV/gem5.opt configs/imc_acc_model/system_validation_DSE.py "
    "--inputs={di} --outputs={do} --m-factor={m} --num-macros={nm} --b_cycles={bc} "
    "--energy-per-mac={emac}"
)

print("Starting Design Space Exploration...")

emac_lut = {}
with open('third_party/zigzag_imc_dimc_28nm/dimc_cost_sweep_results.csv') as f:
    for row in csv.DictReader(f):
        key = (int(row['Di']), int(row['Do']), int(row['M']),
               int(row['num_macros']), int(row['bcycles']))
        emac_lut[key] = float(row['fJ_per_MAC'])
print(f"Loaded {len(emac_lut)} cost-model entries")

if os.path.exists('dse_results.csv'):
    os.remove('dse_results.csv')

for di in rows_sweep:
    for do in cols_sweep:
        for m in m_factors:
            for nm in num_macros:
                for bc in b_cycles_sweep:
                    
                    # CALCULATION: Software tiling must see the logical capacity
                    # of the hardware (Physical Dim * Parallelism/Multiplexing)
                    calculated_rows = di * m
                    calculated_cols = do * nm
                    
                    config_id = f"R{di}_C{do}_M{m}_N{nm}_B{bc}"
                    emac = emac_lut[(di, do, m, nm, bc)]
                    print(f"\n>>> Running Config: {config_id} (Logical Tile: {calculated_rows}x{calculated_cols})")
                    
                    for wl in resnet_workloads:
                        # Pass calculated logical capacity to the C-compiler
                        subprocess.run(compiler_cmd_template.format(
                            hw_rows=calculated_rows, 
                            hw_cols=calculated_cols, 
                            **wl), shell=True, check=True)
                        
                        # Pass raw architectural units to the gem5 simulator
                        subprocess.run(gem5_cmd_template.format(
                            di=di, do=do, m=m, nm=nm, bc=bc, emac=emac), 
                            shell=True, check=True, stdout=subprocess.DEVNULL)
                        
                        # Extraction script appends to the csv
                        os.system(f"python3 extract_results.py --label '{config_id}' --wl '{wl['id']}' "
                                f"--di {di} --do {do} --m {m} --nm {nm} --bc {bc}")

print("\n--- DSE Sweep Complete! ---")