# EEPROM Dumping & PIN Extraction — Full Documentation

## Goal
Read the PIN code from the Lux Tools A-RMR-300-24 (Landxcape) mower PCB via SWD on U13 (GD32F305).

## STATUS: ✅ PIN RECOVERED = **9633**

PIN read from firmware RAM — confirmed working by the user.
U22 EEPROM was ultimately NOT needed to obtain the PIN (though I2C
communication was established and bytes 0x00–0x5F were read).

---

## TL;DR — How the PIN Was Found

1. Dumped U13 flash (1 MB) → `firmware/u13_flash_1mb.bin`.
2. Identified the KV-store system in firmware (EasyFlash).
3. Located firmware functions reading the key `"pwd"` (4 bytes) @ `0x08060850`.
4. Found the KV definition table @ `0x08085458` (18 entries).
5. The table showed that the `"pwd"` key value is cached in RAM at **`0x2000027C`**.
6. Dumped RAM from live firmware (48 KB) → `firmware/ram_full.bin`.
7. Read 4 bytes: `a1 25 00 00` = **`0x25A1` = 9633**.
8. PIN works for logging into the mower.

---

## Hardware

| Component | Description |
|-----------|-------------|
| U13 | GD32F305 (Cortex-M4). Flash **1 MB** — `firmware/u13_flash_1mb.bin` |
| U22 | EEPROM 24C02 (256 B) on I2C2, dev addr **`0xD0`** (7-bit `0x68`) |
| I2C peripheral | **I2C2** @ `0x40005800` (APB1EN bit22). I2C1 @ `0x40005400` DISABLED |
| I2C2 pins | **PB10 = SCL, PB11 = SDA** (GPIOB CRH: PB10=0xF, PB11=0xF = AF open-drain) |
| GPIOB | STM32F1-style @ `0x40010C00` (NOT F3-style `0x48000400`) |
| RCC | `0x40021000` (GD32F30x) |
| U16 | Watchdog — resets board ~250–300 ms after U13 halt |
| P4 | SWD to debugprobe Pico (CMSIS-DAP v2) |

## Flashing, Building, Debugging

### Compiling the stub
```bash
arm-none-eabi-gcc -c -mcpu=cortex-m4 \
    -I . -mthumb -O2 -fomit-frame-pointer \
    -ffreestanding -nostartfiles -o eeprom_diag.o tools/eeprom_diag.c
arm-none-eabi-gcc -T tools/link.ld -nostartfiles -nostdlib -ffreestanding \
    -mcpu=cortex-m4 -mthumb -o tools/eeprom_diag.elf tools/eeprom_diag.o -lgcc
arm-none-eabi-objcopy -O binary eeprom_diag.elf tools/eeprom_diag.bin
```

### Running OpenOCD
```bash
openocd -f tools/dump_chunks.cfg [options]
```

---

## Detailed Findings

### 1. I2C2, not I2C1 — pins PB10/PB11

Firmware uses the peripheral @ `0x40005800` = **I2C2**. Pins PB6/PB7 (checked earlier)
are GPIO unrelated to the EEPROM. The correct pins are **PB10/PB11** (AF open-drain).
Confirmed by reading GPIOB CRH (`0x40010C0C` = `0x3333ff34`) and RCC APB1EN (bit22 I2C2EN=1).

### 2. Device address = 0xD0

Firmware function `0x08053930` uses device control byte **`0xD0`** (7-bit `0x68`).
Differs from the typical `0xA0` for 24C02 — likely multiplexed with another
device on the same bus, or pins A0/A1/A2 pulled differently.

### 3. Thread mode instead of PendSV — no more FPU/MPU faults

The previous approach (PendSV + VTOR) caused MemManage DACCVIOL on FPDSCR
(`0xE000EDF8`) due to lazy FPU stacking during exception entry.

**Solution**: don't use any exception. Set the Thread context directly:
```
reg control 0          ; privileged thread, MSP
reg primask 1          ; mask interrupts
reg faultmask 1
reg msp/sp 0x20000380
reg xPSR 0x01000000    ; T-bit
reg pc <stub_addr+1>
```

Plus in the stub itself at startup: `CPACR=0`, `FPCCR=0`, `MPU_CTRL=0`.
The code runs in Thread mode, no exception entry → no lazy FP stacking → no fault.

### 4. SWRST must preserve clock registers

SWRST (CTL0=`0x8000`) clears CTL1, CKCFG, RT. They must be saved and restored:
| Register | Value |
|----------|-------|
| CTL1 | `0x003C` |
| CKCFG (CCR) | `0x8032` (Fast Mode 400 kHz) |
| RT (TRISE) | `0x0013` |

Restore order: CTL1 → PE=1 → CKCFG → RT → ACKEN.

### 5. Single random-ready reads work; sequential (BTC) — don't

Reading N bytes as N independent transactions (START → dev_w → word_addr →
restart → dev_r → NACK after ADDSEND → STOP → RBNE → DR) works reliably.
Sequential multi-byte reads hung on BTC/ACK handling of the last bytes.

**Important ordering for single-byte read**: wait for ADDSEND first,
THEN set NACK and clear ADDSEND, then STOP, then wait for RBNE.

### 6. Watchdog ~250–300 ms; bus wedge after ~96 B

- Watchdog window allows ~16–96 bytes per `resume`.
- After ~96 bytes, SDA goes low (slave holds it) — SWRST doesn't help.
  Requires **power-cycling the board** (unplug/replug power) to reset the slave.
- Do NOT `resume` firmware between attempts — it permanently worsens bus state.

---

## KV Store in Firmware — Architecture

Firmware contains a built-in key-value system (EasyFlash) with:

### Definition table (flash `0x08085458`, 18 entries × 12 B)
```
struct { u32 key_ptr; u32 value_buf_ptr; u32 max_size; };
```

| # | Key | RAM Buffer | Size | Flags |
|---|-----|------------|------|-------|
| 0 | `cfg_ver` | `0x20000168` | 4 | 0x04 |
| 1 | `mb_hv` | `0x2000017c` | 4 | 0x04 |
| 2 | `mb_sv` | `0x20000180` | 4 | 0x04 |
| 3 | `pdt_ver` | `0x20000178` | 4 | 0x04 |
| 4 | `sn` | `0x200001b0` | 21 | 0x15 |
| 5 | `type_param` | `0x20000188` | 40 | 0x28 |
| 6 | `rb_en_mag` | `0x200001f0` | 16 | 0x30 |
| 7 | `lboard_en` | `0x20000208` | 1 | 0x01 |
| 8 | `shape_param` | `0x200001c8` | 40 | 0x28 |
| 9 | `user_name` | `0x20000280` | 32 | 0x20 |
| 10 | `language` | `0x20000279` | 1 | 0x01 |
| **11** | **`pwd`** | **`0x2000027C`** | **4** | **0x04** |
| **12** | **`usr_pwd_en`** | **`0x20000278`** | **1** | **0x01** |
| 13 | `run_param` | `0x20000220` | 80 | 0x50 |
| 14 | `ota` | `0x20000270` | 4 | 0x04 |
| 15 | `ota_date` | `0x20000274` | 4 | 0x04 |
| 16 | `exhibition_cfg` | `0x200002a0` | 24 | 0x18 |
| 17 | `test_add` | `0x200002b8` | 4 | 0x04 |

### RAM index (lookup table @ `0x200007a0`, 32 entries × 8 B)

Used for fast key lookup via CRC-16 hash:
```
struct { u16 hash; u16 _pad; u32 value_ptr; };
```

Hash function (`0x47a20`): CRC-8 lookup-table with init `~0`, final `~0`
and shift >> 16. CRC table (256× u32) @ `0x08085E70`.

### Persistence

User-modified values are saved in flash @ `0x08028000`
(alias `0x00028000`). The `pwd` value (9633) was found in RAM — it's possible
the PIN was entered by the user earlier and loaded from EEPROM or flash.

---

## Dump of Cached KV Values from RAM

Dumped 48 KB RAM (`firmware/ram_full.bin`, addresses `0x20000000–0x2000BFFF`).

### Key values:

| Key | RAM addr | Value (hex) | Meaning |
|-----|----------|-------------|---------|
| `pwd` | `0x2000027C` | `a1 25 00 00` | PIN = **9633** |
| `usr_pwd_en` | `0x20000278` | `01` | PIN enabled |
| `language` | `0x20000279` | `00` | Default language |
| `user_name` | `0x20000280` | `"MyMower\0"` | Device name |
| `ota` | `0x20000270` | `00 00 00 00` | OTA inactive |
| `run_param` | `0x20000220` | (80 B) | Drive parameters |
| `sn` | `0x200001b0` | `"2312CGF2"` | Serial number |

---

## EEPROM — Read Status

**Verified**: bytes 0x00–0x5F. The rest (0x60–0xFF) remains unread
due to permanent I2C bus hang (slave holds SDA low).

```
00: 00 00 00 00 00 a2 00 00 00 00 00 b6 00 00 00 00
10: 00 00 00 05 03 00 80 00 10 00 10 e0 ff ff 94 ff
20: c7 ff fa 10 3c 00 3c 00 0f 00 0d 00 00 0c 3b 00
30: 68 00 00 00 04 00 00 00 00 00 00 00 00 00 00 00
40: 00 00 00 00 00 00 00 00 00 00 00 00 50 91 0f 28
50: 28 16 11 0d 39 00 82 0c 10 00 00 81 00 00 00 2f
```

Data looks like operational state (hours of operation, calibration, timestamp @ `0x4C`).
No obvious ASCII PIN — the PIN is stored as uint32 LE
in RAM cache, and in the EEPROM it may be stored in the same format in the
unread range 0x60–0xFF.

---

## Project Files

| File | Description |
|------|-------------|
| `firmware/u13_flash_1mb.bin` | Full U13 flash dump (1 MB) |
| `firmware/ram_full.bin` | RAM dump `0x20000000–0x2000BFFF` (48 KB) |
| `firmware/ram_low.bin` | Earlier RAM dump `0x20000000–0x20003FFF` (16 KB) |
| `eeprom/eeprom_part.bin` | Verified EEPROM bytes 0x00–0x5F |
| `tools/eeprom_diag.c` | I2C diagnostic stub (Thread mode) |
| `tools/eeprom_diag.bin` | Compiled stub |
| `tools/link.ld` | Linker script: `0x20000400`, len `0xC00` |
| `tools/dump_chunks.cfg` | OpenOCD config for batch EEPROM dump |
| `tools/dump_full_ram.cfg` | OpenOCD config for RAM dump |

### Useful commands

```bash
# Dump RAM (48 KB)
openocd -f tools/dump_full_ram.cfg

# Read and parse KV definition table from flash
python3 <<'EOF'
import struct
data = open('firmware/u13_flash_1mb.bin','rb').read()
for i in range(18):
    off = 0x85458 + i*12
    kp, vp, sz = struct.unpack('<III', data[off:off+12])
    kend = kp - 0x08000000
    while data[kend] != 0: kend += 1
    key = data[kp-0x08000000:kend].decode()
    print(f'{i:2d} {key:15s} @0x{vp:08x} sz={sz}')
EOF
```

---

## Lessons for the Future

1. **KV-store > I2C**: Instead of fighting I2C (watchdog, bus wedge, restart),
   check first whether the firmware caches values in RAM.
2. **CRC hash in KV**: The index table uses CRC-16 (not a library key hash).
   Use the correct lookup table @ `0x08085E70`.
3. **Thread mode**: When FPU/MPU cause issues with exceptions, Thread mode
   (set via `reg pc` without PendSV) is a simpler workaround.
4. **Flash alias**: GD32F305 aliases flash at both `0x00000000` and `0x08000000` —
   firmware uses both pointers interchangeably.
5. **KV definitions in flash vs runtime**: The definition table (`0x08085458`) contains
   RAM buffer addresses and sizes for each key. Default values are
   initialized in these buffers during boot, then overwritten
   from persistent storage (flash/EEPROM) if changed.
