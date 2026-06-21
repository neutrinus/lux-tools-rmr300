# Inter-Chip Communication Protocols

## Overview

```
ESP32 (Display Board)              U16 (Board MCU)              U13 (Main MCU)
┌──────────────────────┐          ┌──────────────────────┐     ┌──────────────────────┐
│ ESP32-WROOM-32UE     │  JSON    │ GD32F303CGT6         │JSON │ GD32F305AGT6         │
│                      │ 230400   │                      │230400│                      │
│ GPIO17 (TX) ─────────┼──────────┼→ U16 RX              │─────→│ USART0               │
│ GPIO16 (RX) ←────────┼──────────┼← U16 TX              │←─────│                      │
│                      │ 8N1      │                      │ 8N1  │                      │
│ &{"cmd":...}<CRC>##  │          │ JSON bridge          │      │ Motor control        │
│                      │          │ FreeRTOS             │      │ KV-store + PIN      │
└──────────────────────┘          └──────────────────────┘     └──────────────────────┘
```

**One protocol** between ESP32 and U16 (verified by 4 LA captures on 2026-06-21):
- **JSON** over UART at **230400 8N1**, standard polarity
- Frame format: `&{json}<CRC>##`
- U16 acts as a bidirectional JSON bridge to U13
- **CRC**: Dallas CRC-8 (MAXIM) over JSON bytes only (not including `&` prefix or `##` suffix)

Previous documentation of a "binary 0xAA 0x55 protocol at 115200" is **incorrect**.

---

## Frame Format

```
 &  { " c m d " :  1 2 3 , ... }  <CRC>  #  #
 ^                                   ^     ^  ^
 │                                   │     │  └─ 0x23 — frame terminator
 │                                   │     └─ 0x23 — frame terminator
 │                                   └─ Dallas CRC-8 (1 byte)
 └─ 0x26 (&) — frame prefix
```

Example (BOOT command):
```
26 7B 22 63 6D 64 22 3A 31 30 37 33 37 34 31 38 32 38 7D 0A 23 23
 &  {  "  c  m  d  "  :  1  0  7  3  7  4  1  8  2  8  }  \n CRC ##
```

---

## Command ID Structure

Command IDs are 32-bit integers. Prefix indicates subsystem:

| Prefix | Direction | Description |
|--------|-----------|-------------|
| `0x20xxxxxx` | ESP→MB | Action/command (action:0 at boot) |
| `0x22xxxxxx` | MB→ESP | Sensor data (rain) |
| `0x30xxxxxx` | MB→ESP | ESP status requests, keepalive, state |
| `0x33xxxxxx` | ESP→MB | Configuration, state reports, device info |
| `0x40xxxxxx` | Both | System: BOOT, INIT, INFO, RTC heartbeat |
| `0x41xxxxxx` | ESP→MB | PIN, LOCK, START_ACK, EXEC_ACTION, HOME |
| `0x50xxxxxx` | ESP→MB | Battery info |

**NOTE**: Prefix direction above is from LA captures. Earlier docs reversed some prefixes.

---

## Complete Command Catalog

### D1: MB→ESP (U16/13 wysyła do ESP32)

| CMD HEX | DEC | JSON Fields | Opis |
|---------|-----|-------------|------|
| `0x22000000` | 570425344 | `rain:0/1` | Rain sensor state |
| `0x30000005` | 805306373 | — | Keepalive (ciągły, ~100ms) |
| `0x30000021` | 805306401 | `wifi, str` | WiFi status request |
| `0x30000022` | 805306402 | `bt, str` | BT status request |
| `0x30000028` | 805306408 | `state` | ESP state notification |
| `0x300000A1` | 805306529 | — | Poll/heartbeat (ciągły) |
| `0x300000A6` | 805306534 | — | ESP_TRIM (trim schedule request) |
| `0x300000A7` | 805306535 | — | (nieznane, po TRIM) |
| `0x300000A8` | 805306536 | — | (nieznane, po TRIM) |
| `0x40000001` | 1073741825 | `init` | INIT confirmation |
| `0x40000004` | 1073741828 | — | BOOT handshake |
| `0x40000006` | 1073741830 | `hv, sv, mac` | ESP HW/SW/MAC request |
| `0x41000005` | 1090519045 | `pwd` (opc.) | PIN_SEND (od ESP lub MB?) |
| `0x10000001` | 268435457 | — | ESP_ERR_ACK1 |
| `0x10000002` | 268435458 | — | ESP_ERR_ACK? |
| `0x10000007` | 268435463 | — | ESP_ERR_ACK2 |

### D2: ESP→MB (ESP32 wysyła do U16)

| CMD HEX | DEC | JSON Fields | Opis |
|---------|-----|-------------|------|
| `0x20000001` | 536870913 | `action:0` | Action command (idle) |
| `0x20000004` | 536870916 | — | Action ready |
| `0x33000020` | 855638176 | `state, bat_lv, bat_per, ...` | **Stan kosia** (raport cykliczny) |
| `0x33000021` | 855638177 | `name, sn, version, model, ...` | **Device info** (pełna konfiguracja) |
| `0x33000022` | 855638050 | `result` | PIN result (true) |
| `0x33000030` | 855638192 | `map_sn, area` | Map/schedule info |
| `0x330000A6` | 855638182 | `trim, auto, sun_st, ...` | Schedule config (trim times) |
| `0x330000A7` | 855638183 | `rain_en, rain_delay` | Rain config |
| `0x330000A8` | 855638184 | `mul_en, mul_auto, mul_z1..4, ...` | Multi-zone config |
| `0x330000A9` | 855638186 | — | (nieznane) |
| `0x40000008` | 1073741832 | — | System init |
| `0x40000009` | 1073741833 | — | Boot heartbeat |
| `0x40000011` | 1073741841 | `rtc` | **RTC time sync** (co ~1s) |
| `0x40000014` | 1073741844 | — | (nieznane) |
| `0x40000020` | 1073741856 | `lv` | Level? |
| `0x40000021` | 1073741857 | — | (nieznane) |
| `0x41000002` | 1090519042 | `lock:0/1` | LOCK state |
| `0x41000003` | 1090519043 | — | EXEC_ACTION (po STOP) |
| `0x41000004` | 1090519044 | `err` | ERROR_NOTIFY |
| `0x41000005` | 1090519045 | `pwd` (opc.) | PIN_SEND / nieznane |
| `0x41000006` | 1090519046 | — | **★ HOME / RETURN TO DOCK** |
| `0x41000007` | 1090519047 | — | **★ DOCKED / CHARGE START** |
| `0x41000020` | 1090519072 | `result:1` | **START_ACK** (po START/HOME) |
| `0x50000021` | 1342177313 | `bat:0..3` | Battery level |

---

## Stany MB (pole `state` w `0x33000020`)

| State | Znaczenie |
|-------|-----------|
| 0 | Idle (po włączeniu, przed PIN) |
| 1 | Ready (PIN odblokowany) |
| 2 | **MOWING** (lub jazda) |
| 6 | Stop / pauza |
| 7 | Error (z polem `error:N`) |
| 8 | Seek wire? |
| 9 | **RETURNING TO DOCK** |
| 10 | **CHARGING** |

Dodatkowe pola w `0x33000020`:
- `station: bool` — wykryto stację ładującą
- `border_state: 0/1` — kabel ograniczający
- `stop_state: 0/1` — przycisk STOP wciśnięty
- `rain_state: 0/1` — deszcz
- `bat_per: 0..100` — poziom baterii procentowo
- `bat_lv: 0..3` — poziom baterii (kategoryczny)

---

## Boot Sequence (verified by LA captures)

```
MB starts on its own:
  0x20000001 {"action":0}          ← power-on wake
  0x40000009 ×N                    ← boot heartbeat
  0x40000008 ×N                    ← init in progress
  0x50000021 {"bat":2}             ← battery level
  0x20000004 ×N                    ← ready signal
  0x33000021 {"name":"MyMower",   ← device info (full config)
              "sn":"...",
              "version":31018,
              "model":"RMC300E20V-ECDNSS",
              "avail":4,"pwd_en":1,...}
  0x33000022 {"mb_hv":22500,...}   ← hardware versions
  0x33000030 {"map_sn":0,"area":300} ← map config
  0x330000A6 {"trim":36,"auto":false, ← schedule config
              "sun_st":570,...}
  0x33000020 {"state":0,...}       ← initial state report

ESP responds (within ~100ms):
  0x40000004 (BOOT)
  0x30000005 (KEEPALIVE) — ciągle
  0x30000028 {"state":0}
  0x300000A1 (POLL) — ciągle
  0x30000021 {"wifi":0,"str":0}
  0x30000022 {"bt":0,"str":0}
  0x40000006 {"hv":60400,"sv":30202,"mac":"..."}
  0x40000001 {"init":3}

Steady state:
  ESP: 0x30000005 / 0x300000A1 co ~100-200ms
  MB:  0x40000011 {"rtc":...} co ~1s
  MB:  0x22000000 {"rain":0/1} — przy zmianie
```

---

## Action Flow (START / STOP / HOME)

**Wszystkie akcje fizyczne** — START, STOP, HOME — są obsługiwane bezpośrednio przez U16 (przyciski podpięte do U16, nie do ESP32). ESP32 tylko otrzymuje notyfikacje stanu i wysyła ACK.

### START (rozpoczęcie koszenia)

```
User → START button (physical, to U16)
  → U16 starts mowing internally
  → U16 notifies ESP via state change
  → ESP sends: 0x41000020 {"result":1} (START_ACK)
  → State: 0→1→2 (MOWING)
```

### STOP (zatrzymanie)

```
User → STOP button (physical, to U16)
  → U16 stops motors
  → ESP sends: 0x41000003 (EXEC_ACTION)
  → State: 2→6 (STOP)
```

### HOME (powrót do stacji)

```
User → HOME button (physical, to U16)
  → U16 drives off station briefly (state:2)
  → U16 follows boundary wire to station
  → ESP sends: 0x41000006 (HOME/RETURN)
  → State: 6→9 (RETURNING TO DOCK)
  → Mower reaches station: station:true
  → ESP sends: 0x41000007 (DOCKED/CHARGE)
  → State: 9→10 (CHARGING)
```

---

## PIN Verification Flow

```
Step   ESP32                        U16                          U13
────   ─────                        ───                          ───
1.     JSON {"cmd":1090519045,      receives, forwards           receives JSON
             "pwd":9633}                                          
       ──────────────────────────▶  ──────────────────────────▶
2.                                                              reads PIN from EEPROM
                                                                compares
3.                                JSON result ←────────────────  sends OK/FAIL
4.     PIN_RESULT ←─────────────── translates JSON → result
5.     ESP sends: 0x41000002 {"lock":1}
6.     State: 0→1 (READY)
```

UWAGA: W drugim i kolejnych bootach (jeśli nie było断电), PIN jest zapamiętany — ESP nie musi go ponownie wysyłać. Stan przechodzi od razu z 0→1.

---

## Frame CRC

CRC = Dallas CRC-8 (polynomial 0x31, also known as MAXIM CRC-8).

Counted over JSON bytes only (between `&` and CRC byte, exclusive).

Verification tool: `python3 -c "import crcmod; c=crcmod.mkCrcFun(0x131); print(hex(c(b'{\"cmd\":1073741828}')))"`

---

## Protocol: U16 ↔ U13 (Internal)

Same JSON format at 230400. U16 is a transparent bridge + adds its own messages (sensor data, border coil readings).

U13 parses JSON via cJSON (confirmed in firmware strings).

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
- **ESP cannot trigger mowing via UART** — no command exists
- **All actions are physical buttons → U16** — START/STOP/HOME
- **U16 JSON parser**: generic cJSON-based, handles arbitrary JSON
- **U16 limit**: max 128 bytes per message (mport driver)
- **U13 RTC** exists but only used for display, not scheduling
