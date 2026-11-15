import sys
import os
import argparse
import csv

def parse_gem5_stats(stat_file, label, workload, di, do, m, nm, bc):
    if not os.path.exists(stat_file):
        print(f"Error: File '{stat_file}' not found.")
        return

    data = {}
    
    # ------------------------------------------------------------------------
    # DEFINITIONS: Updated to match your specific gem5 stat labels
    # ------------------------------------------------------------------------
    target_stats = [
        "simSeconds", 
        "simInsts", 
        "system.cpu.cpi",
        
        # --- Accelerator ---
        "system.imc.core.total_ops",                
        "system.imc.core.total_energy_fj",          
        "system.imc.core.total_processing_latency", 
        
        # --- Cache Activity (Using Tag Accesses for Physical Accuracy) ---
        "system.cpu_icache.tags.tagAccesses", 
        "system.cpu_icache.ReadReq.hits::total",
        "system.cpu_icache.ReadReq.misses::total",
        "system.cpu_dcache.tags.tagAccesses",
        "system.cpu_dcache.ReadReq.hits::total",   # For Data Reads
        "system.cpu_dcache.WriteReq.hits::total",  # For Demand Writes
        "system.cpu_dcache.overallMisses::total", # For Refills (Data Writes)
        
        # --- SRAM Activity (Using Access Counts) ---
        "system.sram.numReads::total",
        "system.sram.numWrites::total",
        
        # --- Traffic Breakdown (Bytes) ---
        "system.sram.bytesRead::cpu.inst",          
        "system.sram.bytesRead::cpu.data",          
        "system.sram.bytesWritten::cpu.data",       
        "system.sram.bytesRead::imc",               
        "system.sram.bytesWritten::imc",            
    ]

    # Read the file (Scanning for the second "Begin Simulation Statistics" block)
    with open(stat_file, 'r') as f:
        for line in f:
            parts = line.split()
            if not parts: continue
            stat_name = parts[0]
            if stat_name in target_stats:
                try:
                    data[stat_name] = float(parts[1])
                except ValueError: continue

    def get_val(key): return data.get(key, 0.0)

# ------------------------------------------------------------------------
    # PHYSICAL CONSTANTS (Updated from CACTI 28nm LSTP 2nd Runs)
    # ------------------------------------------------------------------------
    
    E_TAG     = 0.8255    # The "Investigation" cost
    E_DATA_RD = 4.5679    # Data array only (Read)
    E_DATA_WR = 4.3301    # Data array only (Write): 5.1557 - 0.8255

    E_SRAM_READ_PJ = 26.88 # From 512KB Scratchpad result
    E_SRAM_WRITE_PJ = 22.32 # From 512KB Scratchpad result
    E_CPU_INST_PJ = 20.40  # Your scaled Rocket Core Baseline

    # ------------------------------------------------------------------------
    # CALCULATION & REPORTING
    # ------------------------------------------------------------------------
    print("\n" + "="*66)
    print(f"{'METRIC':<40} | {'VALUE':<20}")
    print("="*66)

    # 1. CPU
    print(f"{'Total Runtime':<40} | {get_val('simSeconds'):.6f} s")
    print(f"{'CPU Instructions':<40} | {int(get_val('simInsts'))}")
    print(f"{'CPU CPI':<40} | {get_val('system.cpu.cpi'):.3f}")

    # 2. IMC Computing
    imc_ops = get_val("system.imc.core.total_ops")
    imc_energy_pj = get_val("system.imc.core.total_energy_fj") / 1000.0
    print(f"{'IMC MAC Operations':<40} | {int(imc_ops)}")
    print(f"{'IMC Internal Compute Energy':<40} | {imc_energy_pj:.2f} pJ")

    # 3. Cache Energy (L1I + L1D)
    # Every tag access is a physical event
    l1i_tag_accesses = get_val("system.cpu_icache.tags.tagAccesses")
    l1i_read_accesses = get_val("system.cpu_icache.ReadReq.hits::total")
    l1i_read_misses = get_val("system.cpu_icache.ReadReq.misses::total")


    l1d_tag_accesses = get_val("system.cpu_dcache.tags.tagAccesses")
    l1d_read_accesses = get_val("system.cpu_dcache.ReadReq.hits::total")
    l1d_write_accesses = get_val("system.cpu_dcache.WriteReq.hits::total")
    l1d_misses = get_val("system.cpu_dcache.overallMisses::total")
    
    cache_energy = (l1d_tag_accesses + l1i_tag_accesses) * E_TAG + (l1i_read_accesses + l1d_read_accesses) * E_DATA_RD + (l1d_write_accesses + l1d_misses + l1i_read_misses) * E_DATA_WR
    print(f"{'L1 Cache Energy (Read+Write)':<40} | {cache_energy:.2f} pJ")

    # 4. SRAM Energy (The Data Movement Cost)
    sram_rd = get_val("system.sram.numReads::total")
    sram_wr = get_val("system.sram.numWrites::total")
    sram_energy = (sram_rd * E_SRAM_READ_PJ) + (sram_wr * E_SRAM_WRITE_PJ)
    print(f"{'Global SRAM Energy (Data Movement)':<40} | {sram_energy:.2f} pJ")

    # 5. CPU Instruction Energy
    cpu_insts = get_val("simInsts")
    cpu_energy_pj = cpu_insts * E_CPU_INST_PJ
    print(f"{'CPU Compute Energy':<40} | {cpu_energy_pj:.2f} pJ")

    # 6. Total Energy (Updated to include CPU)
    total_sys_energy_nj = (imc_energy_pj + cache_energy + sram_energy + cpu_energy_pj) / 1000.0
    print("-" * 66)
    print(f"{'TOTAL SYSTEM DYNAMIC ENERGY':<40} | {total_sys_energy_nj:.4f} nJ")
    print("="*66 + "\n")

    # 1. Get the Data Movement stats
    sram_accesses = get_val("system.sram.numReads::total") + get_val("system.sram.numWrites::total")
    imc_bytes = get_val("system.sram.bytesRead::imc") + get_val("system.sram.bytesWritten::imc")
    latency = get_val('simSeconds')


    # 2. Write to CSV
    csv_file = 'dse_results.csv'
    write_header = not os.path.exists(csv_file)
    with open(csv_file, 'a', newline='') as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(["Config", "WL", "di", "do", "m", "nm", "bc",
                             "latency_s", "sram_accesses", "imc_bytes",
                             "imc_ops", "cpu_insts",
                             "imc_energy_pj", "cache_energy_pj", "sram_energy_pj",
                             "cpu_energy_pj", "total_energy_nj"])
        writer.writerow([label, workload, di, do, m, nm, bc, latency,
                         sram_accesses, imc_bytes,
                         int(imc_ops), int(cpu_insts),
                         imc_energy_pj, cache_energy, sram_energy,
                         cpu_energy_pj, total_sys_energy_nj])

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    # These catch the info from your Main DSE script
    parser.add_argument("--label", type=str, default="R32")
    parser.add_argument("--wl", type=str, default="WL1")
    parser.add_argument("--di", type=int, default=32)
    parser.add_argument("--do", type=int, default=16)
    parser.add_argument("--m", type=int, default=1)
    parser.add_argument("--nm", type=int, default=1)
    parser.add_argument("--bc", type=int, default=1)
    # This catches the stats file path if you ever change it
    parser.add_argument("--stats", type=str, default="m5out/stats.txt")
    
    args = parser.parse_args()

    # Call your function and pass all the DSE tags to it
    parse_gem5_stats(args.stats, args.label, args.wl, args.di, args.do, args.m, args.nm, args.bc)

    