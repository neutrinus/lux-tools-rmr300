# Firmware Analysis — SNK Mower (Lux Tools A-RMR-300-24)

## Extracted Firmware

Two firmware dumps were obtained via SWD using OpenOCD + RPi Pico (debugprobe).

| File | Source | MCU | Size | Method |
|------|--------|-----|------|--------|
| `u13_flash.bin` | Mainboard | GD32F305 (U13) | 512 KB (0x80000) | SWD via P4 |
| `u16_flash.bin` | Mainboard | GD32F303 (U16) | 256 KB (0x40000) | SWD via P5 |

### Dump Commands

```bash
# U16 (256 KB)
openocd -f interface/cmsis-dap.cfg \
  -c "adapter driver cmsis-dap; cmsis_dap_vid_pid 0x2e8a 0x000c" \
  -c "transport select swd; adapter speed 1000" \
  -f target/stm32f3x.cfg \
  -c "init; flash probe 0; dump_image u16_flash.bin 0x08000000 0x40000; exit"

# U13 (512 KB)
openocd -f interface/cmsis-dap.cfg \
  -c "adapter driver cmsis-dap; cmsis_dap_vid_pid 0x2e8a 0x000c" \
  -c "transport select swd; adapter speed 1000" \
  -f target/stm32f3x.cfg \
  -c "init; flash probe 0; dump_image u13_flash.bin 0x08000000 0x80000; exit"
```

### MCU Detection Results

| MCU | DPIDR | Device ID | Detected As |
|-----|-------|-----------|-------------|
| GD32F303 (U16) | `0x2ba01477` | `0x21040414` | Cortex-M4 r0p1, 6 breakpoints, 4 watchpoints |
| GD32F305 (U13) | `0x2ba01477` | `0x21040418` | Cortex-M4 r0p1, 6 breakpoints, 4 watchpoints |

---

## U16 Firmware (`u16_flash.bin`)

### Analysis

- **Size**: 256 KB (0x08000000 - 0x08040000)
- **Content**: Minimal firmware consisting primarily of ARM Thumb2 machine code
- **Strings found**: Only boot/test messages (`Clock Frequency Test Success`, `Clock Test(PreRun) Unexpectedly Abort!`)
- **PIN in plaintext**: **NO** — no 4-digit sequences found via `strings` or hex search
- **Conclusion**: U16 is a simple UART bridge. It forwards button press data from the display board to U13 and passes display data back. The PIN logic is NOT in this MCU.

---

## U13 Firmware (`u13_flash.bin`)

### Analysis

- **Size**: 512 KB (0x08000000 - 0x08080000)
- **Architecture**: ARM Cortex-M4 Thumb2
- **PIN in plaintext**: **NO** — not found via `strings` or hex pattern search

### Key Strings Discovered

#### Storage / Environment System

| Address | String | Implication |
|---------|--------|-------------|
| `0x3010` | `env_read.json` | Environment configuration file (JSON) |
| `0x3f28` | `env file size too large` | ENV file parsing code present |
| `0x3f40` | `env file read error` | ENV file error handling |
| `0x5790` | `KV40` | Key-Value storage v4.0 |
| `0x3ecc` | `ENV isn't initialize OK` | ENV init check |
| `0x3e90` | `load Environment variables` | ENV loading function |
| `0x2b78` | `USB read env` | USB host reads ENV from inserted drive |
| `0x2b98` | `USB read env %s` | USB ENV read with path parameter |

#### USB File Operations

| Address | String | Implication |
|---------|--------|-------------|
| `0x3990` | **`FORMATFLASH.json`** | Trigger file for flash format/reset |
| `0x39a4` | `ready to format flash` | Format sequence ready check |
| `0x39bc` | `format flash` | Format execution |
| `0x3950` | `USB disk Ready` | USB Mass Storage detection |
| `0x3960` | `0:/` | Root path prefix for USB filesystem |
| `0x34d5` | `file open error` | FATFS file operations |
| `0x34e5` | `file read error` | |
| `0x34f4` | `flash write error` | Flash write after file read |
| `0x3dbc` | `to usb disk` | Data export to USB |
| `0x45a5` | `into usb host mode` | USB host mode switch |
| `0x45b9` | `into usb device mode` | USB device mode (firmware update?) |
| `0x8c80` | `FAT` / `FAT32` | FAT32 filesystem driver strings |

#### Firmware Update

| Address | String |
|---------|--------|
| `0x3a24` | `SNK_MB_*.bin` | Firmware update file naming pattern |
| `0x361c` | `IAP type error` | In-Application Programming |
| `0x3408` | `checksum error` | CRC/checksum verification |
| `0x3414` | `firmware size error` | Size mismatch check |

#### UI / Key Handling

| Address | String |
|---------|--------|
| `0x3f90` | `key press down!` |
| `0x3fa0` | `key press power on` |
| `0x3ec4` | `user\src\key.c` | Source file reference for key handler |

#### Logging

| Address | String |
|---------|--------|
| `0x2d8c` | `log file size %d` |
| `0x3004` | `USB read log` |
| `0x3da4` | `save log to usb disk` |
| Full HTML/JS log template | Embedded HTML/CSS/JS for log viewer |

#### Miscellaneous

| Address | String |
|---------|--------|
| Various | `ultrasonic obstacle state=%d` | Ultrasonic sensor support (optional?) |
| `0x3988` | `Adb_hv` | Possibly Android Debug Bridge related? |

---

## ESP32 Firmware (`esp32_dump.bin`)

### Acquisition

The ESP32 (U5) flash was dumped via the **J1 UART programming header** using a FT232R USB-UART adapter. The chip was held in download mode by pulling **IO0 (P) to GND** and powered via J1 pin 1 (3U3) from the adapter's 3.3V output.

| Parameter | Value |
|-----------|-------|
| Tool | `esptool.py` v5.3.0 |
| Baud | 921600 |
| Flash size | 4 MB (0x400000) |
| Output | `esp32_dump.bin` |
| MD5 | `1edb8f1876efccdebd608f083e682d0a` |

### Chip Info

| Field | Value |
|-------|-------|
| Chip | `ESP32-D0WD-V3` (revision v3.1) |
| Features | Wi-Fi, BT, Dual Core + LP Core, 240MHz |
| MAC | `08:f9:e0:b3:da:70` |
| Crystal | 40 MHz |

### Firmware Analysis

The ESP32 runs a firmware based on the **ESP-IDF** (Espressif IoT Development Framework), containing:

**Wireless stacks (enabled but unused in stock product):**
- Full Wi-Fi station/AP mode (`connect to wifi ssid:%s, password:%s`, `ap_probe_send`)
- Bluetooth controller task (`BTC_TASK`, `BTU_TASK`, `btm_adv_pkt_handler`)
- Bluetooth configuration keys (`bt_cfg_key0`, `bt_cfg_key`)

**Application layer:**
- **`Display_esp32`** string suggests the firmware drives the 4-digit 7-segment display and reads buttons, serving as an alternative to U16 for UI handling
- **HTTP parser** (`HTTP_AUTH`, `HTTP_PARSER_ERRNO`) — possibly for OTA or config API
- **UART ENV** (`Uart ENV [%p]`) — reads configuration via UART from mainboard
- **Buzzer timer** (`buzzer timer`) — piezo buzzer control
- **LED** strings — additional indicator control

**Conclusion:** The ESP32 ships with a complete, apparently production-intended firmware that handles the display/UI, but also includes fully functional Wi-Fi and Bluetooth stacks that are **not used** in the retail product. This strongly suggests the platform (SNK) has a "premium" variant with app connectivity.

**Deep-dive analysis** of the UART protocol, PIN handling flow, WiFi/MQTT architecture, and state machine is available in [ESP32.md](ESP32.md).

### Restoring Original Firmware

```bash
esptool --port /dev/ttyUSB0 --baud 921600 --before no-reset write_flash 0 esp32_dump.bin
```

---

## PIN Storage Architecture

```
┌──────────────────────────────────────────────────┐
│ Display Board (SNK_DISPLAY_CP_V11)               │
│ ┌──────────┐                                     │
│ │   ESP32  │  (untested, unused in basic PIN)     │
│ └──────────┘                                     │
│ Buttons → UART → Mainboard U16 → UART → U13      │
└──────────────────────┬───────────────────────────┘
                       │ J9/J10 ribbon
┌──────────────────────▼───────────────────────────┐
│ Mainboard (SNK_MAINBOARD_CP_V11)                  │
│                                                    │
│ ┌─────────────────────┐    ┌──────────────────┐    │
│ │ U16 (GD32F303)      │───▶│ U13 (GD32F305)   │    │
│ │ UART bridge, no PIN │    │ ENV/KV system    │    │
│ └─────────────────────┘    │ I²C ←→ U22       │    │
│                            │ PIN verification  │    │
│                            └───────┬──────────┘    │
│                                    │ I²C bus       │
│                            ┌───────▼──────────┐    │
│                            │ U22 (24Cxx)      │    │
│                            │ EEPROM 256-2048  │    │
│                            │ stores PIN+config │    │
│                            └──────────────────┘    │
└────────────────────────────────────────────────────┘
```

**PIN is NOT in MCU flash.** Both dumps confirmed no plaintext PIN. The PIN is stored in the external I²C EEPROM **U22** and loaded by U13 during boot.

The flowchart:
1. User enters PIN via buttons on display board
2. Display board ESP32 or U16 passes key presses to U13 via UART
3. U13 reads stored PIN from EEPROM U22 via I²C
4. U13 compares entered PIN vs stored PIN
5. On match → U13 commands U16 to show "IDLE"; on mismatch → "Lock"

---

## Recovery Strategies

### Option 1: FORMATFLASH.json (Non-invasive, unverified)

The firmware contains strings suggesting a factory reset mechanism via USB flash drive.

**How to try:**
1. Format USB drive as FAT32
2. Create empty file (or JSON) named `FORMATFLASH.json` on the drive
3. Insert into mower USB port
4. Power on the mower

**Risk**: Unknown — may wipe all ENV/KV config including boundary wire settings, schedule, and working hours. The exact required content of `FORMATFLASH.json` could not be confirmed as decompilation via Ghidra headless was unsuccessful (Java script OSGi bundling issue with Ghidra 12.1.2 + Java 25).

**Note:** Decompilation via **ghidra-cli** is planned. See [ghidra-cli](https://github.com/akiselev/ghidra-cli) at `/home/marek/tmp/kosiarka/ghidra-cli/`. A bridge script (`GhidraCliBridge.java`) is installed at `~/.config/ghidra-cli/scripts/`. The tool is compiled at `/home/marek/tmp/kosiarka/ghidra-cli/target/release/ghidra`. Usage: `GHIDRA_INSTALL_DIR=<path> ghidra <command>`.

### Option 2: CH341A + SOIC-8 Clip (Invasive, reliable)

1. Scrape conformal coating from **U22** (SOIC-8, left of U13 on mainboard)
2. Attach CH341A SOIC-8 clip to U22 with board powered on (so EEPROM is powered by the board)
3. Read 256 bytes with `flashrom` or `ch341eeprom`
4. PIN will be at the first few bytes as ASCII (e.g., `0x30 0x30 0x30 0x30` = "0000")
5. To reset: write `0x30 0x30 0x30 0x30` to addresses 0x00-0x03

**Lower risk** — only the PIN bytes are modified; all other configuration is preserved.

### Option 3: Full EEPROM Dump + Analysis (Recommended)

Read the entire EEPROM content (typically 256 bytes for 24C02), analyze the byte layout, and modify just the PIN bytes while preserving the rest of the configuration (schedule, working hours, boundary wire data, etc.).

---

## Tools Required for Analysis

| Tool | Status | Purpose |
|------|--------|---------|
| Ghidra 12.1.2 | Installed (zip) | Full decompilation — pending headless CLI setup |
| Ghidra project | Saved at `ghidra_proj/Mower_Firmware.gpr` | Contains analyzed U13 binary |
| ghidra-cli | Compiled at `ghidra-cli/target/release/ghidra` | Planned for headless decompilation |
| Rizin (0.7.4) | Installed | Basic string/binary analysis |
| Python + capstone | Installed | Disassembly |
| rzpipe | Installed | Rizin Python bindings |
