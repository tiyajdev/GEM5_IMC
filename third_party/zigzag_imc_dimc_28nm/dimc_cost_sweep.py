"""
DIMC macro cost sweep via the ZigZag-IMC 28nm analytical cost model.

Cost model: Sun, Houshmand, Verhelst, "Analog or Digital In-Memory
Computing? Benchmarking through Quantitative Modeling," ICCAD 2023.
Vendored from https://github.com/KULeuven-MICAS/zigzag-imc (BSD-3-Clause);
see zigzag_imc_cost_model.py for the single modified return statement.

Anchor configuration: Tu et al., ISSCC 2022 (paper 15.5) -- the 28nm
silicon baseline of the system-level case study. The cost model's energy
estimate for this chip matches reported silicon within 1%.

--------------------------------------------------------------------------
Mapping of the gem5 DigitalIMCCore parameters onto this cost model
--------------------------------------------------------------------------

Circuit style frozen to the anchor chip: Booth-encoded multipliers,
ripple-carry adder trees, 6T cells, no pipeline register, 0.9 V, 32-bit
accumulator (matching the gem5 model's accumulation buffer), 50% input
toggle rate and 50% weight sparsity (the model's validation assumptions).

Approximation: SRAM bank area is scaled linearly with bitcount from the
anchor's CACTI-derived value (bank read energy is zeroed inside the cost
model itself as negligible, so this approximation affects area only).
--------------------------------------------------------------------------
"""
import math
import csv
from zigzag_imc_cost_model import dimc_cost_estimation

# 28nm unit-gate parameters (area um2, delay ns, input cap fF), extracted
# from foundry data by the ZigZag-IMC authors (see their dimc_validation.py)
TECH_28NM = {"unit_area": 0.614, "unit_delay": 0.0478, "unit_cap": 0.7}

ACTIVATION_PRECISION = 8  # bi of the DSE runs (anchor chip: signed INT8)
WEIGHT_PRECISION = 8      # bw of the DSE runs

ANCHOR_BANK_BITS = 32 * 48  # anchor bank geometry: rows=32, cols=48
ANCHOR_CACTI = {            # CACTI values for the anchor bank (from dimc_validation.py)
    "delay": 0.0669052,
    "r_energy": 0.000221196 * 10**6 / 64 * 81,
    "w_energy": 0.000328423 * 10**6 / 64 * 81,
    "area": 0.00065545,
}
ANCHOR_REPORTED_TOPSW = 36.63  # silicon-reported peak TOP/s/W of the anchor chip
PAPER_EMAC_FJ = 64.935

# DSE search space (identical to the paper's 108-point grid)
DI_SWEEP = (32, 64, 128)
DO_SWEEP = (16, 24, 32)
M_SWEEP = (1, 2, 4)
NUM_MACROS_SWEEP = (1, 2)
BCYCLES_SWEEP = (1, 2)


def build_dimc_config(di, do_dim, m_batches, num_macros, bcycles):
    """Map the gem5 DigitalIMCCore parameters onto a ZigZag-IMC config dict."""
    rows = di * m_batches            # M resident weight sets (cell_group)
    cols = do_dim * WEIGHT_PRECISION # 8 columns per stored weight
    config = {
        "paper_idx": f"Di{di}_Do{do_dim}_M{m_batches}_nm{num_macros}_bc{bcycles}",
        "input_toggle_rate": 0.5,
        "weight_sparsity": 0.5,
        "activation_precision": 8,
        "weight_precision": 8,
        "output_precision": 8,
        "input_precision": bcycles,  # input bits processed per cycle
        "input_channel": di,
        "output_channel": do_dim,
        "booth_encoding": True,
        "multiplier_precision": WEIGHT_PRECISION,
        "adder_input_precision": WEIGHT_PRECISION + 1,
        "accumulator_input_precision": WEIGHT_PRECISION + 1 + int(math.log2(di)),
        "accumulator_precision": 32,
        "reg_accumulator_precision": 32,
        "reg_input_bitwidth": di * bcycles,
        "pipeline": False,
        "vdd": 0.9,
        "rows": rows,
        "cols": cols,
        "banks": num_macros,
        # dummies -- only used by the model's validation-mismatch outputs:
        "area": 1.0, "tclk": 1.0, "TOP/s": 1.0, "TOP/s/W": 1.0,
        **TECH_28NM,
    }
    cacti = dict(ANCHOR_CACTI)
    cacti["area"] = ANCHOR_CACTI["area"] * (rows * cols) / ANCHOR_BANK_BITS
    return config, cacti


def query_dimc_costs(di, do_dim, m_batches, num_macros, bcycles):
    """Return (fJ_per_MAC, area_mm2, min_tclk_ns) for one hardware config.

    This is the function a DSE driver imports; fJ_per_MAC feeds the
    DigitalIMCCore.energy_per_mac_fj parameter.
    """
    config, cacti = build_dimc_config(di, do_dim, m_batches, num_macros, bcycles)
    r = dimc_cost_estimation(config, cacti)
    anchor_cfg, anchor_cacti = build_dimc_config(32, 24, 1, 1, 2)
    anchor_fj = dimc_cost_estimation(anchor_cfg, anchor_cacti)["fJ_per_MAC"]
    return PAPER_EMAC_FJ * r["fJ_per_MAC"] / anchor_fj, r["area_mm2"], r["tclk_ns"]


def validate_anchor():
    """Reproduce the silicon anchor chip; returns its predicted fJ/MAC."""
    from dimc_validation import dimc_ISSCC2022_15_5, cacti_ISSCC2022_15_5
    anchor = dict(dimc_ISSCC2022_15_5)
    anchor.update(TECH_28NM)
    r = dimc_cost_estimation(anchor, cacti_ISSCC2022_15_5)
    silicon = 2000 / ANCHOR_REPORTED_TOPSW
    print("ANCHOR (Tu et al., ISSCC 2022 15.5):")
    print(f"  model {r['fJ_per_MAC']:.2f} fJ/MAC vs silicon {silicon:.2f} fJ/MAC"
          f" (mismatch {r['energy_mismatch']*100:.1f}%)")
    print(f"  model {r['area_mm2']:.3f} mm2 (mismatch {r['area_mismatch']*100:.1f}%)")
    return r["fJ_per_MAC"]


def main():
    validate_anchor()  # installation check only
    anchor_fj, _, _ = query_dimc_costs(32, 24, 1, 1, 2)
    results = []
    for di in DI_SWEEP:
        for do_dim in DO_SWEEP:
            for m in M_SWEEP:
                for nm in NUM_MACROS_SWEEP:
                    for bc in BCYCLES_SWEEP:
                        fj, area, tclk = query_dimc_costs(di, do_dim, m, nm, bc)
                        results.append({
                            "Di": di, "Do": do_dim, "M": m,
                            "num_macros": nm, "bcycles": bc,
                            "fJ_per_MAC": round(fj, 3),
                            "E_scale_vs_anchor": round(fj / anchor_fj, 4),
                            "area_mm2": round(area, 5),
                            "min_tclk_ns": round(tclk, 4),
                        })

    out = "dimc_cost_sweep_results.csv"
    with open(out, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=results[0].keys())
        writer.writeheader()
        writer.writerows(results)
    print(f"\n{len(results)} configurations written to {out}\n")

    header = (f"{'Di':>4} {'Do':>3} {'M':>3} {'nm':>3} {'bc':>3} {'fJ/MAC':>8}"
              f" {'xAnchor':>8} {'mm2':>9} {'tclk':>7}")
    print(header)
    print("-" * len(header))
    for r in results:
        print(f"{r['Di']:>4} {r['Do']:>3} {r['M']:>3} {r['num_macros']:>3}"
              f" {r['bcycles']:>3} {r['fJ_per_MAC']:>8} {r['E_scale_vs_anchor']:>8}"
              f" {r['area_mm2']:>9} {r['min_tclk_ns']:>7}")


if __name__ == "__main__":
    main()