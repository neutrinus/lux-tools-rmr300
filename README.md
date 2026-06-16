# SNK Mower PIN Recovery — Lux Tools A-RMR-300-24

![A-RMR-300-24](img/a-rmr-300-24.png)

**Recovered PIN: 9633** ✅

Step-by-step procedure to recover the PIN from the mower firmware via SWD.
This mower is an SNK OEM platform, also sold as **Adano RM5** (Harald Nyborg / Schou).

---

## Quick Start (works)

What you need:
- Raspberry Pi Pico flashed with [debugprobe](https://github.com/raspberrypi/debugprobe) UF2
- 3x Dupont wires (GND, SWCLK, SWDIO)
- Access to the P4 pad on the mainboard — requires disassembling the entire housing and removing the PCB
- OpenOCD on your computer

### 1. Connect SWD to U13 (GD32F305)

P4 on the mainboard (top to bottom): `3V3 DIO CLK JTDO RES GND`

| Pico pin | Pico GPIO | SWD | P4 pin |
|----------|-----------|-----|--------|
| Pin 3 | GND | GND | 6 (bottom) — GND (TP81) |
| Pin 4 | GP2 | SWCLK | 3 — CLK (TP77) |
| Pin 5 | GP3 | SWDIO | 2 — DIO (TP76) |

> Do NOT connect 3.3V from Pico — the mower powers itself.

### 2. Dump live firmware RAM

```bash
openocd -f tools/dump_full_ram.cfg
```

Output: `firmware/ram_full.bin` (48 KB, addresses `0x20000000–0x2000BFFF`).

### 3. Read the PIN

```bash
# PIN = 4 bytes at address 0x2000027C (little-endian uint32)
python3 -c "
import struct
ram = open('firmware/ram_full.bin', 'rb').read()
val = struct.unpack('<I', ram[0x27c:0x280])[0]
print(f'PIN = {val:04d}')
"
```

The PIN is a 4-digit number stored as **uint32 LE** in the firmware's KV-store cache.

### Why does this work?

The U13 (GD32F305) firmware has a built-in **KV-store** (key-value store).
The key `"pwd"` holds the 4-byte PIN. The value is cached in RAM
at address `0x2000027C` — just dump RAM and read it.

KV-store architecture details: [notes/GD32F305.md](notes/GD32F305.md).

---

## Why Other Methods Were Rejected

### ❌ Reading EEPROM U22 via I2C

We tried — I2C2 (`0x40005800`) communication with the EEPROM
(address `0xD0`) works, reading bytes 0x00–0x5F succeeded. However,
after ~96 bytes the I2C bus wedges (slave holds SDA low) and requires
a power cycle. The PIN wasn't in the read range, and further I2C
efforts were unnecessary.

Details: [notes/eeprom_dumping.md](notes/eeprom_dumping.md).

### ❌ FORMATFLASH.json via USB (doesn't work)

The firmware contains a string `"FORMATFLASH.json"` suggesting a factory
reset via USB flash drive. **Dead code** — zero references to this
string in the code. The mechanism does not exist in this firmware.

### ❌ Ghidra headless decompilation

Attempting full U13 firmware decompilation via Ghidra 12.1.2 headless
(with Java 21/25) failed due to OSGi bundling issues with Python/Java
plugins. Partial decompilation was done via ghidra-cli
(`tools/ghidra-cli`). Results in `decomp/decomp_*.c` and `decomp/decompilation.md`.

### ❌ Searching for PIN in flash

No plaintext PIN (ASCII or BCD digits) exists anywhere in the
U13 (1 MB), U16 (256 KB), or ESP32 (4 MB) flash. The PIN is stored
binary as uint32, not as a string.

### ❌ Reading from ESP32

The ESP32 on the display board only handles the user interface
(buttons, display, buzzer). The PIN is sent from the ESP32 to the
mainboard for verification — the ESP32 does not store it.

---

## System Architecture

```
┌──────────────────────────────────────────────────────────────┐
│ Display Board (SNK_DISPLAY_CP_V11)                           │
│  ESP32-WROOM-32UE — UI, WiFi/BT (unused), buzzer            │
│  4-digit 7-segment LED + 4 buttons                           │
└──────────┬───────────────────────────────────────────────────┘
           │ UART @115200, binary protocol (0xAA 0x55 + XOR CS)
           │ Commands: 0x0B = PWD_VERIFY, 0x0C = PWD_RESULT
┌──────────▼───────────────────────────────────────────────────┐
│ Main Board (SNK_MAINBOARD_CP_V11)                            │
│                                                               │
│  U16 (GD32F303) — Board MCU                                  │
│    • Sensors (lift, border, voltage)                          │
│    • Motor control                                            │
│    • Translates ESP32 protocol → JSON for U13                 │
│                                                               │
│  U13 (GD32F305) — Main MCU — ★ contains PIN                 │
│    • KV-store in RAM: key "pwd" @ 0x2000027C                 │
│    • I2C2 EEPROM U22 (24C02) — persistence                   │
│    • USB Host (flash drive, IAP firmware update)              │
│    • OTA over UART from U16                                   │
└──────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
kosiarka/
├── README.md                         ← this file
├── HARDWARE.md                       ← PCB analysis, pinouts, SWD
├── firmware/
│   ├── u13_flash_1mb.bin             ← full U13 flash dump (1 MB)
│   ├── u16_flash.bin                 ← U16 flash dump (256 KB)
│   ├── esp32_dump.bin                ← ESP32 flash dump (4 MB)
│   ├── ram_full.bin                  ← U13 RAM dump (48 KB)
│   └── ota_0.bin                     ← OTA fragment
├── notes/
│   ├── GD32F305.md                   ← U13 firmware analysis + KV-store
│   ├── U16.md                        ← U16 firmware analysis
│   ├── ESP32.md                      ← ESP32 firmware analysis
│   ├── eeprom_dumping.md             ← I2C saga
│   └── firmware_update.md            ← USB update procedure
├── img/
│   ├── mainboard_top.jpg
│   ├── mainboard_bottom.jpg
│   ├── display_front.jpg
│   └── display_back.jpg
├── tools/
│   ├── dump_full_ram.cfg             ← OpenOCD config for RAM dump
│   ├── dump_flash.cfg                ← OpenOCD config for flash dump
│   ├── eeprom_diag.c                 ← I2C stub (Thread mode)
│   └── link.ld                       ← linker script for stubs
├── eeprom/
│   └── eeprom_part.bin               ← partial EEPROM dump (0x00-0x5F)
├── decomp/
│   ├── decompilation.md              ← decompilation notes
│   └── decomp_*.c                    ← selected Ghidra decompilations
```

---

## Hardware

Detailed hardware documentation: [HARDWARE.md](HARDWARE.md).

| Component | Description |
|-----------|-------------|
| U13 | GD32F305 AGT6 — Cortex-M4, 1 MB flash, 48 KB RAM |
| U16 | GD32F303 CGT6 — Cortex-M4, 256 KB flash |
| ESP32 | ESP32-WROOM-32UE — on display board |
| U22 | 24C02 — I2C EEPROM (256 B) on I2C2 @ `0xD0` |
| U? | Winbond 25Q64JVSIQ — 8 MB QSPI NOR flash (external, on main board) |
| SWD P4 | U13 — through-hole pads near USB |
| SWD P5 | U16 — black header on left side |

### udev rule

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/99-pico-debugprobe.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## Notes on Unconfirmed Methods

### FORMATFLASH.json (USB flash drive, factory reset)

The string `"FORMATFLASH.json"` exists in firmware at `0x08003990`.
Our earlier scan found no direct code references in the main app —
but the **MBTL bootloader** (also in the same 1 MB flash) uses wildcard
file matching on USB: `env_config*.json`, `SNK_MB_*.bin`, etc.
`FORMATFLASH.json` is matched by the same mechanism and triggers a full
flash erase.

This was confirmed by cross-referencing with the Brucke RM500 community
([io-tech.fi thread](https://bbs.io-tech.fi/threads/brucke-rm500-rm501-rm800-robottiruohonleikkurin-infopaketti.405186/))
whose firmware shares identical strings and code paths.

**To use**: place an empty `FORMATFLASH.json` on a FAT32 USB stick
(≤16 GB), insert into the mower's internal USB port, and power on.
The bootloader will erase all flash including the KV-store and PIN.
After reboot, the mower will be factory reset — no PIN required.

---

## Related Products

- **Adano RM5** — Harald Nyborg (Denmark), Schou (Scandinavia)
- Part numbers: `80102372-01` (mainboard), `80102373-01` (display)

---

## Related Threads & References

- [Brucke RM500/RM501/RM800 infopaketti (io-tech.fi)](https://bbs.io-tech.fi/threads/brucke-rm500-rm501-rm800-robottiruohonleikkurin-infopaketti.405186/) —
  Finnish forum thread covering the same SNK/Sunseeker platform.
  Confirmed shared firmware codebase: wildcard USB matching, FORMATFLASH.json,
  BMS battery strings, MBTL bootloader architecture.

---

*Documentation produced through reverse engineering for educational purposes.*
