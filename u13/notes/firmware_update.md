# Firmware Update via USB

The mower firmware can be updated via USB pendrive without any special
hardware (no SWD needed). The **MBTL bootloader** (part of U13 flash)
searches for files on a FAT32 pendrive using wildcard matching.

## Requirements

- USB pendrive, **FAT32**, ≤16 GB
- Internal USB port on the mainboard (inside battery compartment)

## Update Procedure (3 steps)

### Step 1 — Bootloader

1. Copy only `btl_MB_xxxxx.bin` or `SNK_MBTL_xxxxx.bin` onto the pendrive
2. Insert into the mower's internal USB port
3. Power on the mower
4. Wait until the display shows "USB"
5. Remove the pendrive
6. Mower reboots — will ask for PIN (standard after bootloader update)

### Step 2 — Main + Display firmware

1. Clear pendrive
2. Copy `SNK_MB_xxxxx.bin` (main board) + `SNK_DB_xxxxx.bin` (display board)
3. Same procedure: plug in, power on, wait for "USB"
4. Remove pendrive, let it reboot

### Step 3 — Version config

1. Clear pendrive
2. Copy only `env_config.json` with content:
   ```json
   {"pdt_ver":23104}
   ```
3. Same procedure

## Newer modules (firmware 23202+)

Additional firmware files for peripheral boards:

| File pattern | Board | Description |
|-------------|-------|-------------|
| `SNK_MB_*.bin` | Main Board | U13 (GD32F305) firmware |
| `SNK_DB_*.bin` | Display Board | ESP32 firmware |
| `SNK_BB_*.bin` | Boundary Board | Boundary sensor board firmware |
| `SNK_LB_*.bin` | LED Board | LED board firmware |
| `SNK_MBTL_*.bin` | Bootloader | MBTL bootloader for U13 |

## FORMATFLASH.json — Factory Reset

1. Put a file named `FORMATFLASH.json` on the pendrive. **It must not be empty**: the
   bootloader skips files of size 0. Any content works, e.g. `{}`.
2. Insert and power on.
3. The bootloader chip-erases the external SPI NOR (W25Q64, `0xC7` at `08001fa0`).
4. Remove the stick and power-cycle.

The SPI NOR holds the EasyFlash env, so this wipes the PIN (`pwd`), user settings,
schedule, statistics, the event log, the firmware staging area and the product config
(`pdt_ver`, `type`, feature flags, `sn`). The GD32 internal flash (bootloader and app)
is not touched.

## How it works

At power-on the U13 bootloader (`0x08000000–0x08017fff`) decides between USB host
(pendrive) and USB device (PC) mode. In host mode it mounts the stick and scans the root
directory in this order (`08003544`):
- `FORMATFLASH.json` → SPI NOR chip erase
- `env_config*.json` → version info + product config (applied by the app on next start)
- `env_read.json`
- `SNK_MB_*.bin`, `SNK_BB_*.bin`, `SNK_DB*_*.bin`, `btl_MB_*.bin` / `SNK_MBTL_*.bin`,
  `SNK_LB_*.bin` → firmware images, copied into the SPI NOR staging area first, then
  programmed by the bootloader's "load app" step (U13) or sent over UART to the other boards

Where several files match a pattern, the highest number in the name wins.
The bootloader itself is updated via `SNK_MBTL_*.bin` / `btl_MB_*.bin`.

Full trace: [`20261010_usb_investigation.md`](../../20261010_usb_investigation.md).

## Known firmware versions

From the Brucke RM500 community:

| Version | File | Notes |
|---------|------|-------|
| 10918 | factory | Original firmware |
| 21841 | `SNK_MB_21841.bin` | Early update |
| 22607 | `SNK_MB_22607.bin` | Pre-23000 |
| 22803 | `SNK_MB_22803.bin` | Pre-23000 |
| 23000 | — | OTA update (killed WiFi for some users) |
| 23104 | `SNK_MB_23104.bin` | Obstacle avoidance on return |
| 23202 | `SNK_MB_23202.bin` | WiFi fix, USB log in browser, BB+LB modules |

Bootloader versions: `btl_MB_40223.bin`, `SNK_MBTL_40404.bin`
Display board versions: `SNK_DB_60411.bin`, `SNK_DB_60709.bin`, `SNK_DB_61004.bin`
