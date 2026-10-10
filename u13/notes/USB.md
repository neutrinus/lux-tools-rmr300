# USB port on the mainboard (J6)

What the USB-A socket on the mainboard is for, traced in the U13 dump
(`u13/firmware/u13_flash.bin`) with `tools/re/gd32dis.py` and a linear Thumb sweep.

Markers as in [`FIRMWARE_MAP.md`](../../FIRMWARE_MAP.md):
**[F]** read from the firmware, **[P]** seen on the board photos, **[I]** inferred, not verified.

## Summary

- The socket is wired to U13 (GD32F305) USBFS (PA11/PA12). **Only the U13 bootloader
  (`0x08000000..0x08017fff`) has USB code** [F]. The application (`0x08018000+`) has no USBFS
  register references and no USB strings [F]. ESP32 and U16 have no USB code either [F].
- So USB is used **only at power-on, before the app starts**. Plugging a stick into a running
  mower does nothing [I: follows from the above].
- At every boot the bootloader picks one of two modes:
  - **Host (pendrive)**: firmware update of every board, writing product config,
    SPI-flash wipe, saving an HTML log report.
  - **Device (PC)**: a custom-HID service protocol (VID `0x28E9` GigaDevice, PID `0x0567`)
    for a factory tool: versions, env dump, log dump, serial number, RTC time,
    firmware download, config.

## Hardware [P]

`img/mainboard_bottom.jpg`, right edge next to the RTC battery:

- J6 USB-A female, silkscreen `GND D- D+ 5V`.
- FB5 / FB6 ferrite beads in the D+/D- lines towards U13 (top-right of U13).
- TUS4 TVS on the data lines, U12 (SOT-23-5/6) with C104/C105/R144 next to the 5V pin
  [I: VBUS power switch].
- The silkscreen does not show which U13 pins PB2 / PB6 (below) go to.
  `HARDWARE.md` names "U3" as the USB IC; on the photo the part near J6 is U12.

## Boot flow [F]

Bootloader `main` is at `0800e800`:

```
0800e800  init: 08004238, 08008948, env 0800a6d8, keys 0800cf04, ADC 080024c4
0800e824  wait for display board (08007b78), 08007c68(0)
0800e834  mode select 080043ec
0800e83c  loop over flag word *0x20000018:
            bit 0           08012930  USB host core state machine
            bit 2           08003e0c  apply env_config*.json
            bit 11          08002fc0  env_read.json
            mask 0xd380     0800303c  IAP: copy SNK_*.bin from the stick
            bit 1           08003cdc  save log to the stick
            bit 4 & bit 6   08007e18  "load app" state machine (update from staging, jump)
            bit 13          08001370  OTA over UART (not USB)
```

### Mode select `080043ec` [F]

Reads env keys `ota`, `lboard_en` and `cfg_rst` (EasyFlash env on the SPI NOR), then:

| Condition | Result |
|---|---|
| `ota == 1` or `ota == 5` | UART OTA mode ("into ota mode"), no USB |
| `cfg_rst == 0xA5A5` | delete `cfg_rst`, **device mode** ("into usb device mode") |
| `cfg_rst == 0xAA55` | write `cfg_rst = 0xA5A5`, set flag 0x10 (boot the app) |
| otherwise | `08002f18`: hardware decides host or device |

`08002f18` [F]:

```
gpio_bit_set(GPIOB, PIN6)
if PB2 == 1                      -> host   (08002ddc)
delay 5 ms; if PB2 == 1          -> host
else                             -> device (08002784)
```

PB2 is never configured in the bootloader GPIO init (`08000f7c`), so it is read in its reset
state (floating input). PB2 is also the BOOT1 pin of the GD32. What drives it on this board is
not traced [I].

### Host mode `08002ddc` [F]

1. PA12 (D+) is configured as floating input and polled for up to 1500 ms
   (`0x5dc`) waiting for it to go high. A full-speed stick pulls D+ up, so this is a
   "is anything plugged in" check.
2. Nothing: flag 0x10, log "Can't detected USB", and the normal boot continues.
   A pendrive-less boot is therefore up to ~1.5 s slower [I].
3. Found: `gpio_bit_set(GPIOB, PIN0)`, log "Detected USB divice", start the GD USB host
   library (`08013ce0`, MSC class; its strings are at `080152be`) and set flag bit 0.
4. When the stick is mounted, the MSC user callback `08003544` ("USB disk Ready") runs the
   file scan below.

PB0 is also set in the key handler (`0800cf84`, "key press power on") and cleared in the
reset routine `08001bdc` before `NVIC_SystemReset` [F]. [I: a bootloader-side power hold,
so the mower stays on during the update.]

### File scan `08003544` [F]

Uses FatFs `f_findfirst/f_findnext` (`0800b5c4` / `0800b5e4`) on `0:/`. Every match is checked
for `fsize != 0`, so **an empty file is ignored**. Where several files match, the one with the
highest number in its name wins (`080071aa` parses it).

| Pattern | Flag | What happens |
|---|---|---|
| `FORMATFLASH.json` | none | **Erases the whole external SPI NOR** (`08001fa0`: WREN, `0xC7` chip erase), then logs "format flash" through the assert handler `080041b4` [I: the mower stops there until power-cycled]. Checked first |
| `env_config*.json` | bit 2 | `08003e0c` loads it, `08009720` parses it (below) |
| `env_read.json` | bit 11 | `08002fc0`: opens/creates the file, reports to the display, waits 2 s. Looks unfinished: nothing is written into the JSON [I] |
| `SNK_MB_*.bin` | 0x80 | Main board (U13 app). Accepted only for version 30000–49999 |
| `SNK_BB_*.bin` | 0x100 | Border board (U16). `ver/10000` must match the current `bb_sv/10000` |
| `SNK_DB_*.bin`, `SNK_DBL_*.bin`, `SNK_DBS_*.bin`, `SNK_DBH_*.bin` | 0x200 / 0x8000 | Display board. The variant is chosen from the display type and `db_hv` (60300/60400 → DB, 70000 → DBL, 10200 → DBS, DBH) |
| `btl_MB_*.bin`, `SNK_MBTL_*.bin` | 0x1000 | U13 bootloader. Accepted only for version 50000–59999 |
| `SNK_LB_*.bin` | 0x4000 | LED/ultrasonic board, only if `lboard_en` |

If no firmware flag is set and the RTC is valid (`BKP_DATA3` is `0xA5A5` or `0x5A5A`,
`0800c91e`; the app marks it in RTC init `0805b5c4`), flag bit 1 is set and the log
report is written.

After the scan, if the display type is 3 or 4, U13 sends the display a JSON
`{"cmd":0x4100000d,"MB":…,"BB":…,"DB":…,"MBTL":…,"LB":…}` with the versions it found.

### IAP `0800303c` [F]

Firmware from the stick is **not** written to U13 flash directly. Each image is copied into
the SPI NOR staging area (`0800214c` "set firmware info", `080046fc` writes,
"flash write %d/%d"), progress goes to the display as `0x4100000a` `action:4` with a
percentage, errors as `action:8`. Then the flags are cleared. With no work left the bootloader
tick (`08011eb8`) sets flag 0x10 after 500 ticks and the "load app" state machine
(`08007e18`) compares current and staged versions ("mb update, current ver, backup ver",
"db update…"), programs U13 or pushes the image to the other board over UART
(`programBB.c`, `programLB.c`, "BB ack time out"), then jumps to the app
("no need load app, jump to app").

### env_config*.json [F]

`08009720` accepts optional objects `MB`, `BB`, `DB`, `LB`, `BTL`, each with `VER`, `BVER`,
`SIZE` and `BRF`, and writes them into env (`MB_VER`/`mb_sv`, `MB_BVER`, …). Then it always
stores the **whole file text** in env `cfgstr` and sets `cfgupdate = 0xA5`.

The app applies `cfgstr` on its next start (`080281aa`, `service_config.c`, parser
`08076314`) and deletes it. Keys the parser reads:

`sn`, `pdt_ver` (refused if lower than the current one), `type`, `type_name`, `available`,
`platform`, `region`, `type_param`, `mb_hv`, and the product feature flags `pwd_en`, `rain_en`,
`fslip_en`, `ult_en`, `led_en`, `gps_en`, `sch_en`, `zone_en`, `com_en`, `hit_en`, `lift_en`,
`mems_en`, `border_en`, `mtrack_en`, `wlch_en`, `auto_off_en`, `blade_check_en`.

So `{"pdt_ver":23104}` from the community procedure works. The same mechanism can change the
model type, region, serial number and feature flags [F]. [I: `pwd_en` here is the product-level
"PIN feature present" flag that the ESP shows; setting it to 0 may hide the PIN. Untested.]

### Log report `08003cdc` [F]

Writes `log_yyyymmdd_hhmmss.html` (time from the RTC) by `0800d158`: model, serial number,
MAC, statistics, configuration (rain, LED, ultrasonic, multizone, schedule), versions of all
boards, then the event log the app keeps in SPI NOR (EasyLogger `elog_flash.c` in the app).
On success the display gets `action:3`, on error `action:1`.

## Device mode (PC) [F]

`08002784` starts the USB device stack, `08002ba8` runs the loop. The descriptor at
`0801525e`: USB 2.0, VID `0x28E9`, PID `0x0567`. One interface, class HID (`03`),
interrupt EP `0x81` IN and EP `0x01` OUT, 64-byte reports, 5 ms.

Frames handled by `080027c8` are `A5 <len> <cmd> <payload…>`:

| cmd | Action |
|---|---|
| `01` | Handshake ("USB shake hands") |
| `02` | Show "finish!", `action:6`, reset |
| `03` | Write `cfg_rst = 0xAA55` and reset (leave device mode) |
| `10` | Append JSON chunk; on the last one parse it like `env_config*.json` |
| `11` | Write `sn` (serial number, < 21 bytes) |
| `12` | Set the RTC counter and mark `BKP_DATA3 = 0x5A5A` |
| `21` | Set firmware info (16 bytes, same as IAP) |
| `31` | Program a firmware chunk into SPI staging |
| `51` | Read `mb_sv`, `bb_sv`, `db_sv`, `MB_BTL`, `pdt_ver`, `lb_sv` |
| `52` | Read software and hardware versions of all boards |
| `61` | Dump the whole env |
| `62` | Dump the log ("pkg%d: send log %d, left %d") |
| `63` | Read one env key by name |

The `cfg_rst` handshake (`0xAA55` → `0xA5A5` → device mode on a later boot) is what the
bootloader does; who writes `0xAA55` outside device mode, and when the app clears `0xA5A5`,
is not traced. The app only reads it (`0804dd58`, "get reset flag value=%d error").

## What the repo got wrong before this note

- `u13/notes/firmware_update.md`, `GD32F305.md`: "**empty** `FORMATFLASH.json`" — empty files
  are skipped. The file must have at least one byte.
- Same files: "erases the entire (U13) flash". It erases the **external SPI NOR**
  (EasyFlash env/KV, logs, firmware staging), not the GD32 internal flash.
- `GD32F305.md`: "no direct code references to `FORMATFLASH.json`". It is loaded by `adr` at
  `0800359e`.
- `GD32F305.md` §8 flag table: bit 2 is `env_config` (`08003e0c`), not IAP; `08002fc0` is
  `env_read.json` (bit 11); IAP is mask `0xd380`.
- `GD32F305.md`: "U13 detects pendrive … programs its own flash". It stages the image in SPI
  NOR and the "load app" step programs it.
