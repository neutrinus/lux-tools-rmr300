# Original firmware: U13 (GD32F305, main MCU)

U13 runs the mower: state machine, motors, navigation, sensors, battery, PIN and settings. On our unit the product version is 31018 (model `RMC300E20V-ECDNSS`), the app `mb_sv` 31315 and the bootloader `mblt_sv` 50517. Addresses were checked with [`tools/re/gd32dis.py`](../tools/re/README.md).

Markers: **[F]** read from the firmware, **[C]** seen in a capture or on the mower, **[I]** inferred, not verified.

## Dumps

| File | What |
|---|---|
| [`dumps/u13/u13_flash.bin`](../dumps/u13/) | 512 KB flash, mapped at `0x08000000` |
| `dumps/u13/u13_flash_1mb.bin` | 1 MB read. **20 bytes at `0x080127a4–0x080127b7` (bootloader) are corrupt** (the bootloader CRC does not match); use `u13_flash.bin` below `0x08080000` |
| `dumps/u13/ram_full.bin`, `ram_low.bin` | RAM snapshots of the running app |
| `dumps/u13/ghidra/` | Ghidra exports: function and string lists, selected decompiled functions, setup notes |
| [`dumps/mi302/u13_flash_mi302.bin`](../dumps/mi302/README.md) | Another unit, another version: same structure, shifted addresses |

## Layout [F]

- **Bootloader** `0x08000000`–`0x08017fff` (`boot.c`, `key.c`, `programBB.c`, `programLB.c`), reset `0x08011a25`. Handles USB, UART OTA and flashing of the other boards ([usb.md](usb.md)).
- **App** vector table at `0x08018000`, SP `0x20017ff8`, reset `0x08018441`. FreeRTOS, EasyLogger, EasyFlash, cJSON.
- **Image CRCs**: hardware CRC32 (poly `0x04C11DB7`, init `0xFFFFFFFF`, 32-bit LE words, no reflection, no final XOR). Bootloader over `0x08000000–0x08017FFB`, stored at `0x08017FFC`, checked by the bootloader. App over `0x08018000–0x080FFFFB`, stored at `0x080FFFFC` (`0x94db942e` in our dump), checked by the app at start-up. Any patch needs a new CRC.
- Literals are PC-relative `ldr`. Log strings are referenced with `adr` from the code right before them, not through a literal pool.

Source tree (paths in strings):

| Area | Files |
|---|---|
| `app/process/` | State machine: `process_wait`, `process_cutting`, `process_departure_smooth`, `process_docking_smooth`, `process_charging`, `process_error`, `process_find_bd`, `process_power_off`, `process_security`, `process_manager`, `process_control` |
| `framework/service_*` | `command`, `dpport`, `bdport`, `ledport`, `border`, `blade`, `bms`, `lift`, `hit`, `slope`, `slip`, `rain`, `multizone`, `user_setting`, `display`, `power`, `stop`, `time`, `ultrasonic`, `workmap`, `movement`, `easylogger` |
| `platform/driver/` | Motors `a4963_snk_v2`; IMU `tdk42688`; battery `snk_v1/v2`; ports `driver_dpport_snk_v1`, `driver_bdport_snk_v1`, `driver_ledport_snk_v1`; RTC |

## Ports [F]

| Port | Peripheral | Base | Peer |
|---|---|---|---|
| `dpport` | USART0 (PA9/PA10) | `0x40013800` | ESP32 on the display board, 230400 ([protocol.md](protocol.md)) |
| `bdport` | USART1 | `0x40004400` | U16 ([firmware-u16.md](firmware-u16.md)) |
| `ledport` | UART3 (PC10/PC11) | `0x40004C00` | Optional LED/ultrasonic board (`lboard_en`) |
| BMS | USART2 (PD8/PD9) | `0x40004800` | Battery pack, 19200 half duplex ([battery.md](battery.md)) |
| USBFS | PA11/PA12 | | J6 socket, bootloader only |

## Key functions (app) [F]

| Address | Function |
|---|---|
| `08044860` | dpport dispatcher for `0x3000xxxx` (`tbh` table at `0804489a`, index = cmd − `0x30000005`) |
| `080446e4` | dpport boot handshake: `0x40000009` every 100 ms ×25 until ESP_INFO, then `0x40000008` every 20 ms ×50 until ESP_INIT; then `0x20000004` ×2 |
| `08046e00` | dpport receive task: 3 s queue timeout, counts bad frames |
| `08046f94` | dpport receive callback: more than 10 bad frames in a row set link state 6 |
| `08044164` | dpport receive timeout: sends `0x20000004` |
| `080706a0` | dpport service config: receive timeout 3000 ms, period 500 ms |
| `08072558` | Send `{"cmd":x}` on dpport |
| `0804650a` | `0x30000023`: sets env `pwd` = 0, clears the wrong-PIN counter, re-enables PIN entry, replies `0x33000023` |
| `08063808` | Key / remote command decoder → action bits (table in [protocol.md](protocol.md#commands-and-responses)) |
| `08076244` | `set_action(bits)`: one byte at `*0x200002c0 + 4` (the last command overwrites the previous one) |
| `0806ab90` | Wait state: action handling. Skipped entirely while a lock/busy check (`[[ctx]+4]->[+0xc]()`) is non-zero. [I] This is the PIN lock: on the mower no command is acted on before the PIN is accepted [C], but the check function itself was not traced |
| `0803a080` | Wait: manual start, "leave to cutting" |
| `08038f6c` | Wait: return to station |
| `0803a2cc` | Wait: edge trim, only in the station ("trim command, but robot not in station, ignore") |
| `08078f3c` | `set_process_state(n)` [I] (internal process numbers, not the `state` reported to the ESP) |
| `0804867c` | EasyLogger output [I] |
| `0804de88` / `0804deb4` | Button driver: PE10 / PE11, active low |
| `08039198` | `service_rain` tick: rain value from the ESP at `+8`; **2 starts the rain delay**, 1 lets it run down; needs `rain_en` |
| `080213c0` | BMS driver init: USART2, PD8/PD9, 19200 |
| `0807ce6a` | IMU WHO_AM_I check (`0x47` or `0x6F`) on I²C `0x68` (driver `08053930`) |
| `080395a4` | `deal_safety`: dpport link lost raises error `0x400000` |
| `08068622` | `process_error` power-off counter (limit 48000 ticks of 25 ms ≈ 20 min) |
| `0805b974` | `rw_init` error loop: `0x20000002` every 2 s without feeding the watchdog (reset after ~16 s) |
| `08060a44` | `rw_init` error bits: 0x1 ultrasonic, 0x2 MB version, 0x4 display board, 0x8 border board |
| `0804b7f0` / `0804b840` | FWDGT config / reload: 16 s during init, 1.6 s afterwards |
| `08070f18` | Port check at manager start: dpport state 6 (or bdport/ledport 4/5/6) logs "communication failed" and cuts power |
| `08070d3c` | Power-off step 1: motor PWM to 0, clears PB12 (motor enable), PE9, PD11 |
| `0807e72c` | Power-off step 2: clears PE7, then holds **PE12** (main latch) low with a watchdog feed until the rail collapses |

## Watchdogs and power-off [F]

- **No handshake at boot**: `rw_init` flags the display board as missing, loops on `0x20000002` without feeding FWDGT, U13 resets after ~16 s and the mower switches off. [I] The reset alone does not drop the latch (the bootloader raises PE12 again at `08000f38`); what turns the mower off after it is not traced.
- **Link timeout while running**: no frame from the ESP for 3 s → `0x20000004`, error `0x400000`, power-off after ~20 min in error; recovers when frames return.
- **Bad frames**: more than 10 unparseable frames in a row set dpport link state 6; if that is the state when the process manager starts, power is cut.

## Env (EasyFlash on the SPI NOR) [F]

The bootloader and the app share one EasyFlash env on the external W25Q64. It holds:

- `pwd` (PIN, uint32), `usr_pwd_en`, `user_name`, `language`, schedule, statistics, `run_param` (wrong-PIN counter, PIN input enable, drive parameters)
- product config: `sn`, `pdt_ver`, `type`, `type_name`, `type_param`, `region`, feature flags (`pwd_en`, `rain_en`, `sch_en`, `zone_en`, `led_en`, `ult_en`, `gps_en`, `lift_en`, `border_en`, …), `mb_hv`
- firmware bookkeeping: `MB_VER`/`mb_sv`/`MB_BVER`/`MB_SIZE`/`MB_BRF` and the same for `BB`, `DB`, `LB`, `BTL`; `ota`, `ota_date`, `cfg_rst`, `cfgstr`, `cfgupdate`, `lboard_en`
- the event log and the firmware staging area

The app caches its keys in RAM. Definition table at `0x08085458` (18 entries × 12 bytes: key pointer, RAM buffer, size):

| Key | RAM | Size |
|---|---|---|
| `cfg_ver` | `0x20000168` | 4 |
| `mb_hv` | `0x2000017c` | 4 |
| `mb_sv` | `0x20000180` | 4 |
| `pdt_ver` | `0x20000178` | 4 |
| `sn` | `0x200001b0` | 21 |
| `type_param` | `0x20000188` | 40 |
| `rb_en_mag` | `0x200001f0` | 16 |
| `lboard_en` | `0x20000208` | 1 |
| `shape_param` | `0x200001c8` | 40 |
| `user_name` | `0x20000280` | 32 |
| `language` | `0x20000279` | 1 |
| **`pwd`** | **`0x2000027C`** | 4 |
| `usr_pwd_en` | `0x20000278` | 1 |
| `run_param` | `0x20000220` | 80 |
| `ota` | `0x20000270` | 4 |
| `ota_date` | `0x20000274` | 4 |
| `exhibition_cfg` | `0x200002a0` | 24 |
| `test_add` | `0x200002b8` | 4 |

`pwd` is loaded with the env getter at `08060858` and saved at `0807c96e`. A lookup index at `0x200007a0` (32 × 8 bytes) is keyed by a CRC hash (`0x47a20`, table at `0x08085E70`). Nothing in the env is encrypted. Ways to read or reset the PIN: [pin-recovery.md](pin-recovery.md).

## PIN logic (strings, `service_user_set.c`)

| Event | Log string |
|---|---|
| Check | `compare pwd correct` / `compare pwd uncorrect=%d` |
| Lockout | `compare pwd uncorrect=%d overtimes, reset and lock` |
| Change | `set pwd success` / `set pwd old failed, because input old password error` |
| Reset (`0x30000023`) | `reset pwd success` / `reset pwd failed` |

## Error codes

A bitmask, matching the Sunseeker "OLD" list: 1 up/down, 2 trapped, 4 lift, 16 no boundary signal (`E11`), 32 out of area, 64 sensor timeout, …; 131072 charging time-out.

## Firmware limits (community reports, not traced)

Charging stops after 4 h; charge current is halved above ~33 °C; without station power the mower switches off after 20 min; returning to the station gives up after ~30 min; edge trimming lasts at most 30 min; it returns to charge at ~14 %; the default rain delay is 180 min; blade 2800 rpm (turbo 3200 rpm), alternating direction.

## Open items

- Which of PE10/PE11 is `Start` and which `OK` on J8.
- The meaning of the process-state numbers passed to `08078f3c`.
- `0x10000003`/`0x10000006` triggers in the display UI; `0x40000014` and `0x40000021`.
