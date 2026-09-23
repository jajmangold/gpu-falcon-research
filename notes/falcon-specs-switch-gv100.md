# Falcon Specs - GV100/CMP/V100 and Nintendo Switch

Research date: 2026-05-19

This note summarizes public Falcon architecture details relevant to local GV100 CMP/V100-personality cards and Nintendo Switch/Tegra X1. It is intentionally non-operational: no firmware bypass procedure, no exploit chain, and no flash steps.

## Short Version

Falcon is not one engine. It is NVIDIA's small embedded control CPU design, instantiated inside many hardware blocks. On desktop/data-center GPUs, Falcon-family firmware commonly appears around security, bootstrapping, power management, video, and graphics context control. On Nintendo Switch/Tegra X1, the most relevant instance is TSEC, a Falcon-backed security processor used in the boot/key-generation path.

The useful comparison is:

- Shared primitive: Falcon CPU core with IMEM/DMEM, IO registers, interrupts, timers, DMA/xfer, and optional crypto/security hardware.
- Switch focus: TSEC Falcon plus SCP/security behavior, console key generation, and boot-chain enforcement.
- GV100 focus: ACR/SEC2/FECS/GPCCS/NVDEC/PMU-style firmware, signed ucode loading, graphics/context initialization, and possible driver/VBIOS feature exposure.

## Public Falcon Architecture

From envytools and Linux kernel NOVA docs:

- Falcon is a class of NVIDIA general-purpose microprocessor units used in multiple GPU blocks.
- A Falcon unit has a core CPU, separate code/data SRAM, host-visible IO space, interrupts, timers, scratch registers, and optional FIFO/memory/crypto support.
- Falcon has 16 32-bit general registers plus special registers for interrupt vectors, trap vector, stack pointer, program counter, external DMA bases, flags, crypto controls, and transfer target selection.
- Code and data live in internal SRAM; external memory movement happens through Falcon transfer/DMA mechanisms.
- Modern secure Falcon flows use security levels commonly described as NS, LS, and HS. The Linux NOVA docs describe root-of-trust establishment as Boot ROM on Falcon -> HS ucode -> LS/NS ucode.

Important implication: if a card is enforcing signed firmware, the control point is usually the signed ucode chain and the engine-specific policy it initializes, not just a simple host-side register toggle.

## GV100 / V100 / CMP 100-210 Context

Local host firmware inventory confirms GV100 firmware families are installed under `/lib/firmware/nvidia/gv100/` and `/usr/lib/firmware/nvidia/gv100/`:

```text
acr/ucode_load.bin.zst
acr/ucode_unload.bin.zst
gr/fecs_bl.bin.zst
gr/fecs_data.bin.zst
gr/fecs_inst.bin.zst
gr/fecs_sig.bin.zst
gr/gpccs_data.bin.zst
gr/gpccs_inst.bin.zst
gr/gpccs_sig.bin.zst
nvdec/scrubber.bin.zst
sec2/desc.bin.zst
sec2/image.bin.zst
sec2/sig.bin.zst
```

Relevant pieces:

- `sec2`: security engine firmware. Public NVIDIA/Linux docs use SEC2 as a canonical secure Falcon example on newer architectures.
- `acr`: Authenticated Code Region loader/unloader naming. This is strongly suggestive of signed firmware/secure bootstrapping.
- `gr/fecs`: front-end control/context-switch firmware for graphics/compute context behavior.
- `gr/gpccs`: GPC context-control firmware.
- `nvdec`: video decode related firmware; less likely to explain AI/tensor behavior directly, but it is another Falcon-related family.

For the local cards, the inventory note already shows the key visible difference:

- CMP baseline GPU 2: PCI device `10de:1d84`, VBIOS `88.00.9D.00.00`, reports `NVIDIA CMP 100-210`.
- V100-personality GPU 14: PCI device `10de:1df4`, VBIOS `88.00.51.00.04`, reports `Tesla V100-PCIE-12GB`.

Working hypotheses for measurable differences:

- VBIOS/device identity changes may alter driver capability tables and power/clock/feature policy.
- Signed Falcon firmware loading may be identical across both, but may receive different board/device parameters.
- FECS/GPCCS behavior could affect what compute paths are exposed or scheduled, but that needs benchmarking and firmware metadata comparison rather than assumption.
- Tensor cores are physical SM subunits. A firmware/personality change is more likely to change visibility, scheduling, clocks, or driver policy than the literal physical tensor-core count.

## Nintendo Switch / Tegra X1 Context

Switchbrew documents TSEC as a Falcon instance and lists Falcon in several Tegra blocks, including TSECA/TSECB, NVDEC, NVENC, NVJPG, VIC, GPU PMU, and XUSB.

Important Switch/TSEC points from public docs:

- TSEC is configured and initialized by the first bootloader during key generation.
- The Falcon CPU has the familiar register model: 16 GPRs plus special registers such as interrupt vectors, exception/trap vector, stack pointer, program counter, IMEM/DMEM transfer bases, flags, security/authentication state, and transfer-target selection.
- TSEC includes or works with SCP, the Secure Co-Processor. Switchbrew describes SCP as present in every Falcon supporting Heavy Secure Mode on Tegra X1: TSECA, TSECB, NVDEC, and GPU PMU.
- Switch firmware changed over time. Public Switchbrew notes say key generation moved into an encrypted TSEC payload in 6.2.0+.
- The Switch cryptosystem docs describe TSEC as generating a console-unique TSEC key, presumably using fuse material available only to authenticated NVIDIA microcode.

The Switch analogy is useful because it shows Falcon used as a security boundary, not merely a helper CPU. But the Switch path is tied to Tegra boot, console fuses, package1/keyslots, and display/SOR side effects. GV100 PCIe cards have a different boot chain, different firmware packaging, and different host-driver relationship.

## Comparison Matrix

| Area | GV100 CMP/V100 | Nintendo Switch/Tegra X1 |
| --- | --- | --- |
| Main Falcon targets | SEC2, ACR, FECS, GPCCS, PMU, NVDEC | TSECA/TSECB, NVDEC, GPU PMU, other Tegra blocks |
| Security role | Signed GPU firmware loading, secure bootstrapping, graphics/context initialization | Boot/key-generation path, secure firmware payloads, console key derivation |
| Root material | Board/VBIOS/device identity plus NVIDIA-signed firmware chain | Console fuses, bootrom, TSEC firmware, SE keyslots |
| Public documentation quality | Linux/Nouveau/envytools explain architecture; exact GV100 policy remains mostly opaque | Switchbrew has extensive boot/key/TSEC reverse-engineering notes |
| Direct transferability | Concepts transfer; addresses/exploit behavior generally do not | Concepts transfer; console-specific chain does not map directly to PCIe GV100 |

## Safe Next Research Steps

1. Compare read-only firmware metadata and hashes for the local installed GV100 blobs.
2. Run controlled CUDA capability probes on GPU 2 and GPU 14 using only normal CUDA APIs: SM count, tensor core throughput via GEMM, clocks, power limits, ECC, and reported attributes.
3. Capture `nvidia-smi -q` and `lspci -vvv -s` for both selected cards into timestamped files.
4. If deeper firmware work is considered later, first document recovery: known-good ROM dumps, external programmer access, isolated host boot, riser/power controls, and rollback criteria.

## Sources

- envytools Falcon overview: https://envytools.readthedocs.io/en/latest/hw/falcon/intro.html
- envytools Falcon ISA: https://envytools.readthedocs.io/en/latest/hw/falcon/isa.html
- envytools Falcon xfer/DMA: https://envytools.readthedocs.io/en/latest/hw/falcon/xfer.html
- Linux kernel NOVA Falcon docs: https://www.kernel.org/doc/html/latest/gpu/nova/core/falcon.html
- Nintendo Switchbrew TSEC: https://switchbrew.org/wiki/TSEC
- Nintendo Switchbrew TSEC firmware: https://switchbrew.org/wiki/TSEC_Firmware
- Nintendo Switchbrew cryptosystem: https://switchbrew.org/wiki/Cryptosystem
- Nintendo Switchbrew package1: https://www.switchbrew.org/wiki/Package1
- NVIDIA DRIVE Falcon ratcheting note: https://developer.nvidia.com/docs/drive/drive-os/archives/6.0.4/linux/sdk/common/topics/bootloader_setup/ratcheting_for_falcon_firmware.html

