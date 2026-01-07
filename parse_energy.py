import sys
import re

def get_stat(file_content, stat_name, is_float=False):
    # Matches the stat name and captures the numeric value
    pattern = r"(?:system\.imc\.)?" + stat_name + r"\s+([\d\.]+)"
    match = re.search(pattern, file_content)
    if match:
        return float(match.group(1)) if is_float else int(float(match.group(1)))
    return 0

def calculate_thesis_metrics(stats_file):
    try:
        with open(stats_file, 'r') as f:
            content = f.read()
    except FileNotFoundError:
        print(f"Error: File {stats_file} not found!")
        return

    # --- PART 1: RAW DATA EXTRACTION (From gem5 stats.txt) ---
    # This demonstrates your "Integration" with gem5 and the RISC-V system
    macs = get_stat(content, "numMacOps")
    bit_ops = get_stat(content, "numTotalBitOps")
    sram_reads = get_stat(content, "numSramReads")
    dma_read = get_stat(content, "totalDmaBytesRead") 
    dma_write = get_stat(content, "totalDmaBytesWritten")
    sim_seconds = get_stat(content, "simSeconds", is_float=True)
    
    total_ops = macs * 2  # Standard metric: 1 MAC = 1 Mult + 1 Add

    print(f"\n" + "="*55)
    print(f" STEP 1: RAW DATA EXTRACTED FROM GEM5 STATS")
    print(f" [Functional & Integration Proof]")
    print(f"="*55)
    print(f"  Total MAC Operations:   {macs}")
    print(f"  Total Bit-Level Ops:    {bit_ops}")
    print(f"  SRAM Read Accesses:     {sram_reads}")
    print(f"  DMA Data Read (Bytes):  {dma_read}")
    print(f"  DMA Data Write (Bytes): {dma_write}")
    print(f"  Total Simulation Time:  {sim_seconds:.9f} s")
    print(f"-"*55)

    # --- PART 2: CALIBRATION CONSTANTS (From Sun et al. / Literature) ---
    # You update these when you find the exact numbers in the paper
    E_BIT_OP = 0.015   # Energy per bit-level operation (pJ)
    E_SRAM_RD = 5.0    # Energy per SRAM read access (pJ)
    E_DMA_BYTE = 20.0  # Energy per byte moved across the bus (pJ)

    print(f" STEP 2: CALIBRATION CONSTANTS (LITERATURE)")
    print(f" [Energy Modeling Proof]")
    print(f"-"*55)
    print(f"  Assumed Energy/Bit-Op:  {E_BIT_OP} pJ")
    print(f"  Assumed Energy/SRAM Rd: {E_SRAM_RD} pJ")
    print(f"  Assumed Energy/DMA Byte:{E_DMA_BYTE} pJ")
    print(f"-"*55)

    # --- PART 3: COMPUTED THESIS METRICS ---
    # This fulfills the "Timing and Energy Model" part of your proposal
    e_comp = bit_ops * E_BIT_OP
    e_mem  = sram_reads * E_SRAM_RD
    e_dma  = (dma_read + dma_write) * E_DMA_BYTE
    total_energy_pJ = e_comp + e_mem + e_dma
    
    gops = (total_ops / sim_seconds) / 1e9 if sim_seconds > 0 else 0
    tops_w = (total_ops / total_energy_pJ) if total_energy_pJ > 0 else 0

    print(f" STEP 3: FINAL EVALUATION METRICS")
    print(f" [Thesis Deliverables]")
    print(f"="*55)
    print(f" [TIMING MODEL]")
    print(f"  Accelerator Throughput: {gops:>10.4f} GOPS")
    print(f"  Execution Latency:      {sim_seconds*1000:>10.4f} ms")
    
    print(f"\n [ENERGY MODEL]")
    print(f"  Compute Energy:         {e_comp:>10.2f} pJ")
    print(f"  Memory Access Energy:   {e_mem:>10.2f} pJ")
    print(f"  Integration DMA Energy: {e_dma:>10.2f} pJ")
    print(f"  -------------------------------------------")
    print(f"  TOTAL ENERGY CONSUMED:  {total_energy_pJ/1000:>10.4f} nJ")
    print(f"  ENERGY EFFICIENCY:      {tops_w:>10.4f} TOPS/W")
    print(f"="*55 + "\n")

if __name__ == "__main__":
    file = sys.argv[1] if len(sys.argv) > 1 else "m5out/stats.txt"
    calculate_thesis_metrics(file)