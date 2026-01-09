import re
import json
import os

def generate_necessary_stats(stats_file, run_name):
    # Create the results folder if it doesn't exist
    output_dir = "results"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # The absolute necessary metrics for the project
    patterns = {
        "Time (s)": r"simSeconds\s+([\d\.]+)",
        "Instructions": r"simInsts\s+(\d+)",
        "CPU Busy Cycles": r"system\.cpu\.exec_context\.thread_0\.numBusyCycles\s+([\d\.]+)",
        "CPI": r"system\.cpu\.cpi\s+([\d\.]+)",
        "IPC": r"system\.cpu\.ipc\s+([\d\.]+)",
        "Syscalls (Driver Overhead)": r"system\.cpu\.workload\.numSyscalls\s+(\d+)",
        "Hardware MAC Ops": r"system\.imc\.numMacOps\s+(\d+)",
        "IMC DMA Read (Bytes)": r"system\.imc\.totalDmaBytesRead\s+(\d+)",
        "IMC DMA Written (Bytes)": r"system\.imc\.totalDmaBytesWritten\s+(\d+)",
        "Total DRAM Read (Bytes)": r"system\.mem_ctrl\.dram\.dramBytesRead\s+(\d+)",
        "Total DRAM Written (Bytes)": r"system\.mem_ctrl\.dram\.dramBytesWritten\s+(\d+)",
        "DRAM Bus Util (%)": r"system\.mem_ctrl\.dram\.busUtil\s+([\d\.]+)",
        "DRAM Energy (pJ)": r"system\.mem_ctrl\.dram\.rank0\.totalEnergy\s+([\d\.]+)"
    }

    results = {}
    if not os.path.exists(stats_file):
        print(f"Error: {stats_file} not found.")
        return

    with open(stats_file, 'r') as f:
        content = f.read()
        for label, pattern in patterns.items():
            match = re.search(pattern, content)
            # If match found, keep it. If not found (baseline), mark as 0 or N/A
            if match:
                results[label] = match.group(1)
            else:
                results[label] = "0"

    # Save to the results folder
    filename = os.path.join(output_dir, f"{run_name}.json")
    with open(filename, 'w') as out:
        json.dump(results, out, indent=4)
    
    print(f"\n--- SUCCESS: Data saved to {filename} ---")
    print(f"{'METRIC':<30} | {'VALUE':<15}")
    print("-" * 50)
    for k, v in results.items():
        print(f"{k:<30} | {v:<15}")

if __name__ == "__main__":
    name = input("Enter a name for this run (e.g. baseline or imc_v1): ")
    generate_necessary_stats("m5out/stats.txt", name)