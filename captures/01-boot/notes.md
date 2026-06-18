# 01-boot — Cold boot, full startup sequence

## Capture
- **File:** `capture.vcd` (944 KB, 1 min @ 2 MHz, 120M samples)
- **Channels:** D0 (MB→ESP), D1 (ESP→MB), D2 (START button)
- **D2 trigger:** START pressed at ~3.449 s → MB power-on sequence

## Analysis

### Protocol
- JSON at **230400 8N1** (NOT 115200 binary as previously reverse-engineered)
- Signal is **standard polarity** (not inverted)
- U16 on mainboard acts as bridge/proxy between ESP32 and U13

### UART settings
```bash
sigrok-cli -i capture.vcd \
  -P "uart:baudrate=230400:data_bits=8:parity=none:stop_bits=1.0:rx=D0:format=hex" \
  -A "uart=rx-data"
```
No `invert_rx=yes` needed.

### Boot sequence (D0 = MB→ESP)

| Step | Direction | Command | Description |
|------|-----------|---------|-------------|
| 1 | MB→ESP | `0x20000001 action=0` | Power-on / wake |
| 2 | MB→ESP | `0x40000009` ×5 | Boot heartbeat |
| 3 | MB→ESP | `0x40000008` ×5 | Init in progress |
| 4 | MB→ESP | `0x50000021 bat=3` | Battery level (3 bars / 100%) |
| 5 | MB→ESP | `0x20000004` ×2 | Ready signal |
| 6 | MB→ESP | `0x330000A1` | **Device info** — name, S/N, version, model, features |
| 7 | MB→ESP | `0x330000A2` | **Hardware versions** — mainboard, BLDC, bumper, etc. |
| 8 | MB→ESP | `0x330000B0` | Map config (area=300m²) |
| 9 | MB→ESP | `0x40000020 lv=255` | Light sensor value |
| 10 | MB→ESP | `0x330000A6` | Schedule config (per-day) |
| 11 | MB→ESP | `0x330000A0 state=0` | Status: IDLE / LOCKED |
| 12 | MB→ESP | `0x41000002 lock=1` | Lock state |
| 13 | MB→ESP | `0x33000021 result=True` | **PIN verified OK** |
| 14 | MB→ESP | `0x330000A0 state=1` | Status: UNLOCKED/READY |
| 15 | MB→ESP | `0x40000011 rtc=TS` | **RTC heartbeat** (every ~1 s, continues) |

### D1 (ESP→MB)

| Step | Command | Description |
|------|---------|-------------|
| 1 | `0x40000004` | Boot notification |
| 2 | `0x30000005` | ACK / alive |
| 3 | `0x30000028 state=0` | ESP state: starting |
| 4 | `0x300000A1` ×N | Heartbeat keepalive |
| 5 | `0x30000021 wifi=0,str=0` | WiFi status (disconnected) |
| 6 | `0x30000022 bt=0,str=0` | BT status (disconnected) |
| 7 | `0x40000006 hv=60400,...` | ESP32 info (ESP32 HW/SW + MAC) |
| 8 | `0x40000001 init=3` | Init complete |
| 9 | `0x30000005` ×60 | Polling/keepalive |

### Device Info (from dump)
- **Name:** MyMower
- **Model:** RMC300E20V-ECDNSS
- **S/N:** 2312CGF250600035167
- **Version:** 31018 (firmware)
- **Battery:** 5S1P_EVE_2000 (5S Li-Ion), bat_t=8
- **PIN enabled:** yes (pwd_en=1)
- **Features:** rain, schedule, zone enabled; no LTE, solar, GPS, map
- **ESP32 MAC:** `08:f9:e0:b3:da:70`

### Key Observations
1. PIN is auto-sent on boot (recovered from U13 RAM: 9633) — mower unlocks without user interaction
2. After PIN, state transitions `0 → 1` (locked → ready)
3. RTC heartbeat (`0x40000011`) fires ~1 Hz after boot
4. D1 mostly sends `0x300000A1` and `0x30000005` as keepalive/polling
5. Lock (`0x41000002 lock=1`) is the LED lock indicator, not PIN lock
