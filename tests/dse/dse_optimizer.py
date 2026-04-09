import optuna
import subprocess
import re
import os

def run_gem5_simulation(inputs, outputs, m_factor, cpu_freq, dma_burst):
    """
    This function takes the parameters, builds the gem5 command, 
    runs it, and waits for it to finish.
    """
    # 1. Build the command to run your specific gem5 configuration
    # (You will need to adjust these flags to match your actual config file)
    gem5_executable = "build/RISCV/gem5.opt"
    config_script = "configs/imc_acc_model/system_validation.py" # Change to your actual file

    # ADD YOUR SCRIPT/BINARY PATH HERE:
    binary_path = "tests/test-progs/imc_acc_model/cnn_layer-o2"
    
    cmd = [
        gem5_executable,
        config_script,
        f"--binary={binary_path}",
        f"--inputs={inputs}",
        f"--outputs={outputs}",
        f"--m-factor={m_factor}",
        f"--cpu-clock={cpu_freq}MHz",
        f"--dma-burst={dma_burst}"
    ]
    
    print(f"Running: {' '.join(cmd)}")
    
    # 2. Execute gem5 and wait for it to finish
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
def parse_gem5_stats(stats_path="m5out/stats.txt"):
    """
    Parses the gem5 output using your custom metrics to return
    Total System Latency and Total System Energy for Optuna.
    """
    # Use infinity as the default fallback. If gem5 crashes, 
    # Optuna will see "infinite cost" and know that configuration failed.
    latency = float('inf') 
    energy = float('inf')  
    
    data = {}
    target_stats = [
        "simSeconds", 
        "system.imc.core.total_energy_fj",
        "system.sram.bytesRead::total",
        "system.sram.bytesWritten::total"
    ]
    
    try:
        with open(stats_path, 'r') as f:
            for line in f:
                parts = line.split()
                if not parts: continue
                
                stat_name = parts[0]
                if stat_name in target_stats:
                    try:
                        data[stat_name] = float(parts[1])
                    except ValueError:
                        continue
                        
        # --- 1. EXTRACT TOTAL SYSTEM LATENCY ---
        # We use simSeconds because we want to measure the whole SoC bottleneck
        latency = data.get("simSeconds", float('inf'))
        
        # --- 2. EXTRACT TOTAL SYSTEM ENERGY ---
        # Get the accelerator math energy (convert fJ to pJ)
        imc_energy_pj = data.get("system.imc.core.total_energy_fj", 0) / 1000.0
        
        # Calculate memory traffic energy overhead
        total_read = data.get("system.sram.bytesRead::total", 0)
        total_write = data.get("system.sram.bytesWritten::total", 0)
        total_bytes = total_read + total_write
        
        # NOTE: Assign a penalty for memory traffic! 
        # (e.g., 5 picoJoules per byte transferred across the bus)
        sram_energy_per_byte_pj = 5.0 
        memory_energy_pj = total_bytes * sram_energy_per_byte_pj
        
        # Total SoC energy = Math Energy + Data Movement Energy
        energy = imc_energy_pj + memory_energy_pj
        
    except FileNotFoundError:
        print(f"Error: {stats_path} not found. gem5 likely crashed.")
        
    return latency, energy

def objective(trial):
    """
    This is the function Optuna runs over and over. It suggests
    parameters, runs the sim, and calculates the Cost.
    """
    # 1. Define the search space (Optuna will pick smartly from these)
    inputs = trial.suggest_categorical('inputs', [16, 32, 64, 128])
    outputs = trial.suggest_categorical('outputs', [6, 16, 32, 64, 128])
    m_factor = trial.suggest_categorical('m_factor', [1, 2, 4])
    cpu_freq = trial.suggest_categorical('cpu_freq', [100, 250, 500, 750])
    dma_burst = trial.suggest_categorical('dma_burst', [8, 16, 32, 64])
    
    # 2. Run the simulation with these specific parameters
    run_gem5_simulation(inputs, outputs, m_factor, cpu_freq, dma_burst)
    
    # 3. Parse the results
    latency, energy = parse_gem5_stats()
    
    # 4. Calculate your Cost Function (e.g., Energy-Delay Product)
    # Note: You can add normalization here later!
    edp = energy * latency
    
    return edp

if __name__ == "__main__":
    print("Starting System-Level DSE...")
    
    # Create the Optuna study
    study = optuna.create_study(direction="minimize")

    # 1. ENQUEUE YOUR DEFAULTS: 
    # This forces Optuna's very first guess to be these exact numbers
    study.enqueue_trial({
        "inputs": 32,
        "outputs": 6,
        "m_factor": 1,
        "cpu_freq": 500,
        "dma_burst": 8
    })
    
    # Run the study for a set number of trials
    study.optimize(objective, n_trials=1) # Start with 50 to test it
    
    # Print the absolute best configuration found
    print("\n=== DSE Complete ===")
    print(f"Best EDP Score: {study.best_value}")
    print("Best SoC Configuration:")
    for key, value in study.best_params.items():
        print(f"  {key}: {value}")