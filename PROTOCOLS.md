# Inter-Chip Communication Protocols

> **Krzyżowa weryfikacja kierunków (2026-06-22):**
> Kierunki poniżej zostały zweryfikowane z **trzech niezależnych źródeł**:
> 1. **Captures 01-06** — D0=MB→ESP, D1=ESP→MB (etykiety w `captures/README.md` i `notes.md` poprawne)
> 2. **Captures 2026-06-21** — D1=ESP TX, D2=MB TX (UWAGA: `captures/2026-06-21/README.md` ma ODWRÓCONE etykiety kierunków — autor sam wyraził wątpliwość notką "(wait, to jest D1!)")
> 3. **Logika sprzętu** — PIN jest wysyłany przez ESP (display board ma klawiaturę), bateria/RTC/device-info pochodzą z MB, czujnik deszczu jest na display board (J4→ESP32 GPIO36), MAC address jest z ESP32 (WiFi)
>
> **Kluczowe odkrycie**: captures 01-06 i 2026-06-21 miały kanały podłączone w różnej kolejności,
> ale po korekcie oba zestawy captures pokazują **identyczne** przypisanie kierunków do komend.

## Overview

```
ESP32 (Display Board)              U16 (Board MCU)              U13 (Main MCU)
┌──────────────────────┐          ┌──────────────────────┐     ┌──────────────────────┐
│ ESP32-WROOM-32UE     │  JSON    │ GD32F303CGT6         │JSON │ GD32F305AGT6         │
│                      │ 230400   │                      │230400│                      │
│ GPIO17 (TX) ─────────┼──────────┼→ U16 RX              │─────→│ bdport (USART)       │
│ GPIO16 (RX) ←────────┼──────────┼← U16 TX             │←─────│                      │
│                      │ 8N1      │                      │ 8N1  │                      │
│ &{json}<CRC>#        │          │ JSON bridge          │      │ Motor control        │
│                      │          │ FreeRTOS             │      │ KV-store + PIN       │
│                      │          │ cJSON                │      │ dpport (display port)│
└──────────────────────┘          └──────────────────────┘     └──────────────────────┘
```

**Jeden protokół** między ESP32 a U16 (zweryfikowany przez 10 nagrań LA):
- **JSON** over UART at **230400 8N1**, standard polarity (not inverted)
- **Frame format**: `&{json}<CRC>#` (pojedynczy `#` — NIE `##` jak wcześniej dokumentowano)
- U16 acts as a bidirectional JSON bridge to U13
- **CRC**: Dallas/Maxim CRC-8 (poly 0x31, init 0x00, ref_in=true, ref_out=true) over JSON bytes only

Wcześniejsza dokumentacja protokołu binarnego (`0xAA 0x55` @115200) w `esp32/notes/ESP32.md` jest **NIEPRAWIDŁOWA**.

---

## Frame Format

```
 &  { " c m d " :  1 2 3 , ... }  <CRC>  #
 ^                              ^         ^
 │                              │         └─ 0x23 (#) — frame terminator (pojedynczy!)
 │                              └─ Dallas/Maxim CRC-8 (1 byte)
 └─ 0x26 (&) — frame prefix
```

Przykład (BOOT command, z capture 01-boot D0):
```
26 7B 22 63 6D 64 22 3A 35 33 36 38 37 30 39 31 33 2C 22 61 63 74 69 6F 6E 22 3A 30 7D A0 23
 &  {  "  c  m  d  "  :  5  3  6  8  7  0  9  1  3  ,  "  a  c  t  i  o  n  "  :  0  } CRC  #
```
CRC = 0xA0 dla `{"cmd":536870913,"action":0}` — zweryfikowane: Dallas/Maxim CRC-8.

### CRC verification

```python
def crc8_maxim(data: bytes) -> int:
    """Dallas/Maxim CRC-8: poly=0x31, init=0x00, ref_in=true, ref_out=true"""
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8C if (crc & 1) else (crc >> 1)
    return crc
```

CRC liczone jest tylko nad bajtami JSON (między `&` a CRC, wyłącznie).

---

## Command ID Structure

Command IDs are 32-bit integers. Prefix indicates source subsystem:

| Prefix | Direction | Description |
|--------|-----------|-------------|
| `0x10xxxxxx` | **ESP→MB** | Error acknowledges |
| `0x20xxxxxx` | **MB→ESP** | Power/action notifications |
| `0x22xxxxxx` | **ESP→MB** | Sensor data (rain — sensor on display board) |
| `0x30xxxxxx` | **ESP→MB** | Settings, keepalive, WiFi/BT status, config |
| `0x31xxxxxx` | **ESP→MB** | Settings menu control |
| `0x33xxxxxx` | **MB→ESP** | Device info, status, config reports |
| `0x4000000x` | **Both** | System: BOOT, INIT, INFO, RTC — direction zależy od sub-ID (patrz niżej) |
| `0x41xxxxxx` | **MB→ESP** | Lock, exec_action, error, shutdown, start_ack, home, docked — **Z WYJĄTKIEM 0x41000005 (PIN_SEND) który jest ESP→MB** |
| `0x50xxxxxx` | **MB→ESP** | Battery info |

> **Ważna uwaga**: Poprzednia wersja tego pliku miała **odwrócone kierunki** dla większości prefixów.
> Korekta oparta na krzyżowym potwierdzeniu captures 01-06 (etykiety poprawne) i 2026-06-21 (etykiety odwrócone, dane potwierdzają).

### Prefix `0x40xxxxxx` — kierunki mieszane

| CMD | Direction | Nazwa |
|-----|-----------|-------|
| `0x40000001` | ESP→MB | ESP_INIT (init confirmation) |
| `0x40000004` | ESP→MB | ESP_BOOT (boot handshake) |
| `0x40000006` | ESP→MB | ESP_INFO (hw/sv/mac) |
| `0x40000008` | MB→ESP | BOOT_INIT (init in progress) |
| `0x40000009` | MB→ESP | BOOT_HEART (boot heartbeat) |
| `0x40000011` | MB→ESP | RTC_HEARTBEAT (co ~1s) |
| `0x40000012` | MB→ESP | START_TIME_QUERY |
| `0x40000013` | MB→ESP | CUT_TIME_QUERY |
| `0x40000014` | MB→ESP | UNKNOWN_14 |
| `0x40000020` | MB→ESP | LIGHT (light sensor level) |
| `0x40000021` | MB→ESP | BOOT_ACK |

Reguła: `0x4000000x` z `x` ≤ 6 → ESP→MB; `0x4000000x` z `x` ≥ 8 → MB→ESP.

---

## Complete Command Catalog

### MB→ESP (Mainboard wysyła do ESP32)

| CMD HEX | DEC | JSON Fields | Nazwa | Opis |
|---------|-----|-------------|-------|------|
| `0x20000001` | 536870913 | `action` | POWER_ON | Power-on wake (action:0) |
| `0x20000004` | 536870916 | — | POWER_READY | Ready signal |
| `0x33000009` | 855638025 | `result` | SETTING_OK_09 | Setting confirm (unknown) |
| `0x33000010` | 855638032 | `result` | PIN_CHANGE_OK | PIN change confirmed |
| `0x33000011` | 855638033 | `result` | RTC_SET_OK | Year/time set confirmed |
| `0x33000012` | 855638034 | `result` | DATE_SET_OK | Date set confirmed |
| `0x33000013` | 855638035 | `result` | TIME_SET_OK | Time set confirmed |
| `0x33000014` | 855638036 | `result` | START_TIME_OK | Start time confirmed |
| `0x33000015` | 855638037 | `result` | HOURS_SET_OK | Daily hours confirmed |
| `0x33000017` | 855638039 | `result` | RAIN_SET_OK | Rain config confirmed |
| `0x33000021` | 855638049 | `result` | PIN_RESULT | PIN verification result |
| `0x33000022` | 855638050 | `result` | PIN_RESULT2 | PIN verification result (2nd) |
| `0x33000027` | 855638055 | `result` | DAYS_WEEK_OK | Days/week confirmed |
| `0x330000A0` | 855638176 | `state, bat_lv, bat_per, ...` | **STATUS** | Stan kosia (raport cykliczny) |
| `0x330000A1` | 855638177 | `name, sn, version, model, ...` | **DEVICE_INFO** | Pełna konfiguracja urządzenia |
| `0x330000A2` | 855638178 | `mb_hv, mblt_sv, bb_hv, ...` | HW_VERSIONS | Wersje sprzętu |
| `0x330000A6` | 855638182 | `trim, auto, sun_st, ...` | SCHEDULE | Konfiguracja harmonogramu |
| `0x330000A7` | 855638183 | `rain_en, rain_delay` | RAIN_CFG | Konfiguracja deszczu |
| `0x330000A8` | 855638184 | `mul_en, mul_auto, mul_z1..4, ...` | MULTIZONE | Multi-zone config |
| `0x330000AA` | 855638186 | — | UNKNOWN_AA | Nieznane |
| `0x330000B0` | 855638192 | `map_sn, area` | MAP_CFG | Map/schedule info |
| `0x40000008` | 1073741832 | — | BOOT_INIT | Init in progress |
| `0x40000009` | 1073741833 | — | BOOT_HEART | Boot heartbeat |
| `0x40000011` | 1073741841 | `rtc` | RTC_HEARTBEAT | RTC time sync (co ~1s) |
| `0x40000012` | 1073741842 | `hour, minute` | START_TIME_QUERY | MB queries start time |
| `0x40000013` | 1073741843 | `len` | CUT_TIME_QUERY | MB queries max cut time (min) |
| `0x40000014` | 1073741844 | — | UNKNOWN_14 | Nieznane |
| `0x40000020` | 1073741856 | `lv` | LIGHT | Light sensor level |
| `0x40000021` | 1073741857 | — | BOOT_ACK | Boot acknowledge |
| `0x41000002` | 1090519042 | `lock:0/1` | LOCK | Lock state |
| `0x41000003` | 1090519043 | — | EXEC_ACTION | Akcja wykonana (po STOP/akcji) |
| `0x41000004` | 1090519044 | `err` | ERROR_NOTIFY | Error code notification |
| `0x41000005` | 1090519045 | (brak `pwd`) | **SEEK_WIRE** | MB→ESP notyfikacja (bez pola pwd! — patrz uwaga) |
| `0x41000006` | 1090519046 | — | RETURN_HOME | Home/return to dock notification |
| `0x41000007` | 1090519047 | — | DOCKED_CHARGE | Docked / charge start |
| `0x41000008` | 1090519048 | — | SHUTDOWN | Power-off command |
| `0x41000020` | 1090519072 | `result` | START_ACK | START button acknowledged |
| `0x50000021` | 1342177313 | `bat:0..3` | BATTERY | Battery level |

> **Uwaga o `0x41000005`**: To ID komendy występuje w **obu kierunkach**:
> - **ESP→MB** z polem `{"pwd":9633}` — wysyłanie PIN do weryfikacji
> - **MB→ESP** bez pola `pwd` (puste `{}`) — notyfikacja przed `state:8` (seek wire?)

### ESP→MB (ESP32 wysyła do Mainboard)

| CMD HEX | DEC | JSON Fields | Nazwa | Opis |
|---------|-----|-------------|-------|------|
| `0x10000001` | 268435457 | — | ESP_ERR_ACK1 | Error acknowledge 1 |
| `0x10000002` | 268435458 | — | ESP_ERR_ACK2 | Error acknowledge 2 |
| `0x10000007` | 268435463 | — | ESP_ERR_ACK7 | Error acknowledge 7 |
| `0x22000000` | 570425344 | `rain:0/1` | RAIN | Rain sensor state (sensor on display board) |
| `0x30000005` | 805306373 | — | KEEPALIVE | Keepalive (ciągły, ~100ms) |
| `0x30000006` | 805306374 | — | SETTING_MODE | Enter settings submenu |
| `0x30000007` | 805306375 | — | SETTING_APPLY | Confirm/apply setting |
| `0x30000009` | 805306377 | `old` | PIN_OLD | Old PIN for change |
| `0x30000010` | 805306384 | `pwd` | PIN_NEW | New PIN |
| `0x30000011` | 805306385 | `year` | SET_YEAR | Set RTC year |
| `0x30000012` | 805306386 | `month, day` | SET_DATE | Set RTC month/day |
| `0x30000013` | 805306387 | `hour, minute` | SET_TIME | Set RTC time |
| `0x30000014` | 805306388 | `hour, minute` | SET_START_TIME | Daily mowing start time |
| `0x30000015` | 805306389 | `hour` | SET_DAILY_HOURS | Hours per day (1-24) |
| `0x30000017` | 805306391 | `rain_en, rain_delay` | SET_RAIN | Rain sensor config |
| `0x30000021` | 805306401 | `wifi, str` | WIFI_STATUS | WiFi status (disconnected=0) |
| `0x30000022` | 805306402 | `bt, str` | BT_STATUS | BT status |
| `0x30000027` | 805306407 | `day` | SET_DAYS_WEEK | Days per week (3, 5, 7) |
| `0x30000028` | 805306408 | `state` | ESP_STATE | ESP state notification |
| `0x300000A1` | 805306529 | — | POLL | Poll/heartbeat (ciągły) |
| `0x300000A6` | 805306534 | — | ESP_TRIM | Trim schedule request (pusty — MB odsyła pełny) |
| `0x300000A7` | 805306535 | — | ESP_RAIN_CFG | Rain config request |
| `0x300000A8` | 805306536 | — | ESP_MULTIZONE | Multi-zone request |
| `0x31000016` | 822083606 | — | SETTING_START | Enter settings menu |
| `0x31000017` | 822083607 | — | SETTING_SUBMENU | Enter submenu |
| `0x40000001` | 1073741825 | `init` | ESP_INIT | Init complete confirmation |
| `0x40000004` | 1073741828 | — | ESP_BOOT | Boot notification |
| `0x40000006` | 1073741830 | `hv, sv, mac` | ESP_INFO | ESP HW/SW/MAC |
| `0x41000005` | 1090519045 | `pwd` | **PIN_SEND** | Send 4-digit PIN (jedyny ESP→MB w prefix 0x41) |

---

## Stany MB (pole `state` w `0x330000A0`)

| State | Znaczenie |
|-------|-----------|
| 0 | Idle (po włączeniu, przed PIN) |
| 1 | Ready (PIN odblokowany) |
| 2 | **MOWING** (lub jazda/odjazd) |
| 6 | Stop / pauza |
| 7 | Error (z polem `error:N`) |
| 8 | Seek wire? (przed `0x41000005` bez pwd) |
| 9 | **RETURNING TO DOCK** |
| 10 | **CHARGING** |
| 11 | **SHUTDOWN / POWER OFF** |

Dodatkowe pola w `0x330000A0`:
- `station: bool` — wykryto stację ładującą
- `border_state: 0/1` — kabel ograniczający
- `stop_state: 0/1` — przycisk STOP wciśnięty
- `rain_state: 0/1` — deszcz
- `bat_per: 0..100` — poziom baterii procentowo
- `bat_lv: 0..3` — poziom baterii (kategoryczny)
- `error: N` — kod błędu (gdy state=7)

---

## Boot Sequence (zweryfikowane przez LA captures)

```
MB starts on its own (D0 w captures 01-06, D2 w captures 2026-06-21):
  0x20000001 {"action":0}          ← power-on wake
  0x40000009 ×N                    ← boot heartbeat
  0x40000008 ×N                    ← init in progress
  0x50000021 {"bat":2}             ← battery level
  0x20000004 ×N                    ← ready signal
  0x40000021 ×N                    ← boot ack
  0x330000B0 {"map_sn":0,"area":300} ← map config
  0x40000020 {"lv":255}            ← light sensor
  0x330000A1 {"name":"MyMower",   ← device info (full config)
              "sn":"2312CGF250600035167",
              "version":31018,
              "model":"RMC300E20V-ECDNSS",
              "avail":4,"pwd_en":1,...}
  0x330000A2 {"mb_hv":22500,...}   ← hardware versions
  0x330000A6 {"trim":36,"auto":false, ← schedule config
              "sun_st":570,...}
  0x330000AA                       ← unknown
  0x330000A0 {"state":0,...}       ← initial state report
  0x41000002 {"lock":1}            ← lock state
  0x33000021 {"result":true}       ← PIN verified OK
  0x33000022 {"result":true}       ← PIN result (2nd)
  0x330000A0 {"state":1,...}       ← READY (unlocked)

ESP responds (D1 w captures 01-06, D1 w captures 2026-06-21):
  0x40000004 (BOOT)                ← boot handshake
  0x30000005 (KEEPALIVE) — ciągle
  0x30000028 {"state":0}
  0x300000A1 (POLL) — ciągle
  0x22000000 {"rain":1}            ← rain sensor (na display board)
  0x30000021 {"wifi":0,"str":0}    ← WiFi disconnected
  0x30000022 {"bt":0,"str":0}      ← BT disconnected
  0x40000006 {"hv":60400,"sv":30202,"mac":"08-f9-e0-b3-da-70"} ← ESP info
  0x40000001 {"init":3}            ← init complete
  0x300000A6 (ESP_TRIM)            ← trim request
  0x41000005 {"pwd":9633}          ← PIN sent

Steady state:
  ESP: 0x30000005 / 0x300000A1 co ~100-200ms
  MB:  0x40000011 {"rtc":...} co ~1s
  MB:  0x22000000 {"rain":0/1} — przy zmianie (UWAGA: deszcz jest wysyłany przez ESP, nie MB)
```

> **Korekta**: W poprzedniej wersji `0x22000000` (RAIN) był błędnie przypisany do MB→ESP.
> Deszcz jest na display board (J4 → ESP32 GPIO36), więc ESP wysyła ten stan do MB.

---

## Action Flow (START / STOP / HOME)

**Wszystkie akcje fizyczne** — START, STOP, HOME — są obsługiwane bezpośrednio przez U16 (przyciski podpięte do U16 przez J8, nie do ESP32). ESP32 tylko otrzymuje notyfikacje stanu i wysyła error ACKs.

### START (rozpoczęcie koszenia)

```
User → START button (physical, J8 pin 6 → U16)
  → U16 starts mowing internally
  → MB→ESP: 0x41000020 {"result":1}    (START_ACK)
  → MB→ESP: 0x330000A0 {"state":2}      (MOWING)
  → MB→ESP: 0x41000003                  (EXEC_ACTION)
  → State: 0→1→2 (MOWING)
```

### STOP (zatrzymanie)

```
User → STOP button (physical, to U16)
  → U16 stops motors
  → MB→ESP: 0x330000A0 {"stop_state":1}
  → MB→ESP: 0x41000003                  (EXEC_ACTION)
  → MB→ESP: 0x330000A0 {"state":6}      (STOP)
  → MB→ESP: 0x330000A0 {"stop_state":0}
```

### HOME (powrót do stacji)

```
User → HOME button (physical, to U16)
  → MB→ESP: 0x41000020 {"result":1}    (START_ACK — krótki odjazd)
  → MB→ESP: 0x330000A0 {"state":2}      (jazda)
  → MB→ESP: 0x41000003                  (EXEC_ACTION)
  → MB→ESP: 0x330000A0 {"state":6}      (stop)
  → MB→ESP: 0x41000006                  (RETURN_HOME notification)
  → MB→ESP: 0x330000A0 {"state":9}      (RETURNING TO DOCK)
  → MB→ESP: 0x330000A0 {"station":true} (stacja wykryta)
  → MB→ESP: 0x41000007                  (DOCKED_CHARGE)
  → MB→ESP: 0x330000A0 {"state":10}     (CHARGING)
```

---

## Error Flow

```
MB detects error (lift, out of wire, etc.):
  → MB→ESP: 0x41000004 {"err":16}       (ERROR_NOTIFY)
  → MB→ESP: 0x330000A0 {"state":7,"error":16}  (ERROR state)

ESP acknowledges:
  → ESP→MB: 0x10000001                   (ESP_ERR_ACK1)
  → ESP→MB: 0x10000002                   (ESP_ERR_ACK2)
  → ESP→MB: 0x10000007                   (ESP_ERR_ACK7)
```

### Error Codes

| Code | Display | Meaning |
|------|---------|---------|
| 16 | E11 | Lift/tilt/blocked (out of wire range) |

---

## PIN Verification Flow

```
Step   ESP32                        U16 (bridge)                 U13
────   ─────                        ────────────                 ───
1.     JSON {"cmd":1090519045,      receives, forwards           receives JSON
            "pwd":9633}                                          
       ──────────────────────────▶  ──────────────────────────▶
2.                                                              reads PIN from KV-store (key "pwd" @ RAM 0x2000027C)
                                                                compares
3.                                 JSON result ←────────────────  sends OK/FAIL
4.     0x33000021 {"result":true} ←─────────────── translates JSON → result
5.     MB→ESP: 0x41000002 {"lock":1}  (lock state — unlocked LED)
6.     MB→ESP: 0x330000A0 {"state":1}  (READY)
```

PIN jest przechowywany w U13 (KV-store, key `"pwd"`, adres RAM `0x2000027C`), NIE na ESP32.
ESP tylko przesyła PIN wprowadzony przez użytkownika do MB w celu weryfikacji.

---

## Settings Protocol

### ESP→MB (setting commands)

| Cmd | Fields | Setting |
|-----|--------|---------|
| `0x30000006` | — | SETTING_MODE — enter settings submenu |
| `0x30000007` | — | SETTING_APPLY — confirm/apply |
| `0x30000009` | `old` | PIN_OLD — old PIN for change |
| `0x30000010` | `pwd` | PIN_NEW — new PIN |
| `0x30000011` | `year` | SET_YEAR |
| `0x30000012` | `month, day` | SET_DATE |
| `0x30000013` | `hour, minute` | SET_TIME |
| `0x30000014` | `hour, minute` | SET_START_TIME |
| `0x30000015` | `hour` | SET_DAILY_HOURS |
| `0x30000017` | `rain_en, rain_delay` | SET_RAIN |
| `0x30000027` | `day` | SET_DAYS_WEEK (3, 5, 7) |
| `0x31000016` | — | SETTING_START — enter settings menu |
| `0x31000017` | — | SETTING_SUBMENU — enter submenu |

### MB→ESP (setting confirmations)

| Cmd | Fields | Setting |
|-----|--------|---------|
| `0x33000009` | `result` | SETTING_OK_09 |
| `0x33000010` | `result` | PIN_CHANGE_OK |
| `0x33000011` | `result` | RTC_SET_OK |
| `0x33000012` | `result` | DATE_SET_OK |
| `0x33000013` | `result` | TIME_SET_OK |
| `0x33000014` | `result` | START_TIME_OK |
| `0x33000015` | `result` | HOURS_SET_OK |
| `0x33000017` | `result` | RAIN_SET_OK |
| `0x33000027` | `result` | DAYS_WEEK_OK |
| `0x40000012` | `hour, minute` | START_TIME_QUERY (MB queries) |
| `0x40000013` | `len` | CUT_TIME_QUERY (MB queries, min) |

---

## Protocol: U16 ↔ U13 (Internal)

Same JSON format at 230400. U16 is a transparent bridge + adds its own messages.

U13 firmware strings confirm architecture:
- `driver_dpport.c` ("dpport drv") — **display port** driver (komunikacja z ESP przez U16 bridge)
- `driver_bdport.c` ("bdport drv") — **board port** driver (komunikacja z U16)
- `service_bdport.c` ("bdport srv") — board port service
- `deal_message.c` — message handling
- `add receive message head callback failed, head=%d` — dynamic command dispatch by ID

U13 parsuje JSON via cJSON (confirmed in firmware strings).
U13 has services: movement, map, time, blade, bms, border, multizone, hit, ultrasonic, slope, stop, power, config.

---

## OTA Protocol

OTA flows: Cloud → ESP32 → U16 → U13 over same UART channel.

U13 OTA framing (from `FUN_08008cb8`):
```
[2B length LE] [N bytes data] [1B XOR checksum]
```

7 OTA commands: GET OTA INFO, DOWNLOAD, SET OTA MODE, SET VER, RETURN, SET FIRMWARE NUMBER, SET BAUDRATE.

---

## Key Architectural Facts

- **U16 runs FreeRTOS** — comm_task, init_task, init_bd
- **U16 uses EasyLogger v2.2.99** — logs to internal buffer
- **ESP cannot trigger mowing via UART** — nie istnieje komenda "start mowing" w protokole UART
- **All physical actions (START/STOP/HOME) go to U16** — ESP only receives notifications + sends error ACKs
- **U16 JSON parser**: generic cJSON-based, handles arbitrary JSON
- **U16 limit**: max 128 bytes per message (mport driver)
- **U13 RTC** exists, sends RTC heartbeat (`0x40000011`) co ~1s do ESP
- **Rain sensor** jest na display board (J4 → ESP32 GPIO36), ESP wysyła stan do MB
- **PIN** przechowywany w U13 KV-store, weryfikowany przez U13, ESP tylko przesyła
- **Battery** info pochodzi z MB (`0x50000021`)
- **Device info** (name, sn, version, model, hw versions) pochodzi z MB (`0x330000A1`, `0x330000A2`)

---

---

## Project Status Summary

### What Works (stable communication via ESPHome component)
- ✅ **Protocol decoding** — JSON + CRC8-Dallas at 230400 8N1, full frame parsing
- ✅ **Boot sequence** — BOOT→KEEPALIVE→STATE→RAIN→WIFI→ESP_INFO→INIT, non-blocking
- ✅ **Handshake** — stable SYNC→DONE transition, PIN accepted → `state:1` (READY)
- ✅ **Watchdog-safe communication** — 36+ s without watchdog (log 22), `safe_mode` counter resets
- ✅ **Periodic reporting** — KEEPALIVE@1s, WIFI/BT@5s, ESP_INFO@30s, ESP_STATE@10s, RAIN@60s
- ✅ **Error ACK** — all 3 commands (`0x10000001` / `0x10000002` / `0x10000007`)
- ✅ **Display** — 4-digit 7-segment LED (SPI, 3× 74HC595, 2MHz, hardware timer)
- ✅ **Sensors** — rain (GPIO36), light, battery, device-info, schedule parsing
- ✅ **HA integration** — full sensor/binary_sensor/text_sensor publishing
- ✅ **Boot delay (30s)** — OTA-safe window, configurable

### What Does NOT Work
- ❌ **START/STOP/HOME buttons** — physical buttons connect through J8 to U16, **not** to ESP32. ESP sees only `START_ACK`/`EXEC_ACTION` notifications. **No UART command exists to trigger mowing.**
- ❌ **Software mowing start** — methods 1-5 (`send_action`, `CMD_EXEC_ACTION`, `CMD_RETURN_HOME`, `CMD_START_ACK`, `ESP_TRIM auto=1`, `ESP_STATE=2`) all ignored by MB. No evidence in any LA capture of an ESP→MB command that initiates mowing.
- ❌ **`start_mowing()`** — MB ignores the TRIM schedule + error ACK + state=2 sequence
- ❌ **`return_to_dock()`** — sends `CMD_RETURN_HOME` but mower stays in idle
- ❌ **GPIO mowing trigger (method 6)** — untested. Would require a jumper wire from a free ESP32 pin to J8 pin 6 (START button line).
- ❌ **PIN is NOT stored in original ESP32 firmware** — PIN is stored in U13 KV-store (EEPROM U22). Original ESP32 firmware only forwards user-entered PIN for verification; there is no `pwd` constant in the firmware binary.
- ❌ **Physical START button** — does not trigger any MB response in log 23. Possible causes: J8 cable seating, GPIO interference, or MB state lock-out condition. Not resolved.
- ❌ **Colon on display** — bit location not found (b0, U4)
- ❌ **ESP not responding to MB queries** — `CMD_ESP_WIFI` / `CMD_ESP_BT` queries are sent by MB but ESP does not track/respond to them in time

### Key Architectural Constraints
1. **ESP cannot start mowing via UART** — no such command exists in the protocol. All physical actions (START/STOP/HOME) go directly to U16 through J8. ESP only receives notifications.
2. **MB watchdog ~30s from power-on** — only `BOOT` (`0x40000004`) resets it. Boot_delay keeps MB alive with POLL/KEEPALIVE but does NOT reset the watchdog.
3. **POLL in DONE causes DEVICE_INFO flood** — MB interprets periodic `CMD_ESP_POLL` as "ESP requests device info" and re-sends `DEVICE_INFO` + `HW_VERSIONS` indefinitely. POLL must be restricted to PRE/SYNC phases.
4. **`0x41xxxxxx` commands are MB→ESP** — all except `PIN_SEND` (`0x41000005`). Sending them reverse (ESP→MB) has no effect.
5. **U16 max 128 bytes/frame** — mport driver limitation.
6. **PIN is stored in U13, not on ESP32** — ESP only forwards user-entered PIN.

### Future Work (if continuing)
1. **Method 6**: GPIO jumper wire from free ESP32 pin to J8 pin 6 (START) — only way to trigger mowing from ESP
2. **Remove GPIO13/15/20/22 binary sensors** from YAML to rule out interference with J8 button circuits → done, commit pending
3. **Investigate MB lock-out**: check `stop_state`, `border_state`, `rain_state` in STATUS when physical START fails
4. **Re-test physical START with cover off** after GPIO sensor removal
5. **Capture LA trace with original firmware + MQTT** to find any omitted UART command for mowing trigger
6. **Find colon bit** on display (b0, U4)
7. **Implement MB query tracking** — respond to `CMD_ESP_WIFI` / `CMD_ESP_BT` in DONE phase

### Current Decision
The project is being **wound down** after exhausting all software-based methods to trigger mowing. The only remaining approach (method 6, GPIO jumper) requires hardware modification. Original firmware has been restored to the mower — it mows normally with stock software.

---

## Channel Mapping in Captures (Reference)

> **Krytyczne**: Kanały analizatora logicznego były podłączane różnie w różnych sesjach!

### Captures 01-06 (`captures/01-boot/` ... `captures/06-settings-all/`)

| Channel | Direction | Verification |
|---------|-----------|-------------|
| D0 | MB→ESP | Zawiera `0x20000001` (POWER_ON), `0x330000A1` (DEVICE_INFO), `0x50000021` (BATTERY) |
| D1 | ESP→MB | Zawiera `0x30000005` (KEEPALIVE), `0x22000000` (RAIN), `0x41000005 {"pwd":9633}` (PIN) |
| D2 | START button | (tylko w 01, 02) |

Etykiety w `captures/README.md` i `notes.md` dla 01-06 są **POPRAWNE**.

### Captures 2026-06-21 (`captures/2026-06-21/pierwszy.sr` ... `czwarty.sr`)

| Channel | Direction | Verification |
|---------|-----------|-------------|
| D1 | **ESP→MB** | Zawiera `0x30000005`, `0x22000000`, `0x41000005 {"pwd":9633}`, `0x40000006` (MAC ESP32) |
| D2 | **MB→ESP** | Zawiera `0x20000001`, `0x330000A1` (device info), `0x50000021`, `0x41000002` (LOCK) |
| D3 | START button | (brak UART) |
| D4 | OK button | (brak UART) |

> **UWAGA**: `captures/2026-06-21/README.md` ma **ODWRÓCONE** etykiety kierunków!
> README pisze "D1=MB TX, D2=ESP TX" ale w rzeczywistości jest **D1=ESP TX, D2=MB TX**.
> Autor zauważył błąd (notka "wait, to jest D1!") ale go nie poprawił.
>
> Dowody: D1 zawiera WiFi MAC address (`08:f9:e0:b3:da:70`) — tylko ESP32 ma WiFi.
> D2 zawiera device info z numerem seryjnym i wersjami mainboard — tylko MB to posiada.
