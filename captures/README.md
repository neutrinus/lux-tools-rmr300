# UART captures

## Setup
- **Probes:** J8 connector (display → mainboard)
  - CH1/D0: orange (→) — MB → ESP (U16 TX)
  - CH2/D1: green (←) — ESP → MB (ESP TX)
  - CH3/D2: blue (START button)
  - GND: black
- **Logic analyzer:** CY7C68013A (FX2 clone), sigrok fx2lafw driver
- **Sampling:** 2 MHz, VCD output
- **UART:** 230400 8N1, standard polarity (not inverted)

## Analysis workflow
```bash
# Decode JSON from both channels
sigrok-cli -i capture.vcd \
  -P "uart:baudrate=230400:data_bits=8:parity=none:stop_bits=1.0:rx=D0:format=hex" \
  -A "uart=rx-data"

# Or use the tool
python3 tools/decode_capture.py captures/XX-scenario/capture.vcd
```

## Scenario index

| # | Scenario | File | Duration | Size | Description |
|---|----------|------|----------|------|-------------|
| 01 | **boot** | `01-boot/capture.vcd` | 1 min | 944 KB | Cold boot, auto PIN, lock → ready, RTC heartbeat |
| 02 | **boot-pin** | `02-boot-pin/capture.vcd` | ~2 min | 876 KB | Boot + PIN + START → error (bench) |
| 03 | **return-home** | `03-return-home/capture.vcd` | ~30s | 132 KB | HOME in error state, E11 shown |
| 04 | **return-home2** | `04-return-home/capture.vcd` | ~2 min | 708 KB | Full boot + PIN + START + HOME + error |
| 05 | **shutdown** | `05-shutdown/capture.vcd` | ~1 min | 482 KB | Error state → set date/time/hours → shutdown |
| 06 | **settings-all** | `06-settings-all/capture.vcd` | ~3 min | 1.9 MB | Full menu: all settings explored |

## Planned scenarios

| # | Scenario | Mower action | Status |
|---|----------|-------------|--------|
| 01 | Boot (cold) | Connect battery, wait for full boot | ✅ captured |
| 02 | Boot + PIN + START | Boot, enter PIN, press START | ✅ captured |
| 03 | HOME in error | Press HOME during error state | ✅ captured |
| 04 | Retry after error | Clear error, try again | ✅ captured |
| 05 | Shutdown | Error → settings → power off | ✅ captured |
| 06 | Full menu | All settings: date, time, schedule, PIN, rain | ✅ captured |
| 06 | Return to dock | HOME during mowing | ❌ pending (dock) |
| 07 | Dock + charge | Charging cycle | ❌ pending (dock) |
| 08 | Error codes | Various error states | ❌ pending |

## Protocol summary

### Command ID structure (from observed prefixes — directions cross-verified)

| Prefix | Prefix (hex) | Source | Description |
|--------|-------------|--------|-------------|
| `0x10xxxxxx` | 0x10000000 | ESP→MB | Error acknowledges |
| `0x20xxxxxx` | 0x20000000 | MB→ESP | Power/action notifications |
| `0x22xxxxxx` | 0x22000000 | ESP→MB | Sensor data (rain — sensor on display board) |
| `0x30xxxxxx` | 0x30000000 | ESP→MB | Settings, keepalive, WiFi/BT status, config |
| `0x31xxxxxx` | 0x31000000 | ESP→MB | Settings menu control |
| `0x33xxxxxx` | 0x33000000 | MB→ESP | Configuration, state reports, device info |
| `0x40xxxxxx` | 0x40000000 | Both | System/heartbeat — direction zależy od sub-ID |
| `0x41xxxxxx` | 0x41000000 | MB→ESP | Lock, exec_action, error, shutdown, start_ack, home, docked |
| `0x50xxxxxx` | 0x50000000 | MB→ESP | Battery info |

> **UWAGA**: Powyższe kierunki zostały zweryfikowane krzyżowo z captures 01-06 (D0=MB→ESP, D1=ESP→MB)
> i captures 2026-06-21 (D1=ESP TX, D2=MB TX — etykiety w README w tym katalogu były odwrócone).
> Pełna dokumentacja: [PROTOCOLS.md](../PROTOCOLS.md)

### Key command IDs

| Hex | Decimal | Name | Fields | Direction |
|-----|---------|------|--------|-----------|
| `0x20000001` | 536870913 | POWER_ON | `action` | MB→ESP |
| `0x20000004` | 536870916 | POWER_READY | — | MB→ESP |
| `0x22000000` | 570425344 | RAIN | `rain` | ESP→MB |
| `0x30000005` | 805306373 | ESP_KEEPALIVE | — | ESP→MB |
| `0x30000021` | 805306401 | ESP_WIFI | `wifi, str` | ESP→MB |
| `0x30000022` | 805306402 | ESP_BT | `bt, str` | ESP→MB |
| `0x30000028` | 805306408 | ESP_STATE | `state` | ESP→MB |
| `0x300000A1` | 805306529 | ESP_POLL | — | ESP→MB |
| `0x300000A6` | 805306534 | ESP_TRIM | `trim, schedule...` | ESP→MB |
| `0x300000A7` | 805306535 | ESP_RAIN_CFG | — | ESP→MB |
| `0x300000A8` | 805306536 | ESP_MULTIZONE | — | ESP→MB |
| `0x33000021` | 855638049 | PIN_RESULT | `result` | MB→ESP |
| `0x33000022` | 855638050 | PIN_RESULT2 | `result` | MB→ESP |
| `0x330000A0` | 855638176 | STATUS | `state, bat_lv, bat_per, ...` | MB→ESP |
| `0x330000A1` | 855638177 | DEVICE_INFO | `name, sn, version, model, ...` | MB→ESP |
| `0x330000A2` | 855638178 | HW_VERSIONS | `mb_hv, mblt_sv, bb_hv, ...` | MB→ESP |
| `0x330000A6` | 855638182 | SCHEDULE | `trim, auto, sun_st, ...` | MB→ESP |
| `0x330000A7` | 855638183 | RAIN_CFG | `rain_en, rain_delay` | MB→ESP |
| `0x330000A8` | 855638184 | MULTIZONE | `mul_en, zones...` | MB→ESP |
| `0x330000AA` | 855638186 | UNKNOWN | — | MB→ESP |
| `0x330000B0` | 855638192 | MAP_CFG | `map_sn, area` | MB→ESP |
| `0x40000001` | 1073741825 | ESP_INIT | `init` | ESP→MB |
| `0x40000004` | 1073741828 | ESP_BOOT | — | ESP→MB |
| `0x40000006` | 1073741830 | ESP_INFO | `hv, sv, mac` | ESP→MB |
| `0x40000008` | 1073741832 | BOOT_INIT | — | MB→ESP |
| `0x40000009` | 1073741833 | BOOT_HEART | — | MB→ESP |
| `0x40000011` | 1073741841 | RTC_HEARTBEAT | `rtc` | MB→ESP |
| `0x40000014` | 1073741844 | UNKNOWN | — | MB→ESP |
| `0x40000020` | 1073741856 | LIGHT | `lv` | MB→ESP |
| `0x40000021` | 1073741857 | BOOT_ACK | — | MB→ESP |
| `0x41000002` | 1090519042 | LOCK | `lock` | MB→ESP |
| `0x41000003` | 1090519043 | EXEC_ACTION | — | MB→ESP | Execute queued button action |
| `0x41000004` | 1090519044 | ERROR_NOTIFY | `err` | MB→ESP | Error code notification |
| `0x41000005` | 1090519045 | PIN_SEND | `pwd` | ESP→MB | Send 4-digit PIN |
| `0x41000020` | 1090519072 | START_ACK | `result` | MB→ESP | START button acknowledged |
| `0x10000001` | 268435457 | ESP_ERR_ACK1 | — | ESP→MB | Error acknowledge |
| `0x10000002` | 268435458 | ESP_ERR_ACK2 | — | ESP→MB | Error acknowledge |
| `0x10000007` | 268435463 | ESP_ERR_ACK7 | — | ESP→MB | Error acknowledge |
| `0x41000008` | 1090519048 | SHUTDOWN | — | MB→ESP | Power-off command |
| `0x40000013` | 1073741843 | CUT_TIME_QUERY | `len` | MB→ESP | Query daily cutting time (min) |
| `0x30000006` | 805306374 | SETTING_MODE | — | ESP→MB | Enter settings mode |
| `0x30000007` | 805306375 | SETTING_APPLY | — | ESP→MB | Apply/confirm setting |
| `0x30000011` | 805306385 | SET_YEAR | `year` | ESP→MB | Set RTC year |
| `0x30000012` | 805306386 | SET_DATE | `month, day` | ESP→MB | Set RTC month/day |
| `0x30000013` | 805306387 | SET_TIME | `hour, minute` | ESP→MB | Set RTC time |
| `0x30000015` | 805306389 | SET_DAILY_HOURS | `hour` | ESP→MB | Set daily mowing hours |
| `0x31000016` | 822083606 | SETTING_START | — | ESP→MB | Enter settings menu |
| `0x33000011` | 855638033 | RTC_SET_OK | `result` | MB→ESP | RTC time set confirm |
| `0x33000012` | 855638034 | DATE_SET_OK | `result` | MB→ESP | Date set confirm |
| `0x33000013` | 855638035 | SCHED_SET_OK | `result` | MB→ESP | Schedule set confirm |
| `0x33000015` | 855638037 | HOURS_SET_OK | `result` | MB→ESP | Daily hours set confirm |
| `0x50000021` | 1342177313 | BATTERY | `bat` | MB→ESP |
