# Home Assistant Integration — SNK Mower ESPHome

## Status: ✅ Boot handshake works — system stable 30+ s

Boot sequence resolved 2026-06-19:

| Phase | Trigger | ESP sends | MB sends |
|-------|---------|-----------|----------|
| PRE | 0ms | BOOT → KEEPALIVE → STATE → RAIN → POLL@30ms + KEEPALIVE@1s | — |
| SYNC | `0x330000A1` (DEVICE_INFO) | ESP_INFO×5 + INIT×6 in ~465ms | DEVICE_INFO(name/SN), HW_VERSIONS |
| DONE | INIT×6 sent | KEEPALIVE@1s + PIN once + periodic ESP_INFO@30s | HEARTBEAT, STATUS, RTC@1Hz, PIN_RESULT, LIGHT, MAP |

Key insight: MB sends `0x330000A1` as a **request** for ESP identification.  
When ESP has preemptively sent INFO, MB sends `0x330000A9` instead.

## Protocol (confirmed by LA capture + CRC verification)

### Physical layer
- UART @ **230400 8N1**, standard polarity
- ESP TX=GPIO17, ESP RX=GPIO16
- Frame: `&<json>{crc_byte>#` (Dallas CRC-8 over JSON bytes)
- Max message: 128 bytes (mport driver limit on U16)

### Verified CRCs (Dallas CRC-8 MAXIM)

| Command | JSON | CRC |
|---------|------|:---:|
| BOOT (0x40000004) | `{"cmd":1073741828}` | 0x0A |
| KEEPALIVE (0x30000005) | `{"cmd":805306373}` | 0x0A |
| STATE (0x30000028) | `{"cmd":805306408,"state":0}` | 0x59 |
| POLL (0x300000A1) | `{"cmd":805306529}` | 0xD1 |
| WIFI (0x30000021) | `{"cmd":805306401,"wifi":1,"str":1}` | 0x1E |
| BT (0x30000022) | `{"cmd":805306402,"bt":0,"str":0}` | 0xBA |

All match original ESP captures.

### Architecture

```
ESP32 (ESPHome, custom snk_mower component)
  │
  │ UART @230400, JSON+CRC via J2 connector
  │ Frame: &JSON{CRC}#
  │
  ▼
U16 (GD32F303) — board MCU, translates JSON ↔ internal
  │
  │ UART @230400, JSON via cJSON
  ▼
U13 (GD32F305) — main MCU (motor, sensors, PIN, USB, OTA)
```

### Boot sequence (original capture)

```
MB→ESP:
  0x20000001 action=0          POWER_ON
  0x40000009 ×5                BOOT_HEART
  0x40000008 ×5                BOOT_INIT
  0x50000021 bat=3             BATTERY
  0x20000004 ×2                POWER_READY
  0x330000A1                   DEVICE_INFO (name, SN, version, model)
  0x330000A2                   HW_VERSIONS
  0x330000B0                   MAP_CFG
  0x40000020 lv=255            LIGHT
  0x330000A6                   SCHEDULE
  0x330000A0 state=0           STATUS (IDLE/LOCKED)
  0x41000002 lock=1            LOCK
  0x33000021 result=True       PIN_RESULT
  0x330000A0 state=1           STATUS (UNLOCKED/READY)
  0x40000011 rtc=TS...         RTC_HEARTBEAT (~1 Hz)

ESP→MB:
  0x40000004                   BOOT
  0x30000005                   KEEPALIVE
  0x30000028 state=0           STATE
  0x300000A1 ×N                POLL (keepalive)
  0x30000021 wifi=0,str=0      WIFI
  0x30000022 bt=0,str=0        BT
  0x40000006 hv=...,sv=...,mac=... INFO
  0x40000001 init=3            INIT
  0x30000005 ×60               KEEPALIVE
```

### Current problems

| Problem | Symptom | Status |
|---------|---------|--------|
| U16 rejects frames | `0x15000001` | Rare; not seen in recent logs |
| MB kills power after ~7-8s | ESP loses power | ✅ **Fixed** — boot handshake completes in ~2-3s |

### MB Supervisor Timer (Watchdog)

Mainboard U13 ma ~7-8s okno nadzoru: jeśli ESP nie odpowie (brak BOOT/KEEPALIVE), U13 odcina zasilanie płytki display. To zabija ESP podczas OTA (10-20s) — watchdog ubija ESP w trakcie flashowania.

**Mechanizm:** watchdog zaczyna się dopiero po handshaku. Jeśli ESP nie wyśle BOOT, MB nie wie o ESP i nie nadzoruje zasilania.

**Workaround OTA:**
- `boot_delay: 10` — opóźnia handshake o 10s po resecie, dając czas na OTA zanim watchdog wystartuje
- `safe_mode: disabled` — ESP musi wystartować normalnie, nie w safe mode, by handshake zadziałał (w safe mode nie ma komunikacji UART, watchdog ubija)
- W oknie opóźnienia OTA może się połączyć i rozpocząć flashowanie bez ryzyka

**Tryb pin_diag:** całkowicie pomija handshake → watchdog nie startuje → ESP bezpieczne nawet bez boot_delay. Przydatne do długich testów GPIO.

### What's confirmed

- CRC algorithm: ✅ Dallas CRC-8 (matches original ESP captures)
- Frame format: ✅ `&JSON{CRC}#`
- UART baud: ✅ 230400 8N1
- RX parser: ✅ receives JSON frames from MB
- PIN: ✅ stored in U13 RAM, value `9633`
- Protocol: ✅ JSON
- Boot handshake: ✅ PRE → SYNC → DONE completes successfully
- System stability: ✅ 40+ seconds and counting (no power cut)
- PIN accepted: ✅ MB confirms PIN

### What's NOT yet working

- Display — never lit up empirically. See [Display test history](#display-test-history) below.
- Buttons (START/HOME/ON) — tylko OK na GPIO19 potwierdzony. Reszta prawdopodobnie przez UART (CMD_EXEC_ACTION = 0x41000003 od MB)
- Mowing start: not yet tested via HA

## ESPHome Entities

### Sensor

| Entity ID | Type | Source | Value |
|-----------|------|--------|-------|
| `sensor.mower_battery_level` | sensor | STATUS | 0–100% |
| `sensor.mower_battery_voltage` | sensor | STATUS | mV |
| `sensor.mower_error_code` | sensor | STATUS/ERROR_NOTIFY | numeric code |

### Binary Sensor

| Entity ID | Source | ON = true |
|-----------|--------|-----------|
| `binary_sensor.mower_is_mowing` | STATUS | Mowing active |
| `binary_sensor.mower_is_charging` | STATUS | On charger |
| `binary_sensor.mower_is_docked` | STATUS | At station |
| `binary_sensor.mower_has_error` | STATUS | Error or locked |
| `binary_sensor.mower_rain_detected` | **GPIO36** ✅ | Rain sensor — reaguje na mokry palec |
| `binary_sensor.mower_start_button` | GPIO26? | ✗ nie działa — czeka na pin_diag |
| `binary_sensor.mower_home_button` | GPIO25? | ✗ nie działa — czeka na pin_diag |
| `binary_sensor.mower_ok_button` | **GPIO19** ✅ | Potwierdzony pin_diag |

### Text Sensor

| Entity ID | Source | Values |
|-----------|--------|--------|
| `text_sensor.mower_status` | STATUS | `unknown`, `idle`, `mowing`, `returning`, `charging`, `docked`, `error`, `locked` |

### Button

| Entity ID | UART Cmd | HA service |
|-----------|----------|------------|
| `button.mower_start_mowing` | 0x300000A6? | `button.press` |
| `button.mower_return_to_dock` | 0x300000A6? | `button.press` |

## Local 7-segment Display

| State | Display | Behavior |
|-------|---------|----------|
| Unknown/booting | `----` | All off |
| Idle | `IdLE` | Steady |
| Mowing | `bXX` | Battery %, e.g. `b85` |
| Returning | `HoME` | Steady |
| Charging | `ChAr` | Steady |
| Docked | `IdLE` | Steady |
| Error | `Err` | Permanently |
| Locked | `LoCK` | Permanently |

## Home Assistant — Template Lawn Mower

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

## Schedule & Time

Original schedule lived entirely in ESP32 NVS. ESPHome replaced it — use HA automations instead:

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

RTC time is set via ESPHome NTP. U13 has its own RTC but doesn't check time for anything.

## PIN Handling

PIN (`9633`) is stored in U13 RAM (address `0x2000027C`) and EEPROM U22.
ESP sends `{"cmd":1090519045,"pwd":9633}` (PIN_SEND) on boot.
MB auto-verifies and sends `PIN_RESULT`.

## Bluetooth Proxy

ESP32-WROOM-32UE supports BT 4.2 BR/EDR + BLE. Enable via `bluetooth_proxy:` in YAML. Shared IPEX antenna.

## UART Connection Reference

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
```

## ESP32-WROOM-32UE Pinout (SNK_DISPLAY_CP_V11)

```
         ┌────────────────────────────────┐
         │  ESP32-WROOM-32UE              │
         │  (top view, USB-UART on top)   │
         ├────────────────────────────────┤
         │  1 GND       38 GND            │
         │  2 3V3       37 GPIO23         │
         │  3 EN        36 GPIO22         │
         │  4 SENSOR_VP 35 U0TXD          │
         │  5 SENSOR_VN 34 U0RXD          │
         │  6 GPIO34    33 GPIO21         │
         │  7 GPIO35    32 NC             │
         │  8 GPIO32    31 GPIO19         │
         │  9 GPIO33    30 GPIO18 ← CLK   │
         │ 10 GPIO25    29 GPIO5  ← CS    │
         │ 11 GPIO26    28 GPIO17 → TX    │
         │ 12 GPIO27    27 GPIO16 → RX    │
         │ 13 GPIO14    26 GPIO4          │
         │ 14 GPIO12    25 GPIO0          │
         ├──────────┬───┴──────────┬──────┤
         │ 15 GND   │ 16-24 (bttm) │      │
         │          │ 16 NC        │      │
         │          │ 17 GPIO13    │      │
         │          │ 18 GND       │      │
         │          │ 19 GPIO02    │      │
         │          │ 20 GPIO15    │      │
         │          │ 21 NC        │      │
         │          │ 22 NC        │      │
         │          │ 23 NC        │      │
         │          │ 24 NC        │      │
         └──────────┴──────────────┴──────┘
```

**Znane połączenia (zweryfikowane wizualnie i/lub empiricalnie):**

| GPIO | Pin | Połączenie |
|------|-----|-----------|
| GPIO33 | 9 | **Display CLK** ✅ (empirycznie potwierdzone) |
| GPIO32 | 8 | **Display CS** ✅ (empirycznie potwierdzone) |
| GPIO18 | 30 | **Display MOSI** ✅ (empirycznie potwierdzone) |
| GPIO5 | 29 | R28 → U6 (74HC595) — FW wskazuje CLK, ale nie działa na naszej płycie |
| GPIO34 | 6 | ❌ input-only — R26 → chipy wyświetlacza |
| GPIO25 | 10 | R34 → TP29 → przelotka — FW wskazuje CS, ale nie działa |
| GPIO17 | 28 | UART TX do mainboard ✅ |
| GPIO16 | 27 | UART RX do mainboard ✅ |
| GPIO39 (SENSOR_VN) | 5 | Ścieżka do chipów wyświetlacza (NC?) |
| GPIO36 (SENSOR_VP) | 4 | **Rain sensor J4** ✅ — reaguje na wilgoć (DIAG + binary_sensor) |
| GPIO35 | 7 | R27 → J2 → mainboard (ADC, input-only) |
| GPIO27 | 12 | **Buzzer** ✅ — DC active buzzer, przelotka w wewnętrznych warstwach |
| GPIO19 | 31 | C13 → **OK button** ✅ (pin_diag: 1→0 przy naciśnięciu) |
| GPIO21 | 33 | C10 → znika (nieznane) |
| GPIO23 | 37 | Prawy górny róg — **NIE działa jako MOSI display** |
| GPIO2 | 19 | Przelotka → znika (spód modułu) |
| GPIO22 | 36 | Nieznane |

**Uwaga:** HARDWARE.md podaje CLK=18, CS=5, MOSI=23 ale **display nigdy nie wyświetlił ani znaku** — te piny są niepotwierdzone. Faktyczne ścieżki do 74HC595 (U1/U3/U4) idą z grupy **GPIO25,33,32,34,SENSOR_VN(39)** — to tam trzeba szukać CLK/CS/MOSI.

**Potwierdzone NC (nic niepodpięte):**
| GPIO | Pin | Uwagi |
|------|-----|-------|
| GPIO4 | 26 | NC |
| GPIO0 | 25 | NC (strapping, boot) |
| GPIO12 | 14 | NC (HARDWARE.md błędnie podawał buzzer) |
| GPIO13 | 17 | NC (spód modułu) |
| GPIO14 | 13 | NC |
| GPIO15 | 20 | NC (spód modułu) |
| GPIO26 | 11 | NC (HARDWARE.md błędnie podawał START) |

## Display test history

| Data | Test | Piny (CLK, CS, MOSI) | Wynik | Uwagi |
|------|------|----------------------|-------|-------|
| 2026-06-19 | lcd_find — 7 kandydatów × 3 role = 210 permutacji | Wszystkie kombinacje {5,18,25,32,33,34,39} | CLK=5, CS=**34**, MOSI=32 (#39/343) — **false positive** | GPIO34 input-only, nie może być CS. Display mógł błysnąć przez coupling. |
| 2026-06-19 | lcd_find zwężony do {18,25,33} dla CS, CLK=5, MOSI=32 | 60 permutacji | Żadna kombinacja nie zapaliła wyświetlacza | GPIO25 testowane jako CS — brak reakcji. |
| 2026-06-19 | lcd_find — 6 permutacji {18,33,32} | {33, 32, 18} **(combo #4) = CLK=33, CS=32, MOSI=18** | **✅ Wyświetlacz zapalił się (bzdura)** | Jedyne empiricalne potwierdzenie. CLK=33, MOSI=18 — odwrotność FW (CLK=5, MOSI=32). Być może inna rewizja PCB. |
| 2026-06-19 | lcd_find — 6 permutacji {5,25,32} (z FW decompilacji) | 6 permutacji {5,25,32} | **Brak reakcji** | FW wskazuje CLK=5, MOSI=32, CS=25 — nie działa. |
| 2026-06-19 | Normal tryb po lcd_find: CLK=5, MOSI=32, CS=25 | 5, 32, 25 | **Brak reakcji** | `Display initialized (CLK=5, MOSI=32, CS=25)` — display refresh w loop, cisza. |

**Empirycznie potwierdzona kombinacja: CLK=GPIO33, CS=GPIO32, MOSI=GPIO18**

**Wnioski:**
- **CLK=33, MOSI=18, CS=32** — jedyna kombinacja, która kiedykolwiek zapaliła wyświetlacz
- FW analysis wskazuje CLK=5, MOSI=32, CS=25 — nie zgadza się z naszym PCB (może inna rewizja SNK_DISPLAY_CP)
- GPIO5 i GPIO25 **NIE są RCLK** (testowane z rising i falling edge)
- Stan power-on wyjść 74HC595 to `8888` (wszystkie segmenty)
- Po teście RCLK, `loop()` wszedł w boot phase state machine (boot_delay expired) → display zapalił `8888`
- **Nowa hipoteza:** CS=32 to **RCLK** (latch), a OE jest na stałe do GND (always active). Poprzednie testy zawodziły bo single-bit patterns nie wybierały cyfry (b1=0x00).

| Data | Test | Piny (CLK, MOSI, CS) | Wynik | Uwagi |
|------|------|----------------------|-------|-------|
| 2026-06-20 | lcd_find_rclk — GPIO5 i GPIO25 jako RCLK z falling edge | CLK=33, MOSI=18, CS=32 | **❌ Żaden nie zadziałał** | Display ciemny przez cały test. Po teście zaskoczył 8888. |
| 2026-06-20 | lcd_find_rclk — transparent mode (CS=0 cały czas, bez latch pulse) | CLK=33, MOSI=18, CS=32 | **❌ Nie działa** | Transparent ALL_ON/ALL_OFF nie pokazały nic. |
| 2026-06-20 | lcd_find_rclk — **glitch minimal test 5 faz** (CS=0, inline shift bez CS toggle) | CLK=33, MOSI=18, CS=32 | **❌ Display ciemny przez cały test** | CS=0 nie włącza OE. `setup_display()` re-init też nie daje glitcha (bo CS=1 w new code). |
| 2026-06-20 | lcd_find_rclk — **replicate v3: shift24(FFFFFF) exact + rapid refresh** | CLK=33, MOSI=18, CS=32 | **❌ Display ciemny przez cały test. Crash w fazie 3-4** | Nawet DOKŁADNA replikacja oryginalnego testu (shift24 FFFFFF + CS=1) nie zapaliła LCD. Watchdog crash od 100× shift24. |

**Wnioski:**
- **Żaden test od oryginalnego lcd_scan nie zapalił LCD** — 8888 było najprawdopodobniej stanem power-on po resecie kontrolera
- Nasze GPIO nie sterują wyświetlaczem, tylko przypadkowo zresetowały kontroler przy pierwszym teście
- **Możliwe: U16/GD32 steruje wyświetlaczem przez te same piny, a ESP jest w trybie INPUT** (nasłuchuje)
- Potrzebny test: słuchać na pinach CLK=33, MOSI=18, CS=32 jako INPUT i zobaczyć czy U16 coś wysyła

**Co dalej:**
1. **Słuchanie pinów** — ustawić CLK, MOSI, CS jako INPUT i logować zmiany przez 10s
2. Jeśli U16 wysyła dane → zdekodować protokół i dostosować nasze wysyłanie
3. Jeśli cisza → wyświetlacz jest nieaktywny (może kontroler odłączony lub niezasilany)
4. **Timing ma znaczenie** — oryginalny test miał inny timing (init + shift + 4s wait) niż nasze testy

**Co dalej:**
1. Zreplikować DOKŁADNIE oryginalny lcd_scan (przeczytać kod i powtórzyć)
2. Spróbować ciągłego odświeżania (tight loop co 10ms)
3. Spróbować innych protokołów (TM1637, MAX7219)


**Błędy w HARDWARE.md zweryfikowane empiricalnie:**
| HARDWARE.md twierdzi | Faktycznie |
|----------------------|------------|
| OK=GPIO33 | OK=**GPIO19** ✅ |
| Buzzer=GPIO12 | Buzzer=**GPIO27** ✅ |
| ON=GPIO27 | GPIO27 to buzzer, ON nie na ESP |
| START=GPIO26 | **NC** — START nie na display board |
| MOSI=GPIO23 | MOSI=**GPIO32** ✅ |
| CLK=GPIO18 | CLK=**GPIO5** ✅ |
| CS=GPIO5 | CS=??? (GPIO34 ❌ input-only) — do znalezienia |

Uwaga: ESP32-WROOM-32UE nie ma wyprowadzonych GPIO36/GPIO39/GPIO37/GPIO38 na zewnętrzne piny.

## Key files

| File | Purpose |
|------|---------|
| `components/snk_mower/snk_mower.cpp` | Main component: JSON encode/decode, UART framing, CRC, boot sequence |
| `components/snk_mower/snk_mower.h` | Header with command constants, class definition |
| `captures/README.md` | All captured commands with hex/decimal IDs |
| `decomp/` | U13 firmware decompilation (1672 functions) |
| `notes/U16.md` | U16 firmware analysis (FreeRTOS, sensors, motors, JSON) |
| `notes/ESP32.md` | Original ESP32 firmware analysis |
| `notes/GD32F305.md` | U13 main MCU analysis |
