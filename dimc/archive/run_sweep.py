import os
import subprocess


# The batch sizes you want to sweep
batch_sizes = [1, 4, 16, 64, 128, 256]

# Your commands
compiler_cmd_template = "riscv64-linux-gnu-gcc -static -O3 -DBATCH_SIZE={} tests/test-progs/imc_acc_model/cpu_stress.c -o tests/test-progs/imc_acc_model/cpu_stress"
gem5_cmd = "build/RISCV/gem5.opt configs/imc_acc_model/system_validation.py tests/test-progs/imc_acc_model/cpu_stress"
extract_cmd = "python3 extract_results.py"

print("Starting Automated GEMM Sweep...")
print("---------------------------------")

for batch in batch_sizes:
    print(f"\n>>> Running Batch Size: {batch}")
    
    # 1. Compile the C code with the new batch size
    compile_cmd = compiler_cmd_template.format(batch)
    subprocess.run(compile_cmd, shell=True, check=True)
    
    # 2. Run gem5 (This completely overwrites m5out/stats.txt with fresh data)
    print("    Simulating in gem5...")
    subprocess.run(gem5_cmd, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    # 3. Run YOUR extraction script
    print("    Extracting results:")
    subprocess.run(extract_cmd, shell=True, check=True)

print("\n---------------------------------")
print("Sweep Complete!")