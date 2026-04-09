import optuna
import subprocess
import re
import os

# --- 1. RESNET-20 WORKLOAD DEFINITIONS ---
resnet_workloads = [
    {"id": "WL-1", "in_h": 32, "in_w": 32, "in_c": 3,  "k_h": 3, "k_w": 3, "filters": 16, "stride": 1, "pad": 1},
    {"id": "WL-2", "in_h": 32, "in_w": 32, "in_c": 16, "k_h": 3, "k_w": 3, "filters": 16, "stride": 1, "pad": 1},
    {"id": "WL-3", "in_h": 32, "in_w": 32, "in_c": 16, "k_h": 3, "k_w": 3, "filters": 32, "stride": 2, "pad": 1},
    {"id": "WL-4", "in_h": 16, "in_w": 16, "in_c": 32, "k_h": 3, "k_w": 3, "filters": 32, "stride": 1, "pad": 1},
    {"id": "WL-5", "in_h": 32, "in_w": 32, "in_c": 16, "k_h": 1, "k_w": 1, "filters": 32, "stride": 2, "pad": 0},
    {"id": "WL-6", "in_h": 16, "in_w": 16, "in_c": 32, "k_h": 3, "k_w": 3, "filters": 64, "stride": 2, "pad": 1},
    {"id": "WL-7", "in_h": 8,  "in_w": 8,  "in_c": 64, "k_h": 3, "k_w": 3, "filters": 64, "stride": 1, "pad": 1},
    {"id": "WL-8", "in_h": 16, "in_w": 16, "in_c": 32, "k_h": 1, "k_w": 1, "filters": 64, "stride": 2, "pad": 0}
]

def run_layer_simulation(wl, inputs, outputs, m_factor, cpu_freq, dma_burst):
    """
    Compiles the C-code for a specific layer and runs gem5.
    """
    # A. COMPILE THE BINARY FOR THIS LAYER
    binary_path = "tests/test-progs/imc_acc_model/cnn_layer-o2"
    compile_cmd = (
        f"riscv64-linux-gnu-gcc -static -O3 "
        f"-DIN_H={wl['in_h']} -DIN_W={wl['in_w']} -DIN_C={wl['in_c']} "
        f"-DK_H={wl['k_h']} -DK_W={wl['k_w']} -DNUM_FILTERS={wl['filters']} "
        f"-DSTRIDE={wl['stride']} -DPADDING={wl['pad']} "
        f"tests/test-progs/imc_acc_model/cnn_layer-o2.c -o {binary_path}"
    )
    subprocess.run(compile_cmd, shell=True, check=True)

    # B. RUN GEM5
    gem5_executable = "build/RISCV/gem5.opt"
    config_script = "configs/imc_acc_model/system_validation.py"
    
    cmd = [
        gem5_executable, config_script,
        f"--binary={binary_path}",
        f"--inputs={inputs}",
        f"--outputs={outputs}",
        f"--m-factor={m_factor}",
        f"--cpu-clock={cpu_freq}MHz",
        f"--dma-burst={dma_burst}"
    ]
    
    # We suppress output to keep the console clean during 8-layer loops
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # C. CALL YOUR ORIGINAL EXTRACTION SCRIPT (Just like your old file)
    # This will print your detailed table to the terminal for every layer
    extract_cmd = "python3 extract_results.py"
    subprocess.run(extract_cmd, shell=True, check=True)
    
    return parse_gem5_stats()
 
def parse_gem5_stats(stats_path="m5out/stats.txt"):
    """ Same as your previous script """
    latency = float('inf') 
    energy = float('inf')  
    data = {}
    target_stats = ["simSeconds", "system.imc.core.total_energy_fj", 
                    "system.sram.bytesRead::total", "system.sram.bytesWritten::total"]
    
    try:
        with open(stats_path, 'r') as f:
            for line in f:
                parts = line.split()
                if not parts: continue
                stat_name = parts[0]
                if stat_name in target_stats:
                    data[stat_name] = float(parts[1])
        
        latency = data.get("simSeconds", float('inf'))
        imc_energy_pj = data.get("system.imc.core.total_energy_fj", 0) / 1000.0
        total_bytes = data.get("system.sram.bytesRead::total", 0) + data.get("system.sram.bytesWritten::total", 0)
        
        sram_energy_per_byte_pj = 5.0 
        energy = imc_energy_pj + (total_bytes * sram_energy_per_byte_pj)
        
    except FileNotFoundError:
        pass # Return infinity if crashed
    return latency, energy

def objective(trial):
    # 1. Suggest Hardware Parameters
    inputs = trial.suggest_categorical('inputs', [16, 32, 64, 128])
    outputs = trial.suggest_categorical('outputs', [6, 16, 32, 64, 128])
    m_factor = trial.suggest_categorical('m_factor', [1, 2, 4])
    cpu_freq = trial.suggest_categorical('cpu_freq', [100, 250, 500, 750])
    dma_burst = trial.suggest_categorical('dma_burst', [8, 16, 32, 64])

    total_network_latency = 0
    total_network_energy = 0

    # 2. LOOP THROUGH ALL RESNET LAYERS
    print(f"\n[Trial {trial.number}] Testing Hardware: {inputs}x{outputs}, M={m_factor}, CPU={cpu_freq}MHz")
    
    for wl in resnet_workloads:
        print(f"   -> Simulating {wl['id']}...")
        lat, nrg = run_layer_simulation(wl, inputs, outputs, m_factor, cpu_freq, dma_burst)
        
        if lat == float('inf'): # Early exit if gem5 crashes
            return float('inf')
            
        total_network_latency += lat
        total_network_energy += nrg

    # 3. Calculate Network EDP
    return total_network_latency * total_network_energy

if __name__ == "__main__":
    study = optuna.create_study(direction="minimize")
    study.enqueue_trial({"inputs": 32, "outputs": 6, "m_factor": 1, "cpu_freq": 500, "dma_burst": 8})
    
    # WARNING: This will run 8 simulations per trial!
    study.optimize(objective, n_trials=1) 
    
    print("\n=== DSE Complete ===")
    print(f"Best Network EDP: {study.best_value}")
    print("Best Configuration:", study.best_params)