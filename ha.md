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
| 8 | UART baud rate 115200 8N1 | ✅ Cross-referenced ESP32+U16 | No communication |
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
│  │ UART (GPIO13/15, 115200 8N1, binary protocol)        │   │
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
