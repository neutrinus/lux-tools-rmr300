# Firmware map: ESP32, U13, U16

This is a navigation aid for agents and people reading the dumps. Every address here was checked against the binaries in this repo with `tools/re/` (see [`tools/re/README.md`](tools/re/README.md)).

The evidence and reasoning behind the findings are in [`20261009_claude_investigation.md`](20261009_claude_investigation.md).

Markers:
- **[F]** read directly from the firmware.
- **[C]** confirmed in an LA capture.
- **[I]** inferred, not verified.

## System overview

```
ESP32 (display board)  ──UART 230400, JSON &{..}crc#──  U13 GD32F305 (main MCU)  ──UART JSON──  U16 GD32F303 (border board MCU)
 buttons START/HOME/OK          dpport (USART0)          motors, nav, PIN, logs      bdport (USART1) ↔ mport (USART2)
 7-seg display, buzzer, rain                              ledport (UART3) → LED/ult module
 WiFi/BT, cloud MQTT
```

---

## ESP32 (`esp32/firmware/`)

| File | What |
|---|---|
| `esp32_dump.bin` | Full 4 MB flash |
| `ota_0.bin` | Active app partition (otadata seq=1). This is the file to analyse |
| `disasm.s` | Annotated listing of `ota_0.bin`. Regenerate it with `esp32dis.py dump` |

**Partition table** (`esp32_dump.bin` at `0x8000`) [F]:

| Name | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 0x4000 |
| otadata | 0xd000 | 0x2000 |
| phy_init | 0xf000 | 0x1000 |
| ota_0 | 0x10000 | 0x170000 |
| ota_1 | 0x180000 | 0x170000 |
| coredump | 0x2f0000 | 0x10000 |
| panic_out | 0x300000 | 0x1000 |

**App image** `Display_esp32` v3.02.02, ESP-IDF 4.4.3. Entry point is `0x400814ec`. Segment data starts at header offset + 8:

| vaddr | len | file off | Kind |
|---|---|---|---|
| 3f400020 | 0x262b4 | 0x20 | DROM (strings, const tables) |
| 3ffbdb60 | 0x6cd4 | 0x262dc | DRAM init data |
| 40080000 | 0x3060 | 0x2cfb8 | IRAM (vectors) |
| 400d0020 | 0xe4004 | 0x30020 | IROM (app code) |
| 40083060 | 0x191f8 | 0x11402c | IRAM |
| 50000000 | 0x10 | 0x12d22c | RTC |

Things to know before reading the disassembly:
- **Literals** sit in pools at `0x400d0xxx`–`0x400d2xxx` and are loaded with `l32r`.
- **JSON numbers** are built with cJSON, so command IDs appear as **double** constants in two `l32r` (lo in a10, hi in a11). `0x41c80000:0x04800000` is `0x30000009`.
- **Some commands are tables of `int`s**, converted with `__floatsidf` (`0x4000c938`).

**Modules** (source names in strings): `rw_display.c` (UI), `driver_mboard_port.c` (UART to MB), `driver_rf.c` (CMT2300A), and the threads "tube service", "data manager service", "MachineStatus", "iot drive", "iot recv thread", "bt thread", "log thread" and "update thread".

### Key functions [F]

| Address | Function |
|---|---|
| `400d77d8` | BSP init: tube, buzzer, button, rain. Loads `iot_mode` |
| `400daf7c` / `400daef8` | Button init / poll: GPIO22=bit0 START, GPIO21=bit1 HOME, GPIO19=bit2 OK, pull-up, active low, 7-sample debounce |
| `400e27f4` | UI main loop. 10 ms tick, edge/long-press/combo logic |
| `400e2194` | UI event dispatcher. Sends `0x1000000x` key commands |
| `400e1a58` | `send_cmd(cmd)`: `{"cmd":cmd}` to the MB |
| `400e0bc4` | Factory test (`ft-key-`) |
| `400e6da0` | `TubeInit`: SPI display MOSI 25, SCLK 33, CS 32, 400 kHz |
| `400d9d20` | Parser for MB `0x330000A0` (MachineState). Fills the state struct |
| `400db4c4` | Builds the cloud status report (`mode`, `power`, `errortype`, `station`, …) |
| `400dc4a8` | IoT command task: cloud `cmd` 100–199 to UART commands. `cmd 101` = `mode` |
| `400dc890` | IoT replies and `command`/`passwd`/`rename`/`ota` handling |
| `4012be00` / `4012be24` / `4012be8c` / `4012be6c` / `4012bf34` | cJSON GetObjectItem / AddItemToObject / CreateNumber / CreateBool / CreateObject [I: from call patterns] |
| `4012ce9c` / `4012ce0c` / `4012cdd0` | `gpio_set_direction` / `gpio_set_pull_mode` / `gpio_get_level` |

### Key data [F]

| Address | What |
|---|---|
| `*0x3ffc5b18` | UI context. +0x10 button driver, +0x26/+0x27 previous/current key bits |
| `0x3ffbf460` | Machine state struct (see below) |
| `0x3f4046b4` | Cloud `mode` to UART command table: `0x10000023, 0x10000021, 0x10000022, 0x10000007, 0x10000015` (pause, start, home, select, border) |

The machine state struct at `0x3ffbf460` is filled by `400d9d20`:

| Offset | Content |
|---|---|
| +0x00 | ESP state (MB `state` 1–5 copied, others remapped; 4 = error) |
| +0x34 | `error` |
| +0x38 | `bat_per` |
| +0x39 | `bat_lv` |
| +0x3d | `station` |
| +0x58..+0x63 | Feature flags `pwd_en`, `rain_en`, `sch_en`, `zone_en`, `com_en`, `sp_en`, `led_en`, `ult_en`, `gps_en`, `info_state`, `map_en` |
| +0x60/+0x64 | `bat_name` / `bat_id` |
| +0x68/+0x6c | `bat_ctime` / `bat_dtime` |
| +0x70 | `bat_health` |
| +0x74..+0x84 | `total_minutes`, `on_minutes`, `cut_area`, `cur_minutes`, `current_area` |
| +0x124 | `rain_delay` |
| +0x168..+0x18c | Board versions `mb_sv/hv`, `bb_sv/hv`, `db_sv/hv`, `lb_sv/hv` |

---

## U13 GD32F305 main MCU (`u13/firmware/`)

| File | What |
|---|---|
| `u13_flash.bin` | 512 KB, mapped at `0x08000000` |
| `u13_flash_1mb.bin` | Same start, 1 MB read |
| `ram_full.bin`, `ram_low.bin` | RAM snapshots |

**Layout** [F]:
- **Bootloader** (`user\src\boot.c`, `key.c`, `programBB.c`, `programLB.c`) runs from `0x08000000` to about `0x08017fff`. Reset is `0x08011a25`.
  - It also flashes the other boards ("BB IAP start", "LB IAP start").
  - `key.c` reads PE10/PE11 at `0x0800d01c` ("key press power on").
- **App** vector table is at `0x08018000`. SP is `0x20017ff8`, reset is `0x08018441`. FreeRTOS, EasyLogger, EasyFlash.
- **Literals** are PC-relative `ldr`.
- **Log strings** are referenced with `adr` from the code right before them, not through a literal pool. Search the bytes next to a function, not with `gd32dis.py lit`.

**Source tree** (from paths in strings):

| Area | Files |
|---|---|
| `app/process/` | State machine: `process_wait`, `process_cutting`, `process_departure_smooth`, `process_docking_smooth`, `process_charging`, `process_error`, `process_find_bd`, `process_power_off`, `process_security`, `process_manager`, `process_control` |
| `framework/service_*` | `command`, `dpport`, `bdport`, `ledport`, `border`, `blade`, `bms`, `lift`, `hit`, `slope`, `slip`, `rain`, `multizone`, `user_setting`, `display`, `power`, `stop`, `time`, `ultrasonic`, `workmap`, `movement`, `easylogger` |
| `platform/driver/` | Motors `a4963_snk_v2`, left/right/blade. MEMS `tdk42688`. Battery `snk_v1/v2`. Ports `driver_dpport_snk_v1`, `driver_bdport_snk_v1`, `driver_ledport_snk_v1`. RTC |

**Ports** [F]:

| Port | Peripheral | Base | Connects to |
|---|---|---|---|
| `dpport` | USART0 | `0x40013800` | ESP32 |
| `bdport` | USART1 | `0x40004400` | U16 |
| `ledport` | UART3 | `0x40004C00` | LED/ultrasonic board |

### Key functions [F]

| Address | Function |
|---|---|
| `08063808` | dpport key/action command decoder. Maps `0x10000001..0a`, `0x10000011..15`, `0x10000021..24` to action bits |
| `08076244` | `set_action(bits)`: stores into the command-service context at `*0x200002c0 + 4` |
| `0804d17c` | `get_action()` |
| `0804ff68` | Command-service vtable (`set_action` at +0x18, `get_action` at +0x1c) |
| `0806ab90` | Idle (wait) state action handling |
| `0803a080` | Wait: manual start, "leave to cutting" |
| `08038f6c` | Wait: home/dock |
| `0803a2cc` | Wait: manual trim. "only in station", otherwise "trim command, but robot not in station, ignore" |
| `08078f3c` | `set_process_state(n)` [I: from call sites] |
| `0804867c` | EasyLogger `elog_output(level, tag, file, func, line, fmt, …)` [I] |
| `0804de88` / `0804deb4` | Button driver reads PE10 / PE11, active low |
| `0805303e` | `gpio_input_bit_get` (`0800ca9a` in the bootloader) |
| `080706a0` | dpport service config: receive timeout `0xbb8` (3000 ms), period `0x1f4` (500 ms) |
| `08044164` | dpport receive-overtime callback: link status 4, sends `0x20000004` to the ESP |
| `08072558` | Send `{"cmd":x}` on dpport |
| `080446e4` | dpport boot handshake: `0x40000009` every 100 ms ×25 until the ESP answers (ESP_INFO), then `0x40000008` every 20 ms ×50 until ESP_INIT sets link state 2; then `0x20000004` ×2. On timeout state 4 |
| `08046e00` | dpport receive task: 3 s queue timeout, counts unparseable frames at drv `+0x48` |
| `08046f94` | dpport receive callback: more than 10 bad frames in a row sets link state 6 ("receive display board message error overtime") |
| `08070f18` | Port check at manager start: dpport state 6 (or bdport/ledport 4/5/6) logs "communication failed" and cuts power |
| `08070d3c` | Power cut: clears PB12, PE9, PD11 (power latch) |
| `0804b7f0` / `0804b840` | FWDGT config / reload. `0805deba` = 16 s (÷256, 2500) during `rw_init`, `0805dea6` = 1.6 s (÷32, 2000) once the config service runs |
| `0805b974` | `rw_init` error loop: `0x20000002` every 2 s, blinks, **does not feed FWDGT**, so U13 resets after ~16 s |
| `08060a44` | Logs `rw_init` error bits: 0x1 ultrasonic, 0x2 MB version, **0x4 display board disconnect**, 0x8 border board disconnect, … |
| `080395a4` | `deal_safety`. dpport is the first checked object (getter `080509a0`); link lost sets error `0x400000` |
| `08068622` | `process_error` power-off counter `[ctx+0x24]` vs limit `[ctx+0x28]` (`0xbb80`, set at `08068756`). State `0xa` = power off. About 20 min [I] |
| `080726fc` | Sends `0x20000002` `{"error":bits}`: driver init error, from `rw_init` (`0805b8a4`, `0805b970`) every 2 s |

**Action bits** (`set_action`) [F]:

| Bit | Keys | Remote | Meaning |
|---|---|---|---|
| 0x01 | `0x1000000a` | `0x10000015` | Edge trim |
| 0x02 | `0x10000001` | `0x10000011`, `0x10000021` | Start mowing |
| 0x04 | `0x10000002` | `0x10000012`, `0x10000022` | Return home |
| 0x08 | `0x10000003` | `0x10000013`, `0x10000023` | Stop/pause [I] |
| 0x10 | `0x10000004` | `0x10000014`, `0x10000024` | **Power off** ("Robot manual power off", `set_process_state(0xa)` in every process) |
| 0x20 | `0x10000006` | — | Unknown |
| 0x40 | `0x10000007` | — | Select (START/HOME pressed) |
| 0x80 | `0x10000008` | — | Clear user setting [I: matches the ESP log] |

The three groups bump separate counters at +0x08, +0x0c and +0x10 of the same context [I: statistics per source].

**Display link and watchdogs** [F]:
- At U13 boot the ESP must answer each `0x40000009` with ESP_INFO and each `0x40000008` with ESP_INIT. If it does not, `rw_init` records "display board disconnect" and loops on `0x20000002` without feeding FWDGT, so U13 resets after ~16 s and the mower loses power [I: the step from handshake timeout to init error bit 0x4 is not traced].
- Running: U13 expects a frame at least every 3 s. Otherwise it sends `0x20000004`, raises `0x400000` (display_error) and powers off after ~20 min in error (48000 ticks of 25 ms; 25 ms from the 2400-ticks-per-minute counter at `0802772e`). It recovers when frames return ("recover dpport").
- More than 10 unparseable frames in a row set link state 6. If that is the state when the process manager starts, `08070f18` cuts power.
Details are in the investigation report §10.

**Error codes** are a bitmask. They match the Sunseeker OLD list: 1 updown, 2 trapped, 4 lift, 16 no border, 32 out of area, 64 sensor timeout, …

---

## U16 GD32F303 border board MCU (`u16/firmware/u16_flash.bin`)

**Layout** [F]:
- **Bootloader** runs from `0x08000000` to `0x0800ffff`. Reset is `0x080001b5`.
- **App** vector table is at `0x08010000`. SP is `0x2000bff8`, reset is `0x0801a5f1`. Flash is used up to `0x08040000`.

**Sources:** `process_comm.c`, `process_deal_board.c`, `rw_bdboard_init.c`, `driver_bdsensor.c`, `driver_mboard_port_snk_v2.c`, plus EasyLogger and FreeRTOS.

**One JSON port:** `mport`, USART2 `0x40004800`, to U13 `bdport`. It has nothing for buttons, the display or the ESP32. It reports border-wire signal, lift/hall sensors and versions ("bdboard").

More detail is in [`u16/notes/U16.md`](u16/notes/U16.md). Its diagram was corrected on 2026-10-09.

---

## Open items

- Which of U13 PE10/PE11 is `ST` and which is `OK` (J8). The trigger for `0x10000003`/`0x10000004` (power off) in the ESP32 UI.
- The meaning of U13 process-state numbers passed to `08078f3c`. They are not the same as the `state` in `0x330000A0`.
- Remote commands `0x10000021/22/23/15` have not been seen on the wire yet.
