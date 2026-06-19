# PIN Recovery — SNK Mower

**Result: PIN `9633`** ✅ — works on all SNK clones.

---

## Primary Method: SWD (works)

Reads the PIN from U13 (GD32F305) RAM via SWD. Works on any SNK mower,
regardless of firmware state.

### Requirements

- Raspberry Pi Pico (any variant) flashed with [debugprobe](https://github.com/raspberrypi/debugprobe) UF2
- 3x Dupont wires (GND, SWCLK, SWDIO)
- Access to P4 pads on the mainboard — **requires full disassembly**
- OpenOCD on your computer

### 1. Connect SWD to U13 (GD32F305)

P4 on mainboard (top to bottom): `3V3 DIO CLK JTDO RES GND`

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

Output: `u13/firmware/ram_full.bin` (48 KB, addresses `0x20000000–0x2000BFFF`).

### 3. Read the PIN

```bash
python3 -c "
import struct
ram = open('u13/firmware/ram_full.bin', 'rb').read()
val = struct.unpack('<I', ram[0x27c:0x280])[0]
print(f'PIN = {val:04d}')
"
```

The PIN is a 4-digit number stored as **uint32 LE** in the firmware's KV-store
RAM cache. Key `"pwd"` at address `0x2000027C`.

### Why this works

The U13 (GD32F305) firmware has a built-in KV-store (key-value store).
The key `"pwd"` holds the 4-byte PIN, cached in RAM. Dump RAM, read it.

Details: [u13/notes/GD32F305.md](u13/notes/GD32F305.md)

### udev Rule (Linux)

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/99-pico-debugprobe.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Full hardware documentation (PCB, SWD ports, pinouts): [HARDWARE.md](HARDWARE.md)

---

## Alternative Methods

### FORMATFLASH.json (factory reset)

The firmware contains string `"FORMATFLASH.json"`. The MBTL bootloader (in U13)
uses wildcard file matching on USB: `env_config*.json`, `SNK_MB_*.bin`, etc.
`FORMATFLASH.json` is caught by the same mechanism and erases all flash
including the KV-store and PIN.

**How to use:**
1. Format a FAT32 USB stick (≤16 GB)
2. Place an empty file named `FORMATFLASH.json` on it
3. Insert into the mower's internal USB port
4. Power on — the bootloader wipes the flash
5. After reboot the mower is factory fresh — **no PIN required**

Confirmed by the Brucke RM500 community (io-tech.fi).

### Does FORMATFLASH.json actually work?

The string exists in our firmware but we found no direct code references in
the main application. However, the MBTL bootloader (also in the 1 MB flash)
uses wildcard matching — the same mechanism as `env_config*.json`.
The Brucke RM500 community confirms it works.

### EEPROM reader (SOIC-8 clip) — partially works

The PIN is stored in **U22 (24C02, I2C EEPROM, 256B)** on I2C2 bus
(address `0xD0`). We tried — I2C communication works, we read bytes
`0x00–0x5F`. After ~96 bytes the I2C bus wedges (slave holds SDA low)
and requires a power cycle.

**Problem:** the entire PCB is covered in conformal coating (protective
lacquer), making direct SOIC clip probing difficult. The I2C bus wedge
after ~96B further complicates things.

The PIN was not in the read range, and further attempts were unnecessary
after a successful SWD read.

Details: [u13/notes/eeprom_dumping.md](u13/notes/eeprom_dumping.md)

### Why other methods DON'T work

- **Searching flash for PIN** — no plaintext PIN (ASCII/BCD) in any dump
  (U13 1 MB, U16 256 KB, ESP32 4 MB). PIN is stored binary as uint32.
- **Reading from ESP32** — the ESP32 on the display board only relays the
  PIN to the mainboard for verification; it does not store it.
- **Ghidra headless** — full decompilation via Ghidra 12.1.2 headless fails
  (OSGi issues). Partial decompilation via ghidra-cli.
