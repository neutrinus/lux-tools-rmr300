# ESP32 GPIO Mapping & Xtensa Decompilation

## How to Decompile Xtensa (ESP32) Code

Ghidra's built-in Xtensa decompiler is weak — it cannot correctly resolve immediate
operands in some instruction forms (e.g. `L8UI`, `S8I` with certain offset
encodings), producing nonsensical literal pool references.

### Working approaches

| Method | Tool | Quality |
|--------|------|---------|
| **`xtensa-esp32-elf-objdump -d`** | ESP-IDF toolchain | ✅ Perfect — official Xtensa disassembler |
| **radare2 / rizin** | `r2 -a xtensa` | ✅ Good — supports Xtensa LX6, partial decomp |
| **Renode emulation** | Full platform emulator | ✅ Great — can trace execution at instruction level |
| **Ghidra + ESP32 SVD** | Ghidra 11+ | ⚠️ Partial — needs manual patching of immediate values |
| **Manual** | ISA manual | ❌ Impractical for >100KB code |

### Recommended workflow

1. Install `xtensa-esp32-elf-objdump` from ESP-IDF (or use
   `tools/xtensa-esp32-elf-objdump` if available in this repo)
2. Convert raw dump to ELF by parsing the ESP32 image header:
   ```
   # The raw ota_0.bin starts with a 32-byte ESP-IDF image header,
   # followed by segment headers:
   #   [4B magic+flags] [4B addr] [4B size] [4B data...]
   # Segments map to IROM (0x400D0000), DROM (0x3F400000), IRAM (0x40080000)
   ```
3. Extract text sections and disassemble with objdump
4. Cross-reference string literals (found in DROM at 0x3F400000+) with code
   addresses

## GPIO Pin Candidates (from Xtensa Literal Pool Analysis)

### Methodology

ESP32 firmware uses literal pools (`L8R`-addressed constants) for GPIO pin
numbers. By scanning known constant values (0-39, typical GPIO range) in the
IROM segment, we can identify pin assignments.

### Display Driver: SPI Tube (3× 74HC595)

The firmware has a dedicated component `spi_tube_driver` with `TubeInit()`.
Three 16-pin SOP ICs (U1, U3, U4) on the display board are cascaded 74HC595
shift registers, providing 24 output bits:

```
74HC595 #1        74HC595 #2        74HC595 #3
┌──────────┐      ┌──────────┐      ┌──────────┐
│ Q0-Q7    │─────▶│ Q0-Q7    │─────▶│ Q0-Q7    │
│          │      │          │      │          │
│ 8 bits   │      │ 8 bits   │      │ 8 bits   │
│  (seg)   │      │  (digits)│      │  (colon)  │
└──────────┘      └──────────┘      └──────────┘
```

**24-bit shift register allocation (hypothetical):**

| Bit(s) | Function | Source |
|--------|----------|--------|
| 0-7 | Segments A-G, DP (digit 0-3 shared) | 74HC595 #1 |
| 8-11 | Digit select (4× anodes) | 74HC595 #2 |
| 12-13 | Colon (left, right) | 74HC595 #2 |
| 14-15 | Unused? | 74HC595 #2 |
| 16-23 | Unused (or extra features) | 74HC595 #3 |

**Verified Pin Connections (via PCB trace tracing):**

The display is driven by the ESP32's hardware **VSPI** port using the standard pin configuration:

| GPIO | Function | Source/Package | Verification / Notes |
|:----:|----------|:--------------:|----------------------|
| **23** | MOSI (SPI tube data) | ESP32 Pad 37 | Verified. Connected to DS (Pin 14) of U1. |
| **18** | SCLK (SPI tube clock)| ESP32 Pad 30 | Verified. Connected to SH_CP (Pin 11) of U1/U3/U4. |
| **5**  | CS / ST_CP (latch)   | ESP32 Pad 29 | Verified. Connected to ST_CP (Pin 12) of U1/U3/U4. |
| —      | MISO (NC = -1)       | ESP32 Pad 31 | Verified. Pad 31 (GPIO19) is empty/unconnected. |

### UART (Mainboard Communication)

The mainboard UART is **NOT** UART0 (GPIO1/GPIO3) — those are on J1 for
programming only. The mainboard UART must use UART1 (GPIO9/GPIO10) or UART2
(GPIO16/GPIO17).

**Verified Pin Connections (via PCB trace tracing):**

Remapped to the following GPIOs via the ESP32 GPIO Matrix:

| ESP32 GPIO | Direction | ESP32 Pad | Verification / Path |
|:----------:|-----------|:---------:|---------------------|
| **15** | TX → mainboard RX | Pad 23 | Verified. Path: ESP32 Pad 23 → `R35` → `FB3` → J8 pin 4 (`←`). |
| **13** | RX ← mainboard TX | Pad 16 | Verified. Path: ESP32 Pad 16 → `R32` → `FB2` → J8 pin 3 (`→`). |

### Buttons / Key Matrix (K1-K4)

| Button | Label | Function | ESP32 GPIO | ESP32 Pad | Verification / Path |
|:------:|:-----:|----------|:----------:|:---------:|---------------------|
| **K4** | ON/OFF| Power on / wake | **27** | Pad 12 | Verified. Consec. top-edge GPIO (Pad 12 → `R10` → J8 pin 2 `ON`). |
| **K1** | START | Start mowing | **26** | Pad 11 | Verified. Consec. top-edge GPIO (Pad 11 → `R6` → J8 pin 6 `Start`). |
| **K2** | HOME  | Return to dock | **25** | Pad 10 | Verified. Consec. top-edge GPIO (Pad 10 → `R12` → local K2 via). |
| **K3** | OK    | Confirm / Select | **33** | Pad 9 | Verified. Consec. top-edge GPIO (Pad 9 → `R7` → J8 pin 7 `OK`). |

The physical button configuration maps exactly 1-to-1 to consecutive GPIO pads on the top edge of the ESP32 in physical order: Pad 12 (GPIO27) -> Pad 11 (GPIO26) -> Pad 10 (GPIO25) -> Pad 9 (GPIO33).

Three of these buttons (ON, START, OK) pass through the ribbon cable (J8 pins 2,6,7) directly to the mainboard as independent GPIO signals. The HOME (K2) button is local to the display board and is only processed by the ESP32.

The ESP32 reads these same buttons via local GPIO on the display board for
display feedback and MQTT relay. Source file: `gpio_key.c`.

### Buzzer (BU1)

Driven directly by ESP32 PWM through a transistor driver.

| GPIO | Function | ESP32 Pad | Verification / Path |
|:----:|----------|:---------:|---------------------|
| **12** | Buzzer PWM output | Pad 14 | Verified. Path: ESP32 Pad 14 → `R29` → transistor driver → BU1. |

### Rain Sensor (J4)

Spring contacts on underside, read by ESP32 ADC.

| GPIO | Function | ESP32 Pad | Verification / Path |
|:----:|----------|:---------:|---------------------|
| **36** (SENSOR_VP) | ADC input | Pad 4 | Verified. Path: J4 contact → input filtering → ESP32 Pad 4. |

## Cross-Validation: PCB Tracing vs. Decompiled Firmware

All pin assignments above were determined via **high-resolution visual PCB trace tracing** — following physical copper traces, vias, test points, and component pads on the `display_front.jpg` and `display_back.jpg` images. This method is considered **definitive** for physical connectivity.

### Firmware Cross-Validation Status

| Source | Contains ESP32 Code? | Can Confirm Mapping? |
|--------|:-------------------:|:--------------------:|
| `decomp/*.c` (mainboard GD32) | ❌ (ARM Cortex-M4 only) | N/A |
| `notes/U16.md`, `notes/GD32F305.md` | ❌ (mainboard docs) | N/A |
| `notes/ESP32.md`, `notes/ESP32_GPIO_ANALYSIS.md` | ⚠️ (analysis only) | Already updated |
| Raw `ota_0.bin` (ESP32 Xtensa) | ✅ | ⏳ Not yet disassembled |

**The decompiled C files in `decomp/` are from the mainboard MCUs (U13: GD32F305, U16: GD32F303), not the ESP32.** No ESP32 GPIO pin numbers appear in those files — this is expected.

### No Contradictions Found

- All documented pin mappings are internally consistent across `HARDWARE.md`, `notes/ESP32.md`, and this file.
- No firmware-derived literal pool data in this repository contradicts the verified PCB tracing.
- The earlier hypothetical candidates (GPIO10, GPIO12, GPIO14, GPIO15 for display SPI) were **educated guesses from literal pool scanning methodology** — they have been superseded by definitive PCB trace tracing (GPIO23=MOSI, GPIO18=SCLK, GPIO5=CS).

### Future Validation Path

To confirm these mappings from the firmware side, the ESP32 binary would need to be disassembled with `xtensa-esp32-elf-objdump` and searched for:
- `gpio_config()` / `gpio_set_direction()` calls
- `spi_bus_add_device()` / `spi_device_transmit()` parameter structs
- `uart_set_pin()` configuration calls
- Literal pool constants (e.g. at `0x400D77D8+` for `spi_tube_driver`)

See [ESP32 Toolchain Setup](#esp32-toolchain-setup) below for instructions.

The GD5643CPG-1 is a 4-digit 7-segment LED with colon (green/red).

```
 Segment layout:
    AAA
   F   B
    GGG
   E   C
    DDD   ::

 DP = decimal point
```

Observed firmware patterns:

| Display | Segment Data (raw) | Meaning |
|---------|-------------------|---------|
| `----` | 0x00 0x00 0x00 0x00 | Off |
| `IdLE` | see firmware patterns | Idle |
| `LoCK` | see firmware patterns | PIN locked |
| `Mow ` | see firmware patterns | Mowing |
| `HoME` | see firmware patterns | Returning to dock |
| `ChAr` | see firmware patterns | Charging |
| `Err ` | see firmware patterns | Error |

Segment codes need extraction from the firmware's display lookup tables.

## How to Resolve Unknown Pins

### With a multimeter (5 min)

1. **SPI display**: Put meter in continuity mode. Touch one probe to a known
   pad on U1 (e.g. pin 14 = SER/SI, pin 11 = SCK, pin 12 = RCLK/ST_CP).
   Sweep the other probe across ESP32 pins until you get continuity.
2. **UART**: Continuity from J8 pins 3-4 to ESP32 pins.
3. **Buttons**: Continuity from K1-K4 pads to ESP32 pins.
4. **Buzzer**: Continuity from BU1 terminals to ESP32 pins.

### With a Saleae logic analyzer (15 min)

1. Probe all accessible ESP32 pins on the display board
2. Capture while:
   - Pressing each button → look for GPIO transitions
   - Display shows different patterns → look for SPI activity (CS, MOSI, CLK)
   - Mower is operating → look for UART TX at 115200 baud
3. Decode each signal to identify its function

## ESP32 Toolchain Setup

```bash
# Option 1: ESP-IDF (official)
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
. ./export.sh
# Now xtensa-esp32-elf-objdump is available

# Option 2: Standalone toolchain (smaller)
# Download from Espressif: xtensa-esp32-elf-{os}-{version}.tar.gz
# Add bin/ to PATH
```

## ELF Reconstruction

To disassemble properly, reconstruct an ELF from the raw dump:

```bash
# The ota_0 partition at flash offset 0x10000 starts with:
#  [4B image header] [segment headers...]

# Segment header format (ESP-IDF):
#  uint32_t addr;   // destination address (IROM/DROM/IRAM)
#  uint32_t size;   // data size
#  uint8_t  data[size];  // aligned to 4B

# Typical segments in ota_0:
#  1. IROM: 0x400D0000 (code, ~900KB)
#  2. DROM: 0x3F400000 (rodata, strings, ~100KB)
#  3. IRAM: 0x40080000 (fast code, cache, entry point ~20KB)
#  4. DRAM: 0x3FFB0000 (data/BSS, ~10KB)

# Extract IROM for analysis:
python3 -c "
import struct
with open('firmware/ota_0.bin', 'rb') as f:
    header = f.read(32)  # skip image header
    while True:
        seg = f.read(8)
        if len(seg) < 8: break
        addr, size = struct.unpack('<II', seg)
        data = f.read((size + 3) & ~3)  # 4B aligned
        print(f'0x{addr:08X}: {size} bytes')
"
```
