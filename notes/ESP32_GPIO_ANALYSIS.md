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

```bash
# Full disassembly (slow, ~1.2MB output)
xtensa-esp32-elf-objdump -d firmware/ota_0.elf > firmware/disasm.txt

# Specific range
xtensa-esp32-elf-objdump -d --start-address=0x400d7704 --stop-address=0x400d7c00 firmware/ota_0.elf
```

Known application code:
- IRAM0 (0x40080000): cache init, interrupt vectors, FreeRTOS startup
- IRAM1 (0x40083060): FreeRTOS task management, some application code
- IROM (0x400d0020): bulk of ESP-IDF framework + application code

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

### Candidate GPIOs from Nearby Data

Before the SPI config (DROM+0x177A8):

| DROM Offset | Value | Candidate |
|-------------|-------|-----------|
| +0x177A8 | 29 | ? |
| +0x177AC | 27 | ? |
| +0x177B0 | 11 | ? |
| +0x177B4 | -1 | NC |

After the SPI config (DROM+0x177F8):

| DROM Offset | Value | Candidate |
|-------------|-------|-----------|
| +0x177FC | 15 | CS (display latch) or UART TX |
| +0x17800 | 14 | CS or UART RX |
| +0x17804 | 16 | ? |
| +0x17808 | 13 | ? |

### Die wichtigsten Kandidaten

| Function | Candidate GPIOs | Confidence |
|----------|----------------|:----------:|
| SPI MOSI | **12** | **HIGH** |
| SPI SCLK | **10** | **HIGH** |
| SPI MISO | -1 (NC) | **HIGH** |
| SPI CS/latch | 15 | MEDIUM |
| UART TX | 14 or 9 | MEDIUM |
| UART RX | 15 or 10 | MEDIUM |

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

## Existing Toolchain

The Xtensa toolchain exists at a backup location:
```
/home/marek/stary_marek/home/marek/.platformio/packages/toolchain-xtensa32/bin/
```

This is GCC 5.2.0 / binutils 2.25.1 (old but functional for disassembly).
Symlinked into `tools/xtensa-toolchain/`.
