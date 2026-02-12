import m5
from m5.objects import *
import sys

# ==============================================================================
#                      CONFIGURATION DATABASE (From Your Excel)
# ==============================================================================
configurations = [
    # ID 0: Paper 1 (JSSC) - Baseline 8-bit
    {
        "name": "Paper 1 (JSSC) - 8-bit Baseline",
        "di": 144, "do_dim": 16, "m": 1, "macros": 1,
        "bi": 8, "bw": 8, "bcycles": 1, 
        "freq": "1.49GHz", "energy_mac": 22.88
    },

    # ID 1: Paper 2 (ISSCC) - 4-bit High Speed
    {
        "name": "Paper 2 (ISSCC) - 4-bit High Freq",
        "di": 64, "do_dim": 1, "m": 1, "macros": 64, 
        "bi": 4, "bw": 4, "bcycles": 1, 
        "freq": "1.44GHz", "energy_mac": 47.73
    },

    # ID 2: Paper 2 (ISSCC) - 4-bit Low Power
    {
        "name": "Paper 2 (ISSCC) - 4-bit Low Freq",
        "di": 64, "do_dim": 1, "m": 1, "macros": 64, 
        "bi": 4, "bw": 4, "bcycles": 1, 
        "freq": "0.36GHz", "energy_mac": 46.62
    },

    # ID 3: Paper 3 (Digital) - 4-bit
    {
        "name": "Paper 3 (Digital) - 4-bit High Density",
        "di": 8, "do_dim": 32, "m": 16, "macros": 1, 
        "bi": 4, "bw": 4, "bcycles": 1, 
        "freq": "2.0GHz", "energy_mac": 10.54
    },

    # ID 4: Paper 5 (Multi-cycle) - 4-bit
    {
        "name": "Paper 5 (Multi-cycle) - 4-bit",
        "di": 64, "do_dim": 16, "m": 1, "macros": 1, 
        "bi": 4, "bw": 4, "bcycles": 4, 
        "freq": "0.654GHz", "energy_mac": 16.53
    },

    # ID 5: Paper 7 (Parallel) - 8-bit
    {
        "name": "Paper 7 (Parallel) - 8-bit",
        "di": 32, "do_dim": 6, "m": 1, "macros": 64, 
        "bi": 8, "bw": 8, "bcycles": 2, 
        "freq": "0.22GHz", "energy_mac": 64.94
    },

    # ID 6: Paper 8 (Low Power) - 8-bit
    {
        "name": "Paper 8 (Low Power) - 8-bit",
        "di": 32, "do_dim": 1, "m": 16, "macros": 2, 
        "bi": 8, "bw": 8, "bcycles": 1, 
        "freq": "0.333GHz", "energy_mac": 73.05
    }
]

# ==============================================================================
#                               ARGUMENT PARSER
# ==============================================================================
TEST_MODE = 0
CONFIG_ID = 0

print(f"\n========================================================")
print(f"      IMC ACCELERATOR VERIFICATION SUITE")
print(f"========================================================")

if len(sys.argv) > 1:
    TEST_MODE = int(sys.argv[1])
    mode_names = ["Unpacked Peak", "Packed Peak", "Unpacked Gating", "Packed Gating"]
    print(f" > Test Mode: {TEST_MODE} ({mode_names[TEST_MODE] if 0 <= TEST_MODE < 4 else 'Unknown'})")
else:
    print(" > No Test Mode selected. Defaulting to 0 (Unpacked Peak).")

if len(sys.argv) > 2:
    CONFIG_ID = int(sys.argv[2])
    if CONFIG_ID >= len(configurations):
        print(f"Error: Config ID {CONFIG_ID} out of range! (Max: {len(configurations)-1})")
        sys.exit(1)
else:
    print(" > No Config ID selected. Defaulting to 0.")

# Load Config
config = configurations[CONFIG_ID]
print(f" > Hardware Target: [{CONFIG_ID}] {config['name']}")
print(f"     | Array Size: {config['di']} Rows x {config['do_dim']*config['macros']} Cols ({config['numMacros' if 'numMacros' in config else 'macros']} Macros)")
print(f"     | Precision:  {config['bi']}-bit Inputs / {config['bw']}-bit Weights")
print(f"     | Processing: {config['bcycles']} bits/cycle")
print(f"     | Frequency:  {config['freq']}")
print("--------------------------------------------------------\n")

# --- AUTO-SWITCH FOR PACKED TESTS ---
# If user requests packed test on a paper that supports 4-bit, we are good.
# But if user runs Mode 1 on an 8-bit paper, we should warn or temporary switch.
if (TEST_MODE == 1 or TEST_MODE == 3) and config['bi'] > 4:
    print(">>> WARNING: Running Packed Test (4-bit) on >4-bit Hardware!")
    print(">>> Forcing Hardware Precision to 4-bit for this test to verify Bandwidth Logic.")
    config['bi'] = 4
    config['bw'] = 4

# ==============================================================================
#                               GEM5 SETUP
# ==============================================================================
root = Root(full_system=False)
root.clk_domain = SrcClockDomain(clock=config['freq'], voltage_domain=VoltageDomain())

root.core = DigitalIMCCore()
root.core.di = config['di']
root.core.do_dim = config['do_dim']
root.core.m_batches = config['m']
root.core.num_macros = config['macros']
root.core.bi = config['bi']
root.core.bw = config['bw']
root.core.bcycles = config['bcycles']
root.core.energy_per_mac_fj = config['energy_mac']

root.tester = IMCTester()
root.tester.core = root.core
root.tester.test_mode = TEST_MODE 

m5.instantiate()
print("--- Starting Simulation ---")
exit_event = m5.simulate()

print(f"\n--- Simulation Finished @ {m5.curTick()} ticks ---")

m5.stats.dump()
print(f"Stats written to m5out/stats.txt")