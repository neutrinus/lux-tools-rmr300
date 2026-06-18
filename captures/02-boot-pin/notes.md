# 02-boot-pin — Boot + PIN entry + START + error on bench

## Capture
- **File:** `capture.vcd` (876 KB, ~2 min @ 2 MHz)
- **Channels:** D0 (MB→ESP), D1 (ESP→MB), D2 (START button)

## Sequence

### Boot (same as 01-boot)
1. POWER_ON → BOOT_HEART(×5) → BOOT_INIT(×5) → BATTERY → POWER_READY
2. DEVICE_INFO + HW_VERSIONS (×2)
3. MAP_CFG, LIGHT, BATTERY, UNKNOWN_14
4. DEVICE_INFO + HW_VERSIONS (×3)

### PIN & Unlock
5. ESP→MB: **`0x41000005 pwd=9633`** ← ESP wysyła PIN!
6. MB→ESP: `PIN_RESULT result=True`
7. MB→ESP: `PIN_RESULT2 result=True`
8. SCHEDULE, UNKNOWN_AA, BOOT_ACK
9. MB→ESP: `STATUS state=0` (IDLE)
10. MB→ESP: `LOCK lock=1`
11. MB→ESP: `STATUS state=1` (UNLOCKED/READY)
12. MULTIZONE config

### START pressed → MOW → ERROR
13. RTC_HEARTBEAT (~1s)
14. MB→ESP: **`0x41000020 result=1`** (START button ACK?)
15. MB→ESP: **`STATUS state=2`** (MOWING)
16. MB→ESP: **`0x41000003`** (unknown cmd)
17. MB→ESP: **`STATUS state=6`** (ERROR — lift/blocked on bench)
18. RTC_HEARTBEAT continues

## New Commands Discovered

| Cmd | Name | Fields | Direction | Meaning |
|-----|------|--------|-----------|---------|
| `0x41000005` | PIN_SEND | `pwd` | ESP→MB | Send 4-digit PIN |
| `0x41000020` | START_ACK | `result` | MB→ESP | START button acknowledged |
| `0x41000003` | MOW_CMD | — | MB→ESP | Mow start command |

## State Machine

| State | Value | Description |
|-------|-------|-------------|
| IDLE | 0 | Powered on, locked or waiting |
| READY | 1 | Unlocked, PIN verified |
| MOWING | 2 | Mower active |
| ERROR | 6 | Error (lift, tilt, block) |

## Key Observations
1. **PIN is sent by ESP as JSON** — `{"cmd":1090519045,"pwd":9633}` — not via separate binary protocol
2. PIN is stored in ESP32 memory and auto-sent after boot
3. State transitions: 0→1 (after PIN) → 2 (START) → 6 (error on bench because no ground contact)
4. `state=6` persists after error (doesn't auto-revert to 0 or 1)
