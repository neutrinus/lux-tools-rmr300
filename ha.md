# Home Assistant Integration — SNK Mower ESPHome

## Status: ✅ Full integration working — display optimized with hardware SPI

All 4 digits driven by U3 (b1). Colon hardware-powered. Decimal points not present/identified. Boot sequence resolved 2026-06-19:

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

- **Decimal points** — not found on seg bits (0x01–0x80) nor on U4 (b0 0x00–0xFF). May not exist physically.
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

**Display driver summary (discovered 2026-06-21):**
- Pins: CLK=GPIO33, MOSI=GPIO25, CS=GPIO32 — 3× 74HC595 daisy-chained
- Data order: shift24(b0=U4, b1=U3, b2=U1) — U1 drives segment lines, U3 drives digit select, U4 unused for digits
- Digit select (U3/b1): `{0x20, 0x10, 0x08, 0x04}` — all 4 digits via U3, bit5→bit2 left→right
- Segments (U1/b2): standard 7-segment a=bit0…g=bit6. Bit7 (DP) not connected.
- Colon: hardware-powered (stays lit whenever display active). Not controlled by any b0 bit in isolation.
- Decimal points: not found on seg bits 0–7 nor on U4 bits 0–7.

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

### 2. Podział sterowania wyświetlaczem (U3 = prawa strona, U4 = lewa strona — HIPOTEZA ODRZUCONA)
Wcześniejsze próby wyświetlania kończyły się ciemną prawą stroną wyświetlacza. Po korekcie orientacji:
- **Ustalono:** Rejestr `U3` (bajt `b1`) steruje **prawymi dwoma fizycznymi pozycjami** (poz. 2 i 3).
- **Fakty:** `b1=0x04` → pozycja fizyczna 2 (druga od prawej), `b1=0x08` → pozycja fizyczna 3 (skrajnie prawa).
- **Początkowa hipoteza:** Rejestr `U4` (bajt `b0`) musi sterować lewymi dwoma pozycjami. **Późniejsze testy obaliły tę hipotezę** — wszystkie 4 cyfry są na U3 (b1), a U4 nie steruje żadną cyfrą.
- Bity `0x01`, `0x02`, `0x03`, `0x04`, `0x08` na `b0` (**nie aktywują żadnych pozycji**). Ostatecznie odkryto brakujące bity na U3: `b1=0x10` i `b1=0x20`.

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

### 4. Celowany test B0 SWEEP — WYNIK: LEWE NIGDY SIĘ NIE ZAŚWIECIŁY (2026-06-21)
Mimo pełnego skanowania wszystkich 8 pojedynczych bitów `b0` (0x01–0x80), **żaden nie aktywował lewych segmentów**. U4 (`b0`) nie steruje tranzystorami cyfr.

### 5. LEFT SIDE SWEEP — PRZEŁOM! (2026-06-21)
**Wynik:** Phase 0 (`b1=0x10` dla poz.0, `b1=0x20` dla poz.1) — wyświetlacz pokazał **4321** — wszystkie 4 cyfry działają!

**KOMPLETNE MAPOWANIE WYŚWIETLACZA:**
| Pozycja fizyczna | Bit b1 (U3) | Wartość |
|------------------|-------------|---------|
| 0 (skrajnie lewa) | bit 4 | 0x10 |
| 1 (druga od lewej) | bit 5 | 0x20 |
| 2 (druga od prawej) | bit 2 | 0x04 |
| 3 (skrajnie prawa) | bit 3 | 0x08 |

- **Wszystkie 4 cyfry są sterowane przez U3 (b1).** U4 (b0) nie uczestniczy w selekcji cyfr.
- **`DIGIT_B1_MAP = {0x20, 0x10, 0x08, 0x04}`** (kolejność lewa→prawa: bit5, bit4, bit3, bit2)
- **`DIGIT_B0_MAP = {0x00, 0x00, 0x00, 0x00}`** (U4 nieużywany — colon jest hardwarowo zawsze włączony, DP nie znaleziony na seg ani na U4)
- **Dwukropek (`display_colon_`):** Stale włączony przez hardware. Ustawienie `display_colon_` na 0x30 koliduje z bitami 4 i 5 U3 i jest niepotrzebne — colon działa bez sterowania.

### 6. COLON BLANK SWEEP — WYNIK: DWUKROPEK DZIAŁA (2026-06-21)
Wykonano pełny skan 16 kombinacji `b0` (wszystkie single bity + wybrane kombinacje + 0x00 + 0xFF) na wygaszonym tle (`seg=0x80` = DP). Wynik z logu:
- **Stan początkowy:** Przy starcie zapalił się tylko dwukropek — i tak zostało. Żadna kropka ani cyfra nie zapaliła się.
- **Wniosek:** Dwukropek jest sprawny i świeci stale, gdy wyświetlacz jest aktywny. Nie reaguje na zmiany `b0` w locie — prawdopodobnie jest hardwarowo zawsze zasilany, sterowany globalnym enable, lub wymaga określonej sekwencji inicjalizacji.
- **Kropki dziesiętne (`seg=0x80`):** Nie zapaliły się — bit 7 (`0x80`) nie steruje DP w tym wyświetlaczu.
- **Dalsze kroki:** Zbadać DP na U4 (b0) poprzez sweep bitów b0 z aktywną cyfrą "8".

### 7. DP SEGMENT BIT SWEEP — WYNIK: DP NIE NA SEGMENTACH (2026-06-21)
Wykonano sweep wszystkich 8 bitów segmentu (0x01–0x80) na lewej cyfrze (`b1=0x20`, `b0=0x00`). Wynik:
- **Bity 0-6:** Zapalają standardowe segmenty 7-segmentowego wyświetlacza (a=bit0, b=bit1, c=bit2, d=bit3, e=bit4, f=bit5, g=bit6).
- **Bit 7 (0x80):** Nie zapalił żadnej kropki dziesiętnej.
- **Wniosek:** DP nie jest na żadnym bicie segmentu (U1/b2).

### 8. DP B0 SWEEP — WYNIK: KROPKA NIE NA U4 (2026-06-21)
Wykonano sweep wszystkich bitów b0 (0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF) z wyświetloną cyfrą "8" (`seg=0x7F`) na lewej cyfrze (`b1=0x20`). Wynik:
- **"8" paliła się cały czas przez cały test** — standardowa multipleksacja działa.
- **Żaden bit b0 nie zapalił kropki dziesiętnej** — ani jako pojedynczy bit, ani jako 0xFF.
- **Wniosek:** DP nie jest sterowany przez U4 (b0). Możliwe wyjaśnienia:
  1. Wyświetlacz nie ma fizycznych kropek dziesiętnych (brak połączeń na PCB)
  2. DP jest na U1 (b2) ale wymaga innego bitu niż 0x80 (sprawdzone 0x01–0x80 — brak)
  3. DP jest sterowany przez inny układ spoza U1/U3/U4
  4. Kropki są połączone szeregowo przez wszystkie cyfry i wymagają jednoczesnej aktywacji wszystkich cyfr + odpowiedniego bitu segmentu
- **Dalsze kroki:** Opcjonalnie sprawdzić wariant (4) — wszystkie 4 cyfry + seg z bitem DP + różne b0. Na razie DP pozostaje nierozpoznany.

### 9. RESTORE NORMAL DISPLAY — PEŁNA KONTROLA (2026-06-21)
Przywrócono normalną logikę wyświetlania:
- `DIGIT_B1_MAP = {0x20, 0x10, 0x08, 0x04}` (lewa→prawa: bit5, bit4, bit3, bit2)
- `DIGIT_B0_MAP = {0x00, 0x00, 0x00, 0x00}` (U4 wyłączony dla cyfr)
- `display_colon_ = 0` (dwukropek stale włączony przez hardware)
- Wyświetlacz działa poprawnie: `set_display_text()`, bateria, ładowanie.

## OPTYMALIZACJA WYŚWIETLACZA - HARDWARE SPI I REDUKCJA MIGOTANIA (2026-06-21)

### Problem: Wyświetlacz zajmuje ESP32
Oryginalny kod bit-banging używał `gpio_set_level()` + `delayMicroseconds(1)` przy każdym bicie, co blokowało task timera przez ~144μs per callback (250Hz = ~36ms/s blokowania). Timer `ESP_TIMER_TASK` ma wysoki priorytet, więc blokowanie opóźniało inne timery (WiFi, keepalive, itp.).

### Optymalizacja P0: Direct GPIO Registers + volatile
**Zmiany:**
- Dodano strukturę `FastPin` z precomputed maskami rejestrów GPIO (`GPIO.out_w1ts/out_w1tc` dla GPIO<32, `GPIO.out1_w1ts/out1_w1tc` dla GPIO≥32)
- Zastąpiono `gpio_set_level()` + `delayMicroseconds()` bezpośrednimi zapisami do rejestrów (`*fp.set_reg = fp.mask`)
- Oznaczono współdzielone dane jako `volatile` (`display_segments_`, `display_colon_`, `current_digit_`, `display_off_`) - zapobiega race conditions między `loop()` a callbackiem timera
- Usunięto podwójne `setup_display()` z `finish_setup()` i martwą funkcję `refresh_display()`

**Wynik:** Callback timera: ~144μs → ~3-4μs (~40x szybciej)

### Optymalizacja P1: Hardware SPI
**Zmiany:**
- Zastąpiono bit-banging (`shift24_fast`) przez hardware SPI2 peripheral
- Konfiguracja: `spi_bus_initialize(SPI2_HOST, {.mosi=25, .sclk=33, ...}, SPI_DMA_DISABLED)`
- Device: `spi_bus_add_device(SPI2_HOST, {.clock_speed_hz=2000000, .mode=0, .spics_io_num=-1, .queue_size=1}, &spi_dev_)`
- Transfer: `spi_transaction_t trans = {.length=24, .flags=SPI_TRANS_USE_TXDATA, .tx_data={b0,b1,seg}}; spi_device_polling_transmit(spi_dev_, &trans);`
- CS kontrolowany ręcznie: `gpio_set_level(display_cs_, 0)` przed transferem, `gpio_set_level(display_cs_, 1)` po
- Usunięto `FastPin` struct, `fp_init/fp_set/fp_clr`, `shift24_fast`

**Wynik:** Zerowe obciążenie CPU - transfer przez hardware. Callback: ~3-4μs → ~1-2μs (głównie overhead API call).

### Problem migotania i rozwiązanie CS (OE)
**Objaw:** Wyświetlacz migotał nawet po optymalizacjach - widoczne "mrugnięcia" przy odświeżaniu.

**Diagnoza:** CS (GPIO32) steruje Output Enable (OE) na wyświetlaczu. Gdy CS=HIGH, wyjścia 74HC595 są wyłączone (high-impedance). W oryginalnym kodzie bit-banging CS szło HIGH po każdym transferze, więc wyświetlacz był wygaszony przez ~99% czasu między odświeżeniami.

**Próby rozwiązania:**
1. **CS=LOW na stałe** - wyświetlacz pokazywał tylko "b" (niepoprawne dane) - CS musi być strobowany per transfer żeby latchować dane
2. **CS kontrolowany ręcznie (HIGH między transferami, LOW tylko podczas shiftowania)** - poprawne działanie, minimalne migotanie

**Finalne rozwiązanie:** CS jest kontrolowany ręcznie - HIGH między transferami, LOW tylko podczas `spi_device_polling_transmit()`. To zapewnia poprawne latchowanie danych przy minimalnym czasie wygaszenia.

**Dodatkowe optymalizacje migotania:**
- `DISPLAY_REFRESH_MS`: 4ms → 2ms (500Hz total, 125Hz per digit zamiast 250Hz/62.5Hz)
- SPI clock: 1MHz → 2MHz (transfer 24 bitów: 24μs → 12μs)

**Status:** Migotanie zredukowane do minimum. Dalsza redukcja wymagałaby DMA (nie używamy - `SPI_DMA_DISABLED`) lub wyższej częstotliwości odświeżania (1ms), ale 500Hz/2MHz jest wystarczające dla akceptowalnego obrazu. Oryginalny firmware producenta nie migotał wcale - prawdopodobnie używał DMA lub dedykowanego hardware (LCD_CAM peripheral).

## WYNIKI EKSPERYMENTU — START MOWING I PRZYCISK FIZYCZNY (2026-06-21)

### Cel
Sprawdzić czy ESP może zainicjować koszenie (przez HA lub przycisk START) i czy fizyczny przycisk START generuje ruch na UART.

### Co zrobiono
1. **Naprawiono boot handshake** — dodano flagę `device_info_received_`, usunięto blokujący guard `&& boot_delay_ms_ == 0` w `handle_device_info()`. Bez tej poprawki ESP nigdy nie wychodził z fazy PRE.
2. **Zaimplementowano `send_trim()`** — wysyła ESP_TRIM (0x300000A6) z harmonogramem natychmiastowego koszenia na wszystkie 7 dni.
3. **Zaimplementowano `start_mowing()`** — sekwencja: ESP_TRIM + error ack (ESP_ERR_ACK1) + STATE(state=2).
4. **Zaktualizowano YAML** — dodano wszystkie sensory, binary_sensory, text_sensory, przyciski (Start Mowing, Return to Dock, OK Button).
5. **Domyślne piny wyświetlacza** zmieniono na CLK=33, MOSI=25, CS=32.
6. Dodano SNTP do synchronizacji czasu.
7. Kompilacja: `esphome compile` — OK.

### Wyniki testów użytkownika (2026-06-21, kompilacja z GitHub z nowym kodem)

| Aspekt | Status | Szczegóły |
|--------|--------|-----------|
| Boot handshake | ✅ Działa | PRE → SYNC → DONE → PIN accepted → state=idle. Logi czyste. |
| Wszystkie sensory w HA | ✅ Działają | Bateria 42%, area 300m², itd. |
| OK Button (GPIO19) | ✅ Działa | `binary_sensor` pokazał ON przy naciśnięciu |
| `start_mowing()` przez HA | ✅ Wykonany, ❌ Nie uruchomił koszenia | Kod wykonał się: ESP_TRIM + ESP_ERR_ACK1 + ESP_STATE(state=2). Mower pozostał w idle. |
| Fizyczny przycisk START | ❌ Brak ruchu UART | W logach ESP nie widać żadnych ramek START_ACK / EXEC_ACTION z MB po naciśnięciu START. |
| `return_to_dock()` | ❌ Bez efektu | ESP wysłało 0x10000001, MB nie zmieniło stanu. Mower pozostał w idle. API disconnect zaraz po — niepewne czy związane. |

### Co się stało po `start_mowing()` z HA

Sekwencja wysłana przez ESP (13:56:41):
1. `ESP_TRIM` (0x300000A6) z harmonogramem natychmiastowym (sun_st=836, czyli 13:56)
2. `ESP_ERR_ACK1` (0x10000001)
3. `ESP_STATE(state=2)` (mowing)

Reakcja MB (13:56:41):
- `Schedule: trim=36 auto=0 pause=0` — **MB odpowiedziało swoim własnym harmonogramem**, a nie potwierdzeniem naszego. Oznacza że MB ma własny wewnętrzny harmonogram (trim=36, start=570=9:30, len=120) z auto=0 (wyłączony).
- Stan pozostał `idle` — MB zignorowało `state=2` od ESP.

**Wniosek:** MB ma własny harmonogram w pamięci i nie nadpisuje go tym z ESP_TRIM (lub ignoruje ESP_TRIM gdy auto=0). ESP_TRIM może działać tylko jeśli MB ma ustawione `auto=1`.

### Kluczowe obserwacje

1. **ESP_ERR_ACK1 (0x10000001) wysłany bez widocznego efektu** — MB nie zmieniło stanu. Prawdopodobnie error ack działa tylko w określonym kontekście (gdy MB oczekuje potwierdzenia błędu).
2. **ESP_STATE(state=2) zignorowane** — MB nie zmienia swojego stanu na podstawie `state` od ESP. To pole jest raczej informacyjne (ESP informuje MB o swoim stanie), a nie komenda.
3. **MB odesłało SWÓJ harmonogram** (`Schedule: trim=36 auto=0`) zaraz po otrzymaniu ESP_TRIM — to może oznaczać że MB faktycznie przetwarza ESP_TRIM, ale `auto=0` blokuje wykonanie. Wartość `trim=36` to prawdopodobnie ustawienie z menu (krawędź 36%?).
4. **Fizyczny START nie generuje żadnych ramek UART** — potwierdza architekturę: START → J8 pin6 → U16 GPIO, całość lokalnie na U16, ESP nie jest informowany.

### Co dalej

1. **Zbadać parametr `auto` w ESP_TRIM** — wysłać ESP_TRIM z `auto=1` (MB może wtedy wykonać harmonogram). Obecnie wysyłamy `auto=1` ale MB odsyła `auto=0` — może MB ma priorytet własnych ustawień.
2. **Zbadać czy MB zapamiętuje ESP_TRIM** — sprawdzić czy po resecie MB wysyła SCHEDULE z naszymi wartościami czy swoimi.
3. **Przeanalizować oryginalny firmware** — jak oryginalny ESP32 ustawiał harmonogram? Czy używał `auto=1` i czy to faktycznie uruchamiało koszenie?
4. **Sniffer UART z oryginalnym firmware** — jedyny sposób by zobaczyć jak oryginalny ESP32 komunikował się z MB przy starcie koszenia.
5. **Przycisk START** — skoro nie generuje UART, jedyna droga przez ESP to symulacja przez U16 (J8 pin6). Ale to wymagałoby fizycznego podłączenia do pinu J8.

### Kluczowy wniosek architektoniczny
**Nie ma znanego JSON command ESP→MB które by inicjowało koszenie.** Fizyczny START jest obsługiwany wyłącznie przez U16. ESP może tylko:
- Otrzymywać notyfikacje (STATE, EXEC_ACTION, START_ACK)
- Ustawiać harmonogram (ESP_TRIM 0x300000A6) — ale MB ma własny harmonogram i może go nie nadpisywać
- Wysyłać error ack (ESP_ERR_ACK1 0x10000001 — to samo co return_to_dock)

## ANALIZA CAPTURE 04 — KLUCZOWE ODKRYCIE (2026-06-21)

### Co oryginalny ESP32 wysyłał po naciśnięciu START

Pełna analiza `captures/04-return-home/decoded.json` (293 wiadomości, 210 ESP→MB, 83 MB→ESP):

**ESP→MB — wszystkie unikalne komendy:**
| Cmd hex | Decimal | Fields | Count | Uwagi |
|---------|---------|--------|-------|-------|
| 0x40000004 | 1073741828 | — | 1 | BOOT |
| 0x30000005 | 805306373 | — | 40 | KEEPALIVE |
| 0x30000028 | 805306408 | state=0 | 1 | ESP_STATE (TYLKO RAZ, state=0!) |
| 0x300000A1 | 805306529 | — | 123 | POLL |
| 0x22000000 | 570425344 | rain=1 | 2 | RAIN |
| 0x30000021 | 805306401 | **wifi=0, str=0** | 12 | WIFI (**DISCONNECTED!**) |
| 0x30000022 | 805306402 | bt=0, str=0 | 12 | BT |
| 0x10000001 | 268435457 | — | 1 | ERR_ACK1 |
| 0x10000002 | 268435458 | — | 1 | ERR_ACK2 |
| 0x10000007 | 268435463 | — | 2 | ERR_ACK7 |
| 0x300000A6 | 805306534 | — | 1 | ESP_TRIM (**PUSTY — bez pól harmonogramu!**) |
| 0x300000A7 | 805306535 | — | 3 | ESP_RAIN_CFG |
| 0x300000A8 | 805306536 | — | 1 | ESP_MULTIZONE |
| 0x40000001 | 1073741825 | init=3 | 4 | INIT |
| 0x40000006 | 1073741830 | hv,sv,mac | 5 | ESP_INFO |
| 0x41000005 | 1090519045 | pwd | 1 | PIN_SEND |

**MB→ESP — sekwencja START:**
1. `[48] 0x41000020 result=1` — START_ACK
2. `[49] 0x330000A0 state=2` — STATUS MOWING
3. `[50] 0x41000003` — EXEC_ACTION
4. `[51] 0x330000A0 state=6` — STATUS ERROR (lift on bench)
5. `[55] 0x41000004 err=16` — ERROR_NOTIFY
6. `[56] 0x330000A0 state=7 error=16`

### KLUCZOWE WNIOSKI

1. **Oryginalny ESP32 NIE wysyłał żadnej komendy koszenia!** Po naciśnięciu START, ESP kontynuował tylko POLL/KEEPALIVE. START jest obsługiwany w 100% lokalnie przez U16 (J8 pin6 → U16 GPIO → U13).

2. **WiFi był DISCONNECTED (wifi=0, str=0)** — oryginalny firmware nie był połączony z MQTT. Wszystkie komendy "action" z dekompilacji (`{"app_main":24.125,"chedule":<value>}`) były wysyłane do chmury MQTT, NIE do MB przez UART.

3. **ESP_STATE wysłane TYLKO RAZ z state=0** — oryginał nie wysyłał cyklicznie state=1. Nasz ESPHome wysyła state=1 co 10s — może nadpisywać state=2 (MOWING) po naciśnięciu START.

4. **ESP_TRIM wysłany jako PUSTY `{"cmd":805306534}`** — bez pól harmonogramu! Harmonogram przychodzi Z MB jako SCHEDULE (0x330000A6). ESP_TRIM od ESP to tylko "odbiór" harmonogramu, nie ustawianie.

5. **Korekta wcześniejszych notatek**: ha.md linia 618 mówiła "Fizyczny START nie generuje żadnych ramek UART". **TO BYŁO BŁĘDNE** — capture 04 pokazuje że START generuje 0x41000020 (START_ACK), 0x41000003 (EXEC_ACTION), STATUS state=2. Ale to są MB→ESP (notyfikacje), nie ESP→MB (komendy).

### DLACZEGO START NIE DZIAŁA Z ESPHOME

Dwie kluczowe różnice między oryginalnym firmware a ESPHome:

| Parametr | Oryginał | ESPHome | Wpływ |
|----------|----------|---------|-------|
| `wifi` | 0 (disconnected) | 1 (connected) | MB może blokować START gdy WiFi "connected" (oczekuje komend z chmury) |
| `state` | 0, wysłany RAZ | 1, wysyłany co 10s | Cykliczne state=1 może nadpisywać state=2 (MOWING) |

### ROZWIĄZANIE: compat_mode

Dodano `compat_mode: true` w YAML które:
- Wymusza `wifi=0, str=0` (jak oryginał — disconnected)
- Wyłącza cykliczne wysyłanie ESP_STATE (tylko state=0 raz przy starcie)

To powinno pozwolić fizycznemu przyciskowi START działać z ESPHome firmware.

## TESTY KOMEND ACTION (2026-06-21)

### Komendy przetestowane przez HA (wszystkie zignorowane przez MB)

| Komenda | JSON wysłany | Reakcja MB |
|---------|-------------|------------|
| Action Mow | `{"app_main":24.125,"chedule":1}` | ❌ Ignorowane (brak `cmd` field) |
| Action Dock | `{"app_main":24.125,"chedule":4}` | ❌ Ignorowane |
| START_ACK | `{"cmd":1090519072}` | ❌ Ignorowane (to MB→ESP cmd, nie ESP→MB) |
| EXEC_ACTION | `{"cmd":1090519043}` | ❌ Ignorowane (to MB→ESP cmd) |
| CMD action=1 | `{"cmd":1090519050,"action":1}` | ❌ Ignorowane |
| CMD action=4 | `{"cmd":1090519050,"action":4}` | ❌ Ignorowane |

### Wniosek
**Nie istnieje komenda ESP→MB która uruchamia koszenie.** Oryginalny firmware też jej nie miał — START jest wyłącznie fizyczny (U16 GPIO).

Jedyna droga do koszenia z HA:
1. **Harmonogram z auto=1** — U13 ma "Robot on schedule, start work" (automatyczne koszenie)
2. **Sniffer oryginalnego firmware** — może odkryje więcej (choć oryginał też nie kosił z ESP)

## PLAN: SNIFFER ORYGINALNEGO FIRMWARE (2026-06-21)

### Cel
Podsłuchać pełną komunikację UART z oryginalnym firmware ESP32 podczas:
- Normalnego bootu
- Naciśnięcia START + OK
- Koszenia
- Powrotu do docku
- Obsługi błędów

### Hardware
1. Wgrać oryginalny firmware na ESP32 na płytce display (z dumpa `esp32_dump.bin`)
2. Dodatkowe ESP32 (sniffer) na pająku, zasilane z szpilek wbitych w J2 na display PCB
3. Sniffer podłączony do obu linii UART J2 (TX i RX)
4. Zamknięta obudowa — sniffer podsłuchuje zdalnie przez WiFi

### Co szukamy
1. Czy oryginalny ESP wysyła cokolwiek ponad POLL/KEEPALIVE/BOOT/PIN/INIT/INFO/WIFI/BT/RAIN/TRIM/RAIN_CFG/MULTIZONE
2. Czy jest jakakolwiek reakcja ESP na START_ACK / EXEC_ACTION (poza kontynuacją POLL)
3. Pełna sekwencja przy harmonogramie auto=1 (jeśli oryginał kiedykolwiek kosił automatycznie)
4. Czy są jakieś nieudokumentowane komendy w innych scenariuszach

### Ograniczenia
- Oryginalny firmware miał WiFi disconnected (wifi=0) — nie kosił z chmury
- START jest lokalny (U16) — sniffer pokaże tylko notyfikacje MB→ESP
- Jeśli nie ma ścieżki ESP→MB dla koszenia, sniffer to potwierdzi ale nie rozwiąże problemu

## Key files

### Główna przyczyna

Oryginalny firmware używa **sprzętowego peryferium LCD_CAM (I80)** z DMA do ciągłego, jitter-free odświeżania wyświetlacza — potwierdzone dekompilacją w `esp32/notes/ESP32_DECOMPILATION.md`. Nasz kod używa **software'owego timera + CPU-initiated SPI** — fundamentalnie różna architektura.

SOC ESP32 potwierdza wsparcie: `SOC_LCD_I80_SUPPORTED=true`, `SOC_LCD_I80_BUSES=2`, `SOC_LCD_I80_BUS_WIDTH=24` (z sdkconfig ESPHome).

### Konkretne problemy w `components/snk_mower/snk_mower.cpp`

1. **`ESP_TIMER_TASK` dispatch** (`snk_mower.cpp:194`) — callback działa w tasku FreeRTOS, podlega jitterowi od WiFi/BLE/loggingu innych tasków systemowych. Opóźnienie = opuszczenie klatki = migotanie.
2. **`skip_unhandled_events = true`** (`snk_mower.cpp:196`) — jeśli callback się spóźni, następny tick jest pomijany. Przeskoczona klatka = widoczne "mrugnięcie".
3. **Multiplexing 125Hz/digit, 25% duty** (`DISPLAY_REFRESH_MS = 2`, 4 cyfry): każda cyfra świeci 2ms, gaszona 6ms. Jitter w 2ms interwale = nierównomierne naświetlenie cyfr = flicker.
4. **Brak double-bufferingu** — `loop()` zapisuje `display_segments_[]` podczas gdy timer czyta. `volatile` zapobiega reorderingowi kompilatora, ale nie torn reads w 4-cyfrowym cyklu.
5. **Overhead per callback**: `gpio_set_level()` (function call + bounds check) × 2 + stack-alloc `spi_transaction_t` + `spi_device_polling_transmit()` API overhead — wszystko CPU-bound, zmienny czas.
6. **`spi_device_polling_transmit()` blokuje CPU** na ~12μs — szybkie, ale nie deterministyczne (bus arbitration, interrupts).

### Rozwiązania (ranking effort/impact)

#### Tier 1 — Szybkie software'owe (niski effort, umiarkowana poprawa)

| # | Co | Gdzie | Dlaczego |
|---|-----|-------|----------|
| 1 | Zwiększ refresh: `DISPLAY_REFRESH_MS` 2→1ms (250Hz/digit) | `snk_mower.h:195` | SPI transfer = 12μs, masz 188μs headroom. Wyższa freq = mniej widoczny flicker |
| 2 | Direct GPIO register writes dla CS zamiast `gpio_set_level()` | `snk_mower.cpp:227,238` | `GPIO.out1_w1tc/w1ts` dla GPIO32 — deterministyczne ~50ns vs zmienny function call |
| 3 | Pre-allocate `spi_transaction_t` jako member/static | `snk_mower.cpp:229` | Unikaj stack alloc + zero-init per callback |
| 4 | Double-buffer `display_segments_[]` z atomowym pointer swap | `snk_mower.h:211` | Eliminuje torn reads między `loop()` a timerem |

#### Tier 2 — Średni effort, duża poprawa

| # | Co | Dlaczego |
|---|-----|----------|
| 5 | **Dedykowany FreeRTOS task** (pinned to Core 1, priority high) z `vTaskDelayUntil()` | Core 0 = WiFi/BT; Core 1 = display. `vTaskDelayUntil` daje precyzyjny period. Większa kontrola niż `esp_timer` |
| 6 | **`ESP_TIMER_ISR` dispatch** zamiast `ESP_TIMER_TASK` | Callback w ISR = nie podlega scheduleringowi. UWAGA: `spi_device_polling_transmit()` może nie być ISR-safe — wymaga low-level SPI register access lub `spi_device_polling_transmit()` z `intr_flags` |
| 7 | **Hardware CS przez SPI** (`spics_io_num` + `cs_ena_pretrans/posttrans`) | Precyzyjne CS timing przez hardware. Zespół próbował i odrzucił, ale warto ponowić z poprawną konfiguracją timing |
| 8 | **SPI DMA** (`SPI_DMA_ENABLED`) | Transfer bez CPU, deterministyczny. Dla 24-bit gain jest mały, ale eliminuje bus arbitration jitter |

#### Tier 3 — Najlepsze (replikuje oryginalny firmware, najwyższy effort)

| # | Co | Dlaczego |
|---|-----|----------|
| 9 | **LCD_CAM (I80) peripheral z 1-bit bus** — **ROZWIĄZANIE ORYGINALNEGO FW** | ESP32 ma dedykowany LCD_CAM z DMA. Bus width konfigurowalny: 1/2/4/8/16/24 bit. Z 1-bit: `LCD_DATA0`→DS(MOSI), `LCD_WR`→SH_CP(CLK), `LCD_CS`→ST_CP(latch). **Zero CPU load, perfect timing, DMA continuous** — identycznie jak oryginał. SOC potwierdza: `SOC_LCD_I80_SUPPORTED=true`. ESP-IDF: `esp_lcd_new_panel_i80()` lub low-level `lcd_cam` HAL. Dekompilacja potwierdza rejestr `0x3FF4E0C4` = LCD_CAM control |
| 10 | **RMT peripheral** (alternatywa) | RMT z DMA + loop = ciągły output bez CPU. Używany dla WS2812 ale napędzi 74HC595. 3 zsynchronizowane kanały (MOSI/CLK/CS) lub encoding 3 sygnałów. ESPHome ma RMT component |
| 11 | **I2S peripheral** (mniej konwencjonalne) | `SOC_I2S_SUPPORTS_LCD_CAMERA=true` — I2S może drive'ować LCD_CAM. Continuous DMA stream. Trudniejsza konfiguracja dla non-audio |

### Diagnostyka (przed / równolegle z implementacją)

| # | Co | Dlaczego |
|---|-----|----------|
| 12 | **Logic analyzer** na CLK/CS/MOSI — zmierz aktualny jitter interwału 2ms | Ilościowo zweryfikuj gdzie jest jitter (timer? SPI? both?) |
| 13 | **Profile `refresh_display_impl()`** — mierz actual vs expected interval | `esp_timer_get_time()` na początku callback |
| 14 | **Tymczasowo wyłącz WiFi** i obserwuj czy flicker maleje | Potwierdza czy WiFi scheduling jest głównym winowajcą |
| 15 | **Zmierz oryginalny firmware** — jeśli masz drugi ESP32 jako sniffer, zmierz refresh rate oryginału | Daje baseline do match'owania |

### Rekomendacja

**Najwyższy ROI**: #1 + #2 + #4 (Tier 1, ~30 min pracy) — powinno zredukować flicker znacząco przy minimalnym ryzyku.

**Docelowo**: #9 (LCD_CAM) — to jest dokładnie to, co robił oryginalny firmware. Dekompilacja już to potwierdza, SOC wspiera I80 z 1-bit bus, ESP-IDF ma API. Po implementacji flicker zniknie całkowicie (hardware DMA, zero jitter).

---

## Wyzwania implementacji LCD_CAM (I80) dla wyświetlacza 74HC595 (2026-06-21)

Implementacja LCD_CAM (#9 z analizy powyżej) jest docelowym rozwiązaniem, ale stoją przed nią konkretne wyzwania:

### 1. Bus width — musimy użyć 1-bit (nieparallel)

Mamy fizycznie tylko 3 linie do 74HC595: CLK=GPIO33, MOSI=GPIO25, CS=GPIO32. LCD_CAM I80 standardowo używa równoległy bus (8/16/24-bit), ale obsługuje też 1-bit. 24-bit frame = 24 cykli zegara z 1-bit bus (zamiast 1 cyklu z 24-bit bus).

**Wątpliwość**: Oryginalny firmware według dekompilacji ("5 × 4 bytes data buffer", "11 items", "24-bit frame format") może używał szerszego busa. Jeśli tak, to znaczy że na innej rewizji PCB był parallel connector. My musimy zrobić 1-bit na naszych pinach — powinno działać, ale wymaga weryfikacji że `esp_lcd_new_panel_i80()` akceptuje `bus_width=1`.

### 2. CS polarity — konflikt 74HC595 vs LCD_CAM

**74HC595 ST_CP**: latchuje na **narastającym** zboczu (rising edge). Po transferze CS musi: LOW (przez shift) → HIGH (latch).

**LCD_CAM I80 CS**: standardowo **active-low** (low podczas transferu, high między). To akurat pasuje — ale LCD_CAM często automatycznie strobuje CS w specyficzny sposób (cs_ena_pretrans / cs_ena_posttrans). Trzeba skonfigurować precyzyjnie, by CS rising edge następował PO ostatnim bicie clocka, nie wcześniej.

**Ryzyko**: jeśli LCD_CAM podniesie CS zanim skończy clockować ostatni bit, ostatni bit nie wejdzie do rejestru. 74HC595 wymaga setup/hold time na SH_CP.

### 3. Multiplexing — DMA continuous vs 4 różne frames

LCD_CAM z DMA jest projektowany do jednorazowego zapisu całego frame buffer (jak LCD panel). My potrzebujemy **cyklicznie** wysyłać 4 różne 24-bit frames (po jednym na cyfrę multiplexu) w pętli, ~500-1000 razy na sekundę.

Opcje:
- **(a) Jeden duży DMA buffer z 4-frame cycle repeat** — np. 1000 powtórzeń cyklu 4 cyfr = 12 KB, re-fill gdy się kończy. Skomplikowane zarządzanie, ale realistyczne.
- **(b) 4 osobne DMA transfers schedulowane po kolei** — interrupt po każdym transferze, schedule następnego. Wraca część jittera.
- **(c) DMA circular mode** — ESP32 LCD_CAM obsługuje? Trzeba weryfikować w TRM §14.

### 4. ESPHome/ESP-IDF API — nie ma gotowego panel driver dla 74HC595

`esp_lcd` component ESP-IDF ma panel drivers dla ST7789, ST7735, ILI9341, NV3024 itd. **Nie ma driver dla "74HC595 multiplexed display"**. Trzeba:

- **Opcja A**: użyć `esp_lcd_new_i80_bus()` (low-level bus) + własne wysyłanie 24-bit frames przez `esp_lcd_panel_io_tx_param()` lub bezpośrednio po DMA. Piszemy własny "panel" bez `esp_lcd_new_panel_*`.
- **Opcja B**: bezpośredni dostęp do rejestrów LCD_CAM (`0x3FF4E0C4` control + `0x3FF4E0130..140` data buffer) — tak jak robił oryginalny firmware. Niskopoziomowe, hardware-specific, ale deterministyczne.
- **Opcja C**: custom ESPHome component wrap'ujący esp_lcd_panel_io_i80.

### 5. GPIO matrix routing — sygnały LCD_CAM do GPIO

LCD_CAM używa sygnały: `LCD_DATA0` (signal ID 100), `LCD_WR` (128), `LCD_CS` (130). Te trzeba przemapować przez `gpio_matrix_out()` na GPIO33/25/32. ESP-IDF `esp_lcd` robi to automatycznie przy `esp_lcd_new_i80_bus()`, ale musimy podać piny poprawnie w `esp_lcd_i80_bus_config_t`.

**Ryzyko**: GPIO32/33 to piny RTC/touch-capable. Może wymagać specjalnej konfiguracji `rtc_gpio_deinit()` przed użyciem jako LCD_CAM. Do weryfikacji.

### 6. Conflict z istniejącym SPI2 — inicjalizacja kolejność

Aktualny kod używa `spi_bus_initialize(SPI2_HOST, ...)`. LCD_CAM to osobny peryferium, nie koliduje sprzętowo, ale:
- Piny GPIO25/33 są już zajęte przez SPI2. Trzeba `spi_bus_remove_device()` + `spi_bus_free()` przed inicjalizacją LCD_CAM.
- Kod musi mieć fallback na SPI jeśli LCD_CAM się nie zainicjalizuje.

### 7. OE (Output Enable) — niepewność hardwarowa

HARDWARE.md mówi: "Piny 13 układów U1/U3/U4 są sprzętowo podłączone do masy (GND), dzięki czemu wyjścia są stale aktywne."

Ale w ha.md sekcji "Problem migotania i rozwiązanie CS (OE)" napisaliśmy: "CS (GPIO32) steruje Output Enable (OE) na wyświetlaczu. Gdy CS=HIGH, wyjścia 74HC595 są wyłączone (high-impedance)."

**Te dwa stwierdzenia się wykluczają.** Jeśli OE=GND stale (HARDWARE.md), to CS nie może być OE. Wtedy "migotanie przy CS=HIGH" w starym bit-banging kodzie miało inną przyczynę — np. puste rejestry 74HC595 kiedy CS nie latchuje poprawnie.

**Wyzwanie**: musimy zrozumieć rzeczywisty hardware przed implementacją LCD_CAM. Inaczej skonfigurujemy CS timing błędnie. Logic analyzer na CS/MOSI/CLK + obserwacja oryginalnego firmware'u rozstrzygnie.

### 8. ESP-IDF version compatibility

ESPHome `snk-mower.yaml` używa `framework: type: esp-idf, version: recommended`. `esp_lcd` component jest stabilny od IDF 4.4+, ale API I80 bus miało zmiany w 5.x. Trzeba:
- Sprawdzić wersję ESP-IDF używaną przez ESPHome (`/home/marek/tmp/kosiarka/.esphome/build/test-mower/.pioenvs/test-mower/config/sdkconfig.json`)
- Zweryfikować dostępność `esp_lcd_new_i80_bus()` i `esp_lcd_panel_io_ops_t` w tej wersji

### 9. Brak przykładów referencyjnych

Dokumentacja ESP-IDF ma przykłady LCD_CAM dla standardowych paneli LCD. Nie ma przykładu "drive 74HC595 shift register chain via LCD_CAM I80 1-bit". Społeczność ESPHome używa MAX7219 / HT16K33 / TM1637 dla 7-seg — to wszystko I2C/SPI bit-bang.

Znalezione referencje do sprawdzenia:
- Espressif `esp_lcd` docs (I80 bus section)
- ESP32 TRM §14 (LCD_CAM) — szczegóły rejestrów `LCD_CLOCK_REG`, `LCD_CTRL_REG`, `LCD_DATA_*`
- Kod oryginalnego firmware (`disasm.s`, funkcje `FUN_401908b4`, `FUN_40190fb4`, `FUN_401913e0`) — jedyny znany przykład LCD_CAM dla 74HC595

### 10. Debugging — brak widoczności stanu LCD_CAM

LCD_CAM jest peryferium hardware — jeśli coś nie działa, logi nic nie pokażą. Diagnostyka wymaga:
- Logic analyzer na CLK/MOSI/CS (podstawowe)
- Dostęp do rejestrów LCD_CAM w runtime (dodatkowy kod diagnostyczny)
- Możliwość porównania z oryginalnym firmware (drugi ESP32 jako sniffer na te same piny — ale koliduje fizycznie)

### Strategia implementacji (proponowana)

1. **Najpierw**: diagnostyka #12-15 z poprzedniej sekcji — logic analyzer na aktualnym SPI, zmierz jitter. To daje baseline.
2. **Następnie**: implementacja Tier 1 (#1+#2+#4) — szybka poprawa, low risk.
3. **Jeśli Tier 1 niewystarczające**: prototyp LCD_CAM w izolowanym środowisku (czysty ESP-IDF, nie ESPHome) — tylko LCD_CAM + 74HC595, bez UART/WiFi. Weryfikacja że hardware path działa.
4. **Po prototypie**: integracja do ESPHome jako custom component z fallback na obecny SPI.
5. **Finalnie**: usunięcie fallback jeśli LCD_CAM działa stabilnie.

### Ryzyka projektowe

- **Brak oryginalnego firmware do porównania na żywo** — mamy tylko dekompilację. Nie widzieliśmy LCD_CAM w akcji na tej samej płytce. Prototyp #3 jest krytyczny.
- **Możliwe że 1-bit bus nie działa poprawnie z LCD_CAM** — peryferium projektowane dla parallel, 1-bit może mieć nieudokumentowane ograniczenia.
- **Czas implementacji**: Tier 1 = ~30 min, Tier 2 = ~2-4h, LCD_CAM prototyp = 4-8h, integracja ESPHome = kolejne 4-8h. Łącznie LCD_CAM to ~1-2 dni pracy.

---

## EKSPERYMENTY I WYNIKI (2026-06-21)

### Eksperyment 1: Boot handshake
**Kod:** `components/snk_mower/snk_mower.cpp`  
**Cel:** Ustabilizować boot sekwencję ESP↔MB

**Zmiany:**
- Usunięto blokujący `&& boot_delay_ms_ == 0` w `handle_device_info()` — MB wysyła DEVICE_INFO tylko raz, więc warunek był niespełnialny po boot_delay
- Dodano INIT burst (×6) po ESP_INFO (×5) w SYNC phase
- PIN wysyłany tylko raz po SYNC→DONE transition

**Wynik:** Boot działa: PRE → SYNC → DONE → PIN accepted → state=idle. Logi czyste.

---

### Eksperyment 2: Capture 04 analysis
**Kod:** `captures/04-return-home/decoded.json`  
**Cel:** Znaleźć ESP→MB command inicjującą koszenie

**Wynik:** ⚠️ **Oryginalny ESP32 nie wysyła żadnej komendy koszenia.** Po naciśnięciu START, ESP kontynuuje tylko POLL/KEEPALIVE. START jest obsługiwany w 100% lokalnie przez U16 (J8 pin6 → U16 GPIO → U13).

**Kluczowe różnice między oryginałem a ESPHome:**
| Aspekt | Oryginał | ESPHome (przed compat_mode) |
|--------|----------|----------------------------|
| `wifi` | 0 (disconnected) | 1 (connected) |
| `state` | 0, wysłany RAZ przy bootcie | 1, co 10s |
| `esp_state` periodic | NIE | TAK |
| POLL interval | ~30ms | TYLKO w PRE/SYNC phase |

**Rozwiązanie:** `compat_mode: true` — wymusza wifi=0 i usuwa periodic state. Plus dodanie POLL w DONE phase.

---

### Eksperyment 3: Przyciski fizyczne i HA
**Kod:** `snk-mower.yaml` (przyciski), `snk_mower.cpp` (start_mowing, return_to_dock, send_action)  
**Cel:** Sprawdzić czy ESP może zainicjować koszenie

**Testy:**
1. **Fizyczny START:** ❌ Kosiarka na podłodze, START wciśnięty — nic. MB nie wysyła START_ACK/EXEC_ACTION
2. **start_mowing() z HA:** ❌ Sekwencja ESP_TRIM + ESP_ERR_ACK1 + ESP_STATE(state=2) wysłana, MB odpowiedziało SCHEDULE (trim=36 auto=0) ale nie zmieniło stanu
3. **return_to_dock() z HA:** ❌ ESP wysłało 0x10000001, MB bez reakcji
4. **send_action(1/4) z HA:** ❌ Ignorowane przez MB (brak `cmd` field)
5. **send_raw_json (START_ACK/EXEC_ACTION/CMD action):** ❌ Wszystkie zignorowane — to są MB→ESP komendy, nie ESP→MB

**Wnioski:**
- Nie istnieje znane ESP→MB JSON command inicjujące koszenie
- Fizyczny START nie generuje UART — wszystko lokalnie na U16
- Jedyna droga przez ESP: harmonogram z `auto=1` (U13 ma "Robot on schedule, start work")

---

### Eksperyment 4: compat_mode na żywej kosiarce
**Kod:** wersja `1ad3c2b` (compat_mode + usunięty periodic state)  
**Cel:** Sprawdzić czy compat_mode (wifi=0, str=0, brak periodic state) pozwala na działanie START

**Logi (16:15:17-16:16:47):**
- Boot OK: ESP_INFO → wifi=0,str=0 → PIN accepted → state=idle
- OK Button (GPIO19) działa: wykrywa naciśnięcie
- KEEPALIVE co 1s OK
- wifi/bt status co 5s OK
- ESP_INFO co 30s OK

**🔴 Problem: BRAK STATUS FRAMES Z MB**
- MB wysyła PIN_RESULT + PIN_RESULT2 **przy każdym wifi/bt status** (co 5s)
- MB **nigdy nie wysyła STATUS** (0x330000A0) — nie widać "Status: state=..." w logach
- Bez STATUS nie widać zmiany stanu po naciśnięciu START
- Kosiarka pozostaje w "idle" na stałe

**Hipotezy:**
1. **Brak POLL w DONE phase** — oryginał wysyła POLL co ~30ms przez cały czas. My wysyłamy tylko KEEPALIVE. MB może nie wysyłać STATUS bez POLL.
2. **wifi/bt w PRE phase** — oryginał nie wysyła wifi/bt przed DEVICE_INFO. My wysyłamy co 5s już w PRE, co może mylić MB i powodować reset sesji.
3. **Init value (init=3)** — może MB oczekuje innej wartości lub sekwencji INIT.
4. **Frame format** — różnica w znakach start/stop/CRC między oryginałem a nami.

**Naprawa (commit `94a38de`):**
- Dodano POLL co 30ms w DONE phase (tak jak oryginał)
- Usunięto wifi/bt z PRE phase (tylko POLL+KEEPALIVE w PRE)

**Czeka na test:** czy POLL w DONE + brak wifi/bt w PRE przywróci STATUS frames od MB.

---

### Eksperyment 5: send_action() — komendy bez cmd field
**Kod:** `snk_mower.cpp:send_action()`  
**Cel:** Przetestować czy MB rozumie komendy `{"app_main":24.125,"chedule":<value>}` z dekompilacji ESP32

**Wynik:** ❌ Wszystkie zignorowane (brak `cmd` field — MB odrzuca)

| Wartość action | Znaczenie (z dekompilacji) | Reakcja MB |
|---------------|---------------------------|------------|
| 0 | idle/stop | ❌ |
| 1 | work/mow | ❌ |
| 2 | edge | ❌ |
| 3 | pause | ❌ |
| 4 | dock/return | ❌ |

---

### Podsumowanie stanu

| Co działa | Co nie działa | Co wymaga testu |
|-----------|--------------|-----------------|
| Boot handshake (PRE→SYNC→DONE) | Fizyczny START (obsługa na U16) | POLL w DONE phase (commit 94a38de) |
| PIN accepted | STATUS frames z MB | |
| KEEPALIVE co 1s | start_mowing() z HA | |
| POLL co 30ms | return_to_dock() z HA | |
| wifi=0,str=0 (compat_mode) | send_action() / send_raw_json | |
| OK Button (GPIO19) | | |
| SNTP time sync | | |

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
