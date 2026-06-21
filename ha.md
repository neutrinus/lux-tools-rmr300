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
| GPIO5 | 29 | R28 → U6 (oznaczenia zatarte) — FW wskazuje CLK, ale nie działa na naszej płycie |
| GPIO34 | 6 | ❌ input-only — R26 → chipy wyświetlacza (U1/U3/U4) |
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

**Uwaga:** HARDWARE.md podaje CLK=18, CS=5, MOSI=23 ale **display nigdy nie wyświetlił ani znaku** — te piny są niepotwierdzone. U1/U3/U4 mają **zatarte / laserowo zmazane oznaczenia** — nie wiadomo co to za układy. Hipoteza że to 74HC595 pochodzi z HARDWARE.md, ale nie ma pewności. Ścieżki od tych układów idą z grupy **GPIO25,33,32,34,SENSOR_VN(39)** — to tam trzeba szukać CLK/CS/MOSI.

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
| 2026-06-20 | **lcd_sweep — CLK=18, MOSI=32, CS=33 (test #81)** — 29 faz × 3-4s | 18, 32, 33 | **❌ Kompletnie ciemno** | Najbardziej wiarygodny test: 24 pojedyncze bity, ALL_ON, ALL_OFF, 3 testy bajtowe. Żaden nie zapalił ani segmentu. SWEEP logi potwierdzają że każda faza była wysyłana. |

**Wnioski końcowe (2026-06-20) — po teście lcd_sweep:**

- **CLK=18, MOSI=32, CS=33 (test #81 z 210 scan) też NIE działa** — "EE:EE" z oryginalnego testu było power-on glitch, tak jak "8888"
- **NIGDY nie zapaliliśmy wyświetlacza** — żadna kombinacja pinów ani protokołów nie dała kontrolowanego, powtarzalnego rezultatu
- **MAX7219 wykluczony** — ma 24 piny, nasze układy U1/U3/U4 są SOP-16
- **Układy U1/U3/U4 mają zatarte oznaczenia — nie wiemy co to za chipy** — ani normalny shift24 (z CS), ani shift24_nocs, ani sweep bitowy nie dał reakcji
- **Możliwe wyjaśnienia:**
  1. **ESP32 NIE steruje wyświetlaczem** — U1/U3/U4 przy wyświetlaczu są sterowane przez main MCU (U16/U13) przez ścieżki na wewnętrznych warstwach PCB, nie przez ESP32. ESP32 może tylko podsłuchiwać albo w ogóle nie ma połączenia.
  2. **Protokół nie jest SPI/bit-bang** — może I²C, lub równoległy (LCD_CAM I80 jak w oryginalnym FW), ale nie mamy odpowiednich pinów GPIO do I80.
  3. **Piny są prawidłowe ale wyświetlacz wymaga inicjalizacji main MCU** — OE lub MR może być sterowany przez main MCU, i dopiero po jego inicjalizacji ESP32 może wysyłać dane.
- **Konieczne: tryb nasłuchu GPIO** — ustawić podejrzane piny display jako input, logować zmiany podczas normalnego bootu (z main MCU). Jeśli main MCU steruje wyświetlaczem, zobaczymy aktywność na liniach.

## KROK 0 — lcd_sweep na CLK=5, MOSI=32, CS=25, boot_delay=10

| Data | Test | Piny (CLK, CS, MOSI) | Wynik | Uwagi |
|------|------|----------------------|-------|-------|
| 2026-06-20 | lcd_sweep — 29 faz (24 bit + ALL_ON + ALL_OFF + 3 byte) | 5, 25, 32 | **❌ Kompletnie ciemno** | CLK=5, MOSI=32 najsilniejsi kandydaci, CS=25 z FW decomp. Żadna z 29 faz nie zapaliła ani jednego segmentu. Potwierdza że (a) coupling-latch z 210-scan był artefaktem, a nie kontrolą, (b) CS=25 nie działa, (c) być może układy to nie 74HC595 lub OE/MR blokuje wyjścia. |

**Wnioski po KROK 0:**
- CLK=5, MOSI=32 — wciąż silni kandydaci (dają coupling w 210-scan), ale nie mamy kontroli
- CS=25 — **obalony** empirycznie (testowany z poprawnym wzorcem 0xFF,0x0F,0xFF i toggle CS)
- Potrzebne: (a) lcd_latch_scan z szerszym zbiorem CS {2,21,22,23,25,33}, (b) analiza disasm (gpio_matrix_out z LCD_CAM), (c) weryfikacja czy U1/U3/U4 to faktycznie 74HC595

**Co dalej:**
1. **Analiza disasm** — grep gpio_matrix_out z LCD_CAM (DATA0=100, WR=128, CS=130, RS=126) w esp32/firmware/disasm.s
2. **lcd_latch_scan** — tryb skanujący CS {2,21,22,23,25,33} z CLK=5, MOSI=32 + 0xFF,0x0F,0xFF + fix sticky state + test OE/MR przez RTC pull na GPIO34/39
3. **Identyfikacja chipów U1/U3/U4** — ustalić czy to 74HC595 (SOP-16, pin 16=VCC, pin 8=GND), czy coś innego (TM1638, MBI5168, I²C GPIO expander)
4. **Tryb nasłuchu GPIO** (pin_diag) — ustawić podejrzane piny display jako input, logować zmiany podczas normalnego bootu


## PRZEŁOM I OSTATECZNE ROZSTRZYGNIĘCIE (2026-06-20)

W wyniku precyzyjnego wizualnego śledzenia ścieżek na PCB wyświetlacza (zdjęcia `PXL_20260616_120305142 (2).jpg` oraz `PXL_20260620_182450200.jpg`) oraz analizy dekompilacji, **zagadka została w 100% rozwiązana**:

| Parametr | Rola | Połączenie fizyczne na PCB |
|----------|------|----------------------------|
| **SCLK (Clock)** | **GPIO33** | ESP32 Pin 9 -> `R33` -> pod `U3` -> `SH_CP` (Pin 11) wszystkich `74HC595` |
| **CS / Latch** | **GPIO32** | ESP32 Pin 8 -> `R31` -> pod `U3` -> `TP27` -> `ST_CP` (Pin 12) wszystkich `74HC595` |
| **MOSI (Data)** | **GPIO25** | ESP32 Pin 10 -> `R34` -> `TP29` -> przelotka -> `DS` (Pin 14) pierwszego rejestru `U1` |
| **Master Reset (MR)** | — | Pin 10 rejestrów `U1/U3/U4` -> pull-up przez `R3` do linii 3.3V (VCC) przy `C16`. Sprzętowo zablokowany reset (stale nieaktywny). |
| **Output Enable (OE)** | — | Pin 13 rejestrów `U1/U3/U4` -> bezpośrednio do masy (GND). Wyjścia są stale aktywne. |

### Dlaczego poprzednie testy oszukiwały?
Podczas testu kombinacji `{33, 32, 18}` (`CLK=33`, `CS=32`, `MOSI=18`), pin `GPIO25` (prawdziwe dane) wisiał w powietrzu i z powodu braku pull-downa/szumów przesyłał ciągły stan wysoki (`1`). Zegar i latch działały prawidłowo, więc w rejestry wsuwały się same jedynki (`0xFFFFFF`), co przy poprawnym clock/latch rozświetlało cały wyświetlacz jako `8888` / `EE:EE`. Jednak próba wyświetlenia konkretnego tekstu (który sterował niewłaściwym pinem `GPIO18`) skutkowała dalszym sypaniem samych jedynków z wiszącego pinu `25`, uniemożliwiając świadome sterowanie.

### Korekta błędów w HARDWARE.md:
| HARDWARE.md twierdzi | Faktycznie | Status |
|----------------------|------------|:------:|
| OK=GPIO33 | OK=**GPIO19** | ✅ Potwierdzone |
| Buzzer=GPIO12 | Buzzer=**GPIO27** | ✅ Potwierdzone |
| ON=GPIO27 | GPIO27 to buzzer, ON nie na ESP | ✅ Potwierdzone |
| START=GPIO26 | **NC** — START nie na display board | ✅ Potwierdzone |
| MOSI=GPIO23 | MOSI=**GPIO25** | ✅ Potwierdzone |
| CLK=GPIO18 | CLK=**GPIO33** | ✅ Potwierdzone |
| CS=GPIO5 | CS=**GPIO32** | ✅ Potwierdzone |

## WYNIKI EKSPERYMENTU SWEEP NA FIZYCZNYCH PINACH (2026-06-20)

Wgrano oprogramowanie z pinami: `CLK=33`, `MOSI=25`, `CS=32`. Wynik:
* **Fazy 1-24 (pojedyncze bity):** Kompletnie ciemno.
* **Faza 25 (ALL_ON, `0xFFFFFF`):** Wyświetlacz **rozświetlił się w całości** (wszystkie segmenty i cyfry)!
* **Faza 26 (ALL_OFF, `0x000000`):** Wyświetlacz natychmiast zgasł.

**Interpretacja sprzętowa (Przełom fizyczny):**
Wyświetlacz jest multipleksowany, a wspólne katody/anody cyfr są kluczowane przez **tranzystory inwertujące** (ULN2003 / PNP). 
Aby zapalić segment, na wyjściu rejestru `74HC595` musi pojawić się `1` (HIGH) dla segmentu ORAZ `1` (HIGH) dla aktywacji cyfry (która po inwersji daje niskie wspólne ujście prądu). 
Podczas faz 1-24 (single-bit) nigdy nie występowały jednocześnie te dwa warunki (mieliśmy tylko jedną jedynkę w całej 24-bitowej ramce), dlatego wyświetlacz był ciemny. Faza `ALL_ON` podała jedynki wszędzie i zapaliła całość.

## URUCHOMIENIE NORMALNEGO WYŚWIETLANIA (2026-06-20)

Wgrano oprogramowanie w trybie normalnym (`lcd_sweep: false`):
1. **Faza rozruchu:** Wyświetlacz zaczął prawidłowo mrugać dwoma myślnikami `----` (podczas PRE-handshake).
2. **Faza po handshaku:** Wyświetlacz zaczął mrugać wartością `73` (poziom naładowania baterii pobrany z płyty głównej kosiarki!).
3. **Zidentyfikowany problem:** Widoczne mruganie/migotanie z częstotliwością ok. 20-30Hz (wynika z cooperative multitaskingu ESPHome w `loop()`, opóźnianego przez VERBOSE logging i obsługę sieci).

## ROZWIĄZANIE PROBLEMÓW I PEŁNA KONTROLA 4 SEGMENTÓW (2026-06-21)

W wyniku dalszych eksperymentów i analizy zachowania wyświetlacza oraz komunikacji UART po ostatnich aktualizacjach oprogramowania, dokonaliśmy następujących przełomowych odkryć:

### ⚠️ KOREKTA ORIENTACJI WYŚWIETLACZA (2026-06-21)
Użytkownik zorientował się, że przez cały czas patrzył na kosiarkę **do góry nogami** (przyciski są pod ekranem, a nie nad). Wszystkie wcześniejsze obserwacje "lewa/prawa strona" są odwrócone.
- **Poprawione rozumienie:** To, co nazywaliśmy "lewymi dwoma segmentami" (Digit 0, 1) to w rzeczywistości **prawe dwa segmenty wyświetlacza** (fizyczne pozycje 2, 3).
- **Konsekwencja:** Cyfry `"1"` i `"2"` w Approach 3 pojawiły się na **prawej** stronie wyświetlacza — blisko przycisków.
- **`b1=0x04` i `b1=0x08` sterują PRAWĄ stroną** wyświetlacza (fizyczne pozycje 2 i 3).
- **Lewa strona (fizyczne pozycje 0 i 1) NIGDY się nie zaświeciła** w żadnym teście — musi być sterowana przez `b0` (rejestr U4), ale nie znamy jeszcze właściwych bitów.

### 1. Rozwiązanie blokady UART RX
Wprowadzone wcześniej wysyłanie ramek `POLL` podczas startowego opóźnienia (`boot_delay`) w celu uniknięcia watchdog-cuta z płyty głównej zawierało błąd logiki: pętla `loop()` wykonywała wczesny powrót (`return;`), całkowicie omijając odczytywanie portu UART RX.
- **Problem:** ESP32 wysyłał zapytania, płyta główna na nie odpowiadała, a nieodczytywany bufor RX ESP32 ulegał całkowitemu przepełnieniu (overflow). Powodowało to stałe zablokowanie procesu handshake w fazie `BootPhase::PRE`.
- **Rozwiązanie:** Usunięto wczesny powrót (`return;`) podczas oczekiwania startowego. Bufor RX jest teraz odczytywany na bieżąco od samego początku, a handshake przebiega natychmiast i bezbłędnie. Zwiększono domyślny `boot_delay` w `snk-mower.yaml` do 30 sekund w celu zapewnienia bardzo bezpiecznego okna dla OTA.

### 2. Podział sterowania wyświetlaczem (U3 = prawa strona, U4 = lewa strona)
Wcześniejsze próby wyświetlania kończyły się ciemną prawą stroną wyświetlacza. Po korekcie orientacji:
- **Ustalono:** Rejestr `U3` (bajt `b1`) steruje **prawymi dwoma fizycznymi pozycjami** (poz. 2 i 3).
- **Fakty:** `b1=0x04` → pozycja fizyczna 2 (druga od prawej), `b1=0x08` → pozycja fizyczna 3 (skrajnie prawa).
- Rejestr `U4` (bajt `b0`) musi sterować **lewymi dwoma fizycznymi pozycjami** (poz. 0 i 1).
- Bity `0x01`, `0x02`, `0x04`, `0x08` na `b0` (testowane w Approach 1) **nie aktywują żadnych pozycji** — lewa strona potrzebuje innych bitów.

### 3. Eksperyment Diagnostyczny: DISPLAY TEST — wyniki po korekcie orientacji (2026-06-21)
Obserwacje z poprawną orientacją (przyciski na dole):

| Approach | `b0` (U4) | `b1` (U3) | Wynik (poprawna orientacja) |
|----------|-----------|-----------|-----------------------------|
| 0 (b1=dig) | 0x00 | dig=1<<i | **Dwa prawe: "4 3"** (poz.3='4' b1=0x08, poz.2='3' b1=0x04) |
| 1 (b0=dig) | dig=1<<i | 0x00 | **Nic** — b0 nie zawiera właściwych bitów |
| 2 (sym A) | {0,0,0x02,0x04} | {0x02,0x04,0,0} | **Skrajny prawy: "2"** (poz.2='2' b1=0x04, poz.3 ciemna) |
| 3 (sym B) | {0,0,0x04,0x08} | {0x04,0x08,0,0} | **Dwa prawe: "2 1"** (poz.3='2' b1=0x08, poz.2='1' b1=0x04) |
| 4 (sym C) | {0,0,0x01,0x02} | {0x02,0x04,0,0} | **Skrajny prawy: "2"** (poz.2='2' b1=0x04) |

**Wnioski:**
- **Potwierdzone mapowanie b1 (U3):** `0x04` → poz.2, `0x08` → poz.3 (prawe pozycje).
- **`b1=0x01` i `b1=0x02` nie aktywują żadnych widocznych pozycji** — te bity nie są podłączone do tranzystorów cyfr.
- **`b0` (U4) nie został jeszcze poprawnie zmapowany** — lewa strona (poz. 0 i 1) pozostała ciemna we wszystkich testach.

### 4. Celowany test B0 SWEEP (2026-06-21) — wersja 2
Celem jest znalezienie bitów `b0`, które aktywują lewe fizyczne pozycje (poz. 0 i 1).

**Poprawiona konstrukcja testu:**
- **Prawa strona (referencja):** Stabilne `"12"` na pozycjach 2 i 3 przez `b1=0x04` i `b1=0x08`.
- **Lewa strona — dwie fazy po 8 kroków (łącznie 16 × 5s = 80s cykl):**
  - **Faza 0 (kroki 0-7):** Test pozycji 0 — cyfra `'3'` z `b0 = 1<<bit_idx`; pozycja 1 wyłączona (segment=0).
  - **Faza 1 (kroki 8-15):** Test pozycji 1 — cyfra `'4'` z `b0 = 1<<bit_idx`; pozycja 0 wyłączona (segment=0).
- **Logowanie:** `B0 SWEEP: phase=X bit=Y b0=0xZZ`
- **Cel:** Gdy test trafi na właściwy bit, z lewej strony pojawi się cyfra `"3"` (faza 0) lub `"4"` (faza 1).

**Obserwacje użytkownika:** Patrząc na wyświetlacz prawą stroną do góry:
- Po prawej stronie widać stabilne `"12"` — to potwierdza działanie multipleksera.
- W pewnym momencie po lewej stronie (fizycznie z dala od przycisków) pojawi się `"3"` lub `"4"`.
- Należy zapisać, w którym kroku (0-7 dla '3', 8-15 dla '4') pojawia się cyfra.

## Key files

| File | Purpose |
|------|---------|
| `components/snk_mower/snk_mower.cpp` | Main component: JSON encode/decode, UART framing, CRC, boot sequence, display refresh with proper latching |
| `components/snk_mower/snk_mower.h` | Header with command constants, class definition |
| `captures/README.md` | All captured commands with hex/decimal IDs |
| `decomp/` | U13 firmware decompilation (1672 functions) |
| `notes/U16.md` | U16 firmware analysis (FreeRTOS, sensors, motors, JSON) |
| `notes/ESP32.md` | Original ESP32 firmware analysis |
| `notes/GD32F305.md` | U13 main MCU analysis |
