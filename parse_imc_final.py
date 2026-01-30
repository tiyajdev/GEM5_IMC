import os
import sys

def parse_stats(stats_file):
    if not os.path.exists(stats_file):
        print(f"Error: File {stats_file} not found.")
        return

    # Map Friendly Names to the Exact Gem5 Stat Strings you provided
    targets = {
        # --- 1. Simulation Basics ---
        "Sim Time (s)": "simSeconds",

        # --- 2. Core Stats ---
        "Core Total Ops":       "system.accelerator.imc_core.total_ops",
        "Core Energy (fJ)":     "system.accelerator.imc_core.total_energy_fj",
        "Core Latency (Ticks)": "system.accelerator.imc_core.total_processing_latency",

        # --- 3. Control Plane (CPU <-> Accelerator MMIO) ---
        "MMIO Pkt Count":       "system.membus.pktCount_system.cpu.dcache_port::system.accelerator.pio_port",
        "MMIO Pkt Size (B)":    "system.membus.pktSize_system.cpu.dcache_port::system.accelerator.pio_port",

        # --- 4. Data Plane (Accelerator DMA <-> Memory Controller) ---
        "DMA Bus Pkt Count":    "system.membus.pktCount_system.accelerator.dma::system.mem_ctrl.port",
        "DMA Bus Total Pkts":   "system.membus.pktCount_system.accelerator.dma::total",
        "DMA Bus Size (B)":     "system.membus.pktSize_system.accelerator.dma::system.mem_ctrl.port",
        "DMA Bus Total Size":   "system.membus.pktSize_system.accelerator.dma::total",

        # --- 5. Input SRAM Stats ---
        "InSRAM Bytes Read (Accel)":    "system.input_sram.bytesRead::accelerator",
        "InSRAM Bytes Read (Total)":    "system.input_sram.bytesRead::total",
        "InSRAM Bytes Written (Accel)": "system.input_sram.bytesWritten::accelerator",
        "InSRAM Bytes Written (Total)": "system.input_sram.bytesWritten::total",
        "InSRAM Num Reads (Accel)":     "system.input_sram.numReads::accelerator",
        "InSRAM Num Reads (Total)":     "system.input_sram.numReads::total",
        "InSRAM Num Writes (Accel)":    "system.input_sram.numWrites::accelerator",
        "InSRAM Num Writes (Total)":    "system.input_sram.numWrites::total",
        "InSRAM BW Read (Accel)":       "system.input_sram.bwRead::accelerator",
        "InSRAM BW Read (Total)":       "system.input_sram.bwRead::total",
        "InSRAM BW Write (Accel)":      "system.input_sram.bwWrite::accelerator",
        "InSRAM BW Write (Total)":      "system.input_sram.bwWrite::total",
        "InSRAM BW Total (Accel)":      "system.input_sram.bwTotal::accelerator",
        "InSRAM BW Total (Total)":      "system.input_sram.bwTotal::total",

        # --- 6. Output SRAM Stats ---
        "OutSRAM Bytes Read (Accel)":    "system.output_sram.bytesRead::accelerator",
        "OutSRAM Bytes Read (Total)":    "system.output_sram.bytesRead::total",
        "OutSRAM Bytes Written (Accel)": "system.output_sram.bytesWritten::accelerator",
        "OutSRAM Bytes Written (Total)": "system.output_sram.bytesWritten::total",
        "OutSRAM Num Reads (Accel)":     "system.output_sram.numReads::accelerator",
        "OutSRAM Num Reads (Total)":     "system.output_sram.numReads::total",
        "OutSRAM Num Writes (Accel)":    "system.output_sram.numWrites::accelerator",
        "OutSRAM Num Writes (Total)":    "system.output_sram.numWrites::total",
        "OutSRAM BW Read (Accel)":       "system.output_sram.bwRead::accelerator",
        "OutSRAM BW Read (Total)":       "system.output_sram.bwRead::total",
        "OutSRAM BW Write (Accel)":      "system.output_sram.bwWrite::accelerator",
        "OutSRAM BW Write (Total)":      "system.output_sram.bwWrite::total",
        "OutSRAM BW Total (Accel)":      "system.output_sram.bwTotal::accelerator",
        "OutSRAM BW Total (Total)":      "system.output_sram.bwTotal::total",

        # --- 7. Main Memory (DDR) Details ---
        "DRAM Bytes Read (Accel)":    "system.mem_ctrl.dram.bytesRead::accelerator",
        "DRAM Bytes Written (Accel)": "system.mem_ctrl.dram.bytesWritten::accelerator",
        "DRAM Read BW (Accel)":       "system.mem_ctrl.dram.bwRead::accelerator",
        "DRAM Write BW (Accel)":      "system.mem_ctrl.dram.bwWrite::accelerator",
        "DRAM Avg Latency (Ticks)":   "system.mem_ctrl.dram.avgMemAccLat",
        "DRAM Bus Util (%)":          "system.mem_ctrl.dram.busUtil",
        "DRAM Row Hit Rate (%)":      "system.mem_ctrl.dram.pageHitRate",

        # --- SRAM POWER (From SimplePowerModel) ---
        "InSRAM Dynamic (J)":   "system.input_sram.power_model.dynamic_energy",
        "InSRAM Static (J)":    "system.input_sram.power_model.static_energy",
        "OutSRAM Dynamic (J)":  "system.output_sram.power_model.dynamic_energy",
        "OutSRAM Static (J)":   "system.output_sram.power_model.static_energy",

        # --- DRAM POWER (Supports up to 8 Ranks) ---
        "DRAM Rank0 (pJ)": "system.mem_ctrl.dram.rank0.totalEnergy",
        "DRAM Rank1 (pJ)": "system.mem_ctrl.dram.rank1.totalEnergy",
        "DRAM Rank2 (pJ)": "system.mem_ctrl.dram.rank2.totalEnergy",
        "DRAM Rank3 (pJ)": "system.mem_ctrl.dram.rank3.totalEnergy",
        "DRAM Rank4 (pJ)": "system.mem_ctrl.dram.rank4.totalEnergy",
        "DRAM Rank5 (pJ)": "system.mem_ctrl.dram.rank5.totalEnergy",
        "DRAM Rank6 (pJ)": "system.mem_ctrl.dram.rank6.totalEnergy",
        "DRAM Rank7 (pJ)": "system.mem_ctrl.dram.rank7.totalEnergy"
    }

    data = {}

    print(f"--- Parsing {stats_file} ---")
    
    with open(stats_file, 'r') as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                gem5_name = parts[0]
                value = parts[1]
                
                # Check match
                for friendly, target_stat in targets.items():
                    if gem5_name == target_stat:
                        try:
                            # If value is 'nan', store as 0
                            if value.lower() == 'nan':
                                data[friendly] = 0.0
                            else:
                                data[friendly] = float(value)
                        except ValueError:
                            pass

# ---------------------------------------------------------
    # A. ENERGY CALCULATIONS (Normalize everything to pJ)
    # ---------------------------------------------------------
    
    # 1. Core Energy (fJ -> pJ)
    e_core_pj = data.get("Core Energy (fJ)", 0) / 1000.0
    
# 2. SRAM Energy (Calculated Manually)
    # Constants for your 64KB SRAMs
    SRAM_ENERGY_PER_OP_PJ = 5.0   # 5 pJ per Read or Write
    SRAM_LEAKAGE_MW       = 1.0   # 1 mW Static Leakage
    
    # Get Simulation Time for Leakage Calculation
    sys_time_s = data.get("Sim Time (s)", 0)
    
    # Input SRAM Calculation
    in_accesses = data.get("InSRAM Num Reads (Total)", 0) + \
                  data.get("InSRAM Num Writes (Total)", 0)
    
    # Output SRAM Calculation
    out_accesses = data.get("OutSRAM Num Reads (Total)", 0) + \
                   data.get("OutSRAM Num Writes (Total)", 0)
                   
    # Total SRAM Dynamic Energy (Ops * 5pJ)
    e_sram_dynamic_pj = (in_accesses + out_accesses) * SRAM_ENERGY_PER_OP_PJ
    
    # Total SRAM Static Energy (Time * Power)
    # 1 mW = 0.001 J/s.  1 Joule = 1e12 pJ.
    # Therefore: 1 mW = 1e9 pJ per second.
    e_sram_static_pj = (sys_time_s * SRAM_LEAKAGE_MW) * 1e9 * 2 # x2 for two SRAMs
    
    e_sram_pj = e_sram_dynamic_pj + e_sram_static_pj

    # 3. DRAM Energy (Sum of all found ranks)
    e_dram_pj = 0
    # Loop through keys we defined above
    for i in range(8): 
        e_dram_pj += data.get(f"DRAM Rank{i} (pJ)", 0)

    # 4. Total System Energy
    e_total_pj = e_core_pj + e_sram_pj + e_dram_pj

    # ---------------------------------------------------------
    # B. PERFORMANCE CALCULATIONS
    # ---------------------------------------------------------
    
    # 1. System Time (Wall Clock)
    sys_time_s = data.get("Sim Time (s)", 1e-9)
    sys_time_ns = sys_time_s * 1e9

    # 1. Core Latency (Ticks -> ns)
    # Assuming 1 Tick = 1 ps (Default Gem5)
    core_latency_ticks = data.get("Core Latency (Ticks)", 0)
    core_latency_ns = core_latency_ticks / 1000.0
    
    # 2. Throughput
    ops = data.get("Core Total Ops", 0)
    
    # Validated Throughput (Hardware Limit)
    validation_gops = 0
    if core_latency_ns > 0:
        validation_gops = (ops / core_latency_ns) * 2.0 # x2 for Ops vs MACs

    # System Throughput (Real World Speed)
    system_gops = 0
    if sys_time_s > 0:
        system_gops = (ops / sys_time_s) / 1e9 * 2.0

    # System Efficiency (TOPS/W)
    # TOPS/W = Ops / pJ
    sys_efficiency = 0
    if e_total_pj > 0:
        sys_efficiency = (ops / e_total_pj) * 2.0

    # Unit Efficiency (Core Only - for Paper Check)
    core_efficiency = 0
    if e_core_pj > 0:
        core_efficiency = (ops / e_core_pj) * 2.0

    # --- Print Report ---
    print("\n" + "="*50)
    print("      IMC ACCELERATOR FINAL REPORT      ")
    print("="*50)

# 1. VALIDATION (Unit Level)
    print(f"\n[1] UNIT VALIDATION (Hardware Model vs Paper)")
    print(f"  Active Time:          {core_latency_ns:,.2f} ns")
    print(f"  Throughput (Core):    {validation_gops:.4f} GOPS  <-- Should match ~858")
    print(f"  Efficiency (Core):    {core_efficiency:.2f} TOPS/W <-- Should match ~87")

    # 2. SYSTEM LEVEL (Integration)
    print(f"\n[2] SYSTEM INTEGRATION (Real Performance)")
    print(f"  Wall Time:            {sys_time_ns:,.2f} ns")
    print(f"  Throughput (System):  {system_gops:.6f} GOPS")
    print(f"  Efficiency (System):  {sys_efficiency:.2f} TOPS/W (Includes DRAM/SRAM)")

    # 3. ENERGY BREAKDOWN
    print(f"\n[3] ENERGY BREAKDOWN")
    print(f"  Core Logic:           {e_core_pj:,.2f} pJ")
    print(f"  SRAMs:                {e_sram_pj:,.2f} pJ")
    print(f"  DRAM (Memory):        {e_dram_pj:,.2f} pJ")
    print(f"  -------------------------------------------")
    print(f"  TOTAL:                {e_total_pj:,.2f} pJ")

    # Helper to safely print
    def print_section(title, keys):
        print(f"\n[{title}]")
        for key in keys:
            val = data.get(key, 0)
            # Format large numbers nicely
            if val > 1000000:
                print(f"  {key:<30} {val:,.0f}")
            else:
                print(f"  {key:<30} {val:.2f}")

    # 1. Core Logic
    print_section("CORE LOGIC", ["Core Total Ops", "Core Energy (fJ)"])
    
    # 2. Control Plane
    print_section("CONTROL PLANE (MMIO)", ["MMIO Pkt Count", "MMIO Pkt Size (B)"])

    # 3. Data Plane (DMA)
    print_section("DATA PLANE (DMA BUS)", ["DMA Bus Pkt Count", "DMA Bus Size (B)"])

    # 4. Input SRAM
    print_section("INPUT SRAM TRAFFIC", [
        "InSRAM Bytes Read (Accel)", "InSRAM Bytes Written (Accel)",
        "InSRAM Num Reads (Accel)", "InSRAM Num Writes (Accel)",
        "InSRAM BW Total (Accel)"
    ])

    # 5. Output SRAM
    print_section("OUTPUT SRAM TRAFFIC", [
        "OutSRAM Bytes Read (Accel)", "OutSRAM Bytes Written (Accel)",
        "OutSRAM Num Reads (Accel)", "OutSRAM Num Writes (Accel)",
        "OutSRAM BW Total (Accel)"
    ])

    # 6. Main Memory Details
    print_section("MAIN MEMORY (DDR3) PERFORMANCE", [
        "DRAM Bytes Read (Accel)", "DRAM Bytes Written (Accel)",
        "DRAM Read BW (Accel)", "DRAM Write BW (Accel)",
        "DRAM Avg Latency (Ticks)", "DRAM Bus Util (%)", "DRAM Row Hit Rate (%)"
    ])
    
    print("\n" + "="*50)

if __name__ == "__main__":
    filename = sys.argv[1] if len(sys.argv) > 1 else "m5out/stats.txt"
    parse_stats(filename)