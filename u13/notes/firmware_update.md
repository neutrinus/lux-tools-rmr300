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

To perform a full factory reset (erases entire flash including PIN):

1. Create an empty file named `FORMATFLASH.json` on the pendrive
2. Insert and power on
3. The bootloader will wipe all flash memory

After this, the mower is in factory state — no PIN required on first boot.

## How it works

The MBTL bootloader uses wildcard file matching:
- `env_config*.json` → reads version configuration
- `FORMATFLASH.json` → triggers full flash erase
- `SNK_MB_*.bin`, `btl_MB_*.bin`, etc. → firmware update

The bootloader is part of the U13 1 MB flash image and is updated
separately via `SNK_MBTL_*.bin`.

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
