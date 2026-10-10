# Display ↔ mainboard protocol

The ESP32 on the display board talks to **U13** (GD32F305, main MCU) over one UART in the J8 ribbon cable. Everything the display board does — buttons, PIN, rain, status — goes through this link. U16 is not involved: it sits on a separate U13 port (see [firmware-u16.md](firmware-u16.md)).

Command IDs, directions and sequences below come from the original firmware of both chips, from logic-analyser captures of the original firmware ([captures/](../captures/README.md)) and from the ESPHome component running on the mower.

## Physical layer

| | |
|---|---|
| Wiring | ESP32 GPIO17 (TX) → J8 pin 4 `←` → U13 USART0 RX; U13 USART0 TX → J8 pin 3 `→` → ESP32 GPIO16 (RX) |
| Format | 230400 baud, 8N1, standard polarity |
| U13 side | `dpport` driver (`driver_dpport_snk_v1.c`), USART0 at `0x40013800` |

## Frame format

```
&{"cmd":536870913,"action":0}<CRC>#
^                             ^    ^
0x26 start                    |    0x23 end (single '#')
                              CRC-8/MAXIM over the JSON bytes only
```

- The payload is one JSON object. `cmd` is the command ID as a **decimal** integer.
- CRC: Dallas/Maxim CRC-8 (poly 0x31 reflected = 0x8C, init 0x00) over the bytes from `{` to `}` inclusive.
- Example: `{"cmd":536870913,"action":0}` → CRC `0xA0`.
- The CRC byte can be any value, including `{` (0x7B) or `#`. A receiver must start a frame at the two bytes `&{` and find its end by matching braces outside strings, not by searching for `{`.

```python
def crc8_maxim(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8C if crc & 1 else crc >> 1
    return crc
```

## Command ID classes

| Prefix | Direction | Contents |
|---|---|---|
| `0x10xxxxxx` | ESP → MB | Key and remote commands |
| `0x15xxxxxx` | MB → ESP | U13 log lines |
| `0x20xxxxxx` | MB → ESP | Power-on, link state, init errors |
| `0x22xxxxxx` | ESP → MB | Rain sensor |
| `0x30xxxxxx` | ESP → MB | Keepalive, poll, WiFi/BT status, settings |
| `0x31xxxxxx` | ESP → MB | Settings menu |
| `0x33xxxxxx` | MB → ESP | Status, device info, configuration, setting acks |
| `0x40xxxxxx` | both | Boot handshake (`0x40000001/4/6` ESP → MB, `0x40000008` and up MB → ESP) |
| `0x41xxxxxx` | MB → ESP | Lock, action notifications, errors, shutdown — **except** `0x41000005` with `pwd`, which is the PIN from the ESP |
| `0x50xxxxxx` | MB → ESP | Battery |

## Session

### Boot handshake

U13 starts the handshake when it powers on. Each request must be answered within a few milliseconds:

| U13 sends | ESP answers | U13 gives up after |
|---|---|---|
| `0x20000001 {"action":0}` (power on) | — | |
| `0x40000009` every 100 ms | `0x40000006 {"hv":60400,"sv":30202,"spw":0,"mac":"xx-xx-xx-xx-xx-xx"}` | 25 tries (~2.5 s) |
| `0x40000008` every 20 ms | `0x40000001 {"init":3}` | 50 tries (~1 s) |
| `0x20000004` ×2 (link up) | — | |

Without the answers U13 marks the display board as missing, sends `0x20000002 {"error":bits}` (bit `0x4` = display board) every 2 s, stops feeding its hardware watchdog and resets after ~16 s, and the mower switches off.

The ESP opens every session with `0x40000004` (ESP_BOOT), `0x30000005` (keepalive), `0x30000028 {"state":0}` and `0x22000000 {"rain":1}`, and sends `0x300000A1` (poll) every 100 ms until the handshake is done. **U13 answers `0x40000004` by replaying its boot sequence** (`0x20000001`, `0x20000004`, lock state and full status) even when it is already running, so an ESP that restarts on its own (e.g. after an OTA update) gets back in sync without power-cycling the mower.

For about 2 s after U13 powers on, the ESP's own frames come back on its RX line verbatim. Frames that only the ESP sends (`0x10…`, `0x22…`, `0x30…`, `0x40000001/4/6`, `0x41000005` with `pwd`) must be ignored on RX.

After the handshake U13 sends its configuration (`0x330000B0`, `0x330000A1`, `0x330000A2`, `0x330000A6`, …) and the first full status. The ESP queries `0x300000A6`, `0x300000A7` and `0x300000A8` **without fields** (with fields, `0x300000A6` overwrites the mowing schedule).

### Keeping the link

| Who | What | Interval |
|---|---|---|
| ESP | `0x30000005` keepalive | 500 ms |
| ESP | `0x30000021 {"wifi":0,"str":0}` and `0x30000022 {"bt":0,"str":0}` | 1 s |
| U13 | `0x40000011 {"rtc":<unix time>}` | 1 s |
| U13 | `0x33000021` / `0x33000022 {"result":true}` (acks of the WiFi/BT frames) | 1 s |

U13 expects a frame at least every 3 s. Otherwise it sends `0x20000004`, raises error `0x400000` and powers off after ~20 min; it recovers as soon as frames return. More than 10 unparseable frames in a row also drop the link. `0x300000A1` (poll) is for the boot phase only: once the link is up, U13 answers every poll with `0x330000A1` and `0x330000A2` again (one poll is a handy way to fetch the device info after an ESP-only restart).

### PIN

Right after the handshake U13 sends `0x41000002 {"lock":1}` and `state:1`. The ESP answers with the PIN:

```
MB → ESP  0x41000002 {"lock":1}
ESP → MB  0x41000005 {"pwd":1234}
MB → ESP  0x41000020 {"result":1}
MB → ESP  0x330000A0 {"state":2}
MB → ESP  0x41000003
MB → ESP  0x330000A0 {"state":6}
```

**Until U13 has accepted the PIN it silently ignores every key and remote command.** The unlock survives an ESP-only restart: U13 then replays the boot sequence without `lock:1`, and does not answer a PIN sent while unlocked. 10 wrong PINs block PIN entry for a while, after which it works again (tested; the manufacturer's FAQ says 10 minutes with the mower switched on). The PIN is stored in U13 (see [pin-recovery.md](pin-recovery.md)); the ESP only forwards it.

### Commands and responses

Keys are a two-step sequence, exactly as the original display firmware sends them: START or HOME sends `0x10000007`, then OK within 3 s sends `0x10000001` (after START) or `0x10000002` (after HOME).

| Action | ESP → MB | MB → ESP | Time |
|---|---|---|---|
| Start mowing | `0x10000007`, then `0x10000001` | `0x41000005` (departure), `state:8` | ~45 ms |
| Return to station | `0x10000007`, then `0x10000002` | `0x41000006`, `state:9` | ~40 ms |
| Stop | `0x10000023` | `0x41000003`, `state:6` | ~100 ms |
| Edge trim (only from the station) | `0x10000015` | `0x41000013`, `state:16`, then `{"station":false}` | ~160 ms |
| Physical STOP button (handled by U13) | — | `{"stop_state":1}`, `0x41000003`, `state:6`, `{"stop_state":0}` | |
| Arrival at the station | — | `{"station":true}`, `{"border_state":0}`, ~2 s later `0x41000007`, `state:10` | |
| Power switch off | — | `0x41000008`, `{"state":11,...}` | |
| Error (lift, no wire, …) | — | `0x41000004 {"err":N}`, `{"state":7,"error":N}` | |

U13 decodes key and remote commands into action bits (`0x08063808`). Several IDs map to the same action:

| Action | Keys | Remote (cloud `mode`) |
|---|---|---|
| Edge trim | `0x1000000a` | `0x10000015` |
| Start mowing | `0x10000001` | `0x10000011`, `0x10000021` |
| Return to station | `0x10000002` | `0x10000012`, `0x10000022` |
| Stop / pause | `0x10000003` | `0x10000013`, `0x10000023` |
| **Power off** | `0x10000004` | `0x10000014`, `0x10000024` |
| Select (START/HOME pressed) | `0x10000007` | — |
| Clear user settings | `0x10000008` | — |

`0x10000004`, `0x10000014` and `0x10000024` switch the mower off.

## Status: `0x330000A0`

U13 sends the full status once after the handshake and partial updates (only the changed fields) afterwards.

| Field | Meaning |
|---|---|
| `state` | Mower state, see below |
| `error` | Error code, present with `state:7` |
| `bat_per` | Battery, % |
| `bat_lv` | Battery, 0–3 bars |
| `bat_health`, `bat_ctime`, `bat_dtime`, `bat_min_temp` | Battery health, charge/discharge counters, minimum temperature |
| `station` | On the charging station |
| `border_state` | Boundary wire signal present |
| `stop_state` | STOP button pressed |
| `rain_delay` | Rain delay, minutes |
| `work_area`, `cut_area`, `current_area` | m² |
| `total_minutes`, `on_minutes`, `cur_minutes` | Operating time counters |

| `state` | Meaning |
|---|---|
| 0 | Powered on, before the lock request |
| 1 | Waiting for the PIN |
| 2 | Transient, right after the PIN is accepted (always followed by `0x41000003` and `state:6`) — not mowing |
| 6 | Stopped / ready |
| 7 | Error (`error` field) |
| 8 | Mowing |
| 9 | Returning to the station |
| 10 | Charging |
| 11 | Shutting down |
| 16 | Edge trimming |

Error codes are a bitmask matching the Sunseeker "OLD" list: 1 up/down, 2 trapped, 4 lift, 16 no boundary signal (`E11` on the display), 32 out of area, 64 sensor timeout, …

## Command catalogue

### MB → ESP

| ID | Dec | Fields | Meaning |
|---|---|---|---|
| `0x15000001` | 352321537 | `log` | A U13 log line |
| `0x20000001` | 536870913 | `action` | Power on |
| `0x20000002` | 536870914 | `error` | Driver init failed (bit `0x4` = display board missing); every 2 s, then watchdog reset |
| `0x20000004` | 536870916 | — | Link up (×2 after the handshake), or "no frame from the ESP for 3 s" |
| `0x33000009`–`0x33000027` | | `result` | Acks of the setting commands (see Settings) |
| `0x33000021` | 855638049 | `result` | Ack of `0x30000021` (WiFi status) |
| `0x33000022` | 855638050 | `result` | Ack of `0x30000022` (BT status) |
| `0x33000023` | 855638051 | `result` | Ack of `0x30000023` (PIN reset) |
| `0x330000A0` | 855638176 | see above | Status |
| `0x330000A1` | 855638177 | `name`, `sn`, `version`, `model`, `avail`, `pwd_en`, `rain_en`, `sch_en`, `bat_name`, … | Device info |
| `0x330000A2` | 855638178 | `mb_hv`, `mb_sv`, `mblt_sv`, `bb_hv`, `bb_sv`, `db_hv`, `db_sv`, `lb_hv`, `lb_sv` | Board versions |
| `0x330000A6` | 855638182 | `trim`, `auto`, `sun_st`, `sun_len`, … `sat_len` | Schedule (start minute and length per weekday) |
| `0x330000A7` | 855638183 | `rain_en`, `rain_delay` | Rain configuration |
| `0x330000A8` | 855638184 | `mul_en`, `mul_auto`, `mul_z1..4`, `meter_en`, `meter_z1..4` | Multi-zone configuration |
| `0x330000AA` | 855638186 | — | Follows the schedule; meaning unknown |
| `0x330000AB` | 855638187 | `led_en`, `white_en`, `night_en`, `night_start`, `night_end` | Settings of the optional LED lamp board |
| `0x330000B0` | 855638192 | `map_sn`, `area` | Map / work area |
| `0x40000008` | 1073741832 | — | Boot handshake, wants `0x40000001` |
| `0x40000009` | 1073741833 | — | Boot handshake, wants `0x40000006` |
| `0x40000011` | 1073741841 | `rtc` | Clock, every second |
| `0x40000012` | 1073741842 | `hour`, `minute` | Start-time query (settings menu) |
| `0x40000013` | 1073741843 | `len` | Cutting-time query (settings menu) |
| `0x40000014` | 1073741844 | — | Sent at boot; meaning unknown |
| `0x40000020` | 1073741856 | `lv` | Light level; every ~0.5 s while mowing |
| `0x40000021` | 1073741857 | — | Sent at boot and every ~0.5 s while mowing; meaning unknown |
| `0x41000002` | 1090519042 | `lock` | Lock state; `1` = asks for the PIN |
| `0x41000003` | 1090519043 | — | Stopped (after stop, PIN accept, STOP button) |
| `0x41000004` | 1090519044 | `err` | Error |
| `0x41000005` | 1090519045 | — | Departure to mow (no `pwd` field) |
| `0x41000006` | 1090519046 | — | Returning to the station |
| `0x41000007` | 1090519047 | — | Docked, charging starts |
| `0x41000008` | 1090519048 | — | Shutting down |
| `0x41000013` | 1090519059 | — | Departure to trim the edge |
| `0x41000020` | 1090519072 | `result` | PIN result |
| `0x50000021` | 1342177313 | `bat` | Battery bars 0–3 |

### ESP → MB

| ID | Dec | Fields | Meaning |
|---|---|---|---|
| `0x10000001` | 268435457 | — | OK after START: start mowing |
| `0x10000002` | 268435458 | — | OK after HOME: return to station |
| `0x10000004` | 268435460 | — | **Power off** |
| `0x10000007` | 268435463 | — | START or HOME pressed |
| `0x10000008` | 268435464 | — | Clear user settings |
| `0x10000009` | 268435465 | — | IMU (MEMS) correction |
| `0x10000015` | 268435477 | — | Edge trim (only from the station) |
| `0x10000021` | 268435489 | — | Remote start mowing |
| `0x10000022` | 268435490 | — | Remote return to station |
| `0x10000023` | 268435491 | — | Remote stop |
| `0x22000000` | 570425344 | `rain` | Rain sensor: 1 dry, 2 raining. U13 only starts its rain delay on 2 |
| `0x30000005` | 805306373 | — | Keepalive |
| `0x30000021` | 805306401 | `wifi`, `str` | WiFi status |
| `0x30000022` | 805306402 | `bt`, `str` | Bluetooth status |
| `0x30000023` | 805306403 | — | **Reset the PIN to `0000`** (keeps all other settings) |
| `0x30000028` | 805306408 | `state` | Display state (0 at boot) |
| `0x300000A1` | 805306529 | — | Poll (boot phase) |
| `0x300000A6` | 805306534 | — / schedule fields | Query the schedule; with fields: **write** it |
| `0x300000A7` | 805306535 | — | Query the rain configuration |
| `0x300000A8` | 805306536 | — | Query the multi-zone configuration |
| `0x40000001` | 1073741825 | `init` | Boot handshake answer (`3`) |
| `0x40000004` | 1073741828 | — | ESP boot; makes U13 replay its boot sequence |
| `0x40000006` | 1073741830 | `hv`, `sv`, `spw`, `mac` | Boot handshake answer: display board versions (original: `hv` 60400, `sv` 30202) |
| `0x41000005` | 1090519045 | `pwd` | PIN |

### Settings (display menu)

The original display firmware edits settings with these commands; U13 acks each with `0x33xxxxxx` of the same number and `{"result":true}`.

| ESP → MB | Fields | Setting | Ack |
|---|---|---|---|
| `0x31000016` | — | Enter the settings menu | |
| `0x31000017` | — | Enter a submenu | |
| `0x30000006` | — | Setting mode | |
| `0x30000007` | — | Apply | |
| `0x30000009` | `old` | Old PIN (PIN change) | `0x33000009` |
| `0x30000010` | `pwd` | New PIN | `0x33000010` |
| `0x30000011` | `year` | Year | `0x33000011` |
| `0x30000012` | `month`, `day` | Date | `0x33000012` |
| `0x30000013` | `hour`, `minute` | Time | `0x33000013` |
| `0x30000014` | `hour`, `minute` | Daily start time | `0x33000014` |
| `0x30000015` | `hour` | Mowing hours per day | `0x33000015` |
| `0x30000017` | `rain_en`, `rain_delay` | Rain sensor | `0x33000017` |
| `0x30000027` | `day` | Days per week (3, 5, 7) | `0x33000027` |

## Other U13 links

| Port | U13 peripheral | Peer | Protocol |
|---|---|---|---|
| `bdport` | USART1 `0x40004400` | U16 `mport` (USART2) | JSON frames ≤ 128 bytes, see [firmware-u16.md](firmware-u16.md) |
| `ledport` | UART3 `0x40004C00` | Optional LED/ultrasonic board (not fitted on the Lux) | JSON |
| BMS | USART2 `0x40004800`, 19200 8N1 half duplex | Battery pack | Binary, see [battery.md](battery.md#bms-protocol) |
| UART OTA | bootloader, env `ota` = 1 or 5 | Firmware update over UART | `[len u16 LE][data][xor]`, commands GET OTA INFO, DOWNLOAD, SET OTA MODE, SET VER, RETURN, SET FIRMWARE NUMBER, SET BAUDRATE (230400 → 921600) |
