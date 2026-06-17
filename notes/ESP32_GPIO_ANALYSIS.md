# ESP32 GPIO Mapping & Xtensa Decompilation

## ELF Generation

Convert raw ota_0.bin (ESP32 OTA image) to ELF for `xtensa-esp32-elf-objdump`:

```
tools/esp32_img2elf.py firmware/ota_0.bin firmware/ota_0.elf
```

Segments from esptool:

| Segment | Type | File Offset | Load Address | Size |
|---------|------|-------------|--------------|------|
| 0 | DROM (rodata) | 0x000018 | 0x3f400020 | 0x262b4 |
| 1 | DRAM (data) | 0x0262d4 | 0x3ffbdb60 | 0x06cd4 |
| 2 | IRAM0 | 0x02cfb0 | 0x40080000 | 0x03060 |
| 3 | IROM (code) | 0x030018 | 0x400d0020 | 0xe4004 |
| 4 | IRAM1 | 0x114024 | 0x40083060 | 0x191f8 |
| 5 | RTC_DATA | 0x12d224 | 0x50000000 | 0x00010 |

IROM first function at 0x400d2f24; first ~0x2F04 bytes are data tables.
3579 function entries (`entry a1, N`) across all segments.

## Decompilation

### Toolchain Setup

The Xtensa toolchain exists at a backup location:
```
/home/marek/stary_marek/home/marek/.platformio/packages/toolchain-xtensa32/bin/
```

GCC 5.2.0 / binutils 2.25.1 — old but functional for disassembly.
Symlinked into `tools/xtensa-toolchain/`:

```bash
export PATH="$PWD/tools/xtensa-toolchain:$PATH"
```

### ELF Generation

The firmware dump `ota_0.bin` is a raw ESP32 OTA image (not an ELF).
Convert it first using the Python script:

```bash
python3 tools/esp32_img2elf.py firmware/ota_0.bin firmware/ota_0.elf
```

The script parses the 24-byte ESP-IDF image header and 6 segment headers,
then creates a minimal ELF with 6 LOAD segments and proper section headers.

### Disassembly

```bash
# Full disassembly (~400K lines)
xtensa-esp32-elf-objdump -d firmware/ota_0.elf > firmware/disasm.s

# Specific range
xtensa-esp32-elf-objdump -d --start-address=0x400d7704 --stop-address=0x400d7c00 firmware/ota_0.elf

# Section/header info
xtensa-esp32-elf-objdump -x firmware/ota_0.elf
```

### Memory Regions

| Region | Virtual Address | Physical Flash | Contents |
|--------|----------------|----------------|----------|
| IRAM0 | 0x40080000 | file+0x2cfb0 | Cache init, vectors, FreeRTOS startup |
| IRAM1 | 0x40083060 | file+0x114024 | FreeRTOS tasks, some app code |
| IROM  | 0x400d0020 | file+0x30018 | ESP-IDF framework + application code |
| DROM  | 0x3f400020 | file+0x00018 | Read-only data, strings, configs |
| DRAM  | 0x3ffbdb60 | file+0x262d4 | Data, BSS, GOT |

3579 function entries (`entry a1, N`) across all code segments.
IROM first function at 0x400d2f24; first ~0x2F04 bytes are data tables.

### Approach for Finding GPIO Numbers

1. **Literal pool scanning** — IROM contains 4285 references to DROM as 32-bit literal pool entries. Each `L32R a, label` instruction loads an absolute address from a pool.
2. **Struct matching** — Known struct patterns (e.g. `spi_bus_config_t` with fields `{mosi, miso, sclk, quadwp, quadhd, ...}`) can be found by scanning DROM for sequences of small integers (-1, 0-39).
3. **String cross-references** — Application strings (log tags, error messages) are referenced indirectly through the GOT in DRAM, not directly via L32R. Search DRAM for DROM pointers.
4. **Call tracing** — Each function is identified by `entry a1, N` prologue. Trace `call8` targets to understand call hierarchy.

## GPIO Pin Mapping

### SPI Tube Display — CONFIRMED

**SPI Bus Config** found at DROM+0x177C0 (file offset 0x177D8):

```
struct spi_bus_config_t {
    mosi_io_num:     12   // GPIO12 — MOSI
    miso_io_num:     -1   // not connected (NC)
    sclk_io_num:     10   // GPIO10 — SCLK
    quadwp_io_num:   -1   // NC
    quadhd_io_num:   -1   // NC
    max_transfer_sz: -1   // default (4096)
    flags:           -1   // default
    intr_flags:      -1   // default
}
```

Hex at file+0x177D8:
```
0c 00 00 00 ff ff ff ff 0a 00 00 00 ff ff ff ff
12            -1           10           -1
ff ff ff ff  ff ff ff ff  ff ff ff ff  ff ff ff ff
   -1          -1           -1           -1
```

The CS (chip select) pin is set in `spi_device_interface_config_t.spics_io_num` —
not found yet (candidate: GPIO15 based on proximity in data table).

### Other GPIOs from Nearby Data Table

Adjacent to the SPI bus config there are more GPIO pin numbers in the same
data structure (likely a GPIO config table or similar struct):

| DROM Offset | File Offset | GPIO | Candidate Function | Confidence |
|-------------|-------------|------|-------------------|:----------:|
| +0x177A8 | 0x177C0 | **29** | Buzzer PWM or Rain sensor | LOW |
| +0x177AC | 0x177C4 | **27** | Buzzer PWM or Rain sensor | LOW |
| +0x177B0 | 0x177C8 | **11** | UART TX or Button | LOW |
| +0x177B4 | 0x177CC | -1 | NC | HIGH |
| +0x177D8 | 0x177F0 | — | *SPI bus config starts here* | — |
| +0x177FC | 0x17814 | **15** | SPI CS (ST_CP/latch) or UART | MEDIUM |
| +0x17800 | 0x17818 | **14** | UART TX or RX | MEDIUM |
| +0x17804 | 0x1781C | **16** | Buzzer or button | LOW |
| +0x17808 | 0x17820 | **13** | CS or button | LOW |

### Summary

| Function | GPIO | Status |
|----------|:----:|:------:|
| SPI MOSI | **12** | **CONFIRMED** via spi_bus_config_t in DROM |
| SPI SCLK | **10** | **CONFIRMED** via spi_bus_config_t in DROM |
| SPI MISO | -1 (NC) | **CONFIRMED** via spi_bus_config_t in DROM |
| SPI CS / ST_CP (latch) | **15** | **LIKELY** — candidate based on data table proximity |
| UART TX (to mainboard) | **14** | **CANDIDATE** — paired with 15? |
| UART RX (from mainboard) | **15** | **CANDIDATE** — but conflicts with CS |
| Buzzer PWM | 16 or 29 or 27 | **UNKNOWN** |
| Rain sensor ADC | 11 or 29 or 27 | **UNKNOWN** |
| Button K1 (START) | ? | **UNKNOWN** — needs continuity test |
| Button K2 (HOME) | ? | **UNKNOWN** |
| Button K3 (OK) | ? | **UNKNOWN** |
| Button K4 (ON) | ? | **UNKNOWN** |

> **Note**: GPIO15 appears in both UART and SPI CS candidate lists — it cannot
> serve both functions. Continuity test with a multimeter will resolve this.

## Display Driver

Source file: `../main/src/app/rw_display.c`
Component: `../components/spi_tube_driver/src/spi_tube_drive.c`

Strings found in DROM trace:
- `"spi_tube_driver"` @ 0x3f407532
- `"TubeInit"` @ 0x3f407557
- `"tube driver not initialed"` @ 0x3f4039ae
- `"rw_display.c"` @ 0x3f403467
- `"W (%s) %s: tube driver not initialed\n"` context

Display patterns ("IdLE", "LoCK", "Mow", "HoME", "ChAr") are **NOT stored as ASCII strings** in DROM. They are likely encoded as 7-segment bitmap patterns in a lookup table.

### 3× 74HC595 Cascaded (24-bit shift register)

```
74HC595 #1        74HC595 #2        74HC595 #3
┌──────────┐      ┌──────────┐      ┌──────────┐
│ Q0-Q7    │─────▶│ Q0-Q7    │─────▶│ Q0-Q7    │
│ segments │      │ digits   │      │ colon/etc│
│ (8 bits) │      │ (4 bits) │      │ (2+ bits) │
└──────────┘      └──────────┘      └──────────┘
```

Each write: 24 bits shifted serially via MOSI (GPIO12), clocked by SCLK (GPIO10),
latched by ST_CP (CS, likely GPIO15).

To decode the segment map: write known patterns to the display via SPI (in a custom
firmware build) and observe which segments light up.

## UART (Mainboard Communication)

Protocol binary framing: `0xAA 0x55 [CMD] [payload...] [XOR CS]` @ 115200 8N1.

UART NOT on UART0 (GPIO1/GPIO3 — those are J1 programming only).
Likely UART1 (GPIO9/GPIO10) or UART2 (GPIO14/GPIO15).

Candidate pairs from code analysis:
- TX=14, RX=15 (or vice versa)
- TX=9, RX=10 (but GPIO10 is SCLK — shared unlikely)

### Known Command IDs

| ID | Direction | Name | Payload |
|----|-----------|------|---------|
| 0x01 | ESP→MB | BTN_UP | 1B scancode |
| 0x02 | ESP→MB | BTN_DOWN | 1B scancode |
| 0x04 | ESP→MB | BTN_OK | 1B scancode |
| 0x0B | ESP→MB | PWD_VERIFY | 4B ASCII PIN |
| 0x0C | MB→ESP | PWD_RESULT | 1B (0=OK, 1=fail) |
| 0x0D | ESP→MB | STATUS_REQ | 0B |
| 0x0E | MB→ESP | STATUS_RSP | 4B+ flags |
| 0x0F | ESP→MB | MOW_START | 0B |
| 0x10 | ESP→MB | CHARGE_RET | 0B |
| 0x14 | ESP→MB | BAT_INFO_REQ | 0B |
| 0x15 | MB→ESP | BAT_INFO_RSP | 2B voltage mV |

## How to Verify Pins (Multimeter / Saleae)

### Continuity test (5 min)
1. **SPI**: meter in continuity mode, probe between U1 pins and ESP32
   - U1 pin 14 (SER/SI) → ESP32 GPIO12 (MOSI)
   - U1 pin 11 (SCK) → ESP32 GPIO10 (SCLK)
   - U1 pin 12 (RCLK/ST_CP) → GPIO15 (CS)
2. **UART**: continuity between J8 pins 3-4 and ESP32 pins
3. **Buttons**: K1-K4 pads → ESP32 pins
4. **Buzzer**: BU1 → ESP32 PWM pin

### Saleae capture (15 min)
Probe all accessible ESP32 pins, then:
- Press buttons → identify GPIO inputs
- Display patterns → identify SPI signals  
- Sniff J8 UART → confirm baud + protocol

## Unknowns Requiring Physical Probing

| What | How to resolve |
|------|----------------|
| GPIO15: CS or UART? | Continuity from ESP32 pin to U1 pin 12 (RCLK) vs J8 pin 3-4 |
| UART TX/RX pins | Continuity from J8 pins 3-4 to ESP32 pins |
| Button GPIOs | Continuity from K1-K4 pads to ESP32 pins |
| Buzzer GPIO | Continuity from BU1 to ESP32 pin |
| Rain sensor GPIO | Continuity from J4 to ESP32 pin |
| Display 7-seg bitmap | Trial: write SPI patterns in custom firmware, observe |
| Protocol command 0x03 | Not found in decompilation — sniff UART traffic |
