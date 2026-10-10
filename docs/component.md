# ESPHome component `snk_mower`

The custom component in [`components/snk_mower/`](../components/snk_mower/) replaces the original display-board firmware: it keeps the link to the mainboard (U13) alive, unlocks it with the PIN, drives the display, buzzer and buttons, reads the rain sensor and exposes the mower to Home Assistant. A complete configuration is [`esphome/snk-mower.yaml`](../esphome/snk-mower.yaml); installation is in [install.md](install.md).

It needs ESP-IDF (`framework: type: esp-idf`) and a UART at 230400 baud on GPIO17 (TX) / GPIO16 (RX).

```yaml
external_components:
  - source: github://neutrinus/lux-tools-rmr300@main:components
    components: [snk_mower]

uart:
  id: mower_uart
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 230400
  rx_buffer_size: 512

snk_mower:
  id: my_mower
  uart_id: mower_uart
  pin: !secret mower_pin
  buzzer_pin: 27
  rain_pin: 36
  mower_state:
    name: "State"
  battery_level:
    name: "Battery"
```

## Options

| Option | Default | |
|---|---|---|
| `pin` | **required** | The mower's 4-digit PIN, as a string |
| `display_clk`, `display_mosi`, `display_cs` | 33, 25, 32 | Display shift-register pins |
| `display_off_timeout` | 0 (never) | Minutes of inactivity after which the display goes blank (not while mowing, returning, charging or in error) |
| `display_night_brightness` | 20 | Display brightness in night mode, % |
| `buzzer_pin` | — | 27 on the display board. Without it the buzzer is silent |
| `rain_pin` | — | 36 on the display board. Without it the rain sensor is off and "dry" is reported |
| `rain_drive_a`, `rain_drive_b` | 18, 5 | Rain electrode drive pins |
| `rain_threshold` | 3000 | ADC average (0–4095) above which the sensor is dry |

## Entities

All entities are optional; add the ones you want with at least a `name`. Standard ESPHome entity options (`disabled_by_default`, `entity_category`, filters, …) apply.

| Key | Type | Content |
|---|---|---|
| `mower_state` | text | `idle`, `mowing`, `trimming`, `returning`, `charging`, `docked`, `error`, `locked`, `unknown` |
| `battery_level` | sensor, % | Battery charge |
| `error_code` | sensor | Mainboard error bitmask, 0 = no error |
| `raining` | binary | Rain sensor wet |
| `is_mowing`, `is_charging`, `is_docked`, `is_returning` | binary | Derived from the state (mowing includes edge trimming; docked includes charging) |
| `cut_area`, `work_area` | sensor, m² | Diagnostic |
| `total_minutes`, `on_minutes` | sensor, duration | Operating time counters; Home Assistant can show them in hours or days. Diagnostic |
| `rain_delay` | sensor, duration | Rain delay set on the mower. Diagnostic |
| `bat_health` | sensor, % | Diagnostic |
| `light_level` | sensor | Mainboard light level. Diagnostic |
| `rain_adc` | sensor | Raw rain-sensor average. Diagnostic |
| `device_name`, `model`, `serial`, `firmware_version`, `battery_name` | text | Mainboard device info. Diagnostic |

## Actions

Call them from lambdas (`id(my_mower).start_mowing();`):

| Method | Effect |
|---|---|
| `start_mowing()` | START, then OK: `0x10000007`, `0x10000001` |
| `return_to_dock()` | HOME, then OK: `0x10000007`, `0x10000002` |
| `stop_mowing()` | Remote stop `0x10000023` |
| `trim_edge()` | Edge trim `0x10000015` (only from the station) |
| `key_start()`, `key_home()`, `key_ok()` | Front buttons: START/HOME arm a 3 s window, OK confirms. Wire them to `gpio` binary sensors on GPIO22/21/19 (`INPUT_PULLUP`, inverted) |
| `set_display_night(bool)` | Night mode on/off |
| `send_raw_json(json)` | Sends any JSON object as a frame (debugging) |

## What it does

- **Link**: sends ESP_BOOT and polls at start, answers the mainboard's boot handshake, then sends a keepalive every 500 ms and the WiFi/BT status every second, like the original. A timer keeps the keepalive going while the main loop is blocked (e.g. during an OTA upload), so the mainboard never sees a gap of 3 s. Echoes of its own frames during mainboard boot are ignored.
- **PIN**: sent when the mainboard asks for it (`{"lock":1}`) and once at link up; up to five retries if rejected, then the state shows `locked`.
- **Display**: shows `boot`, then the state (`IdLE`, `Mow`, `Cut`, `HoME`, `ChAr`, `dock`, `Err` + code, `LoCK`), alternating with the battery percentage while mowing or charging; `byE` on power-off. Multiplexed at 2 ms per digit from a hardware timer, like the original.
- **Buzzer**: short beeps on key presses, a beep when mowing starts, a long one on errors.
- **Rain**: same measurement and thresholds as the original firmware; the state goes to the mainboard as `0x22000000` (1 dry, 2 raining), which starts the mainboard's own rain delay.
- **Logging**: at DEBUG every non-periodic frame is logged as `TX {...}` / `RX {...}`, and the first 80 frames after boot are printed again 60 s after boot (the API log connects too late to see them). Mainboard log lines (`0x15000001`) appear as `MB log: ...`. Link statistics every 10 s at VERBOSE.

The protocol itself is documented in [protocol.md](protocol.md).
