# Mainboard (MB) Reverse Engineering Documentation — RMR300 Lawn Mower

## Document History

| Date | Author | Description |
|------|--------|-------------|
| 2026-06-22 | Marek | Comprehensive reverse-engineering documentation |
| 2026-06-22 | Marek | Watchdog experiment results (logs 12-22) |

---

## 1. Hardware Configuration

### Mainboard (MB)
- **MCU (U16)**: GD32F303CGT6 (ARM Cortex-M4F) — communicates with ESP32
- **MCU (U13)**: GD32F305AGT6 (ARM Cortex-M4F) — motor control, KV-store, PIN
- U16 acts as a JSON bridge between ESP32 and U13 running FreeRTOS

### ESP32-WROOM-32UE (Display Board)
- **Physical pins**:
  - `GPIO17` — TX (UART to MB)
  - `GPIO16` — RX (UART from MB)
  - `GPIO33` — CLK (SPI, 7-segment display)
  - `GPIO25` — MOSI (SPI, 7-segment display)
  - `GPIO32` — CS (SPI, display / 74HC595 latch)
  - `GPIO27` — Buzzer (digital PWM)
  - `GPIO36` — Rain sensor (GPIO36 = ADC1_CH0, digital input)
  - `GPIO19` — OK button (to U16, active LOW)
- **UART**: 230400 8N1, standard polarity
- **Built-in light sensor**: on ADC
- **Wi-Fi/BT sensor pad**: unpopulated (pins assigned to UART)

### Display
- 4-digit 7-segment LED (clock-style)
- Driven by 3× 74HC595 (shift registers)
- Fake SPI: 24-bit frames (`b0 + b1 + seg`)
- Refresh: hardware timer 8ms (125Hz)
- `CS` acts as `OE` (Output Enable) — held HIGH, lowered during transmission
- Segment mapping: standard 7-segment (bits: g, f, e, d, c, b, a, DP)
- Digit addressing: `b1 = {0x20, 0x10, 0x08, 0x04}` (for digits 0..3 from left)
- Colon — location not found (TODO)

### Rain Sensor
- Connected to GPIO36, active LOW (0 = rain, 1 = no rain)
- Original firmware sends `{"cmd":570425344,"rain":0/1}` on change

### Buzzer
- GPIO27, digital HIGH = on
- Used on errors (300ms) and mowing start (100ms)
- Original firmware also triggers on PIN accepted (5 cycles)

---

## 2. Protocol Discoveries

### Frame Format

```
&{json}<CRC>#
```

- Prefix: `0x26` (`&`)
- JSON with `cmd` field (32-bit integer)
- CRC: Dallas/Maxim CRC-8 (poly 0x31, init 0x00, ref_in=true, ref_out=true) — computed only over JSON bytes (excluding `&` and `#`)
- Terminator: `0x23` (`#` — **single**, not `##`)
- Limit: max 128 bytes per frame (U16 mport driver limitation)

> **Correction 2026-06-22**: Previous version documented terminator `##` (two `0x23`).
> Capture decoding shows single `#` — e.g. `...7D A0 23 26...` = `}` CRC `#` `&`(next frame).

### All CMD Constants from Source Code

> **⚠️ CORRECTION 2026-06-22**: The tables below "ESP → MB" and "MB → ESP" have **incorrect direction assignments**
> for many commands. The tables were generated from source code that defines CMD constants
> independently of direction — many commands are handled by both sides (e.g. PIN_SEND is sent
> by ESP but PIN_RESULT is received from MB).
>
> **Authoritative direction table**: [PROTOCOLS.md](PROTOCOLS.md) — section "Complete Command Catalog".
> In short: prefixes `0x20/0x33/0x50` = MB→ESP, `0x10/0x22/0x30/0x31` = ESP→MB, `0x41` = MB→ESP (except 0x41000005 PIN_SEND = ESP→MB).

#### ESP → MB (sent by ESP32)

| Constant | HEX | DEC | Description |
|----------|-----|-----|-------------|
| `CMD_POWER_ON` | `0x20000001` | 536870913 | Power-on wake with `action:0` |
| `CMD_POWER_READY` | `0x20000004` | 536870916 | Ready for operation |
| `CMD_SETTING_MODE` | `0x30000006` | 805306374 | Settings mode |
| `CMD_SETTING_APPLY` | `0x30000007` | 805306375 | Apply settings |
| `CMD_ESP_STATE` | `0x30000028` | 805306408 | ESP state report |
| `CMD_ESP_WIFI` | `0x30000021` | 805306401 | Wi-Fi status with `wifi`, `str` fields |
| `CMD_ESP_BT` | `0x30000022` | 805306402 | BT status with `bt`, `str` fields |
| `CMD_ESP_POLL` | `0x300000A1` | 805306529 | Poll/heartbeat (continuous, ~30ms) |
| `CMD_ESP_TRIM` | `0x300000A6` | 805306534 | Mowing schedule |
| `CMD_ESP_RAIN_CFG` | `0x300000A7` | 805306535 | Rain configuration |
| `CMD_ESP_MULTIZONE` | `0x300000A8` | 805306536 | Multi-zone configuration |
| `CMD_SETTING_START` | `0x31000016` | 822083606 | Settings start |
| `CMD_SETTING_SUB` | `0x31000017` | 822083607 | Sub-setting |
| `CMD_PIN_RESULT` | `0x33000021` | 855638177 | PIN result (collides with DEVICE_INFO!) |
| `CMD_PIN_RESULT2` | `0x33000022` | 855638050 | PIN result (confirm) — in code: `0x33000022` (!= 0x33000021) |
| `CMD_STATUS` | `0x330000A0` | 855638176 | Mower status (bat, state, error, etc.) |
| `CMD_DEVICE_INFO` | `0x330000A1` | 855638177 | Device info (name, sn, model, version) |
| `CMD_HW_VERSIONS` | `0x330000A2` | 855638178 | Hardware versions (MB, BB, DB, MBLT) |
| `CMD_SCHEDULE` | `0x330000A6` | 855638182 | Schedule |
| `CMD_RAIN_CFG_RSP` | `0x330000A7` | 855638183 | Rain config response |
| `CMD_MULTIZONE_RSP` | `0x330000A8` | 855638184 | Multi-zone response |
| `CMD_MB_DEVICE_INFO` | `0x330000A9` | 855638185 | MB device info (sw/hv/sv) |
| `CMD_SCHEDULE_END` | `0x330000AA` | 855638186 | End of schedule block |
| `CMD_MAP_CFG` | `0x330000B0` | 855638192 | Map configuration (area, map_sn) |
| `CMD_ESP_BOOT` | `0x40000004` | 1073741828 | BOOT handshake |
| `CMD_ESP_INIT` | `0x40000001` | 1073741825 | INIT with `init:3` |
| `CMD_ESP_INFO` | `0x40000006` | 1073741830 | ESP HW/SW info (hv, sv, mac) |
| `CMD_BOOT_INIT` | `0x40000008` | 1073741832 | Init in progress |
| `CMD_BOOT_HEART` | `0x40000009` | 1073741833 | Boot heartbeat |
| `CMD_RTC` | `0x40000011` | 1073741841 | RTC time sync (co ~1s) |
| `CMD_START_TIME_Q` | `0x40000012` | 1073741842 | Start time query |
| `CMD_CUT_TIME_Q` | `0x40000013` | 1073741843 | Cut time query |
| `CMD_UNKNOWN_14` | `0x40000014` | 1073741844 | Unknown |
| `CMD_LIGHT` | `0x40000020` | 1073741856 | Light level |
| `CMD_BOOT_ACK` | `0x40000021` | 1073741857 | Boot ACK |
| `CMD_LOCK` | `0x41000002` | 1090519042 | Lock with `lock:0/1` |
| `CMD_EXEC_ACTION` | `0x41000003` | 1090519043 | Exec action (after STOP) |
| `CMD_ERROR_NOTIFY` | `0x41000004` | 1090519044 | Error notification |
| `CMD_PIN_SEND` | `0x41000005` | 1090519045 | Send PIN (with `pwd`) |
| `CMD_RETURN_HOME` | `0x41000006` | 1090519046 | ★ Return to dock |
| `CMD_SHUTDOWN` | `0x41000008` | 1090519048 | Shutdown |
| `CMD_START_ACK` | `0x41000020` | 1090519072 | START ACK for MB |
| `CMD_BATTERY` | `0x50000021` | 1342177313 | Battery level |

#### MB → ESP (received by ESP32)

| Constant | HEX | DEC | Description |
|----------|-----|-----|-------------|
| `CMD_POWER_ON` | `0x20000001` | 536870913 | Power-on (action:0) |
| `CMD_POWER_READY` | `0x20000004` | 536870916 | Power ready |
| `CMD_RAIN` | `0x22000000` | 570425344 | Rain sensor state (`rain:0/1`) |
| `CMD_ESP_KEEPALIVE` | `0x30000005` | 805306373 | Keepalive (continuous, ~1s) |
| `CMD_ESP_WIFI` | `0x30000021` | 805306401 | Wi-Fi status query |
| `CMD_ESP_BT` | `0x30000022` | 805306402 | BT status query |
| `CMD_ESP_STATE` | `0x30000028` | 805306408 | State notification |
| `CMD_ESP_POLL` | `0x300000A1` | 805306529 | Poll MB → ESP (echo) |
| `CMD_ESP_TRIM` | `0x300000A6` | 805306534 | Schedule query |
| `CMD_PIN_RESULT` | `0x33000021` | 855638177 | PIN result (with `result:true/false`) |
| `CMD_PIN_RESULT2` | `0x33000022` | 855638050 | Second PIN result |
| `CMD_STATUS` | `0x330000A0` | 855638176 | Mower status (periodic) |
| `CMD_DEVICE_INFO` | `0x330000A1` | 855638177 | Full device configuration |
| `CMD_HW_VERSIONS` | `0x330000A2` | 855638178 | Hardware versions |
| `CMD_SCHEDULE` | `0x330000A6` | 855638182 | Schedule |
| `CMD_RAIN_CFG_RSP` | `0x330000A7` | 855638183 | Rain configuration |
| `CMD_MULTIZONE_RSP` | `0x330000A8` | 855638184 | Multi-zone |
| `CMD_MAP_CFG` | `0x330000B0` | 855638192 | Map (area, map_sn) |
| `CMD_SCHEDULE_END` | `0x330000AA` | 855638186 | End of schedule |
| `CMD_ESP_BOOT` | `0x40000004` | 1073741828 | BOOT from MB |
| `CMD_ESP_INIT` | `0x40000001` | 1073741825 | INIT confirm |
| `CMD_ESP_INFO` | `0x40000006` | 1073741830 | ESP info query |
| `CMD_BOOT_INIT` | `0x40000008` | 1073741832 | Init in progress |
| `CMD_BOOT_HEART` | `0x40000009` | 1073741833 | Boot heartbeat |
| `CMD_RTC` | `0x40000011` | 1073741841 | RTC heartbeat |
| `CMD_LIGHT` | `0x40000020` | 1073741856 | Light level |
| `CMD_BOOT_ACK` | `0x40000021` | 1073741857 | Boot ACK |
| `CMD_LOCK` | `0x41000002` | 1090519042 | Lock state |
| `CMD_EXEC_ACTION` | `0x41000003` | 1090519043 | Exec action |
| `CMD_ERROR_NOTIFY` | `0x41000004` | 1090519044 | Error notify |
| `CMD_PIN_SEND` | `0x41000005` | 1090519045 | PIN send (from MB!) |
| `CMD_START_ACK` | `0x41000020` | 1090519072 | START ACK |
| `CMD_ERR_ACK1` | `0x10000001` | 268435457 | Error ACK |
| `CMD_ERR_ACK2` | `0x10000002` | 268435458 | Error ACK |
| `CMD_ERR_ACK7` | `0x10000007` | 268435463 | Error ACK (sent by ESP) |
| `CMD_SUPERVISION` | `0x20000002` | 536870914 | ★ Supervision — MB cuts power |
| `CMD_FRAME_ERROR` | `0x15000001` | 352321537 | Frame error (U16 reports bad CRC) |

### Command Catalog by Prefix (directions cross-verified)

| Prefix | Direction | Description |
|--------|-----------|-------------|
| `0x10xxxxxx` | ESP→MB | Error ACK |
| `0x15xxxxxx` | MB→ESP | Frame error (U16 reports bad CRC) |
| `0x20xxxxxx` | **MB→ESP** | Power/action notifications (power-on, ready) |
| `0x22xxxxxx` | **ESP→MB** | Rain sensor (sensor on display board J4→ESP32 GPIO36) |
| `0x30xxxxxx` | **ESP→MB** | Keepalive, WiFi/BT status, settings, poll |
| `0x31xxxxxx` | ESP→MB | Settings menu control |
| `0x33xxxxxx` | **MB→ESP** | Device info, status, versions, schedule, PIN results |
| `0x40xxxxxx` | Both | System: 0x40000001/04/06 = ESP→MB, 0x40000008+ = MB→ESP |
| `0x41xxxxxx` | **MB→ESP** | Lock, exec_action, error, shutdown, start_ack, home, docked — **except 0x41000005 (PIN_SEND = ESP→MB)** |
| `0x50xxxxxx` | **MB→ESP** | Battery |

> **Correction**: Previous version of this table had reversed directions for `0x20`, `0x22`, `0x30`, `0x33`, `0x41`, `0x50`.
> See PROTOCOLS.md for full documentation with cross-verification from captures and firmware.

### MB States (field `state` in `0x330000A0`)

| Value | Meaning |
|-------|---------|
| 0 | Idle (before PIN) |
| 1 | Ready (after PIN) |
| 2 | MOWING (or driving) |
| 6 | Stop / pause |
| 7 | Error |
| 9 | RETURNING TO DOCK |
| 10 | CHARGING |
| 11 | CHARGING (alternative) |

### Additional Status Fields

- `station` — docking station detected (bool)
- `border_state` — boundary wire (0/1)
- `stop_state` — STOP button (0/1)
- `rain_state` — rain (0/1)
- `bat_per` — battery level (0-100)
- `bat_lv` — battery level (0-3)
- `error` — error code
- `work_area`, `cut_area` — area
- `total_minutes`, `on_minutes` — times
- `bat_health` — battery health
- `bat_ctime`, `bat_dtime` — charging times
- `rain_delay` — rain delay

---

## 3. Boot Sequence Evolution

### Log 1 (`kosiarka-logs (1).txt`)

**Date**: 2026-06-21 16:33

**Tested**: First run with `snk_mower` component. ESP sends only `CMD_ESP_POLL` (0x300000A1) every ~30ms and `CMD_ESP_KEEPALIVE` (0x30000005) every ~1s. No boot sequence.

**Result**: ESP sends, but MB does not respond (silence). No MB reaction. Log contains 1583 TX lines.

**Conclusions**: A boot sequence is needed — POLL alone is not enough.

### Log 2 (`kosiarka-logs (2).txt`)

**Date**: 2026-06-22 ~09:52

**Tested**: Added boot sequence (BOOT → KEEPALIVE → STATE → RAIN → INIT). ESP waits 5s, then sends boot_seq.

**Result**: No clear MB responses in log.

### Log 5 (`kosiarka-logs (5).txt`)

**Date**: 2026-06-22 ~11:50

**Tested**: More iterations of boot sequence.

**Result**: Still no MB response.

### Log 6 (`kosiarka-logs (6).txt`)

**Date**: 2026-06-22 19:02

**Tested**: First successful communication! Sequence:
1. boot delay (200ms POLL)
2. Boot seq: BOOT → KEEPALIVE → STATE → RAIN=1 → INIT
3. Wait for DEVICE_INFO ~2s
4. DONE → send PIN
5. Received `RX: 0x300000A1` — MB starts sending POLL!
6. Received `Boot ACK` (0x40000021)
7. Received `Power ON (action=0)` (0x20000001)
8. Received DEVICE_INFO

**Key events** (timestamp):
```
19:02:57.079 Boot: sending BOOT + KEEPALIVE + STATE + RAIN
19:02:57.195 Boot sequence sent — waiting for DEVICE_INFO
19:02:57.203 Power ON (action=0)          ← MB responds!
19:02:58.841 RX: 0x300000A1               ← MB sends POLL
19:02:59.084 DEVICE_INFO not received — entering SYNC anyway
19:02:59.588 Boot DONE — switching to keepalive mode
19:02:59.591 Sending PIN to mainboard
19:03:01.166 Boot ACK                     ← MB confirms
19:03:01.240 Boot ACK
19:03:01.295 Device: MyMower (...)        ← DEVICE_INFO received!
```

**Problem**: Watchdog (ESPHome component watchdog) kills ESP. Loop `loop()` takes ~119ms because of `delay()` in `send_boot_sequence_next()` (sending all 7 messages at once in a single loop invocation). ESPHome limit is 50ms.

### Log 7 (`kosiarka-logs (7).txt`)

**Date**: 2026-06-22 19:07

**Changed**: Increased POLL interval from 30ms to 200ms during boot_delay to prevent watchdog kill.

**Result**: Watchdog does not kill (POLL every 200ms instead of 30ms = less TX), but MB shuts off after ~6s:
```
19:07:37.762 Boot sequence sent
19:07:41.796 Boot ACK
19:07:41.817 Boot ACK
19:07:41.963 Device: MyMower
19:07:43.991 Map: area=300
19:07:44.010 SUPERVISION: MB sent 0x20000002 — power may be cut soon
```

**Conclusions**: POLL every 200ms is too slow — MB has a supervision timer that requires responses every ~30-50ms. MB sends `0x20000002` (SUPERVISION) and cuts power.

### Log 8 (`kosiarka-logs (8).txt`)

**Date**: 2026-06-22 19:30

**Changed**: Added 10× POLL burst (every 30ms) after boot_seq so MB doesn't think ESP is dead.

**Result**: Still SUPERVISION. Determined that problem is not watchdog (MB power is cut, not ESP reboot):
```
19:31:00.069 Boot sequence sent
19:31:04.015 Boot ACK
19:31:04.038 Boot ACK
19:31:04.219 Device: MyMower
19:31:06.210 Map: area=300
19:31:06.225 SUPERVISION: MB sent 0x20000002
```

### Log 9 (`kosiarka-logs (9).txt`)

**Date**: 2026-06-22 19:48

**Changed**: Changed boot order — ESP waits for MB to initiate (per PROTOCOLS.md). ESP only sends KEEPALIVE and POLL, does not send boot_seq until MB starts.

**Result**: MB completely silent — sends nothing. After 15s boot_delay ESP sends boot_seq anyway, MB responds, then SUPERVISION again after ~3s.

### Log 10 (`kosiarka-logs (10).txt`)

**Date**: 2026-06-22 19:55

**Changed**: Reverted to ESP-initiated order (same as logs 6-8). ESP sends boot_seq immediately after boot_delay.

**Result**: **MB completely silent** — zero responses, zero RX. ESP sends POLL every 200ms throughout 30s boot_delay, then boot_seq — and nothing. Log has 2467 lines — all TX.

**Hypothesis**: Something broke in communication between programmable uploads — maybe ESP was being programmed while MB was powered on and reset, or UART registers got out of sync.

### Log 11 (`kosiarka-logs (11).txt`)

**Date**: 2026-06-22 20:06

**Changed**: Returned to ESP-initiated order. Blocked RAIN sending in boot_seq (removed) and changed `rain=1` to `rain=0`. Non-blocking boot sequence (one message per loop). DEVICE_INFO timeout increased to 10s.

**Result**: **MB responds!** Sequence:
1. 15s boot_delay (POLL every 200ms)
2. Boot seq sent (with `rain=1`) — but one message per loop iteration (non-blocking)
3. Warning: `component took 121ms, max is 50ms` — still timing issues despite non-blocking
4. DEVICE_INFO timeout (2s)
5. PIN sent (`pwd:9633`)
6. RX from MB: POLL echo (0x300000A1)
7. Boot ACK ×2
8. DEVICE_INFO received (MyMower, RMC300E20V-ECDNSS, S/N=..., v=31018, pwd_en=1)
9. HW versions: MB hv=22500 sv=31315, BB hv=230500 sv=50003, DB hv=60400 sv=30202, MBLT sv=50517
10. Battery bars: 3

**Log ends after ~10s normal operation** — only TX POLL. No SUPERVISION 0x20000002 in log 11! Log is truncated (449 lines).

**Observation**: Boot sequence still contains `rain:1` (line 180). Commits show it was later changed to `rain:0` (commit `13f98d4` — "Send rain=0 in boot sequence instead of rain=1").

**Problem 121ms**: Non-blocking boot sequence (one message per loop) still did `delay(5)` in `send_boot_sequence_next` for ESP_INFO (sends one, then flash write), or the blocking came from `send_pin()` with `delay()`.

### Current state (after log 11)

In latest code (after commits):
- Non-blocking boot sequence (one message per `loop()`)
- 30s boot_delay for OTA safety
- POLL every 200ms during boot_delay
- POLL every 30ms after transitioning to DONE
- DEVICE_INFO timeout: 10s
- `rain=0` in boot sequence (commit `13f98d4`)
- `compat_mode` — sends `wifi=0`, `str=0` (matching original firmware)
- Significantly reduced log spam

---

## 4. MB Power-off Mystery

### Symptoms
MB sends `0x20000002` (SUPERVISION) and cuts power ~3-6s after PIN send / DEVICE_INFO reception. This happens in all logs 6-9.

### Possible Causes

#### A. POLL too slow (most likely)
MB requires `CMD_ESP_POLL` (0x300000A1) every ~30-50ms. When interval is 200ms, the MB supervision timer triggers SUPERVISION:
- Log 6-7: POLL 200ms → ESP works (watchdog OK), but MB shuts down
- Log 8: added 30ms burst after boot_seq → still shuts down

**Counterargument**: In log 8/9, even after switching to 30ms POLL in DONE, MB shuts down. Maybe it's about the response latency — MB sends POLL, ESP responds with its own POLL. If ESP response is too late, MB thinks ESP is dead.

#### B. `rain=1` in boot sequence
In boot_seq_, ESP sends `{"cmd":570425344,"rain":1}`. This might be interpreted as "it's raining" → MB shuts down (battery saving in rain). In original firmware, rain is reported by the MB to ESP, not the other way.

**Commit `13f98d4`**: changed to `rain:0` — this may solve the problem.

#### C. PIN rejection
If PIN is incorrect, MB may shut down after 3-5 attempts. In logs:
- PIN sent (`{"cmd":1090519045,"pwd":9633}`)
- No `PIN_RESULT` (0x33000021) response in logs 6-8 — only in log 11 is DEVICE_INFO visible after PIN
- PIN might be checked asynchronously, and MB may shut down if PIN doesn't pass

**Counterargument**: In log 7, PIN_RESULT (0x33000021) does not appear but MB still shuts down. In log 11 MB does not shut down during logging.

#### D. No response to MB queries
MB sends queries (WiFi/BT status, ESP info) and ESP may not respond in time. In original firmware:
- `CMD_ESP_WIFI` and `CMD_ESP_BT` are sent by MB → ESP responds
- If ESP does not respond, MB may consider ESP dead

#### E. CMD_BATTERY
MB expects `CMD_BATTERY` (0x50000021) from ESP. The code sends it only at boot in boot_seq (in original firmware MB sends `bat:x`, and ESP responds). In the implementation, ESP does not send `CMD_BATTERY` during normal operation.

### Status: Unresolved

Current hypothesis: **rain=1** in boot sequence is the most suspicious. It was changed to `rain=0` in commit `13f98d8`. Further testing should verify whether this solves the problem.

---

## 5. Current Implementation Status

### Working
- ✅ Boot sequence: BOOT → KEEPALIVE → STATE(0) → RAIN(0) → WIFI → ESP_INFO → INIT
- ✅ DEVICE_INFO — full parsing (name, model, sn, version, bat_name, pwd_en)
- ✅ HW versions — parsing (mb_hv, mb_sv, bb_hv, bb_sv, db_hv, db_sv, mblt_sv)
- ✅ PIN sending (`pwd:9633`)
- ✅ POLL/KEEPALIVE in normal operation (30ms / 1s)
- ✅ RX parser — JSON handling with `{`..`}`, string state, CRC verification
- ✅ CRC8-Dallas — correct for `&{json}<CRC>##` frames
- ✅ Display — 4-digit 7-segment (SPI, 3× 74HC595, 2MHz, hardware timer)
- ✅ Display auto-off after idle timeout
- ✅ State display cycling: text ↔ battery every 5s
- ✅ Shutdown display ("byE ")
- ✅ Error display ("E" + code)
- ✅ Buzzer (GPIO27)
- ✅ Rain sensor (GPIO36) with `0x22000000` send every 60s
- ✅ Rain config, schedule, multizone, map_cfg parsing
- ✅ Compat mode (wifi=0, str=0)
- ✅ sensor/binary_sensor/text_sensor publishing to HA
- ✅ Boot delay (30s default, configurable)
- ✅ Non-blocking boot sequence (one message per loop)
- ✅ 10s timeout for DEVICE_INFO

### Not Working / Problematic
- ❌ **MB shuts down after boot_seq** (SUPERVISION 0x20000002) — likely caused by `rain=1`, to be verified after change to `rain=0`
- ❌ **Normal operation** — ESP cannot reach "stable communication" state long enough for MB to enter normal mode
- ❌ `start_mowing()` — sends TRIM schedule + error ACK + state=2, but MB ignores (no software command exists for physical start — START only through U16 button)
- ❌ `return_to_dock()` — sends CMD_RETURN_HOME, but mower stays in idle
- ❌ Colon on display — bit not found (TODO in `set_display_text`)
- ❌ ESP does not respond to `CMD_ESP_WIFI` / `CMD_ESP_BT` in time because it doesn't track MB queries

---

## 6. Key Technical Details

### CRC8-Dallas (MAXIM)

Polynomial: `0x31` (x⁸ + x⁵ + x⁴ + 1)

```python
python3 -c "import crcmod; c=crcmod.mkCrcFun(0x131); print(hex(c(b'{\"cmd\":1073741828}')))"
```

In code: 256-entry lookup table, `dallas_crc8(data, len)` function.

CRC is computed from JSON bytes (between `&` and `<CRC>`). Does NOT include prefix `&` or suffix `#`.

### JSON Framing

```
&{json}<CRC>#
```

- Prefix: `0x26` (`&`) — signals frame start
- JSON: arbitrary JSON with `cmd` field (uint32)
- CRC: 1 byte
- Terminator: `0x23` (`#`) — single `#`

RX parser:
- Searches for `{` to start buffering JSON
- Tracks `rx_in_string_` for proper handling of quotes in strings
- On `}` ends JSON and deserializes
- Does not verify CRC on RX (LA buffers show U16 already verified and rejected bad frames)

### Display

- 3× 74HC595 in cascade (24-bit shift register)
- SPI bit-banging → hardware SPI (ESP-IDF SPI2, 2MHz)
- Format: 24 bits per digit: `[b0: 8b] [b1: 8b] [segments: 8b]`
- Digit addressing (`b1`): `{0x20, 0x10, 0x08, 0x04}` (digits 0-3 from left)
- Segments: standard bit map for 7-segment (dp, g, f, e, d, c, b, a)
- `b0`: always `0x00` (possibly responsible for colon/DP on lower digits — TODO)
- CS (GPIO32) acts as OE — must be lowered during transmission, raised after
- Timer: hardware timer esp_timer, callback every 8ms (125 Hz refresh)
- Each digit lit for 2ms (8ms / 4 digits)
- Startup sequence: `8888` → `boot` → state

### BootPhase State Machine

```
PRE ──→ DONE
```

**PRE**:
1. **boot_delay** (if > 0): only POLL every 200ms + KEEPALIVE every 1s — waits for OTA
2. **boot_seq**: one message per loop (non-blocking, every 8ms):
   - 0: BOOT (0x40000004)
   - 1: KEEPALIVE (0x30000005)
   - 2: STATE (0x30000028, state=0)
   - 3: RAIN (0x22000000, rain=0)
   - 4: WIFI + BT (0x30000021 / 0x30000022)
   - 5: ESP_INFO (0x40000006, hv=60400, sv=30202, mac)
   - 6: INIT (0x40000001, init=3)
3. **wait for DEVICE_INFO**: POLL every 30ms, timeout 10s

**DONE**:
- PIN (if not sent and pwd_en)
- POLL every 30ms (MB supervision requirement)
- KEEPALIVE every 1s
- Rain read every 60s

### MB Supervision Requirements

- MB expects `CMD_ESP_POLL` (0x300000A1) every ~30-50ms
- If POLL does not arrive within ~50-100ms, MB sends `0x20000002` (SUPERVISION) and cuts power
- Similarly for KEEPALIVE (0x30000005) every ~1s
- MB also monitors responses to its queries (WiFi, BT, ESP_INFO)

### Boot Delay (30s)

- Configured to 30s in YAML (`boot_delay: 30`)
- During this time ESP sends only POLL and KEEPALIVE, not boot_seq
- Purpose is to enable OTA — flashing ESP while MB is powered on
- MB may shut down during boot_delay (no fast POLL), but this is acceptable risk

---

## 7. All Git Commits

### Phase 1: Reverse Engineering and Hardware Documentation

| Hash | Description |
|------|-------------|
| `3121e1f` | Complete hardware & firmware documentation for Lux Tools A-RMR-300-24 |
| `82bc517` | Add firmware dumps (U13 512KB, U16 256KB) |
| `ec38024` | Add ESP32 firmware dump (4 MB) and analysis |
| `bd3ec0a` | Add decompilation setup documentation and USB analysis script |
| `c71e95f` | Update docs: J8 inter-board pinout, button fork, UART on ribbon, not J1 |
| `93acdf6` | Update docs: J8 inter-board pinout, button fork, UART on ribbon, not J1 |
| `7295b07` | Add comprehensive firmware analysis results |
| `a45d29c` | Add ghidra-cli OSGi fix patch |
| `1333b36` | Fix PIN analysis: 4-digit PIN stored on EEPROM U22, not ESP32 |
| `aa94cfd` | Add PIN system analysis, FORMATFLASH.json dead code, security assessment |
| `7b91c7a` | EEPROM dumping via SWD: PendSV VTOR trick + FPU fix |
| `09f42df` | repo cleanup: organize files into subdirs, add PIN recovery README |
| `a56e3cb` | Translate remaining Polish documentation to English |
| `f9d0723` | Remove dcd059.pdf, move decompile_usb.py to tools/ |
| `ace14a0` | Fix FORMATFLASH.json (confirmed working via MBTL wildcard) |
| `022f666` | Add note about PCB protective coating on EEPROM |
| `c162784` | Add firmware and EEPROM dumps to repo |
| `a55f454` | ESP32 GPIO pin analysis: literal pool candidates, Xtensa decompilation workflow |
| `9a84118` | Add PROTOCOLS.md: inter-chip communication cross-validation |
| `3d7ceb0` | doc: add schedule configuration guide from manual (PL) |
| `781b69c` | Add PCB docking station photo |
| `95dd2e4` | Verified ESP32 GPIO map based on PCB trace analysis |
| `59f7072` | Xtensa disassembly: full objdump listing, GPIO pin mapping confirmed |
| `f9848db` | ESP32 GPIO analysis: confirmed MOSI=12 SCLK=10 MISO=NC |
| `e598d93` | ESPHome custom component + HA design + sniffing plan |
| `22e68f6` | ha.md: add schedule/data-availability section, final KV-store confirmation |

### Phase 2: First ESPHome Component Attempts

| Hash | Description |
|------|-------------|
| `171ed4e` | snk-mower.yaml: add wifi_signal and uptime sensors |
| `de34e6c` | switch to external_components, drop unused spi dependency |
| `d47426f` | remove text_sensor dependency (not available in ESPHome 2026.6.0) |
| `248e169` | fix for ESPHome 2026.6.0: drop designated init, use ESP-IDF GPIO directly |
| `be4bf2c` | feat: response timeout, buzzer, display auto-off |
| `ce7256d` | refactor: simplify component code |
| `a2640de` | Add UART sniffing captures and JSON protocol analysis |
| `a3b2826` | Rewrite component for JSON protocol at 230400 baud |
| `18864b5` | Expand protocol support based on decompilation and captures |
| `52c7649` | fix: add AUTO_LOAD for text_sensor dependency |
| `2c1042d` | fix: use ESPHome json component and ArduinoJson v7 API |
| `3e432d7` | fix: compilation errors - use ESPHome APIs and correct types |

### Phase 3: UART and Boot Sequence

| Hash | Description |
|------|-------------|
| `6f031cd` | minimal config: wifi + ota + api only |
| `ce590c4` | add uart (rx tx) without custom component |
| `96fd9eb` | full config with snk_mower, web_server, ota |
| `019e6ee` | step 2: add uart + snk_mower component with all sensors |
| `1fd2e4b` | add sram1_as_iram and minimum_chip_revision |
| `52e4e89` | add ESP_POLL (0x300000A1) sending every 100ms, raw RX byte logging |
| `a20f1ff` | uart: try rx=GPIO14 (decompilation candidate) |
| `5ab11d8` | uart: try tx=14,rx=15; display: clk=10,mosi=12,cs=15 (decompilation) |
| `4c21e0d` | display: set pins to -1 (NC) to avoid GPIO15 conflict with UART RX |
| `cc91517` | display: remove pin overrides (use defaults 18/23/5) |
| `c6e54df` | uart: try 115200 with tx=15,rx=13 (HARDWARE.md) |
| `a9b531a` | add pin_diag: true — scan 26 GPIOs every 20ms |
| `c9bfc7e` | fix: add sensor/binary_sensor to AUTO_LOAD |
| `24403a2` | fix: move BOOT/INIT/INFO/STATE sequence to setup() instead of loop() |
| `bce8f2f` | fix: send boot init=2 (not init=3), add 2s delay before POLL/KEEPALIVE |
| `6134038` | fix: match original ESP boot order: BOOT→KEEPALIVE→STATE, send INFO at 5s |
| `b3a6150` | fixed frame format: &JSON{CRC}#, CRC-8 MAXIM, boot sequence cleanup |
| `fc5378f` | fix: remove VERBOSE RX byte log, limit 256 bytes/loop |
| `827a064` | fix: RX parser tracks string state, POLL every 200ms |
| `0e0e873` | reorganize repo: per-processor dirs, clean root, English docs |
| `5897b94` | fix: U16 is not a simple UART bridge, update descriptions |
| `927cc8a` | add note about hidden WiFi/BT in all SNK mowers |
| `05cb6b8` | update ha.md: remove stale RX analysis, document current protocol/CRC/issues |
| `6170e24` | fix snk_mower boot sequence: correct POLL/KEEPALIVE pattern, add pinout docs |
| `d2b8ea0` | fix: don't reset SYNC burst on repeated DEVICE_INFO, remove 1s delay |
| `7fac4c9` | fix: revert CMD_DEVICE_INFO to 0x330000A1, add CMD_MB_DEVICE_INFO 0x330000A9 |
| `21e4c3f` | fix: correct CMD_DEVICE_INFO (0x330000A1→0x330000A9), add KEEPALIVE |
| `446a6a4` | update YAML header comments with verified PCB wiring |

### Phase 4: Display and Buzzer

| Hash | Description |
|------|-------------|
| `3a3bbd9` | boot handshake works! system stable 30s+. cleanup, GPIO27 buzzer |
| `50b1c55` | temp: test GPIO2 as buzzer |
| `1025b10` | fix: cast buzzer_pin/rain_pin to gpio_num_t |
| `fe53ad7` | remove periodic buzz(80) from PIN accepted |
| `887d628` | docs: buzzer confirmed on GPIO27 |
| `38c3ed3` | fix GPIO32→R31, GPIO33→R33, GPIO27 buzzer path unknown |
| `ab94e34` | docs: GPIO27 buzzer via via internal layer |
| `757220b` | pin_diag: scan all GPIOs 0-39 |
| `2427fa6` | remove pin_diag, return to normal operation |
| `1365262` | pin_diag: skip flash pins 6-11 |
| `220ee7a` | add boot_delay config: delays handshake for OTA safety window |
| `2031f9f` | boot_delay: 10s default |
| `e4cbc1b` | docs: MB supervisor timer and boot_delay workaround |
| `e15e97c` | docs: GPIO36 = rain sensor confirmed by wet finger test |
| `b6d156b` | docs: display pins unconfirmed, add empirical corrections |
| `7ab3a84` | add lcd_find: auto-scan 7 candidates x 3 roles = 210 combos |
| `31ef84c` | display confirmed: CLK=5, CS=34, MOSI=32 via lcd_find |
| `88a26fa` | lcd_find: exclude input-only GPIO34/39, fix CLK=5 MOSI=32 |
| `bc50f42` | external_components: refresh: 0s |
| `e7654ba` | lcd_find: quick 6-permutation scan of pins {18,33,32} |
| `18344e1` | lcd_find: explicit 6-permutation test, no index math |
| `ba250b3` | fix: remove orphaned total reference |
| `b7e4f62` | fix: add delays in shift24 bit-banging |
| `70bac18` | fix: lcd_find now exits cleanly to handshake, display pins {5,32,25} |
| `e681887` | feat: add lcd_sweep mode — single-bit sweep |
| `473937` | snk_mower: add per-phase logging to LCD sweep, slow to 8ms |
| `7459de8` | snk_mower: inverted CS polarity |
| `606ba0d` | snk_mower: add lcd_find_rclk mode |
| `6125972` | snk_mower: RCLK test falling edge |
| `a9aa55f` | snk_mower: RCLK test with transparent mode |
| `fe7c5a7` | HARDWARE: OK button confirmed on GPIO19 |
| `d695f60` | HARDWARE: remove unconfirmed button GPIO mappings |

### Phase 5: Display Optimization

| Hash | Description |
|------|-------------|
| `615afc5` | snk_mower: expand pin_diag scan — add GPIO0 |
| `48a5100` | snk_mower: display pattern test |
| `942c865` | ha.md: update with pattern test results |
| `7fb3e55` | snk_mower: replace pattern test with minimal glitch test |
| `a360c1a` | document button GPIO/ADC experiments |
| `77aad1e` | snk_mower: replicate v3 — exact shift24(FFFFFF) |
| `6d9b54c` | snk_mower: LCD listen mode |
| `33046a5` | snk_mower: restore 210-combo lcd_find test |
| `2d337b8` | ha.md: update with 210 test results |
| `4aaa98f` | ha.md: add EE:EE result |
| `a979fa9` | snk_mower: use shift24_nocs (no CS toggle) |
| `ddf91b4` | Add MAX7219 test mode |
| `b09f898` | Update YAML: chip_rev 3.1, sram_as_iram, DEBUG log |
| `ff4460d` | snk_mower: fix display driver latching, set definitive GPIO33/25/32 |
| `be19270` | docs: document successful display sweep results |
| `deb660a` | feat: implement high-resolution hardware timer for display |
| `834422b` | feat: boot display, battery on three right digits, error E+code |
| `9c12d2a` | chore: cleanup repo |
| `322d7fc` | fix: send POLL during boot_delay to prevent watchdog kill |
| `b8168af` | fix: fix UART RX deadlock during boot_delay |
| `3dd5262` | chore: clean up redundant YAML files |
| `ca7c0f2` | feat: increase boot_delay to 30s |
| `588393d` | feat: implement split digit select (b0/b1) |
| `b4ff3cd` | feat: complete display layout with symmetrical mapping |
| `d6cf72e` | feat: correct digit mapping with symmetric 0x02/0x04 |
| `67da34e` | feat: implement 16-step DIAG SWEEP test |
| `077254f` | feat: implement safe multiplexed DISPLAY TEST |
| `220810a` | feat: implement targeted B0 SWEEP |
| `b8ca378` | fix: correct display orientation |
| `73554a8` | feat: LEFT SIDE SWEEP |
| `149ab93` | feat: complete digit mapping — all 4 digits |
| `1036fea` | fix: correct DIGIT_B1_MAP order |
| `0368db7` | docs: update DIGIT_B1_MAP order |
| `bcaf301` | feat: COLON SWEEP |
| `d9445cc` | feat: DP test — show decimal points |
| `a4d7279` | ha.md: colon/DP sweep results |
| `08a6357` | DP SWEEP: test all 8 segment bits |
| `a118697` | DP SWEEP: show 8 + sweep b0 (U4) bits |
| `2a9b105` | ha.md: DP segment sweep result |
| `5f2847a` | DP investigation concluded |
| `b45126e` | ha.md: update status, display summary |
| `17c2c13` | snk_mower: remove GPIO/LCD discovery code |
| `0ae836c` | snk_mower: show 'byE' on display at shutdown |
| `8f4d335` | snk_mower: keep 'byE' for 3s during shutdown |
| `119d077` | snk_mower: idle display cycles IdLE 5s ↔ battery 5s |
| `303c49e` | snk_mower: cycle text↔battery on all non-error states |
| `f90d008` | fix: don't reset display cycle on every STATUS message |
| `ed30fd6` | fix boot handshake, implement start_mowing/send_trim |
| `9e4e008` | ha.md: update experiment results |
| `30e4e22` | ha.md: return_to_dock result |

### Phase 6: Actions and Commands (Additional Experiments)

| Hash | Description |
|------|-------------|
| `644e9d2` | compat_mode + fix compile errors + capture 04 analysis |
| `1ad3c2b` | remove periodic send_esp_state entirely |
| `94a38de` | add POLL in DONE phase; remove wifi/bt from PRE phase |
| `204203f` | document all experiments in ha.md |
| `f038818` | fix PIN sequence: send PIN_SEND only after PIN_RESULT prompt |
| `25b0c42` | LA captures: full UART protocol decoded |
| `e43d408` | 4th capture: docking sequence |
| `e05cf20` | docs: update PROTOCOLS.md and ha.md with LA capture findings |
| `fc2af6c` | ha.md: add LA capture analysis conclusions |
| `86fd1ac` | ha.md: add hardware notes - ON pin sensitivity |
| `2744cbd` | fix: boot→idle transition, missing DEVICE_INFO handling |
| `4c2b5cf` | snk_mower: fix boot sequence and deduplicate device info |
| `ddfbff1` | snk_mower: add missing member variables for device info |
| `515f54b` | snk_mower: increase CMD_POLL interval from 30ms to 200ms |
| `c6a4329` | snk_mower: add 30ms×10 poll burst after boot_seq |
| `d3d0473` | snk_mower: restore 30ms polling for normal operation |
| `33360ba` | snk_mower: fix boot sequence order — wait for MB first |
| `08e9149` | Revert to ESP-initiated boot order |
| `22a1a08` | Non-blocking boot sequence, reduced log spam, 10s DEVICE_INFO timeout |
| `13f98d4` | Send rain=0 in boot sequence instead of rain=1 |

---

## 8. Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│ ESP32-WROOM-32UE (Display Board)                                    │
│                                                                     │
│  GPIO17 ──TX──┐                                          ┌────────┐ │
│  GPIO16 ──RX──┤  UART 230400 8N1                        │Display │ │
│                │  JSON &{cmd}<CRC>##                      │ 7-seg  │ │
│  GPIO33 ──CLK─┤  SPI (2MHz)  ────────────────────────────┤ 4-digt │ │
│  GPIO25 ──MOSI┤                                            │ 74HC595│ │
│  GPIO32 ──CS──┤                                            └────────┘ │
│  GPIO27 ──────┤ Buzzer                                                │
│  GPIO36 ──────┤ Rain sensor                                          │
│  GPIO19 ──────┤ OK button (to U16)                                   │
│                                                                      │
└──────────────────┬───────────────────────────────────────────────────┘
                   │ JSON @ 230400
                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│ U16: GD32F303CGT6 (FreeRTOS)                                        │
│  → JSON bridge ESP32 ↔ U13                                          │
│  → Handles buttons (START/STOP/HOME)                                │
│  → Own sensors (rain, border coil, light)                           │
│  → max 128 bytes/frame                                              │
│  → EasyLogger v2.2.99                                               │
└──────────────────┬───────────────────────────────────────────────────┘
                   │ JSON @ 230400
                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│ U13: GD32F305AGT6                                                    │
│  → Motor control                                                     │
│  → KV-store (EEPROM U22 — PIN, configuration)                       │
│  → cJSON parser                                                     │
│  → RTC (display only, not schedule)                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Architectural Notes

- **ESP cannot physically start mowing** — there is no UART command for START. START/STOP/HOME are physical buttons connected to U16. ESP only receives state notifications.
- **U16 is not a simple UART bridge** — adds its own messages (sensors), aggregates data from U13, has its own logic.
- **U13 parses JSON via cJSON** — confirmed in firmware strings.
- **OTA**: Cloud → ESP32 → U16 → U13 (same UART channel, different framing: `[2B length LE][N bytes][1B XOR checksum]`).

---

## 9. Conclusions and Next Steps

### What We Know For Sure
1. Protocol is JSON with CRC8-Dallas, `&` prefix, `#` suffix, 230400 8N1
2. Boot sequence is required for MB to respond
3. MB requires POLL every ~30ms — supervision timer
4. PIN (4-digit) is stored in EEPROM U22, verified by U13
5. Display is 3× 74HC595, SPI, 24 bits per digit

### What Is Uncertain
1. **Why MB shuts down after boot_seq** — hypothesis: `rain=1` in boot sequence. Changed to `rain=0`.
2. **Will MB stay on after `rain=0`** — to be verified in next test.
3. **Are additional responses to MB queries needed** — WiFi/BT status, ESP_INFO.
4. **Does MB expect CMD_BATTERY from ESP** — LA shows MB sends `bat:x`, ESP responds.

### Priority for Further Work
1. Test with `rain=0` in boot sequence (commit `13f98d4`) — does MB stay on?
2. If yes: add responses to `CMD_ESP_WIFI` / `CMD_ESP_BT` in normal mode
3. If no: investigate whether missing `CMD_BATTERY` causes SUPERVISION
4. Find colon bit on display (b0, U4)
5. Understand RTC time format (0x40000011) — time synchronization

---

## 10. Watchdog Experiments (logs 12-22, 2026-06-22)

### Context

After flashing a new version of the ESPHome component (with corrected state mapping and handlers), MB kept cutting power (watchdog) within a few to tens of seconds. A series of experiments (logs 12-22) identified the root causes.

### Individual Log Results

| Log | Commit | Result | Root Cause |
|-----|--------|--------|------------|
| 12 | `956ae67` | ❌ Watchdog ~8s after PIN | WIFI+BT+ESP_INFO burst immediately after PIN (timers expired) |
| 13 | `85e9416` | ❌ Watchdog ~4s after PIN | Same burst + ESP_TRIM in boot sequence |
| 14 | `d597775` | ❌ Watchdog ~2s after boot seq | uint32_t underflow: `now` before UART read, timers reset internally |
| 15 | `24b7991` | ❌ Watchdog <1s | BOOT sent after boot_delay — MB watchdog ~30s from power-on |
| 16 | `5480356` | ✅ **Stable >1min** | Reverted to stable base 5be3712 + protocol fixes |
| 17 | `0a1bc44` | ❌ BOOT_INIT flood + SUPERVISION | Proactive PIN before SYNC + SYNC timeout 2s (interrupted burst) |
| 18 | `204ccb2` | ❌ DEVICE_INFO flood | Display CS fix (gpio_set_level in timer) interfered with timing |
| 19 | `aea0004` | ❌ DEVICE_INFO flood 68× | Burst ESP_INFO+ESP_STATE after PIN (timers not reset) |
| 20 | `029086b` | ❌ DEVICE_INFO flood 198× | POLL at 200ms in DONE → MB re-sends DEVICE_INFO |
| 21 | `fe90293` | ❌ DEVICE_INFO flood 138× | POLL at 30ms in DONE → same effect |
| 22 | `e74b56c` | ✅ **Stable** | Removed POLL from DONE — only KEEPALIVE every 1s |

### Key Discoveries

#### 1. POLL in DONE causes DEVICE_INFO flood

**Problem:** `CMD_ESP_POLL` (`0x300000A1`) sent periodically in DONE phase causes MB to interpret it as "ESP requests device info" and re-sends `DEVICE_INFO` + `HW_VERSIONS` in a loop (68-198 repetitions in ~20s).

**Solution:** POLL should be sent **only in PRE and SYNC phases** (to detect DEVICE_INFO during boot). In DONE phase only `KEEPALIVE` (`0x30000005`) every 1s.

**Evidence:** Log 16 (no POLL in DONE) — 12 repetitions of DEVICE_INFO. Log 21 (POLL@30ms in DONE) — 138 repetitions. Log 20 (POLL@200ms) — 198 repetitions (MB even more confused).

#### 2. Command burst after PIN confuses MB

**Problem:** If ESP immediately sends `ESP_INFO` (`0x40000006`) and/or `ESP_STATE` (`0x30000028`) right after sending PIN (`0x41000005`), MB enters a re-initialization loop (BOOT_INIT flood) and eventually triggers watchdog (`0x20000002` SUPERVISION).

**Solution:** Reset periodic timers (`last_wifi_status_`, `last_esp_info_`, `last_esp_state_`) at the SYNC→DONE transition, so the first periodic sends occur after a full interval (5s/30s/10s), not immediately.

**Mechanism:** Timers are initialized in `finish_setup()` (at start). SYNC lasts ~1s. Without reset, `now - last_esp_info_ > 30000` is true immediately after entering DONE → burst.

#### 3. Proactive PIN before SYNC breaks handshake

**Problem:** Sending PIN (`0x41000005`) right after receiving DEVICE_INFO, but **before** completing the SYNC burst (5×ESP_INFO + 6×INIT), causes MB to not finish processing the full boot sequence → re-init flood.

**Solution:** PIN sent only after SYNC completes (`init_burst_count_ >= 6`), in DONE phase.

#### 4. MB watchdog ~30s from power-on

**Problem:** MB has a watchdog ~30s from power-on. If ESP does not send `BOOT` (`0x40000004`) within this window, MB cuts power.

**Solution:** `boot_delay` (30s) sends POLL+KEEPALIVE during the delay, but BOOT is sent after the delay expires. With `boot_delay: 30` the watchdog would trigger simultaneously.

**Status:** Solved in stable base 5be3712 — BOOT+KEEPALIVE+STATE+RAIN sent immediately in `finish_setup()`, before boot_delay. During boot_delay POLL+KEEPALIVE are sent at 200ms/1s.

#### 5. Display CS fix interferes with communication

**Problem:** Manual CS control (`gpio_set_level` in timer callback every 2ms) interferes with UART timing, destabilizing communication with MB.

**Solution:** Reverted to SPI-managed CS (`spics_io_num = display_cs_`). Display flicker is cosmetic — MB stability is critical.

### Stable Configuration (commit `e74b56c`)

```
Boot sequence:
  finish_setup(): BOOT + KEEPALIVE + STATE(0) + RAIN(1) (immediately)
  boot_delay (30s): POLL@200ms + KEEPALIVE@1s + WIFI/BT@5s
  After boot_delay: BOOT + KEEPALIVE + STATE(0) + RAIN(1) (again)
  PRE→SYNC: after DEVICE_INFO from MB
  SYNC: 5×ESP_INFO + 6×INIT (burst ~1s)
  SYNC→DONE: reset timers (WIFI/ESP_INFO/ESP_STATE)
  DONE: PIN(1×) + KEEPALIVE@1s + WIFI/BT@5s + ESP_INFO@30s + ESP_STATE@10s + RAIN@60s
  DONE: NO POLL (only KEEPALIVE)
```

### Open Questions

1. **Will MB stop sending PIN_RESULT periodically?** — In log 22, PIN accepted appears every 5s. This might be normal (MB resends result with every WIFI/BT status) but worth verifying.
2. **Is `send_esp_state(state_)` with `state:1` correct?** — Original sent `state:0` at boot, then current state. Check in captures.
3. **Is omitting ESP_TRIM in boot OK?** — Original sent `0x300000A6` after INIT. MB may not send full SCHEDULE without it, but log 22 works without it.

---

## 11. Session 2026-06-23: Final Cleanup & Project Wind-Down

### Context
After exhausting all software-based methods to trigger mowing from the ESP32 (methods 1–5), and failing to get a response from the physical START button even with stable communication (log 23), the project was wound down. The original firmware was restored to the mower — it mows normally with stock software.

### Changes Made This Session

| Change | Description |
|--------|-------------|
| Removed GPIO13/15/20/22 binary sensors | Exclude interference with J8 button circuits |
| Removed GPIO19/OK binary sensor | Also removed to rule out any GPIO conflict |
| Removed all test buttons | `Action: Mow`, `Action: Dock`, `Test: START_ACK`, `Test: EXEC_ACTION`, `Test: CMD action=1/4`, `Test: ESP_TRIM auto=1` — all confirmed useless |
| Translated `ha.md` to English | Full Polish→English translation |
| Removed `ALIASES.md` references | Cleaned up README.md |
| Removed `olx_results/` and `results/` | Marketplace search results, no longer needed |
| Merged `doc/` → `docs/` | Consolidated documentation directories |
| Reorganized `captures/2026-06-21/` | Each capture now in its own subfolder with `capture.vcd` + `notes.md` |
| Removed `.opencode/plans/` | Agent planning files |
| Removed `img/crops/` | Unused cropped PCB photos |
| Removed `secrets.yaml` from git & disk | Security cleanup |
| Removed `kosiarka-logs*.txt` from git | Log files now gitignored (kept on disk for reference) |
| Added status summary to `PROTOCOLS.md` | Documented what works/doesn't, key constraints, future work |

### Key Findings

#### 1. GPIO sensors do not affect START button
Even after removing all binary_sensor GPIO definitions (GPIO13/15/20/22 + GPIO19/OK), the physical START button still did not trigger any MB response. This confirms the buttons are **exclusively handled by U16 through J8** — ESP32 has no influence over them.

#### 2. PIN is NOT stored in ESP32 firmware
Confirmed by firmware decompilation agent:
- Zero GPIO input reads in original ESP32 firmware
- No `pwd` constant, no `rw_key.c`, no button matrix, no touch sensor
- No evidence in any capture that a UART message carries button-press info during PIN entry
- PIN is stored in **U13 KV-store (EEPROM U22)** — ESP only forwards user-entered PIN for verification
- Original firmware cannot auto-send PIN because it doesn't have it stored

#### 3. No UART command triggers mowing (definitive conclusion)
Methods 1–5 all failed:
- `send_action(1/4)`
- `CMD_EXEC_ACTION` (0x41000003) sent from ESP
- `CMD_START_ACK` (0x41000020) sent from ESP
- `CMD_RETURN_HOME` (0x41000006) sent from ESP
- `CMD 0x4100000A` with `action:1/4`
- `ESP_TRIM auto=1` with current-time schedule
- `ESP_STATE(2)` sent periodically

No evidence in any LA capture of an ESP→MB command that initiates mowing.

### Only Untested Approach: Method 6 — GPIO jumper
- Connect a free ESP32 GPIO pin (e.g., GPIO13/15/20/22) to **J8 pin 6** (START line)
- Pulse LOW to simulate START button press
- Requires soldering or probing on lacquer-coated PCB (not attempted)

### Final State
- The ESPHome component (`snk_mower`) achieves stable communication with MB (36+ s, state=1 READY, watchdog-safe)
- All sensors, display, buzzer, HA integration work
- Mowing control from ESP is **impossible via UART** — the protocol has no such command
- Physical buttons (START/STOP/HOME) are hardwired to U16 through J8, bypassing ESP entirely
- Original firmware restored — mower operates normally

---

## 12. Contradictions & Open Hypotheses (2026-06-23)

### The Core Contradiction

| Observation | Implication |
|-------------|-------------|
| ESP32 decompilation shows **zero GPIO input reads** | Buttons are NOT read by ESP32 |
| No UART message carries button-press info in any capture | MB does NOT tell ESP which button was pressed |
| **Yet digits change on the display during PIN entry** | ESP32 **must** know which digit key was pressed |

The display is driven entirely by ESP32 (SPI → 74HC595). There is no way for the digits to change without ESP32 knowing the button state. This means **at least one of the decompilation findings is wrong** or we missed something fundamental.

### Hypotheses

#### H1: Buttons ARE connected to ESP32 (but not found)
- PCB lacquer prevented full trace routing
- Decompilation may have missed GPIO reads via:
  - **Timer ISR** — the display refresh callback (8ms, `esp_timer`) could read GPIOs. Static analysis of timer callbacks is notoriously easy to miss
  - **Register-level access** — direct GPIO register reads (`GPIO.in` / `GPIO.in1`) bypass standard HAL functions
  - **Xtensa-specific instructions** — Ghidra decompiler may not handle all addressing modes
- **Test**: Rescan PCB without lacquer, or re-examine ESP32 firmware with focus on timer callbacks and register-level GPIO access

#### H2: ESP32 must "enable" button path (GPIO as mux select)
- A free ESP32 GPIO (e.g., GPIO14 or GPIO18 — connected but unknown function) might act as a **mux select** that routes button signals either to U16 (default) or to ESP32
- Original firmware might set this pin HIGH/LOW at boot to "take over" buttons, then release it
- **Test**: Check original firmware writes to all unknown GPIOs during boot; try cycling each unused GPIO while monitoring button response

#### H3: START does not work on custom firmware because MB is in wrong state
- Our boot sequence differs from original (e.g., missing `CMD_ESP_TRIM`, different timing, no `CMD_BATTERY` response)
- U13/U16 state machine might have preconditions for START that aren't met
- **Test**: Compare the full STATUS sequence (`0x330000A0`) between original capture (`drugi.sr`) and our firmware (log 23) — byte-for-byte, every field. Look for missing flags or different field values
- **Test**: Replay the exact original firmware boot sequence command-by-command (including timing) from ESPHome to see if START starts working

#### H4: STA/OK/ON lines carry additional signaling
- These 3 button lines on J8 might be more than simple digital inputs:
  - Pulse-width modulated signals
  - I²C-like bidirectional protocol
  - Daisy-chain with state encoding (like a shift register)
- **Counter-evidence**: LA captures show clean UART without correlation to activity on D3/D4 (START/OK) lines. The lines appear static or just digital button states.

#### H5: Too many unknown ESP32 pins — incomplete understanding
| GPIO | Connection | Status |
|------|-----------|--------|
| GPIO4 | ? | Untraced (lacquer), present on PCB |
| GPIO14 | ? | Untraced, mentioned in decompilation |
| GPIO18 | ? | Untraced, candidate in display scan |
| GPIO23 | VSPI MOSI? | Untraced |
| GPIO26 | ? | Untraced |
| GPIO34 | ? | Input-only, untraced |
| GPIO35 | ? | Input-only, untraced |
| GPIO39 | ? | Input-only, untraced |

- **Test**: Full decompilation of all 3 MCUs (ESP32 + U16 + U13) with cross-referencing would reveal all peripheral interactions

### Recommended Next Steps (if project is revisited)

1. **Re-examine ESP32 firmware** — focus on ISRs, timer callbacks, and direct register access (`GPIO.in`, `GPIO.out`, `GPIO.enable`)
2. **Compare STATUS frames** — original `drugi.sr` vs our log 23, look for any difference in `0x330000A0` fields
3. **Replay original boot sequence** — send exact commands at exact timings from our firmware to isolate the state machine difference
4. **Cycle unknown GPIOs** — write HIGH/LOW to each unknown pin on J8/display board while monitoring for button functionality change
5. **Deep U16 decompilation** — the button routing logic is likely in U16 firmware (FreeRTOS, EasyLogger), which has 256 KB of code
6. **Trace GPIO4/14/18/26 on PCB** — remove lacquer and trace to find hidden connections
