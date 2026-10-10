# USB port on the mainboard (J6) — investigation 2026-10-10

What the USB-A socket on the mainboard is for, traced in the U13 dump
(`u13/firmware/u13_flash.bin`) with `tools/re/gd32dis.py` and a linear Thumb sweep.

Markers as in [`FIRMWARE_MAP.md`](FIRMWARE_MAP.md):
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
- Sharper top-side photo (2026-10-10, `PXL_20260616_111216406.jpg` in the project thread): U12 sits between
  the 5V buck (U7/L1) and the J6 5V pin, with C102/C104/C105, R144, FB2 and TP48. TUS4 with R143/R153
  sits next to the data lines.
- The PCB is conformal-coated, so continuity tests are not practical. PB2 / PB6 were traced visually
  instead (below). U13 LQFP100 pins: PB2 = 37, PB6 = 92, PA11 (D-) = 70, PA12 (D+) = 71
  [I: GD32F30x LQFP100 pinout].

### Visual trace of PB2 / PB6 [P]

Annotated crops: [`img/pb2_trace.jpg`](img/pb2_trace.jpg).

- **U13 orientation.** The pin-1 dot is not visible under the coating. The crystals Y1 (32 kHz) and
  Y2 (HSE) sit at the middle-left of the bottom pin row (OSC32 = pins 8/9, OSC = 12/13), so the bottom
  row is pins 1–25 left to right. Then the right row is 26–50 bottom to top, the top row 51–75 right
  to left, the left row 76–100 top to bottom. Pins were located by intensity peaks (pitch ≈ 12.9 px in
  the 3472×4624 photo). Consistent with that: R141 sits at pin 94 (BOOT0, normally a pull-down) [I].
- **PB2 (pin 37, right row, 12th from the bottom).** A short stub to a via just right of the pin row
  (front ≈ (1645, 3402) px). The front and back photos were registered with the four mounting holes
  and refined on this via and its three neighbours, whose pattern matches on both sides. On the back
  the via joins a short trace to **test pad TP257**, and on to a via under the U13 body. Beyond that
  the net is hidden by the chip. On the front the via also has a trace into the bundle running to the
  lower right, away from J6. **No visible link from PB2 to U12 or J6.**
- **PB6 (pin 92, left row, 17th from the top).** The trace leaves left into a bundle of parallel
  traces that turns down between TP47 and FB5. At this resolution, and under the coating, the traces
  of the bundle cannot be told apart, so where PB6 ends is **unresolved**.
- U12 is SOT-23-5 (three pins up, two down), with C102 on the input side and C105/C107/FB2 towards the
  J6 5V pin. Its control-pin traces disappear under the coating or into vias.

[I] Since PB2 does not visibly go to the USB area, it is more likely a board-variant / strap input
than a VBUS sense. TP257 is a bare test pad on the back. Measuring its voltage needs no scraping:
power on with no stick, then with a stick. High in both cases means the strap explanation;
a change means a sense line.

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
| `FORMATFLASH.json` | none | Name loaded by `adr` at `0800359e`. **Erases the whole external SPI NOR** (`08001fa0`: WREN, `0xC7` chip erase), then logs "format flash" through the assert handler `080041b4` [I: the mower stops there until power-cycled]. Checked first |
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

## What FORMATFLASH.json erases [F]

The SPI NOR (W25Q64) holds the EasyFlash env shared by the bootloader and the app.
The app keeps the PIN there: it loads `pwd` with the env getter at `08060858` into the
RAM cache `0x2000027C` and saves it with the env setter at `0807c96e`. The same env holds
`usr_pwd_en`, user settings, schedule, statistics, `cfg_rst`, `cfgstr`, the product config
(`pdt_ver`, `type`, `sn`, feature flags), the event log and the firmware staging area.
The chip erase removes all of it. The GD32 internal flash (bootloader and app) is untouched.

So a non-empty `FORMATFLASH.json` removes the PIN. [I] After it the app starts with
default values; whether the product config then needs restoring with `env_config*.json`
is untested.

U22 (I2C2, address `0x68`) is not where the PIN is kept; no `pwd` path leads to I2C.

## J7 [P]

J7 is a populated 4-pin header below U13, next to U22, silkscreen `+5V ↑ ↓ GND`. In the
assembled mower nothing is plugged into it (owner's observation, 2026-10-10). The two signal lines have
TVS diodes TUS5/TUS6 and series resistors R169/R171 (R157, R218 nearby).

It is a UART, not USB:
- The GD32F305 has one USBFS peripheral (PA11/PA12), and it is wired to J6 through FB5/FB6
  [P, F: the only USB code is in the bootloader and uses that peripheral].
- J7's signal pins carry direction arrows, the same notation the board uses for the UART
  pins of J8 (`→ ←`). USB D+/D- are bidirectional and are labelled `D- D+` on J6. There are
  no ferrites on J7 as there are on J6.

[I] The app has exactly three serial ports: `dpport` (USART0, ESP32 via J8), `bdport`
(USART1, U16 on the board) and `ledport` (UART3, `0x40004C00`, LED/ultrasonic board). The
LED board is optional (`lboard_en`; the bootloader only flashes `SNK_LB_*.bin` when it is
set), and the Lux unit has none. So J7 is most likely the `ledport` connector, left empty on
models without that board. The traces from R169/R171 were not followed to U13 pins under
the coating, so the UART3 pins (PC10/PC11, LQFP100 pins 78/79) are unconfirmed. To check
without scraping: a USB-UART adapter on J7 (GND plus the two arrow pins, levels not
measured yet) during boot would show whether U13 polls an LED board there.
