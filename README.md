# DIMC in gem5: System-Level Modeling of Digital In-Memory Computing Accelerators

This repository contains the gem5 model used in:

> **System-Level Modeling of Digital In-Memory Computing Accelerators in gem5 for Edge SoC**
> Tiya Jreige, Tommaso Spagnolo, Cristina Silvano — Politecnico di Milano
> _IEEE International System-on-Chip Conference (SOCC) 2026_

It adds a Digital In-Memory Computing (DIMC) accelerator and a DMA engine to gem5, as memory-mapped devices next to a RISC-V CPU, to evaluate DIMC at the **system level** (CPU + caches + memory + accelerator) instead of in isolation.
Per-configuration energy and area come from a ZigZag-IMC 28 nm cost model, calibrated to a 64.935 fJ/MAC silicon anchor (Tu et al., ISSCC'22).

This is a fork of [gem5](https://github.com/gem5/gem5). The original gem5 README is in [`README-gem5.md`](README-gem5.md).

---

## What is added to gem5

| Path                                                                                  | Content                                                                                                                                                                  |
| ------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `src/imc_acc_model/`                                                                  | DIMC core (`digital_imc_core.cc/.hh`) and DMA engine (`IMC_DMA_Engine.cc/.hh`) SimObjects                                                                                |
| `configs/imc_acc_model/`                                                              | System configurations: `system_validation.py` (main), `system_validation_DSE.py` (DSE), `_noL1D`, `_no_cache` (cache ablations), `cpu_baseline.py`, `validate_stats.py`  |
| `tests/test-progs/imc_acc_model/`                                                     | Bare-metal workloads: `cnn_layer-o2.c` (DIMC driver, im2col + tiling) and `cpu_cnn_layer.c` (CPU-only baseline). The layer shape is set at compile time with `-D` flags. |
| `third_party/zigzag_imc_dimc_28nm/`                                                   | ZigZag-IMC DIMC cost model (BSD-3) and the precomputed per-configuration energy table `dimc_cost_sweep_results.csv`                                                      |
| `run_gemm.py`, `run_resnet_sweep.py`, `run_resnet_sweep_DSE.py`, `extract_results.py` | Experiment drivers and stats extraction (run from the repository root)                                                                                                   |
| `dimc/results/`                                                                       | Published DSE results (`dse_results.csv`)                                                                                                                                |
| `dimc/archive/`                                                                       | Exploratory scripts and plots, **not needed** to reproduce the paper                                                                                                     |

Apart from `.gitignore`, no existing gem5 file is modified; all additions are self-contained.

---

## Build

Requirements: the usual gem5 dependencies, Python 3, and a RISC-V cross-compiler (`riscv64-linux-gnu-gcc`).

```bash
scons build/RISCV/gem5.opt -j$(nproc)
```

---

## Reproduce the paper

All commands are run from the repository root.

### Table V: GEMM batch sweep (DIMC vs CPU)

A 1×1 convolution is a GEMM, so the GEMM is run through the same CNN-layer workloads with `IN_C=32`, `NUM_FILTERS=24` and batch = `IN_W` (1 to 256).
Set `TARGET = "dimc"` or `TARGET = "cpu"` at the top of the script, then:

```bash
python3 run_gemm.py
```

### Table VI: ResNet-20 layers (DIMC vs CPU)

**CPU column**: the 8 ResNet-20 layers on the CPU-only baseline:

```bash
python3 run_resnet_sweep.py
```

**DIMC column**: the case-study configuration Di=32, Do=24, M=1, 1 macro, 2 bit-cycles.
In `run_resnet_sweep_DSE.py`, comment out the full search space and uncomment the single-configuration block below it, then:

```bash
python3 run_resnet_sweep_DSE.py
```

### Design Space Exploration (108 configurations)

| Parameter               | Values      |
| ----------------------- | ----------- |
| Di (rows)               | 32, 64, 128 |
| Do (columns)            | 16, 24, 32  |
| M (weight-reuse factor) | 1, 2, 4     |
| Parallel macros         | 1, 2        |
| Bit-serial cycles       | 1, 2        |

108 configurations × 8 ResNet-20 layers = 864 gem5 simulations. Each configuration uses its own energy/MAC from the ZigZag-IMC table.

```bash
python3 run_resnet_sweep_DSE.py
```

Results are written to `dse_results.csv` in the repository root.
**Note:** the script deletes any existing `dse_results.csv` in the root before starting. The published results in `dimc/results/` are not affected.

The published `dimc/results/dse_results.csv` comes from a later rerun of the sweep; it matches the paper's numbers up to rounding.

---

## Citation

```bibtex
@inproceedings{jreige2026dimc,
  author    = {Jreige, Tiya and Spagnolo, Tommaso and Silvano, Cristina},
  title     = {System-Level Modeling of Digital In-Memory Computing Accelerators in gem5 for Edge SoC},
  booktitle = {Proceedings of the IEEE International System-on-Chip Conference (SOCC)},
  year      = {2026},
  note      = {DOI and pages to be added}
}
```

---

## License and credits

- gem5 is distributed under its original license (see `LICENSE` and `COPYING`).
- The DIMC cost model in `third_party/zigzag_imc_dimc_28nm/` is based on [ZigZag-IMC](https://github.com/KULeuven-MICAS/zigzag-imc) (BSD-3, see its `LICENSE`).
