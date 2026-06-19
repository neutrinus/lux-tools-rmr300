# Home Assistant Integration — SNK Mower ESPHome

## Design Assumptions

These are educated guesses based on reverse engineering. Mark with ✅ once confirmed by UART sniffing.

| # | Assumption | Status | If wrong, impact |
|:-:|-----------|:------:|------------------|
| 1 | STATUS_RSP (0x0E) is 1+ bytes with bit flags: `0x01=mowing`, `0x02=returning`, `0x04=charging`, `0x08=docked`, `0x10=error`, `0x20=locked` | ⚠️ Unconfirmed | Rewrite `handle_status_response()` |
| 2 | START button (K1) direct GPIO line (J8 pin 6) bypasses PIN check on mainboard | ⚠️ Unconfirmed | Must always send PIN before MOW_START |
| 3 | PIN is 4-digit number stored in U13 EEPROM, known value `9633` | ✅ Confirmed (RAM dump) | No action needed |
| 4 | BAT_RSP (0x15) is 2 bytes, little-endian mV | ✅ Cross-referenced U16↔U13 JSON chain | None — works as implemented |
| 5 | ERROR_INFO (0x12) is 2 bytes, big-endian error code | ⚠️ Unconfirmed length | Adjust `handle_error_info()` |
| 6 | Mainboard sends no unsolicited messages (polling-only model) | ⚠️ Unconfirmed | Add async handler in `loop()` |
| 7 | Display pins: CLK=GPIO18, MOSI=GPIO23, CS=GPIO5 | ✅ PCB trace verified | Wrong pin order causes blank display |
| 8 | UART baud rate **230400 8N1** (not 115200) | ✅ LA capture decode | Wrong baud → no communication |
| 9 | 74HC595 cascade: chip1=segments, chip2=digits+colon, chip3=unused | ✅ PCB verified | Segments wrong / digits wrong |
| 10 | 7-segment segments: active HIGH, digit select: active HIGH | ⚠️ Unconfirmed (likely inverted from 74HC595) | Display shows nothing or wrong digits |
| 11 | Checking state derived from STATUS_RSP flag OR voltage >20V | ⚠️ Unconfirmed | `set_charging_display()` never called |
| 12 | Voltage lookup table (15.0V–20.4V → 0–100%) is accurate enough | ⚠️ Approximate | Battery % off by ±10% |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ ESP32 (ESPHome)                                             │
│                                                             │
│  snk_mower (custom component)                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ UART (GPIO16/17, 230400 8N1, JSON protocol)          │   │
│  │                                                      │   │
│  │  Periodic polling (every 2s):                        │   │
│  │   ├── STATUS_REQ (0x0D)  → STATUS_RSP (0x0E)        │   │
│  │   └── BAT_REQ    (0x14)  → BAT_RSP   (0x15)         │   │
│  │                                                      │   │
│  │  On startup:                                         │   │
│  │   └── PWD_VERIFY (0x0B)  → PWD_RESULT (0x0C)        │   │
│  │                                                      │   │
│  │  On command:                                         │   │
│  │   ├── MOW_START  (0x0F)                              │   │
│  │   └── CHARGE_RET (0x10)                              │   │
│  │                                                      │   │
│  │  Async from mainboard:                               │   │
│  │   └── ERROR_INFO (0x12)  → error code                │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
│  Buttons (GPIO):                                            │
│   ├── K1 (START) GPIO26  → binary_sensor                   │
│   ├── K2 (HOME)  GPIO25  → binary_sensor                   │
│   ├── K3 (OK)    GPIO33  → (unused, direct to mainboard)   │
│   └── K4 (ON)    GPIO27  → (unused, direct to mainboard)   │
│                                                             │
│  Display (SPI → 74HC595×3 → 4-digit 7-segment LED):        │
│   ├── Battery % while mowing (e.g. "b85")                  │
│   ├── Status: IdLE, Mow, HoME, ChAr, Err, LoCK             │
│   └── "----" during boot                                   │
│                                                             │
│  Rain sensor: GPIO36 (ADC) → binary_sensor                 │
│  Buzzer:        GPIO12 (PWM)  → (optional)                  │
│                                                             │
│  ── WiFi ──→ Home Assistant (native API)                   │
│  ── BLE  ──→ Bluetooth proxy (optional)                    │
└─────────────────────────────────────────────────────────────┘
```

## ESPHome Entities

### Sensor

| Entity ID | Type | Source | Value |
|-----------|------|--------|-------|
| `sensor.mower_battery_level` | sensor | BAT_RSP (mV→%) | 0–100% |
| `sensor.mower_battery_voltage` | sensor | BAT_RSP (mV) | 15.0–21.0 V |
| `sensor.mower_error_code` | sensor | ERROR_INFO | numeric code |

### Binary Sensor

| Entity ID | Source | ON = true |
|-----------|--------|-----------|
| `binary_sensor.mower_is_mowing` | STATUS_RSP flags | Mowing active |
| `binary_sensor.mower_is_charging` | STATUS_RSP flags | On charger |
| `binary_sensor.mower_is_docked` | STATUS_RSP flags | At station (docked or charging) |
| `binary_sensor.mower_has_error` | STATUS_RSP / ERROR_INFO | Error or locked |
| `binary_sensor.mower_rain_detected` | GPIO36 ADC | Rain sensor wet |
| `binary_sensor.mower_start_button` | GPIO26 | Physical START pressed |
| `binary_sensor.mower_home_button` | GPIO25 | Physical HOME pressed |

### Text Sensor

| Entity ID | Source | Values |
|-----------|--------|--------|
| `text_sensor.mower_status` | STATUS_RSP | `unknown`, `idle`, `mowing`, `returning`, `charging`, `docked`, `error`, `locked` |
| `text_sensor.mower_status_message` | STATUS_RSP | Human-readable description |

### Button

| Entity ID | UART Cmd | HA service |
|-----------|----------|------------|
| `button.mower_start_mowing` | 0x0F | `button.press` |
| `button.mower_return_to_dock` | 0x10 | `button.press` |

### Number (optional, future)

| Entity ID | Purpose | Default |
|-----------|---------|---------|
| `number.mower_battery_capacity` | Battery capacity (Ah) for voltage→% fine-tuning | 5.0 |

## Local 7-segment Display

| State | Display | Behavior |
|-------|---------|----------|
| Unknown/booting | `----` | All off |
| Idle | `IdLE` | Steady |
| Mowing | `bXX` | Battery %, e.g. `b85` |
| Returning | `HoME` | Steady |
| Charging | `ChAr` | Steady (battery % shown briefly) |
| Docked | `IdLE` | Steady |
| Error | `Err` | Permanently (error code via HA only) |
| Locked | `LoCK` | Permanently (PIN failed) |

## Home Assistant — Template Lawn Mower

> **Note:** ESPHome does not yet have a native `lawn_mower` platform. Use a template lawn_mower in HA.

```yaml
# configuration.yaml

lawn_mower:
  - name: "SNK Mower"
    unique_id: "snk_mower"
    state: >
      {% set s = states('text_sensor.mower_status') %}
      {% if s == 'mowing' %} mowing
      {% elif s == 'docked' or s == 'charging' %} docked
      {% elif s == 'error' or s == 'locked' %} error
      {% else %} paused
      {% endif %}
    start_mowing:
      service: button.press
      target:
        entity_id: button.mower_start_mowing
    dock:
      service: button.press
      target:
        entity_id: button.mower_return_to_dock
```

### Lovelace Dashboard

```yaml
type: entities
title: Mower
entities:
  - entity: lawn_mower.snk_mower
  - entity: sensor.mower_battery_level
  - entity: sensor.mower_battery_voltage
  - entity: binary_sensor.mower_is_mowing
  - entity: binary_sensor.mower_is_charging
  - entity: binary_sensor.mower_rain_detected
  - entity: text_sensor.mower_status_detail
  - entity: sensor.mower_error_code
  - type: buttons
    entities:
      - entity: button.mower_start_mowing
        name: Start
      - entity: button.mower_return_to_dock
        name: Home
```

## Voltage → Battery % (5S Li-Ion)

Lookup table in custom component:

| % | Voltage | Notes |
|:-:|:-------:|-------|
| 100% | 20.40 V | Full (4.08 V/cell) |
| 80% | 19.40 V | |
| 50% | 18.00 V | Nominal (3.6 V/cell) |
| 20% | 16.60 V | |
| 5% | 15.60 V | Low |
| 0% | 15.00 V | Cut-off (3.0 V/cell) |

Configurable via YAML `substitutions` in future.

## Schedule & Time

### How the original system worked

Confirmed from firmware analysis:

- **ESP32** stores schedule in its NVS (`mon_st`, `mon_len`, `tue_st` ... = start hour + duration per day of week, secondary slots `mon2_st`/`mon2_len`)
- **ESP32** has `rw_timer.c` — checks current time against schedule, sends MOW_START / CHARGE_RET at appropriate times
- **U13** (main MCU) has **no schedule logic** — its main loop has no timer/calendar function, its KV-store (18 keys) has no schedule keys
- **U16** (board MCU) also has no schedule logic — only sensors, motors, JSON forwarding

**Schedule lives entirely on ESP32.** When you flash ESPHome, the schedule from NVS is gone.

### What we can do instead

**Option A: HA Automations (recommended)**

```yaml
automation:
  - alias: "Mower weekday schedule"
    trigger:
      - platform: time
        at: "08:00:00"
    condition:
      - condition: time
        weekday:
          - mon
          - tue
          - wed
          - thu
          - fri
    action:
      - service: button.press
        target:
          entity_id: button.mower_start_mowing
      - delay:
          hours: 2
      - service: button.press
        target:
          entity_id: button.mower_return_to_dock
```

Advantages over original:
- Full UI in HA (not 4-character display + 3 buttons)
- Can add conditions (rain, battery level, season)
- Easy to modify, history tracking

**Option B: ESPHome local schedule** — can implement in custom component later if desired (ESPHome has `time` component with NTP sync).

### RTC / Time setting

ESP32 in original firmware fetched time via NTP (WiFi). ESPHome also supports NTP natively via `time:` platform. **No manual time setting needed** — time is always correct.

### Time drift on U13

U13 has its own RTC (`rtc calibration` string found), but it's irrelevant — U13 doesn't check time for anything. All time-sensitive decisions (schedule, duration) were made by ESP32.

## Data Availability Summary

### What we can expose via UART (implemented in custom component)

| Data | Source | Status |
|------|--------|:------:|
| Mowing / charging / idle status | STATUS_RSP (0x0E) | ⚠️ format to confirm |
| Battery voltage (mV → %) | BAT_RSP (0x15) | ✅ |
| Error code | ERROR_INFO (0x12) | ⚠️ length to confirm |
| Start mowing command | MOW_START (0x0F) | ✅ |
| Return to dock command | CHARGE_RET (0x10) | ✅ |
| Rain sensor | GPIO36 ADC | ✅ |
| Physical buttons | GPIO25-27 | ✅ |
| 7-segment display | SPI → 74HC595 | ✅ |

### What's NOT available via UART (stays in U13/U16 only)

| Data | Where | Reason |
|------|-------|--------|
| Total runtime / mileage | `run_param` (U13 KV, 80B) | No read cmd |
| Battery temperature | BMS log (U13 internal) | Not exposed |
| Charge cycle count | BMS log (U13 internal) | Not exposed |
| Area calibration shape | `shape_param` (U13 KV, 40B) | No read/write cmd |
| RTC time | U13 internal | No cmd |
| Schedule / timer | Was in ESP32 NVS | Flash ESPHome → gone |

### What we can build locally in ESPHome / HA

| Feature | Approach |
|---------|----------|
| Mowing time today | Template sensor counting time `is_mowing` = ON |
| Total runtime | ESPHome `total_daily_energy`-style meter on mowing state |
| Schedule | HA automations (better than original) |
| Mowing timer (auto-return after X min) | ESPHome script or HA automation delay |
| Rain hold-off | HA condition on rain binary sensor |

## WiFi Loss Behavior

- **Mower continues mowing** — U13/U16 are independent from ESP32
- **ESPHome auto-reconnects** — configurable timeout
- **Local buttons** — START (direct wire) works without ESP; HOME needs ESP to send UART command
- **Local display** — works as long as ESP32 is running
- **HA** — last known state, entity goes `unavailable` after `wifi.reboot_timeout`
- **On reconnect** — HA refreshes state via native API

## PIN Handling

At startup:
1. Send `0xAA 0x55 0x0B "9633" CS`
2. Wait for `0xAA 0x55 0x0C 0x00 CS`
3. If OK → start status + battery polling
4. If FAIL → retry up to 5 times, then state = LOCKED

PIN is configurable in YAML (`pin: "9633"`), validated for exactly 4 digits.

## Bluetooth Proxy

ESP32-WROOM-32UE supports BT 4.2 BR/EDR + BLE. Enable via `bluetooth_proxy:` in YAML. Works simultaneously with WiFi and custom component. Shared IPEX antenna — BT range may be slightly shorter than dedicated BT-only ESP32.

## Current Implementation State

### What works (in `snk_mower` component)
- Component compiles, links, boots on target
- `setup()` sends boot sequence: BOOT → KEEPALIVE → STATE(0)
- `loop()` sends POLL every 100ms, KEEPALIVE every 200ms
- INFO sent at 5s after boot (delayed), INIT at 8s after boot (delayed)
- JSON encoder/decoder handles all 64 known command IDs
- All sensor types, display driver, button handlers, PIN send implemented
- Safe mode validated at 60s — cold boot no longer triggers rollback

### What doesn't work
- **Zero RX** on both cold and warm boot
- No sensor data published to HA (no entities configured in YAML yet — diagnostic setup)
- No display output (no display pins in diagnostic YAML)

### What changed (RX before vs after boot sequence)
| Aspect | Before boot (old firmware) | After boot (new firmware) |
|--------|---------------------------|---------------------------|
| Boot sequence | none | BOOT→KEEPALIVE→STATE |
| POLL sent | yes (via loop()) | yes (via loop()) |
| KEEPALIVE sent | yes (via loop()) | yes (via loop()) |
| RX | saw echo of POLL/KEEPALIVE | nothing |
| Cold boot | n/a | no rollback at 60s ✓ |
| U16 state | uninitialized pass-through | initialized (after BOOT) |

## Hypotheses — Why Zero RX

### H1: MB sends power-on before ESP UART is initialized
**Verdict: LIKELY**
- The MB sends `0x20000001 action=0` immediately on power-up, before ESP's UART driver (ESP-IDF `esp_driver_uart`) loads (~300-500ms)
- In the capture this is index 0 — it's the first thing on the wire
- Our ESP boots, UART initializes, we send BOOT, but the MB may have already timed out waiting for ESP and entered a wrong state
- **Mitigation:** could probe with LA to see what MB actually sends on power-up

### H2: UART polarity is wrong
**Verdict: UNLIKELY**
- LA decoded valid JSON at standard polarity on both D0 (MB→ESP) and D1 (ESP→MB) without `invert_rx=yes`
- If the original ESP firmware had used inverted UART, D1 would show garbage
- Therefore the wire signal IS standard polarity, and our ESPHome config (no invert) is correct

### H3: Previous RX was self-echo (own TX heard on RX)
**Verdict: UNLIKELY**
- Pins are different: TX=GPIO17, RX=GPIO16
- No hardware loopback on the PCB would connect these two
- If it were a self-echo, we'd STILL see it now (we're still transmitting POLL/KEEPALIVE)
- The echo stopped exactly when we added boot sequence — this is a real behavior change in U16

### H4: Boot sequence is incorrect (missing message, wrong timing, wrong values)
**Verdict: POSSIBLE**
- Original sends INFO and INIT immediately (within ~100ms of BOOT)
- We delay INFO to 5s and INIT to 8s
- MB might be waiting for these before it sends anything else
- Original sends `init=3` (correct, we do too)
- Original sends WiFi/BT status early — we send it at 5s intervals starting from 2s

### H5: U16 enters a state where it expects a drop-in replacement for the original ESP, and our different behavior causes it to wait silently
**Verdict: POSSIBLE**
- The GD32 firmware might have a timeout: "if ESP doesn't send INFO within X ms → go to error/idle"
- Or it might require messages in a very specific order

### H6: GPIO conflict with another ESPHome component
**Verdict: UNLIKELY**
- UART2 pins (GPIO16/17) don't overlap with logger (baud_rate=0 disables UART0 output)
- No other components configured that would use these pins

## Deep Failure Mode Analysis

Below is a structured analysis of every possible reason we see zero RX with the boot sequence
but saw echo without it. Organized by layer — each failure mode includes a testable prediction
that LA or a code change can verify.

### Layer 1: Physical / Electrical

| # | Failure Mode | Why it would cause symptoms | Likelihood | Test |
|:-:|-------------|----------------------------|:----------:|------|
| 1.1 | **TX pin floating / wrong output type** — ESP TX (GPIO17) not configured as push-pull output, or drive strength too weak | U16 RX never sees a valid signal; U16 can't decode our frames → no response | LOW | LA on J2 pin 2 shows our TX waveform — should be clean 0-3.3V square at 230400 |
| 1.2 | **RX pin pull-up missing** — GPIO16 has no pull-up, U16 TX in Hi-Z during startup causes floating RX | We see noise (random 0/1 transitions) or constant break; U16's first bytes corrupted | LOW | LA on J2 pin 1 shows U16 TX — if it's floating we'll see noise |
| 1.3 | **Level mismatch** — U16 is 3.3V but something is 5V, or vice versa | One side can't read the other | LOW | LA captures worked at same probe point → levels are compatible |
| 1.4 | **Crosstalk / interference on J2 cable** — ribbon cable picks up motor noise or adjacent signal coupling | Gibberish on RX, or U16 reads gibberish | LOW (on bench) | Compare LA of J2 vs J8 — J8 was probed successfully |
| 1.5 | **GND missing or high resistance** — common ground between ESP and MB has poor connection | TX levels shift relative to RX reference; communication unreliable | VERY LOW (worked before) | Visual check of GND wire in J2 |

### Layer 2: UART Configuration

| # | Failure Mode | Why it would cause symptoms | Likelihood | Test |
|:-:|-------------|----------------------------|:----------:|------|
| 2.1 | **Inverted polarity on TX** — ESP TX configured as inverted, U16 expects normal | Every bit inverted → every byte wrong → U16 discards/ignores all our frames | **MODERATE** | LA decode of our TX with `invert_rx=yes` should show valid JSON. If it only decodes with invert, we have this bug |
| 2.2 | **Inverted polarity on RX** — ESP RX expects inverted, U16 sends normal | We can't decode U16's transmissions → nothing parsed | LOW | LA decode of U16 TX at standard polarity gave valid JSON. So if we don't see it, our RX config is wrong, BUT we saw echo before (RX worked partially) |
| 2.3 | **Baud rate mismatch** — actual baud rate on wire differs slightly from 230400 (e.g. 115200 due to clock config) | Framing errors, bytes dropped | LOW | LA decode at 230400 gave valid JSON → wire IS 230400 |
| 2.4 | **Parity / stop bits mismatch** — expects parity or different stop bit count | Framing errors, no valid frames | LOW | LA decode with 8N1 works → wire IS 8N1 |
| 2.5 | **Wrong UART peripheral** — ESPHome uses UART_NUM_0 (shared with console) or UART_NUM_1 (shared with flash) instead of UART_NUM_2 | TX/RX pins not connected to actual UART hardware → no data transmitted | VERY LOW | Logger has `baud_rate: 0` so UART0 is free; pins 16/17 map to UART_NUM_2 on classic ESP32 |
| 2.6 | **UART driver not installed** — `uart_driver_install()` not called / fails silently | Hardware never initialized → no TX or RX | LOW | ESPHome UART component handles this; we see TX messages in log → driver is working |
| 2.7 | **RX buffer overflow** — we send before reading, or read too slowly → hardware FIFO overflows, bytes lost | We miss some U16 responses, but not ALL of them | LOW | `rx_buffer_size: 512` in YAML; at 230400, 512 bytes = ~22ms of continuous data. Messages are 30-50 bytes → can hold 10+ messages |

### Layer 3: JSON Framing

| # | Failure Mode | Why it would cause symptoms | Likelihood | Test |
|:-:|-------------|----------------------------|:----------:|------|
| 3.1 | **Memory allocation failure** — `ArduinoJson` fails to allocate Document (usually 256-512 bytes on stack) | JSON encode returns 0 bytes → nothing transmitted | LOW | Our `send_json()` checks `n > 0` before `write_array()`. If we get zero, it doesn't send. Log would show no TX lines |
| 3.2 | **TX buffer too small** — `tx_buf_[512]` not enough for serialized JSON | serializeJson returns 0 → nothing transmitted | LOW | Our messages are <100 bytes |
| 3.3 | **RX buffer too small** — `rx_buf_[512]` truncates JSON before closing `}` | Partial JSON → deserializeJson fails → never reaches handler | LOW | But we'd log incomplete JSON in `ESP_LOGV(TAG, "RX byte: ...")` |
| 3.4 | **Deserialization error** — valid JSON but schema mismatch (e.g. `doc["cmd"]` as string not int) | `doc.containsKey("cmd")` fails → RX ignored | LOW | Original firmware uses numeric `cmd`, we mirror that |
| 3.5 | **Multiple JSON objects in one RX read** — U16 sends messages back-to-back without delay, our parser only handles one per loop() iteration | Some messages dropped if they arrive in the same `available()` batch | LOW | Our parser tracks `rx_in_json_` and `rx_index_`, collects one complete `{...}` per cycle. Multiple messages in buffer are handled over multiple loop() iterations |

### Layer 4: Command Structure

| # | Failure Mode | Why it would cause symptoms | Likelihood | Test |
|:-:|-------------|----------------------------|:----------:|------|
| 4.1 | **Wrong BOOT command ID** — our `0x40000004` doesn't match what U16 expects | U16 ignores BOOT → stays in echo mode or enters error state | LOW | Capture shows original ESP sends `0x40000004` |
| 4.2 | **BOOT missing required fields** — original firmware might send `{"cmd":0x40000004,"hw":...}` with additional fields | U16 checks for extra fields, rejects our bare `{"cmd":1073741828}` | **POSSIBLE** | Compare with capture: original sends bare `{"cmd":1073741828}` too |
| 4.3 | **STATE (0x30000028) value wrong** — we send `state=0` but MB expects a different initial state | MB ignores or rejects state transition | LOW | Capture shows `state=0` originally |
| 4.4 | **Wrong KEEPALIVE command** — we use `0x30000005` but U16 expects `0x300000A1` for keepalive | MB ignores our keepalives → times out | LOW | Both are used in original. The original sends `0x30000005` once early, then `0x300000A1` repeatedly |
| 4.5 | **Command framing missing** — original might send multiple commands in one write (e.g. `{"cmd":1}{"cmd":2}`) | We send one JSON per `write_array()` call; extra overhead but same data | LOW | Each JSON is self-contained |

### Layer 5: Timing

| # | Failure Mode | Why it would cause symptoms | Likelihood | Test |
|:-:|-------------|----------------------------|:----------:|------|
| 5.1 | **BOOT sent too late** — original ESP sends BOOT within ~40-60ms of power-on. We send at ~500ms+ (ESP32 boot + ESPHome init) | U16 has a timeout window for BOOT. If BOOT arrives after timeout, U16 enters fallback mode (echo mode or silent) | **HIGH** | Measure our first TX time with LA. Compare to original 40-60ms. If we're >200ms late, this is likely the root cause |
| 5.2 | **Delayed INFO (5s) and INIT (8s) too late** — original sends INFO and INIT immediately after BOOT (within ~100ms) | U16 transitions through boot states and expects INFO/INIT in sequence. After a timeout, it moves on without them | **HIGH** | LA will show if U16 sends anything between our TXs. Combined with 5.1 — we're late with everything |
| 5.3 | **POLL every 100ms too fast** — original polls at ~200ms intervals | U16/U13 may queue commands, but shouldn't cause total silence | LOW | Capture shows original sends POLL at irregular intervals ~100-200ms |
| 5.4 | **POLL starts too late** — `if (now > 2000)` delays first POLL to 2s after boot | MB may expect POLL earlier to confirm ESP is alive | **MODERATE** | In capture, original sends POLL within ~150ms of BOOT (before INFO even) |
| 5.5 | **loop() not called often enough** — ESPHome delays due to WiFi reconnection, OTA, or other components | UART buffer grows, we read less frequently, miss messages | LOW | Snapshot scenario — WiFi not configured to connect, OTA not active, no other custom components |
| 5.6 | **delay() in setup() blocks boot** — we call `delay(10)` twice in setup() (between BOOT→KEEPALIVE→STATE) | Total 20ms delay in setup(). Negligible compared to 500ms boot time | VERY LOW | |

### Layer 6: U16/U13 State Machine

| # | Failure Mode | Why it would cause symptoms | Likelihood | Test |
|:-:|-------------|----------------------------|:----------:|------|
| 6.1 | **U16 expects an initial message that we don't send** — some command before or instead of BOOT | U16 never transitions out of its initial state | **MODERATE** | We're reconstructing protocol from captures. Original firmware might send a message we haven't decoded yet |
| 6.2 | **U16 expects commands in a specific order within a time window** — BOOT must be followed by INFO within X ms, INIT within Y ms, etc. | U16 boots partially but hits timeout waiting for expected message → enters error state | **HIGH** | Our INFO is at 5s, INIT at 8s. If U16 expects them within 100ms of BOOT, we're orders of magnitude late |
| 6.3 | **U13 is not communicating with U16** — internal UART between U16 and U13 not initialized, or U13 is asleep/booting | U16 receives our commands but can't forward them → responds with nothing (or error code we don't handle) | **MODERATE** | LA of U16↔U13 internal bus would confirm, but we don't have access. Indirect test: does U16 ever send anything after our BOOT? |
| 6.4 | **U16 enters bootloader (DFU) mode instead of normal mode** — a specific GPIO level or UART pattern triggers bootloader | U16 accepts no commands, echoes nothing, responds to nothing | LOW | If this were true, original firmware would trigger it too |
| 6.5 | **U16 firmware has a bug triggered by our timing** — race condition or buffer overflow caused by receiving commands in unexpected order | U16 hardfaults or enters WDT reset loop → no communication | **POSSIBLE** | If U16 is in WDT reset loop, it would periodically reset and send power-on again. We'd see burst of messages followed by silence. LA would show this |

### Layer 7: ESPHome / Build Environment

| # | Failure Mode | Why it would cause symptoms | Likelihood | Test |
|:-:|-------------|----------------------------|:----------:|------|
| 7.1 | **`external_components` pulling wrong version** — GitHub source `neutrinus/lux-tools-rmr300@main` might have cached a stale version | We're not running the code we think we are | LOW | Check `esphome logs` timestamp vs build time. But we verified TX in log |
| 7.2 | **ArduinoJson version mismatch** — ESPHome 2026.6.0 might use a different ArduinoJson version than expected | `serializeJson()` or `deserializeJson()` API changed → compile may work but behavior differs | LOW | No compilation warnings about deprecated API |
| 7.3 | **Flash corruption / partial OTA** — ESP8266-style issue, but on ESP32 with OTA | Firmware runs partially, UART init fails | LOW | OTA completes successfully, safe mode not triggered after 60s |

## Root Cause Synthesis

The most probable root cause chain, based on all evidence:

```
Timeline:
  t = 0 ms    MB powers on. U16 boots (GD32 boots fast, ~tens of ms).
              U16 starts sending: 0x20000001 (power-on), 0x40000009 (heartbeat) ×5,
              0x40000008 (init) ×5, 0x50000021 (battery), 0x20000004 (ready) ×2,
              0x330000A1 (device info), 0x330000A2 (hw versions), 0x330000B0 (map), ...

  t = ~300ms  ESP32 finishes IDF boot, ESPHome starts, UART driver installs.
              By this time MB has already sent all its boot-time messages.
              ESP starts reading UART RX buffer — it's empty (all messages were sent
              before driver was installed).
              
              U16 has now transitioned to "waiting for ESP" state, expecting BOOT
              within a short window (perhaps 200-500ms from power-on).

  t = ~500ms  ESP setup() runs: we send BOOT → KEEPALIVE → STATE(0).
              U16 receives BOOT. But:
                a) U16's BOOT timeout may have already expired (hypothesis: 100-200ms)
                b) U16 has already finished its initialization and is running steady-state
                c) BOOT at this point is ignored or has a side effect (silence)

              If (a) → U16 entered fallback mode (echo/silent) before BOOT arrived.
              If (b) → U16 is in normal steady state but BOOT is unexpected.
              
              Either way: U16 processes BOOT and possibly transitions to a state
              where the internal link to U13 was never properly initialized.
              U16 is now in a mode where it doesn't echo AND doesn't forward
              data to/from U13. Result: silence.

  t = 2s      loop() starts sending POLL (every 100ms) + KEEPALIVE (every 200ms).
              U16 receives these but discards them (in wrong state / no U13 link).

  t = 5s      We send INFO. U16 receives it, but no change.

  t = 8s      We send INIT (init=3). U16 receives it, but no change.

  After 8s:  We're stuck in a loop of POLL+KEEPALIVE that U16 ignores.
              MB never sends data again. Zero RX forever.
```

**The timing mismatch is the strongest candidate.** The original capture shows:
- MB sends first power-on at ~index 0 (within ms of power-up)
- ESP sends BOOT at ~index 122 (~40-60ms later)
- ESP sends INFO and INIT within ~100ms of BOOT

We are 10-100× slower at every step.

### Why old firmware (no boot sequence) worked differently

Without the boot sequence, we only sent POLL and KEEPALIVE starting at 2s.
U16's behavior:
1. U16 boots, sends power-on + init sequence (unheard by us)
2. U16 finishes init, enters "waiting for BOOT" state with a timeout
3. Timeout expires (~200ms) → U16 enters fallback/echo mode
4. U16 now echoes everything it receives
5. We send POLL at 2s → U16 echoes it back
6. We see the echo on RX
7. Circular communication with no real state progression

Adding BOOT to this mix:
1-3 same as above
3b. Before timeout expires (~150ms), we send BOOT at our ~500ms — wait, timeout expired at ~200ms
3c. U16 already in fallback mode when BOOT arrives at ~500ms
4. BOOT received in fallback mode → U16 tries to exit fallback and enter normal mode
5. But U16 already missed the window to init U13 link → normal mode is broken
6. U16 stops echoing (because it's no longer in fallback mode)
7. U16 also can't communicate because U13 link never initialized
8. Result: total silence

**This explains ALL symptoms:**
- Old firmware: echo (because we never sent BOOT, U16 stayed in fallback)
- New firmware: silence (because BOOT arrives after timeout, exits fallback into broken normal mode)
- Cold boot works without rollback (ESPHome safe-mode timer resets at 60s, unrelated to U16 state)

### Predictions (to verify with LA or code change)

| # | Prediction | Test |
|:-:|-----------|------|
| P1 | Our first TX (BOOT) appears on wire between 300-800ms after power-on | LA timestamp of first D1 pulse |
| P2 | MB sends data on wire before our first TX, but we don't see it in ESP logs | LA shows D0 activity before first D1 |
| P3 | U16 sends nothing AFTER our BOOT (no D0 activity after first D1 pulse) | LA shows D0 quiet after first D1 |
| P4 | If we remove BOOT from setup(), echo returns (U16 stays in fallback) | Code change: comment out `send_boot()`, reflash, observe RX |
| P5 | If we add INFO+INIT immediately after BOOT (no delay), U16 might respond | Code change: call `send_esp_info()` and `send_init()` right after `send_esp_state()` in setup() |
| P6 | Adding a power-on delay on the ESP (wait for MB to finish boot handshake) doesn't help | We already boot as fast as possible; the problem is we need to be FASTER

| # | Question | How to resolve |
|:-:|----------|---------------|
| 1 | Does the MB actually transmit anything on power-up when connected to our ESPHome firmware? | **Logic analyzer on J2 connector** (probe both MB→ESP and ESP→MB from power-on) |
| 2 | Did we miss the initial power-on message because UART wasn't ready? | LA will show if MB sends anything before our first TX |
| 3 | Is our TX waveform correct (voltage levels, timing, baud rate)? | LA decode of our TX |
| 4 | Does the MB respond at all to our BOOT/KEEPALIVE/STATE? | LA will show if MB sends any data after our TX |
| 5 | Does `init=3` matter vs `init=2`? | Compare captures, both observed — `init=3` used in original |
| 6 | Is the delayed INFO/INIT causing MB to timeout? | Try sending INFO+INIT immediately at boot (match original timing) |
| 7 | What would happen if we DON'T send BOOT and just send the delayed sequence? | Test without boot sequence (restore old behavior) — should get echo back |

## Gemini Report Analysis (`esphome-gemini-report.md`)

Analiza raportu wygenerowanego przez Gemini, który czytał cały kod i przechwyty.

### Co raport ma dobrze (zweryfikowane przez LA)

| # | Claim | Evidence | Action taken |
|:-:|-------|----------|:------------:|
| 1 | **Brak `\n` w ramkach** — U16 może czekać na newline zanim przetworzy komendę | D1 (ESP→MB) ma 87× `0x0A` (LF) — każda ramka kończy się `\n`. D0 (MB→ESP) ma 0 LF — MB nie używa line terminators | `send_json()` dodaje `\n` |
| 2 | **Brak prefiksu `#&`** — każda ramka ESP→MB zaczyna się od `#&{"cmd":...}` | Wszystkie 236 ramek na D1 mają `#&` przed JSONem. Wyjątek: pierwsza ramka w nagraniu ma tylko `&` (bo `#` był w poprzedniej, przed triggerem) | `send_json()` dodaje `#&` |
| 3 | **PIN deadlock przez `power_ready_`** — flagę ustawia POWER_READY (0x20000004), który MB wysyła bardzo wcześnie, zanim UART ESP jest gotowy | POTWIERDZONE: POWER_READY w oryginalnym nagraniu pojawia się w pierwszych 20ms, ESP UART gotowy ~300-500ms | PIN przeniesiony do `setup()` bez warunku `power_ready_` |
| 4 | **INFO/INIT za późno (5s/8s)** — oryginalny ESP wysyła w ~100ms po BOOT | POTWIERDZONE: w nagraniu BOOT→INFO→INIT w odstępie ~100ms | `setup()` wysyła INFO+INIT natychmiast po BOOT |
| 5 | **2s ciszy w POLL** — `now > 2000` opóźnia pierwszy POLL o 2s | POTWIERDZONE: w loop() był warunek `if (now > 2000 ...)` | Usunięty — POLL startuje od razu z `boot_sent_` |
| 6 | **Potrzebny `\r\n` (CR+LF)** | **FAŁSZ** — oryginał używa samego `\n` (LF=87, CR=0) | Użyto `\n` bez `\r` |
| 7 | **Cała sekwencja w setup() z delay(50)** | Kierunek dobry, 6×50ms=300ms OK, ale skrócono do 6×20ms=120ms dla bezpieczeństwa WDT | 20ms między ramkami |

### Dokładny format ramki (zweryfikowany hexdumpem D1)

```
Każda ramka:  # & { " c m d " : 1 2 3 , . . . } \n
              ^ ^                                   ^
              | |                                   └─ 0x0A (LF) — terminator linii
              | └─ 0x26 (&) — prefix ramki
              └─ 0x23 (#) — separator między ramkami

Pierwsza ramka w secji:  & { " c m d " : ... } \n
                         ^ (bez '#') — '#' był końcem poprzedniej sekcji

Hex przykład (BOOT):  26 7B 22 63 6D 64 22 3A 31 30 37 33 37 34 31 38 32 38 7D 0A
                      &  {  "  c  m  d  "  :  1  0  7  3  7  4  1  8  2  8  }  \n

Hex przykład (druga): 23 26 7B 22 63 6D 64 22 3A 38 30 35 33 30 36 33 37 33 7D 0A
                      #  &  {  "  c  m  d  "  :  8  0  5  3  0  6  3  7  3  }  \n
```

### Wdrożone zmiany w kodzie

| Plik | Funkcja | Zmiana |
|------|---------|--------|
| `snk_mower.cpp:321` | `send_json()` | Format: `#&JSON\n` (było: goły JSON; potem `JSON\r\n` bez prefiksu) |
| `snk_mower.cpp:151` | `setup()` | Sekwencja: BOOT→KEEP→STATE→WIFI→INFO→INIT→PIN (było: BOOT→KEEP→STATE, reszta z opóźnieniem w loop()) |
| `snk_mower.cpp:152-164` | `setup()` | delay(20) między ramkami, łącznie ~120ms (było: delay(10) × 2 tylko dla BOOT+KEEP+STATE) |
| `snk_mower.cpp:477` | `loop()` | Usunięto blokadę PIN (`power_ready_`), INFO (5s), INIT (8s) — flagi ustawione w setup() |
| `snk_mower.cpp:491-519` | `loop()` | POLL startuje od razu, dodano `boot_sent_` guard do wifi/info/state/rain |

## Next Steps (prioritized)

### Jutro z LA na J2:
1. **Potwierdzić że nasz TX ma `#&JSON\n`** i jest odbierany poprawnie przez U16
2. **Zobaczyć czy MB w ogóle nadaje coś po power-on** (czy zdążymy odczytać pierwsze komunikaty MB)
3. **Zweryfikować czy MB odpowiada po naszej sekwencji boot**
4. **Sprawdzić czy PIN_RESULT wraca**

### Jeśli nadal zero RX:
5. Sprawdzić czy `#` przed `&` jest faktycznie potrzebne (spróbować bez `#`, samo `&JSON\n`)
6. Sprawdzić baud rate z dokładnością — LA pokaże rzeczywisty baud U16
7. Sprawdzić czy U16 wysyła w ogóle cokolwiek na UART po resecie (może być martwy lub w bootloaderze)

## UART Connection Reference (PCB)
```
ESP32 (SNK_DISPLAY_CP_V11)      Mainboard (via J2)
┌─────────────┐                ┌──────────────┐
│ GPIO17 (TX) ──────────────────→ U16 RX      │
│ GPIO16 (RX) ←────────────────── U16 TX      │
│ GND         ──────────────────  GND         │
└─────────────┘                │              │
                                │ U16 (GD32F303)│
                                │   ↕ UART     │
                                │ U13 (main MCU)│
                                └──────────────┘

J2 pinout (top to bottom, looking at ESP board):
  Pin 1: RX (ESP RX = U16 TX)  ← orange wire in LA
  Pin 2: TX (ESP TX = U16 RX)  ← green wire in LA
  Pin 3: GND

Note: J8 connector (display ↔ mainboard) has different pinout:
  J8 was used for LA captures, but J2 is the direct ESP↔MB connection.
  We should probe J2 for our debugging — no display board in circuit.
```
