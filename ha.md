# Home Assistant Integration — SNK Mower ESPHome

## Status: ⚠️ Boot handshake works — display dark, protocol unknown

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

- Display (LED 7-segment) — never reliably lit up. See [Display test history](#display-test-history) below.
  - CLK=5, MOSI=32 confirmed as working data pins (210 scan) but no protocol works
  - 74HC595 shift register protocol likely wrong — probably **MAX7219** (standard LED 7-segment driver, 3-wire), I²C or main MCU controls display
  - GPIO34/39 are input-only (silent gpio_set_level failure)
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

## Local 7-segment LED Display (NOT LCD)

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
| 2026-06-20 | lcd_find — **210 test restored** (0xFF,0x0F,0xFF, 1.5s/phase) | Wszystkie 7 kandydatów | **✅ CLK=5, MOSI=32 działają!** | #39: CLK=5 CS=34 MOSI=32 → lewa "8". #46: CLK=5 CS=39 MOSI=32 → "_8:8_". #81: CLK=18 CS=33 MOSI=32 → "EE:EE". |

**Przełom: CLK=5 i MOSI=32 potwierdzone!**
- Oba działające combos mają CLK=5, MOSI=32 — to są prawidłowe piny
- CS = 34 lub 39 (oba **input-only** na ESP32!) — podejrzane. Może CS nie jest potrzebny (2-wire protocol)?
- GPIO34 i GPIO39 nie mogą być ustawione jako output — gpio_set_level na nich nie działa
- Mimo to, różne wartości CS dają różne wyniki — CS pin wpływa na wyświetlacz, ale nie bezpośrednio
- **Wniosek: protokół może być 2-przewodowy (CLK + DATA), a CS jest niepodłączony; różne wyniki dla różnych CS wynikają z innego stanu pinów po poprzednich testach**

| 2026-06-20 | shift24_nocs — CLK=33, MOSI=18, CS=32, CS=0, FFFFFF | 33, 18, 32 | **❌ Ciemno** | shift24_nocs() nie dotyka CS w ogóle. CS=0. Replika pierwszego lcd_find combo #4 — bez efektu. |
| 2026-06-20 | shift24_nocs — CLK=5, MOSI=32 (prawidłowe piny), bez CS | 5, 32, NC | **❌ Ciemno** | CLK=5, MOSI=32 z 210 testów, shift24_nocs, CS nie używany. Nic. |

**Wnioski końcowe (2026-06-20):**
- **CLK=5, MOSI=32** — prawidłowe piny danych (potwierdzone 210 scan)
- **CS=34/39 input-only** — gpio_set_level fails silently. W 210 scan CS=34/39 "działały" bo CS nie był faktycznie toggle'owany, co przypadkiem pomogło
- **shift24_nocs()** (bez CS toggle) też nie działa — problem leży gdzie indziej
- **CLK=33, MOSI=18** też nie działa — 8888 z pierwszego testu był najprawdopodobniej przypadkowy (power-on glitch lub interference z main MCU)
- **Hipoteza: protokół to NIE 74HC595** — mimo że na flexie są 3×74HC595, mogą one być sterowane przez main MCU (U16/U13), a ESP32 nie ma bezpośredniego dostępu do ich linii kontrolnych
- **Alternatywa: MAX7219** — popularny driver 7-segment, ta sama 3-przewodowa magistrala, ale inny format danych (16-bitowe słowa z adresami rejestrów). Warto spróbować.
- **Alternatywa: I²C** (HT16K33) — możliwe że 74HC595 są po stronie main MCU, a ESP32 komunikuje się przez I²C
- **GPIO34/39 input-only** — nie da się na nich ustawić outputu. Wyjaśnia to wszystkie "false positive" wyniki z 210 scan.

**Co dalej:**
1. Wypróbować protokół MAX7219 (16-bitowe słowa, rejestry 0x1-0x8 dla digitów, 0x9 decode, 0xA brightness, 0xB scan limit, 0xC shutdown)
2. Wypróbować I²C (HT16K33) na podejrzanych pinach
3. Uruchomić tryb nasłuchu GPIO (pins as inputs, log changes) — sprawdzić czy main MCU steruje wyświetlaczem
4. Jeśli wszystko zawiedzie — zaakceptować że ESP32 nie może sterować wyświetlaczem bezpośrednio


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
