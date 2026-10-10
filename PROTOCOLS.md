# Inter-Chip Communication Protocols

> **Korekta 2026-10-09** (szczegóły i dowody: [`20261009_claude_investigation.md`](20261009_claude_investigation.md)):
> 1. ESP32 rozmawia **bezpośrednio z U13** (`driver_dpport`, USART0). U16 nie jest mostem: to MCU czujników przewodu i podnoszenia, podłączony do U13 osobnym portem (`bdport`, USART1 w U13, USART2 w U16).
> 2. Przyciski START/HOME/OK czyta **ESP32** (GPIO22/21/19). Klawisze wysyła do MB jako `0x1000000x`. To **nie są** "error ACK".
> 3. Mowing **can** be started over UART: `0x10000007`, then `0x10000001` (START, then OK). Return to station: `0x10000007`, then `0x10000002` (HOME, then OK). **Confirmed on the mower on 2026-10-10**, once U13 has accepted the PIN (`ha.md` §15).

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
ESP32 (Display Board)                 U13 (Main MCU)                    U16 (border board MCU)
┌──────────────────────────┐ J8      ┌──────────────────────────┐      ┌────────────────────────┐
│ ESP32-WROOM-32UE         │ JSON    │ GD32F305AGT6             │ JSON │ GD32F303CGT6           │
│ GPIO17 TX ───────────────┼────────→│ dpport  (USART0)         │      │                        │
│ GPIO16 RX ←──────────────┼─────────│                          │      │                        │
│                          │ 230400  │ bdport  (USART1) ←──────→│──────│ mport (USART2)         │
│ START=GPIO22 HOME=GPIO21 │         │ ledport (UART3)          │      │ border coils, lift     │
│ OK=GPIO19 (pull-up, act.L)│        │ button drv: PE10/PE11 ←──┼─ J8 ST/OK? (inferred)      │
│ display, buzzer, rain    │         │ motors, nav, KV-store+PIN│      │ hall sensors           │
└──────────────────────────┘         └──────────────────────────┘      └────────────────────────┘
```

Dowody: stringi i literały w U13 (`driver_dpport_snk_v1.c`, literał `0x40013800`; `driver_bdport_snk_v1.c`, literał `0x40004400`), w U16 (`driver_mboard_port_snk_v2.c`, literał `0x40004800`, stringi tylko "bdboard"/border/lift). Ścieżki J8 na `img/mainboard_bottom.jpg` biegną w stronę U13.

**Jeden format** na łączu ESP32 ↔ U13 (zweryfikowany przez 10 nagrań LA):
- **JSON** over UART at **230400 8N1**, standard polarity (not inverted)
- **Frame format**: `&{json}<CRC>#` (pojedynczy `#` — NIE `##` jak wcześniej dokumentowano)
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
| `0x10xxxxxx` | **ESP→MB** | Komendy klawiszy/akcji z UI (START/HOME/OK), patrz „Action Flow” |
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
| `0x20000002` | 536870914 | `error` | INIT_ERROR | Błąd inicjalizacji U13 (bit 0x4 = brak płytki wyświetlacza), co 2 s, potem reset przez watchdog |
| `0x20000004` | 536870916 | — | LINK_UP | Koniec handshake (×2); także „brak ramek od ESP przez 3 s” |
| `0x33000009` | 855638025 | `result` | SETTING_OK_09 | Setting confirm (unknown) |
| `0x33000010` | 855638032 | `result` | PIN_CHANGE_OK | PIN change confirmed |
| `0x33000011` | 855638033 | `result` | RTC_SET_OK | Year/time set confirmed |
| `0x33000012` | 855638034 | `result` | DATE_SET_OK | Date set confirmed |
| `0x33000013` | 855638035 | `result` | TIME_SET_OK | Time set confirmed |
| `0x33000014` | 855638036 | `result` | START_TIME_OK | Start time confirmed |
| `0x33000015` | 855638037 | `result` | HOURS_SET_OK | Daily hours confirmed |
| `0x33000017` | 855638039 | `result` | RAIN_SET_OK | Rain config confirmed |
| `0x33000021` | 855638049 | `result` | WIFI_STATUS_ACK | Reply to ESP `0x30000021` (WiFi status): `true`, or `false` for a bad field (`080461d8`). Not a PIN result (corrected 2026-10-10) |
| `0x33000022` | 855638050 | `result` | BT_STATUS_ACK | Reply to ESP `0x30000022` (BT status), same handler shape (`080464be`). Not a PIN result (corrected 2026-10-10) |
| `0x33000023` | 855638051 | `result` | RESET_PWD_ACK | Reply to `0x30000023`: `true` = "reset pwd success", `false` = "reset pwd failed" (env write failed) |
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
| `0x40000021` | 1073741857 | — | BOOT_ACK? | At boot, and also every ~0.5 s while mowing (alternating with `0x40000020 {"lv":255}`); meaning uncertain |
| `0x41000002` | 1090519042 | `lock:0/1` | LOCK | Lock state |
| `0x41000003` | 1090519043 | — | EXEC_ACTION | Akcja wykonana (po STOP/akcji) |
| `0x41000004` | 1090519044 | `err` | ERROR_NOTIFY | Error code notification |
| `0x41000005` | 1090519045 | (brak `pwd`) | **DEPARTURE** | MB→ESP zaraz po `0x10000001` (START+OK), przed `state:8` — odjazd do koszenia |
| `0x41000006` | 1090519046 | — | RETURN_HOME | Home/return to dock notification |
| `0x41000007` | 1090519047 | — | DOCKED_CHARGE | Docked / charge start |
| `0x41000008` | 1090519048 | — | SHUTDOWN | Power-off command |
| `0x41000020` | 1090519072 | `result` | PIN_UNLOCK_RESULT | Wysyłane ~10 ms po `0x41000005 {"pwd"}` z ESP (wszystkie captures). Wcześniej nazwane START_ACK, ale nigdy nie następuje po naciśnięciu START |
| `0x50000021` | 1342177313 | `bat:0..3` | BATTERY | Battery level |

> **Uwaga o `0x41000005`**: To ID komendy występuje w **obu kierunkach**:
> - **ESP→MB** z polem `{"pwd":9633}` — wysyłanie PIN do weryfikacji
> - **MB→ESP** bez pola `pwd` (puste `{}`) — notyfikacja przed `state:8` (seek wire?)

### ESP→MB (ESP32 wysyła do Mainboard)

| CMD HEX | DEC | JSON Fields | Nazwa | Opis |
|---------|-----|-------------|-------|------|
| `0x10000001` | 268435457 | — | KEY_START_CONFIRM | OK po START (≤3 s): **start koszenia** |
| `0x10000002` | 268435458 | — | KEY_HOME_CONFIRM | OK po HOME (≤3 s): **powrót do stacji** |
| `0x10000003` | 268435459 | — | KEY_EVT_3 | Zdarzenie UI 3 (wyzwalacz nieustalony) |
| `0x10000004` | 268435460 | — | POWER_OFF | **Wyłącza kosiarkę** (bit akcji 0x10, „Robot manual power off” we wszystkich procesach U13). Tak samo `0x10000014` i `0x10000024` |
| `0x10000007` | 268435463 | — | KEY_SELECT | Wciśnięto START lub HOME (otwiera okno 3 s na OK) |
| `0x10000008` | 268435464 | — | CLEAR_USER_SETTINGS | log ESP: "send command clear user setting". Also sent for cloud `cmd 113` |
| `0x10000009` | 268435465 | — | MEMS_CORRECTION | log ESP: "send command mems correction" |
| `0x22000000` | 570425344 | `rain:1/2` | RAIN | Rain sensor state: **1 = dry, 2 = raining** (sensor on the display board). U13 only starts rain handling on 2 (`service_rain`, `0x08039198`) |
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
| `0x30000023` | 805306403 | — | **RESET_PWD** | **Resets the PIN to `0000`**. Sent by the ESP for cloud `cmd 112`. U13 handler `0804650a`: sets env `pwd` = 0 (`0807c95c`), clears the wrong-PIN counter (`run_param[0]`) and re-enables PIN input (`run_param[1]` = 1), replies `0x33000023`. No state check [F] |
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
| 0 | After power-on, before `{"lock":1}` |
| 1 | Waiting for the PIN (after `{"lock":1}`, before `0x41000020`) |
| 2 | Transient, right after `0x41000020 {"result":1}`; always followed by `0x41000003` and `state:6`. **Not mowing** (all captures and the 2026-10-10 test) |
| 6 | Stopped / ready (rest state after unlocking and after every STOP) |
| 7 | Error (with `error:N`) |
| 8 | **Mowing**: `0x10000001` is answered by `0x41000005` (departure) and `state:8` (confirmed on the mower 2026-10-10) |
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

**Handshake jest pytanie-odpowiedź** (`captures/02-boot-pin`, la_decode, oraz U13 `0x080446e4`):

| MB wysyła | ESP odpowiada w ~3 ms | Limit w U13 |
|---|---|---|
| `0x40000009` BOOT_HEART, co ~100 ms | `0x40000006` ESP_INFO `{hv,sv,spw,mac}` | 25 prób (~2,5 s) |
| `0x40000008` BOOT_INIT, co ~20 ms | `0x40000001` ESP_INIT `{"init":3}` | 50 prób (~1 s) |
| `0x20000004` ×2 | — (łącze gotowe, potem DEVICE_INFO) | |

Bez tych odpowiedzi U13 uznaje płytkę wyświetlacza za odłączoną, wysyła co 2 s `0x20000002 {"error":…}` i nie karmi watchdoga sprzętowego, więc po ~16 s resetuje się i odcina zasilanie. Szczegóły: `20261009_claude_investigation.md` §10.

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
  0x33000021 {"result":true}       ← ack for ESP WiFi status 0x30000021
  0x33000022 {"result":true}       ← ack for ESP BT status 0x30000022
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
  0x300000A6 (bez pól)             ← zapytanie o harmonogram (z polami nadpisuje harmonogram!)
  0x300000A7, 0x300000A8 (bez pól) ← zapytania o deszcz i strefy
  0x41000005 {"pwd":9633}          ← PIN sent

Steady state:
  ESP: przed handshake 0x300000A1 co 100 ms; po nim 0x30000005 co ~500 ms, 0x30000021/22 co 1 s
  MB:  0x40000011 {"rtc":...} co ~1s
  ESP: 0x22000000 {"rain":1|2} — przy zmianie, 1 = sucho, 2 = deszcz (deszcz wysyła ESP, nie MB)
```

> **Korekta**: W poprzedniej wersji `0x22000000` (RAIN) był błędnie przypisany do MB→ESP.
> Deszcz jest na display board (J4 → ESP32 GPIO36), więc ESP wysyła ten stan do MB.

---

## Action Flow (START / STOP / HOME)

START, HOME i OK są podłączone do **ESP32** (GPIO22/21/19, aktywne niskim stanem, wewnętrzny pull-up). ESP32 prowadzi UI i wysyła do MB komendy klawiszy. STOP (czerwony, złącze `STOP`/U19 na płycie głównej) obsługuje MB sam (`stop_state`).

Logika z firmware ESP32 (`ota_0.bin` 3.02.02, pętla UI `0x400e27f4`, dyspozytor `0x400e2194`), tick 10 ms, debounce 70 ms:
- wciśnięcie START albo HOME → ESP zapamiętuje klawisz na 3 s i wysyła `0x10000007`,
- OK w ciągu 3 s po START → `0x10000001`; po HOME → `0x10000002`.

### START, potem OK (start koszenia), `captures/2026-06-21/trzeci`, D3=START, D4=OK

```
16.7372  START pressed
16.8128  ESP→MB 0x10000007          (+75 ms = debounce)
17.6022  OK pressed
17.6828  ESP→MB 0x10000001          (+80 ms)
17.7145  MB→ESP 0x41000005          (departure)
17.7364  MB→ESP 0x330000A0 {"state":8}
```

To samo w `drugi` (26.20 s / 27.33 s) i w `04-return-home` (28.35 s). Tam MB odpowiada `err:16`, bo kosiarka stała poza przewodem.

### HOME, potem OK (powrót do stacji), `captures/2026-06-21/czwarty`

```
17.6614  ESP→MB 0x10000007          (HOME nie był podpięty do LA)
18.0219  OK pressed
18.1013  ESP→MB 0x10000002
18.1488  MB→ESP 0x41000006          (RETURN_HOME)
18.1998  MB→ESP 0x330000A0 {"state":9}
20.2982  MB→ESP 0x330000A0 {"station":true}
```

### STOP (zatrzymanie), obsługiwany przez MB

```
MB→ESP: 0x330000A0 {"stop_state":1}
MB→ESP: 0x41000003                  (EXEC_ACTION / stop)
MB→ESP: 0x330000A0 {"state":6}
MB→ESP: 0x330000A0 {"stop_state":0}
```

### Po wprowadzeniu PIN

```
ESP→MB: 0x41000005 {"pwd":9633}
MB→ESP: 0x41000020 {"result":1}
MB→ESP: 0x330000A0 {"state":2} → 0x41000003 → {"state":6}
```

---

## Error Flow

```
MB detects error (lift, out of wire, etc.):
  → MB→ESP: 0x41000004 {"err":16}       (ERROR_NOTIFY)
  → MB→ESP: 0x330000A0 {"state":7,"error":16}  (ERROR state)
```

ESP **nie** wysyła potwierdzeń błędów. `0x1000000x` widoczne w captures po błędach to naciśnięcia klawiszy przez użytkownika (patrz wyżej).

### Error Codes

| Code | Display | Meaning |
|------|---------|---------|
| 16 | E11 | Lift/tilt/blocked (out of wire range) |

---

## PIN Verification Flow

```
Step   ESP32                                   U13 (dpport)
────   ─────                                   ────────────
1.     user enters PIN with START/HOME/OK
2.     {"cmd":1090519045,"pwd":9633} ────────▶  reads PIN from KV-store (key "pwd" @ RAM 0x2000027C), compares
3.     ◀──────── 0x41000020 {"result":1}
```

U13 asks for the PIN with `0x41000002 {"lock":1}` right after the handshake. Until it answers `0x41000020 {"result":1}` it ignores key and remote commands. After `result:1` it sends `state:2`, `0x41000003` and `state:6`.

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

JSON przez osobny UART: U16 `mport` (USART2), U13 `bdport` (USART1). U16 **nie** przekazuje ruchu ESP32. Jego wiadomości to dane przewodu granicznego, podnoszenia i wersji ("bdboard").

U13 firmware strings confirm architecture:
- `driver_dpport.c` / `driver_dpport_snk_v1.c` ("dpport drv") — **display port** driver, USART0 (literał `0x40013800`), bezpośrednio do ESP32
- `driver_bdport.c` / `driver_bdport_snk_v1.c` ("bdport drv") — **board port** driver (komunikacja z U16), USART1
- `driver_ledport_snk_v1.c` — trzeci port, UART3 (moduł LED/ultradźwięków, "ledport no board connect")
- `service_bdport.c` ("bdport srv") — board port service
- `deal_message.c` — message handling
- `add receive message head callback failed, head=%d` — dynamic command dispatch by ID

U13 parsuje JSON via cJSON (confirmed in firmware strings).
U13 has services: movement, map, time, blade, bms, border, multizone, hit, ultrasonic, slope, stop, power, config.

---

## OTA Protocol

OTA flows: Cloud → ESP32 → U13 (dpport). Firmware U16 ("BB") i innych płytek U13 programuje dalej swoimi portami (stringi `BB IAP start`, `LB IAP start`). Wniosek z firmware, nie z capture'u.

U13 OTA framing (from `FUN_08008cb8`):
```
[2B length LE] [N bytes data] [1B XOR checksum]
```

7 OTA commands: GET OTA INFO, DOWNLOAD, SET OTA MODE, SET VER, RETURN, SET FIRMWARE NUMBER, SET BAUDRATE.

---

## Key Architectural Facts

- **U16 runs FreeRTOS** — comm_task, init_task, init_bd
- **U16 uses EasyLogger v2.2.99** — logs to internal buffer
- **ESP uruchamia koszenie po UART**: `0x10000007`, potem `0x10000001` (dokładnie to, co robi oryginalny firmware po START+OK)
- **START/HOME/OK czyta ESP32** (GPIO22/21/19). STOP obsługuje MB. ON (K4) idzie tylko do MB
- **U16 JSON parser**: generic cJSON-based, handles arbitrary JSON
- **U16 limit**: max 128 bytes per message (mport driver)
- **U13 RTC** exists, sends RTC heartbeat (`0x40000011`) co ~1s do ESP
- **Rain sensor** jest na display board (J4 → ESP32 GPIO36), ESP wysyła stan do MB
- **PIN** przechowywany w U13 KV-store, weryfikowany przez U13, ESP tylko przesyła
- **Battery** info pochodzi z MB (`0x50000021`)
- **Device info** (name, sn, version, model, hw versions) pochodzi z MB (`0x330000A1`, `0x330000A2`)

---

---

## Project Status Summary (2026-10-10)

The ESPHome component `components/snk_mower` drives the mower. Confirmed on the hardware on 2026-10-10 (details and trace: `ha.md` §15).

### Works (confirmed on the mower)
- ✅ **U13 handshake**: `ESP_BOOT`, answers to `0x40000009`/`0x40000008`, KEEPALIVE every 500 ms, WiFi/BT every 1 s. Stable link, 0 bad frames. After an ESP-only restart (OTA), U13 answers `ESP_BOOT` by replaying its whole boot sequence.
- ✅ **PIN**: U13 sends `0x41000002 {"lock":1}`, the ESP answers `0x41000005 {"pwd":N}`, U13 replies `0x41000020 {"result":1}`. The PIN is a required YAML option (`pin:`).
- ✅ **Start mowing**: `0x10000007` + `0x10000001` → `0x41000005` + `state:8`, from HA and from START→OK.
- ✅ **Return to station**: `0x10000007` + `0x10000002` → `0x41000006` + `state:9`, from HA and from HOME→OK.
- ✅ **Remote stop**: `0x10000023` → `0x41000003` + `state:6`. The physical STOP is handled by U13 (`stop_state:1/0`).
- ✅ **Buttons** START/HOME/OK on ESP32 GPIO22/21/19.
- ✅ **Display**, buzzer, rain sensor, battery, statistics.

### Untested
- `0x10000015` (edge trim) from the station. Outside the station U13 ignores it, as the firmware says.
- `0x10000021/22` (remote start/home, as sent by the app).
- Docking and charging (`station:true`, `state:10`) in the component.
- Display colon (bit not found).

### Protocol constraints
1. **Until U13 accepts the PIN it silently ignores every key and remote command.**
2. **U13 powers the mower off ~16 s after boot without the handshake**, and sends `0x20000004` after 3 s without frames from the ESP (`20261009_claude_investigation.md` §10).
3. **POLL (`0x300000A1`) only before the handshake.** Afterwards U13 answers every POLL with `DEVICE_INFO` + `HW_VERSIONS` again.
4. **`0x41xxxxxx` are MB→ESP commands**, except `0x41000005` (PIN). Sending them to the MB does nothing.
5. **`0x10000004/14/24` power the mower off.**
6. **The PIN lives in U13**; the ESP only forwards it.
7. **A CRC byte can equal `{`**: the parser must start a frame at `&{`.

---

## Channel Mapping in Captures (Reference)

> **Krytyczne**: Kanały analizatora logicznego były podłączane różnie w różnych sesjach!

### Captures 01-06 (`captures/01-boot/` ... `captures/06-settings-all/`)

| Channel | Direction | Verification |
|---------|-----------|-------------|
| D0 | MB→ESP | Zawiera `0x20000001` (POWER_ON), `0x330000A1` (DEVICE_INFO), `0x50000021` (BATTERY) |
| D1 | ESP→MB | Zawiera `0x30000005` (KEEPALIVE), `0x22000000` (RAIN), `0x41000005 {"pwd":9633}` (PIN) |
| D2 | START button | (01, 02, 04) |

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
