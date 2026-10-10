# Original firmware: ESP32 (display board)

The display board runs ESP-IDF 4.4.3 project `Display_esp32`, version **3.02.02** on our unit (3.02.05 on the MI 302 dump). This page is a map for reading the dump; addresses were checked with [`tools/re/esp32dis.py`](../tools/re/README.md).

Markers: **[F]** read from the firmware, **[C]** seen in a logic-analyser capture, **[I]** inferred, not verified.

## Dumps

| File | What |
|---|---|
| [`dumps/esp32/esp32_dump.bin`](../dumps/esp32/) | Full 4 MB flash, read over J1 with `esptool.py read_flash` at 921600 baud |
| `dumps/esp32/ota_0.bin` | Active app partition (otadata seq = 1). The file to analyse |
| `dumps/esp32/disasm.s` | Annotated listing of `ota_0.bin` (`esp32dis.py dump`) |
| [`dumps/mi302/esp32_dump_mi302_v3.02.05.bin`](../dumps/mi302/README.md) | Same board of another unit, firmware 3.02.05 |

### Partition table [F]

| Name | Offset | Size |
|---|---|---|
| nvs | `0x9000` | `0x4000` |
| otadata | `0xd000` | `0x2000` |
| phy_init | `0xf000` | `0x1000` |
| ota_0 | `0x10000` | `0x170000` |
| ota_1 | `0x180000` | `0x170000` |
| coredump | `0x2f0000` | `0x10000` |
| panic_out | `0x300000` | `0x1000` |

### App image [F]

Entry point `0x400814ec`. Segment data starts at header offset + 8:

| vaddr | len | file offset | Kind |
|---|---|---|---|
| `3f400020` | `0x262b4` | `0x20` | DROM (strings, constant tables) |
| `3ffbdb60` | `0x6cd4` | `0x262dc` | DRAM init data |
| `40080000` | `0x3060` | `0x2cfb8` | IRAM (vectors) |
| `400d0020` | `0xe4004` | `0x30020` | IROM (app code) |
| `40083060` | `0x191f8` | `0x11402c` | IRAM |
| `50000000` | `0x10` | `0x12d22c` | RTC |

Reading tips:
- Literals sit in pools at `0x400d0xxx`–`0x400d2xxx` and are loaded with `l32r`.
- JSON numbers are built with cJSON, so command IDs appear as **double** constants in two `l32r` (low word in a10, high in a11). `0x41c80000:0x04800000` is `0x30000009`.
- Some command tables are `int`s converted with `__floatsidf` (`0x4000c938`).

## Modules

Source files named in strings: `rw_display.c` (UI), `driver_mboard_port.c` (UART to U13), `driver_rf.c` (CMT2300A sub-GHz radio, not fitted). Threads: "tube service", "data manager service", "MachineStatus", "iot drive", "iot recv thread", "bt thread", "log thread", "update thread".

## Key functions [F]

| Address | Function |
|---|---|
| `400d77d8` | BSP init: display, buzzer, buttons, rain. Loads `iot_mode` |
| `400daf7c` / `400daef8` | Button init / poll: GPIO22 = bit0 START, GPIO21 = bit1 HOME, GPIO19 = bit2 OK; pull-up, active low, 7-sample debounce |
| `400e27f4` | UI main loop: 10 ms tick, edge / long-press / combo logic |
| `400e2194` | UI event dispatcher: sends the `0x1000000x` key commands |
| `400e1a58` | `send_cmd(cmd)`: `{"cmd":cmd}` to U13 |
| `400e0bc4` | Factory test (`ft-key-`) |
| `400e6da0` | `TubeInit`: SPI display MOSI 25, SCLK 33, CS 32, 400 kHz. Sets GPIO26 high, writes a blank frame, sets GPIO26 low; starts the "tube LP timer" |
| `400dfe9c` | Display start: `TubeInit`, then the "tube scan" task |
| `400dfe50` | "tube scan" task (priority 25, core 1): sends frame-buffer slots 0–3 (`0x3ffc5ab8`) with `vTaskDelay(2)` after each, i.e. 2 ms per digit. Creates the 700 ms "tube flash timer" (`400e03a8`) that blinks the colon |
| `400e6d44` | Display write: 16-bit word, high byte first. Digit select bits 13/12/11/10, colon bit 8, segments bits 0–7 |
| `400e6d7c` / `400e6d34` | "tube LP timer": GPIO2 high and restart the 15 s one-shot / GPIO2 low when it expires; restarted when OK is pressed |
| `400d9d20` | Parser of U13 `0x330000A0` (status) into the state struct |
| `400db4c4` | Builds the cloud status report (`mode`, `power`, `errortype`, `station`, …) |
| `400dc4a8` | IoT command task: cloud `cmd` → UART commands (table below) |
| `400dc890` | IoT replies and `command`/`passwd`/`rename`/`ota` handling |
| `400df064` | Rain driver init: GPIO18/GPIO5 outputs, ADC1 channel 0 (GPIO36) at 11 dB, 12 bit; starts the rain thread |
| `400defb8` | Rain thread: 1 s per polarity (`400def40`); 5 samples at the end of polarity 1 into a running average (`acc += raw − acc/4`); `acc > 11999` (average > 3000) is dry; 16 in a row change the state (1 dry, 2 raining). Average kept at `0x3ffc5aee` |
| `400e2774` | Sends `0x22000000 {"rain":state}` when the rain state changes |
| `4012ce9c` / `4012ce0c` / `4012cdd0` | `gpio_set_direction` / `gpio_set_pull_mode` / `gpio_get_level` |
| `4012be00` / `4012be24` / `4012be8c` / `4012be6c` / `4012bf34` | cJSON GetObjectItem / AddItemToObject / CreateNumber / CreateBool / CreateObject [I] |

## UI

| Input | Action |
|---|---|
| START, then OK within 3 s | Start mowing (`0x10000007`, `0x10000001`) |
| HOME, then OK within 3 s | Return to station (`0x10000007`, `0x10000002`) |
| START, long press | Date and time |
| START + OK, 3 s | Daily start time |
| OK, 3 s | Mowing hours per day |
| HOME + OK, 3 s | Days per week |
| HOME, 3 s | Rain sensor on/off and delay |
| START + HOME, 3 s | Change the PIN |

(Long presses from the user manual and capture `06-settings-all`.)

Display texts include `IdLE`, `LoCK`, `Mow`, `HoME`, `ChAr`, `Err` with a code, and the PIN entry digits.

## State struct `0x3ffbf460` [F]

Filled by `400d9d20` from U13 `0x330000A0`:

| Offset | Content |
|---|---|
| +0x00 | Display state (U13 `state` 1–5 copied, others remapped; 4 = error) |
| +0x34 | `error` |
| +0x38 / +0x39 | `bat_per` / `bat_lv` |
| +0x3d | `station` |
| +0x58..+0x63 | Feature flags `pwd_en`, `rain_en`, `sch_en`, `zone_en`, `com_en`, `sp_en`, `led_en`, `ult_en`, `gps_en`, `info_state`, `map_en` |
| +0x60 / +0x64 | `bat_name` / `bat_id` |
| +0x68 / +0x6c | `bat_ctime` / `bat_dtime` |
| +0x70 | `bat_health` |
| +0x74..+0x84 | `total_minutes`, `on_minutes`, `cut_area`, `cur_minutes`, `current_area` |
| +0x104..+0x10c | `led_en`, `white_en`, `night_en`, `night_start`, `night_end` from U13 `0x330000AB` (optional LED lamp board on U13's `ledport`; relayed to the cloud only) |
| +0x124 | `rain_delay` |
| +0x168..+0x18c | Board versions `mb_sv/hv`, `bb_sv/hv`, `db_sv/hv`, `lb_sv/hv` |

## WiFi, Bluetooth and cloud

The hardware and firmware support WiFi, Bluetooth and a cloud client, used by the Sunseeker app on Brucke/Sunseeker units. Lux does not advertise it; pairing a Lux unit with the app has not been tried.

### NVS

| Key | Value on our unit | Meaning |
|---|---|---|
| `robot_ssid` | `cy-public` | WiFi network |
| `robot_password`, `wifi_passwd` | `88888888` | WiFi / app password (not the mower PIN) |
| `robot_name` | `MyMower` | Device name |
| `robot_sn` | `2312CGF250600035167` | Serial number |
| `iot_mqtt_uri` | `mqtt://server.sk-robot.com` | MQTT broker |
| `pdt_ver`, `model` | | Product version and model |

### MQTT [F]

- Broker `mqtt://server.sk-robot.com`, plain MQTT on port 1883, no TLS. The image also contains `mqtt://test1.sk-robot.com` … `test4` and a `mqtt://%s` template.
- Topics: `/%s/%s/get` (commands to the mower) and `/%s/%s/update` (reports), seen in the wild as `/device/<id>/get|update`.
- Client username and password: [I] `robot_sn` / `robot_password`, not traced.

### Cloud commands

The IoT task `400dc4a8` reads `cmd` from the JSON message. Rows marked [F] were traced; the others come from [OlliKantola/Sunseeker_LawnMower_Control](https://github.com/OlliKantola/Sunseeker_LawnMower_Control).

| `cmd` | Payload | Action | UART to U13 |
|---|---|---|---|
| 101 | `mode` 0/1/2/3/4 | stop / mow / home / select / edge trim [F, table `0x3f4046b4`] | `0x10000023` / `0x10000021` / `0x10000022` / `0x10000007` / `0x10000015` [F] |
| 102 | `ymd`, `hms`, `week` | Set the clock [F] | |
| 103 | `slice`, `trimming`, `auto`, `pause` | Schedule | |
| 105 | `rain_en`, `rain_delay_set` | Rain delay [F] | |
| 107 | `rename` | Device name | |
| 108 | `mul_*` | Start points / zones | |
| 109 | — | Restart communications | |
| 111 | `type`, `ver` | Firmware info [F] | |
| 112 | — | **Reset the PIN to `0000`** [F, `400dcfbc`] | `0x30000023` |
| 113 | — | Clear user settings [F, `400dcfd8`] | `0x10000008` |
| 115 | `led_*`, `white_en`, `night_*` | LED lamp board [F] | |
| 200–210 | — | Queries, replies 500–516 (`208` → `508`: versions and serial number) | |

When the display is in its error state, every `mode` command becomes `0x10000007`. Handlers for `passwd_old`/`passwd_new`, `ssid`/`passwd`, `mul_*` and `ult_*` exist; their `cmd` numbers were not mapped.
