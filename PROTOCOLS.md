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
│ #&{"cmd":...}\n     │          │ JSON bridge          │      │ Motor control        │
│                      │          │ FreeRTOS             │      │ KV-store + PIN      │
└──────────────────────┘          └──────────────────────┘     └──────────────────────┘
```

There is **one protocol** between ESP32 and U16 (verified by 6 LA captures):
- **JSON** over UART at **230400 8N1**, standard polarity (not inverted)
- Frame format: `#&{"cmd":<int>,"key":val,...}\n`
- The U16 acts as a **bidirectional JSON bridge** to U13

Previous documentation of a "binary 0xAA 0x55 protocol at 115200" was based on
preliminary decompilation and is **incorrect** — the wire traffic is JSON at 230400.

---

## Frame Format

```
#  &  { " c m d " :  1 2 3 ,  . . . }  \n
^  ^                                   ^
│  │                                   └─ 0x0A (LF) — line terminator
│  └─ 0x26 (&) — frame prefix
└─ 0x23 (#) — inter-frame separator
```

The first frame in a burst has no leading `#` (the `#` was the end of the previous burst).

Hex example (BOOT command):
```
26 7B 22 63 6D 64 22 3A 31 30 37 33 37 34 31 38 32 38 7D 0A
&  {  "  c  m  d  "  :  1  0  7  3  7  4  1  8  2  8  }  \n
```

---

## Command ID Structure

Command IDs are 32-bit integers with prefix-based routing:

| Prefix | Prefix (hex) | Source | Description |
|--------|-------------|--------|-------------|
| `0x20xxxxxx` | 0x20xxxxxx | MB→ESP | Power/action commands |
| `0x22xxxxxx` | 0x22xxxxxx | ESP→MB | Sensor data (rain) |
| `0x30xxxxxx` | 0x30xxxxxx | ESP→MB | ESP status/requests |
| `0x33xxxxxx` | 0x33xxxxxx | MB→ESP | Configuration & state |
| `0x40xxxxxx` | 0x40xxxxxx | Both | System/heartbeat |
| `0x41xxxxxx` | 0x41xxxxxx | MB→ESP | Lock status, PIN, errors |
| `0x50xxxxxx` | 0x50xxxxxx | MB→ESP | Battery info |

Full 64-command map with fields: [captures/README.md](captures/README.md)

---

## Key Commands

| Hex | Name | Fields | Direction | Description |
|-----|------|--------|-----------|-------------|
| `0x20000001` | POWER_ON | `action` | MB→ESP | Power-on wake signal |
| `0x20000004` | POWER_READY | — | MB→ESP | Ready signal |
| `0x22000000` | RAIN | `rain` | ESP→MB | Rain sensor data |
| `0x30000005` | ESP_KEEPALIVE | — | ESP→MB | Keepalive heartbeat |
| `0x30000021` | ESP_WIFI | `wifi, str` | ESP→MB | WiFi status |
| `0x30000022` | ESP_BT | `bt, str` | ESP→MB | Bluetooth status |
| `0x30000028` | ESP_STATE | `state` | ESP→MB | ESP state notification |
| `0x300000A1` | ESP_POLL | — | ESP→MB | Poll/keepalive |
| `0x330000A0` | STATUS | `state, bat_lv, bat_per` | MB→ESP | Mower status |
| `0x330000A1` | DEVICE_INFO | `name, sn, version` | MB→ESP | Device identity |
| `0x330000A2` | HW_VERSIONS | `mb_hv, bb_hv` | MB→ESP | Hardware versions |
| `0x33000021` | PIN_RESULT | `result` | MB→ESP | PIN verification result |
| `0x40000001` | ESP_INIT | `init` | ESP→MB | Init complete signal |
| `0x40000004` | ESP_BOOT | — | ESP→MB | Boot notification |
| `0x40000006` | ESP_INFO | `hv, sv, mac` | ESP→MB | ESP hardware/software info |
| `0x40000011` | RTC_HEARTBEAT | `rtc` | MB→ESP | RTC time heartbeat |
| `0x41000004` | ERROR_NOTIFY | `err` | MB→ESP | Error code |
| `0x41000005` | PIN_SEND | `pwd` | ESP→MB | Send PIN for verification |
| `0x50000021` | BATTERY | `bat` | MB→ESP | Battery level |

---

## Boot Sequence (verified by LA capture)

```
MB starts on its own (before any ESP message):
  0x20000001 action=0          ← power-on wake
  0x40000009 ×5                ← boot heartbeat
  0x40000008 ×5                ← init in progress
  0x50000021 bat=3             ← battery level
  0x20000004 ×2                ← ready signal
  0x330000A1                   ← device info (name, S/N, model, version)
  0x330000A2                   ← hardware versions
  0x330000B0                   ← map config

ESP then responds (within ~100ms of boot):
  0x40000004                   ← boot notification (BOOT)
  0x30000005                   ← keepalive
  0x30000028 state=0           ← ESP state: starting
  0x300000A1 ×N                ← polling / keepalive
  0x30000021 wifi=0,str=0      ← WiFi status (disconnected)
  0x30000022 bt=0,str=0        ← BT status (disconnected)
  0x40000006 hv=60400,...      ← ESP info (HW/SW/MAC)
  0x40000001 init=3            ← init complete

After PIN verify:
  MB sends 0x330000A0 state=0  ← IDLE
  MB sends 0x41000002 lock=1   ← lock indicator (not PIN lock, just LED)
  MB sends 0x330000A0 state=1  ← READY/UNLOCKED

Steady state:
  ESP sends 0x30000005 / 0x300000A1 (keepalive/poll) every ~100-200ms
  MB sends 0x40000011 rtc=...  ← RTC heartbeat every ~1s
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
4.     PIN_RESULT ←─────────────── translates JSON → PIN_RESULT
```

---

## Protocol: U16 ↔ U13 (Internal)

The U16 and U13 communicate via a separate UART (USART0 @ 230400) using the
same JSON format. U16 is the bridge — it:
- Receives JSON from ESP32 → forwards to U13
- Receives JSON from U13 → forwards to ESP32
- Also sends its own messages (sensor data, border coil readings)

U13 parses JSON commands via cJSON (confirmed in firmware strings).

---

## OTA Protocol

OTA flows: Cloud → ESP32 → U16 → U13 over the same UART channel.

U13 OTA framing (from FUN_08008cb8):
```
[2B length LE] [N bytes data] [1B XOR checksum]
```

7 OTA commands: GET OTA INFO, DOWNLOAD, SET OTA MODE, SET VER,
RETURN, SET FIRMWARE NUMBER, SET BAUDRATE.

---

## Key Architectural Facts

- **U16 runs FreeRTOS** — has comm_task, init_task, init_bd
- **U16 uses EasyLogger v2.2.99** — logs to internal buffer
- **U13 has no schedule logic** — all scheduling was in ESP32 NVS
- **U13 RTC exists** but is only used for display, not scheduling
- **U13 KV-store**: 18 entries × 12B in flash def table, 32 entries × 8B RAM cache
- **U16 JSON parser**: generic cJSON-based, handles arbitrary JSON
- **U16 limit**: max 128 bytes per message (mport driver)
