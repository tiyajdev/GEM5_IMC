import subprocess

# ---- SWITCH HERE: "cpu" or "dimc" ----
TARGET = "cpu"

SRC = {"cpu":  "tests/test-progs/imc_acc_model/cpu_cnn_layer.c",
       "dimc": "tests/test-progs/imc_acc_model/cnn_layer-o2.c"}[TARGET]
BIN = SRC[:-2]   # strip ".c"

# GEMM: batch = IN_W, Di = IN_C = 32, Do = NUM_FILTERS = 24
gemm_workloads = [
    {"id": "Batch 1",   "in_w": 1},
    {"id": "Batch 4",   "in_w": 4},
    {"id": "Batch 16",  "in_w": 16},
    {"id": "Batch 128", "in_w": 128},
    {"id": "Batch 256", "in_w": 256},
]

compiler_cmd_template = (
    "riscv64-linux-gnu-gcc -static -O3 "
    "-DIN_H=1 -DIN_W={in_w} -DIN_C=32 "
    "-DK_H=1 -DK_W=1 -DNUM_FILTERS=24 "
    "-DSTRIDE=1 -DPADDING=0 "
    "-DHW_MAX_ROWS=32 -DHW_MAX_COLS=24 "
    f"{SRC} -o {BIN}"
)

gem5_cmd = (f"build/RISCV/gem5.opt configs/imc_acc_model/system_validation.py "
            f"--binary {BIN} --inputs 32 --outputs 24 --m-factor 1")
extract_cmd = "python3 extract_results.py"

print(f"Starting GEMM batch sweep ({TARGET})...")
for wl in gemm_workloads:
    print(f"\n>>> Running {wl['id']}")
    subprocess.run(compiler_cmd_template.format(**wl), shell=True, check=True)
    print("    Simulating in gem5...")
    subprocess.run(gem5_cmd, shell=True, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("    Extracting results:")
    subprocess.run(extract_cmd, shell=True, check=True)

print("\nGEMM sweep complete.")