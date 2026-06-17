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

**Candidates from literal pools at address 0x400D77D8+:**

| GPIO | Function | Address | Confidence |
|------|----------|---------|:----------:|
| **12** | MOSI (SPI tube data) | 0x400D77D8 | HIGH |
| — | MISO (SPI, NC = -1) | 0x400D77DC | HIGH |
| **10** | SCLK (SPI tube clock) | 0x400D77E0 | HIGH |
| **15** | CS / ST_CP (latch) | 0x400D7800 | MEDIUM |

> GPIO15 also appears in UART context — needs continuity check.

### UART (Mainboard Communication)

The mainboard UART is **NOT** UART0 (GPIO1/GPIO3) — those are on J1 for
programming only. The mainboard UART must use UART1 (GPIO9/GPIO10) or UART2
(GPIO16/GPIO17).

**Candidates from firmware analysis:**

| ESP32 GPIO | Direction | Notes |
|-----------|-----------|-------|
| **14** or **9** | TX → mainboard RX | Paired with (14,15) or (9,10) |
| **15** or **10** | RX ← mainboard TX | Paired with (14,15) or (9,10) |

> GPIO10 and GPIO15 overlap with SPI tube candidate — unlikely both share same
> pin. Need continuity test between J8 pins 3-4 and ESP32 pins.

### Buttons / Key Matrix (K1-K4)

| Button | Label | Function | ESP32 GPIO |
|--------|-------|----------|:----------:|
| K1 | START | Start mowing | **TBD** |
| K2 | HOME | Return to dock | **TBD** |
| K3 | OK | Confirm/PIN entry | **TBD** |
| K4 | ON/OFF | Power on | **TBD** |

All pass through ribbon cable (J8 pins 2,6,7) directly to mainboard as
independent GPIO signals — mower can be controlled even without ESP32.

The ESP32 reads these same buttons via local GPIO on the display board for
display feedback and MQTT relay. Source file: `gpio_key.c`.

### Buzzer (BU1)

Driven directly by ESP32 PWM.

| GPIO | Function |
|:----:|----------|
| TBD | Buzzer PWM output |

### Rain Sensor (J4)

Spring contacts on underside, read by ESP32 ADC.

| GPIO | Function |
|:----:|----------|
| TBD | ADC input |

## Display Segment Mapping

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
