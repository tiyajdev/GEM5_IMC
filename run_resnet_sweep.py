import os
import subprocess

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

# Inject all 8 dimensions into the C-code at compile time
compiler_cmd_template = (
    "riscv64-linux-gnu-gcc -static -O3 "
    "-DIN_H={in_h} -DIN_W={in_w} -DIN_C={in_c} "
    "-DK_H={k_h} -DK_W={k_w} -DNUM_FILTERS={filters} "
    "-DSTRIDE={stride} -DPADDING={pad} "
    "tests/test-progs/imc_acc_model/cnn_layer-o2.c "
    "-o tests/test-progs/imc_acc_model/cnn_layer-o2"
)

gem5_cmd = "build/RISCV/gem5.opt configs/imc_acc_model/system_validation.py tests/test-progs/imc_acc_model/cnn_layer-o2"
extract_cmd = "python3 extract_results.py"

print("Starting ResNet-20 Layer Sweep...")
print("---------------------------------")

for wl in resnet_workloads:
    print(f"\n>>> Running {wl['id']}")
    
    # Compile
    compile_cmd = compiler_cmd_template.format(**wl)
    subprocess.run(compile_cmd, shell=True, check=True)
    
    # Simulate
    print("    Simulating in gem5...")
    subprocess.run(gem5_cmd, shell=True, check=True, stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    
    # Extract
    print("    Extracting results:")
    subprocess.run(extract_cmd, shell=True, check=True)

print("\n---------------------------------")
print("ResNet-20 Sweep Complete!")