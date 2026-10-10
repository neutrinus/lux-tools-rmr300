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

The U13 bootloader checks a FAT32 pendrive in the mainboard USB socket (J6) at every
power-on. If it finds `FORMATFLASH.json`, it chip-erases the external SPI NOR
(W25Q64, command `0xC7`). That flash holds the EasyFlash env, which includes `pwd`
(the PIN), `usr_pwd_en`, the user settings, the product config, the event log and the
firmware staging area. Details and addresses:
[`20261010_usb_investigation.md`](20261010_usb_investigation.md).

**How to use:**
1. Format a FAT32 USB stick (≤16 GB).
2. Put a file named `FORMATFLASH.json` on it. **It must not be empty**: the bootloader
   skips files of size 0. Any content works, e.g. `{}`.
3. Insert it into the mainboard USB socket and power on.
4. The bootloader erases the SPI NOR. Remove the stick and power-cycle.

After that the PIN is gone, and so is everything else in the env: user settings,
schedule, statistics, log, and the product config (`pdt_ver`, `type`, feature flags,
serial number). The mower may need its config restored with `env_config*.json`
before it behaves as before. Untested on this unit.

### U22 (I²C device) — does not hold the PIN

U22 sits on I2C2 (PB10/PB11, 7-bit address `0x68`). Bytes `0x00–0x5F` were read
over a SOIC clip before the bus wedged, and the PIN was not among them. The firmware
keeps the PIN in the SPI NOR env (`pwd`, loaded at `08060858`, saved at
`0807c96e`), not in U22.

Details: [u13/notes/eeprom_dumping.md](u13/notes/eeprom_dumping.md)

### Why other methods DON'T work

- **Searching flash for PIN** — no plaintext PIN (ASCII/BCD) in any dump
  (U13 1 MB, U16 256 KB, ESP32 4 MB). PIN is stored binary as uint32.
- **Reading from ESP32** — the ESP32 on the display board only relays the
  PIN to the mainboard for verification; it does not store it.
- **Ghidra headless** — full decompilation via Ghidra 12.1.2 headless fails
  (OSGi issues). Partial decompilation via ghidra-cli.
