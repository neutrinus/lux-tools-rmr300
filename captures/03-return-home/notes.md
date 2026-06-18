# 03-return-home — HOME pressed during error state

## Capture
- **File:** `capture.vcd` (132 KB, ~30s @ 2 MHz)
- **Channels:** D0 (MB→ESP), D1 (ESP→MB)

## Context
- Previous state: ERROR (state=6) from 02-boot-pin (lift on bench)
- User pressed HOME → display showed "E11"

## Sequence
1. RTC_HEARTBEAT (continuing from previous session)
2. MB→ESP: **`0x41000004 err=16`** — error notification
3. MB→ESP: **`STATUS state=7, error=16`** — state=7 = ERROR (persistent)
4. RTC_HEARTBEAT continues
5. ESP→MB: `0x10000007` — ESP acknowledges error?
6. ESP→MB: `0x10000002` — unknown

## New Commands

| Cmd | Name | Fields | Direction | Meaning |
|-----|------|--------|-----------|---------|
| `0x41000004` | ERROR_NOTIFY | `err` | MB→ESP | Error code notification |
| `0x10000007` | ESP_ERROR_ACK | — | ESP→MB | Error acknowledgment |
| `0x10000002` | ESP_UNKNOWN | — | ESP→MB | Unknown |

## State Machine Update

| State | Value | Description |
|-------|-------|-------------|
| ERROR | 6 | Transient error (e.g., lift detected during mowing) |
| ERROR | 7 | Persistent error (displayed on screen with E-code) |

## Error Codes

| Code (decimal) | Display | Meaning |
|----------------|---------|---------|
| 16 | E11 | Lift/tilt/blocked (bench test) |

## Notes
- HOME button during error state does NOT trigger return sequence
- Error 16 likely corresponds to lift sensor (mower on box)
- Error persists in state=7 until cleared (power off or OK button?)
