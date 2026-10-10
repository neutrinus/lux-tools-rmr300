# USB socket, firmware update and factory reset

The mainboard has a USB-A socket (**J6**) inside the battery compartment, under a rubber cover. It is wired to U13's USBFS (PA11/PA12, via FB5/FB6 and the TUS4 TVS). **Only the U13 bootloader has USB code**: the socket is used at power-on, before the app starts, and a stick plugged into a running mower does nothing.

At every boot the bootloader picks one of two modes:

- **Host** (pendrive): firmware update of every board, product config, SPI flash wipe, HTML log report.
- **Device** (PC): a custom-HID service protocol for a factory tool (VID `0x28E9`, PID `0x0567`).

Markers: **[F]** read from the firmware, **[W]** community reports (Brucke owners on io-tech.fi), **[I]** inferred.

## Using a stick

- FAT32, ≤ 16 GB (or a ≤ 16 GB partition). exFAT and NTFS do not work; some sticks are ignored and another one helps [W].
- Insert the stick **before** switching the mower on.
- The display shows `USB`, a percentage, then `LOAD`. Do not switch off during `LOAD` [W].
- Files of size 0 are ignored [F]. Where several files match a pattern, the highest number in the name wins [F].

| File on the stick | Effect [F] |
|---|---|
| `FORMATFLASH.json` (non-empty) | **Chip-erases the external SPI NOR**: PIN, settings, schedule, statistics, product config, log, firmware staging. U13's internal flash is untouched. Checked first |
| `env_config*.json` | Version bookkeeping and product config (below) |
| `env_read.json` | Opened and reported to the display only |
| `SNK_MB_*.bin` | U13 app. Accepted only for version 30000–49999 |
| `SNK_MBTL_*.bin`, `btl_MB_*.bin` | U13 bootloader. Accepted only for version 50000–59999 |
| `SNK_BB_*.bin` | U16. `ver/10000` must equal the current `bb_sv/10000` |
| `SNK_DB_*.bin`, `SNK_DBL_*`, `SNK_DBS_*`, `SNK_DBH_*` | Display board (ESP32). Variant chosen by `db_hv` (60300/60400 → `DB`); **no version check** |
| `SNK_LB_*.bin` | LED/ultrasonic board, only if `lboard_en` |

With no firmware file and a valid RTC, the bootloader writes `log_yyyymmdd_hhmmss.html`: model, serial number, MAC, statistics, configuration, versions of all boards and the event log.

**A `SNK_DB_*.bin` replaces the ESP32 firmware, including ESPHome.** Do not leave one on a stick.

### Firmware update procedure [W]

1. Stick with only `SNK_MBTL_*.bin` (or `btl_MB_*.bin`). Power on, wait for `USB`, remove the stick. The mower reboots and asks for the PIN.
2. Stick with `SNK_MB_*.bin` and `SNK_DB_*.bin`. Same procedure.
3. Stick with only `env_config.json` containing the product version of the package you install, e.g. `{"pdt_ver":23104}` for the Brucke 23104 package. The app refuses a `pdt_ver` lower than the current one [F].

After an update the app may report firmware version `0` (not carried over in the env); the mower works normally. A wrong `SNK_BB_*` makes the mower switch off before the PIN prompt; going back to the matching BB file plus `env_config.json` with `{"BB":{"VER":0,"BVER":0,"BRF":0}}` recovers it. A display stuck on `USB` with no stick inserted was a wet socket.

### Known versions

Our unit (Lux A-RMR-300-24): product 31018, model `RMC300E20V-ECDNSS`, MB `sv` 31315 (`hv` 22500), bootloader `mblt_sv` 50517, BB `sv` 50003, DB `hv` 60400 `sv` 30202.

Files published for Brucke RM500/RM501 (product versions 22100–23205: `SNK_MB_21841`, `SNK_MB_22905`, `SNK_MBTL_40223`, `SNK_MBTL_40501`, `SNK_BB_30505`, `SNK_DB_60411`, `SNK_DB_61107`, …) are an older generation: our bootloader rejects their MB, MBTL and BB files, and would accept their DB file. Product versions 23303 (RM801, 2024) and 31300 (RM501/RM800 from 2025) exist; no files for them are public.

### Factory reset: `FORMATFLASH.json`

1. Put a file named `FORMATFLASH.json` with any content (e.g. `{}`) on a FAT32 stick.
2. Insert it and switch the mower on. The bootloader erases the SPI NOR (`08001fa0`, command `0xC7`).
3. Remove the stick and switch off and on.

The PIN is gone, and so is everything else in the env, including the product config (`pdt_ver`, `type`, feature flags, serial number). [I] The mower may need it restored with `env_config*.json`; not tested.

## `env_config*.json` [F]

Parsed by `08009720`. Optional objects `MB`, `BB`, `DB`, `LB`, `BTL`, each with `VER`, `BVER`, `SIZE`, `BRF`, are written into the env (`MB_VER`/`mb_sv`, `MB_BVER`, …). The whole file text is also stored as env `cfgstr` with `cfgupdate = 0xA5`, and the app applies it on its next start (`service_config.c`, parser `08076314`). Keys the app reads:

`sn`, `pdt_ver` (refused if lower than the current one), `type`, `type_name`, `available`, `platform`, `region`, `type_param`, `mb_hv`, and the feature flags `pwd_en`, `rain_en`, `fslip_en`, `ult_en`, `led_en`, `gps_en`, `sch_en`, `zone_en`, `com_en`, `hit_en`, `lift_en`, `mems_en`, `border_en`, `mtrack_en`, `wlch_en`, `auto_off_en`, `blade_check_en`.

## Bootloader internals [F]

`main` at `0800e800`:

```
init: 08004238 peripherals, 08008948 USB, 0800a6d8 env, 0800cf04 keys, 080024c4 ADC
08007b78  wait for the display board
080043ec  mode select
loop over the flag word *0x20000018:
  bit 0          08012930  USB host state machine
  bit 2          08003e0c  apply env_config*.json
  bit 11         08002fc0  env_read.json
  mask 0xd380    0800303c  IAP: copy SNK_*.bin from the stick to SPI NOR staging
  bit 1          08003cdc  write the log report
  bit 4 & bit 6  08007e18  "load app": program staged images, then jump to the app
  bit 13         08001370  OTA over UART
```

### Mode select `080043ec`

| Condition (env) | Result |
|---|---|
| `ota` = 1 or 5 | UART OTA mode, no USB (an interrupted OTA leaves this set) |
| `cfg_rst` = `0xA5A5` | delete `cfg_rst`, device mode |
| `cfg_rst` = `0xAA55` | write `cfg_rst = 0xA5A5`, boot the app |
| otherwise | `08002f18`: set PB6, read PB2; high = host (`08002ddc`), low = device (`08002784`) |

PB2 (BOOT1) is not configured by the bootloader and is read as a floating input. On the PCB it goes to test pad TP257 on the back, not visibly to the USB area [I: a strap input].

### Host mode

`08002ddc` polls D+ (PA12) for up to 1.5 s. Nothing → "Can't detected USB", normal boot (so a boot without a stick is up to 1.5 s slower). A stick → PB0 is set (bootloader power hold [I]), the GD USB host MSC library starts, and the "USB disk Ready" callback `08003544` scans the files with FatFs.

Firmware from the stick is copied into the SPI NOR staging area (`0800303c`); progress goes to the display as `0x4100000a {"action":4}` with a percentage, errors as `action:8`. The "load app" step (`08007e18`) then compares current and staged versions, programs U13 or sends the image to the other board over UART (`programBB.c`, `programLB.c`), and jumps to the app. After the scan, with display type 3 or 4, U13 sends `{"cmd":0x4100000d,"MB":…,"BB":…,"DB":…,"MBTL":…,"LB":…}`.

### Device mode

Descriptor at `0801525e`: USB 2.0, VID `0x28E9`, PID `0x0567`, one HID interface, interrupt EP `0x81` IN / `0x01` OUT, 64-byte reports, 5 ms. Frames handled by `080027c8` are `A5 <len> <cmd> <payload…>`:

| cmd | Action |
|---|---|
| `01` | Handshake |
| `02` | Show "finish!", reset |
| `03` | Write `cfg_rst = 0xAA55` and reset (leave device mode) |
| `10` | JSON chunk; the last one is parsed like `env_config*.json` |
| `11` | Write `sn` (< 21 bytes) |
| `12` | Set the RTC |
| `21` | Firmware info (16 bytes) |
| `31` | Firmware chunk into SPI staging |
| `51` | Read `mb_sv`, `bb_sv`, `db_sv`, `MB_BTL`, `pdt_ver`, `lb_sv` |
| `52` | Read software and hardware versions of all boards |
| `61` | Dump the whole env |
| `62` | Dump the log |
| `63` | Read one env key by name |
