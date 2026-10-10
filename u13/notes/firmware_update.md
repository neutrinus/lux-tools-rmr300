# Firmware Update via USB

The mower firmware can be updated via USB pendrive without any special
hardware (no SWD needed). The **MBTL bootloader** (part of U13 flash)
searches for files on a FAT32 pendrive using wildcard matching.

## Requirements

- USB pendrive, **FAT32**, ≤16 GB (or a ≤16 GB partition). exFAT and NTFS do not work, and
  some sticks are ignored; owners report that another stick helped.
- Internal USB port on the mainboard (inside battery compartment)
- Insert the stick **before** power-on. USB is handled only by the bootloader at boot.

Practical notes from the Brucke community (io-tech.fi, see
[`20261010_forum_io-techfi.md`](../../20261010_forum_io-techfi.md)) [W]:
- The display shows `USB`, then a percentage, then `LOAD`. Do not power off during `LOAD`.
- After an update the app may show firmware version `0`: the version in the env was not carried
  over. The mower works normally.
- A display stuck on `USB` with no stick inserted was a wet USB socket; drying it fixed it.
- A wrong `SNK_BB_*` for the border board makes the mower switch off before the PIN prompt.
  Fixed by going back to the matching BB file plus `env_config` with
  `{"BB":{"VER":0,"BVER":0,"BRF":0}}`.
- Writing a lower `pdt_ver` no longer works on newer firmware, and after it the bootloader
  showed `BAD USB` (2025–2026 reports). This matches the app parser refusing a lower `pdt_ver` [F].

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
   This value is from the Brucke 23104 package. The app refuses a `pdt_ver` lower than the
   current one, so on our mower (31018) it would be ignored. Use the version of the package
   you install.
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

> **Corrected 2026-10-10.** The earlier table mixed product versions (`pdt_ver`, what the app
> shows) with file numbers. `SNK_MB_23104.bin` / `SNK_MB_23202.bin` etc. were never seen; the
> file numbers are per board and differ from the product version.

**Our mower (Lux A-RMR-300-24)** [F]: product `version` 31018 (`0x330000A1`; [I] the same number as `pdt_ver`), model `RMC300E20V-ECDNSS`;
MB `sv=31315` (`hv=22500`), bootloader `mblt_sv=50517`, BB (U16) `sv=50003`,
DB (ESP32) `hv=60400 sv=30202` (captures, `0x330000A1` / `0x330000A2`).

**Brucke RM500/RM501 files seen on io-tech.fi** [W]:

| Product version | Files | Source |
|---|---|---|
| 10918 / 10920 | factory / first OTA | |
| `MB_22100` (API label, 2022) | `SNK_MB_21841.bin`, `SNK_MBTL_40223.bin`, `SNK_DB_60411.bin` | vendor API |
| 23202 | `SNK_MB_22905.bin`, `SNK_MBTL_40501.bin`, `SNK_DB_61107.bin` | posted 06.2023 |
| 23205 (RM501) | `RMA501M20V-23205.zip`, BB rescue `SNK_BB_30505.bin` | Google Drive link, 08.2023 |
| 23303 | — | RM801 (2024), support's "latest" for RMA501M20V |
| 31300 | — | RM501/RM800 from 2025, no files shared |

Other names seen: `btl_MB_40223.bin`, `SNK_MBTL_40404.bin`, `SNK_DB_60709.bin`, `SNK_DB_61004.bin`.

**None of these fit our mower.** Our bootloader accepts `SNK_MB_*` only for 30000–49999 and
`SNK_MBTL_*`/`btl_MB_*` only for 50000–59999, and `SNK_BB_*` only with the same
`ver/10000` as the current BB (5 here) [F, `20261010_usb_investigation.md`]. All Brucke MB, MBTL
and BB files are outside those ranges. `SNK_DB_*` is picked by `db_hv` without a version range, so
a Brucke `SNK_DB_6xxxx.bin` would probably be taken and would replace our ESP32 firmware (3.02.02)
with a different line [I]. **Do not put Brucke DB files on the stick.**
