# Reverse-engineering toolkit

How the dumps and captures in this repository were made, and the tools to work with them.

## Repository data

| Path | Contents |
|---|---|
| [`dumps/esp32/`](../dumps/esp32/) | ESP32 full flash, app partition, annotated disassembly ([firmware-esp32.md](firmware-esp32.md)) |
| [`dumps/u13/`](../dumps/u13/) | U13 flash and RAM, Ghidra exports ([firmware-u13.md](firmware-u13.md)) |
| [`dumps/u16/`](../dumps/u16/) | U16 flash ([firmware-u16.md](firmware-u16.md)) |
| [`dumps/mi302/`](../dumps/mi302/README.md) | Dumps of a second unit (VILLARTEC MI 302) |
| [`captures/`](../captures/README.md) | Logic-analyser captures of the original firmware on J8 |

## SWD

Wiring of the Pico debug probe to P4 (U13) and P5 (U16): [hardware.md](hardware.md#swd). The OpenOCD scripts in [`tools/swd/`](../tools/swd/) use the CMSIS-DAP interface with the Pico's VID/PID and the `stm32f3x` target (it works for the GD32F30x), and write into `dumps/u13/`. Run them from the repository root with the mower switched on:

```bash
openocd -f tools/swd/dump_flash.cfg       # 1 MB of U13 flash  -> dumps/u13/u13_flash_1mb.bin
openocd -f tools/swd/dump_full_ram.cfg    # 48 KB of RAM       -> dumps/u13/ram_full.bin
openocd -f tools/swd/dump_ram.cfg         # first 16 KB of RAM -> dumps/u13/ram_low.bin
```

The flash is not read-protected.

## ESP32

J1 on the display board is the ESP32 programming UART ([hardware.md](hardware.md#j1-programming-header)). With a 3.3 V USB-serial adapter and GPIO0 (`P`) held low at reset:

```bash
esptool.py --port /dev/ttyUSB0 --baud 921600 read_flash 0 0x400000 esp32_dump.bin
```

`tools/esp32_img2elf.py` turns an app image into an ELF for objdump or Ghidra.

## Disassembly tools

[`tools/re/`](../tools/re/README.md) has pure-Python tools built on capstone 6:

- `esp32dis.py`: annotated Xtensa disassembly of an ESP-IDF app image, with literal, string and call resolution, xrefs and callers.
- `gd32dis.py`: Thumb disassembly of the GD32 dumps with resolved literals, `bl` callers and peripheral bases.
- `la_decode.py`: one time-ordered timeline of both UART directions and the button lines from a capture.

Ghidra project notes and exports for U13 are in [`dumps/u13/ghidra/`](../dumps/u13/ghidra/decompilation.md).

## Logic analyser

A cheap FX2-based analyser (sigrok `fx2lafw`) at 2–4 MHz on J8 pins 3 and 4 (plus GND) and optionally the button lines. Decode with PulseView, `sigrok-cli` (UART 230400 8N1) or `tools/re/la_decode.py`. The frame format is in [protocol.md](protocol.md#frame-format).
