# 04-return-home — Full boot + PIN + START + HOME + error

> **Korekta 2026-10-09** (dowody: [`20261009_claude_investigation.md`](../../20261009_claude_investigation.md)): `0x10000007` = wciśnięto START/HOME, `0x10000001` = OK po START (start koszenia), `0x10000002` = OK po HOME (powrót). To nie są potwierdzenia błędów.

## Capture
- **File:** `capture.vcd` (708 KB, ~2 min @ 2 MHz)
- **Channels:** D0 (MB→ESP), D1 (ESP→MB)

## Sequence

### Boot + PIN (0–34)
Same as 01-boot and 02-boot-pin:
1. POWER_ON → boot → DEVICE_INFO → PIN (auto-sent) → state=0→1

### START pressed (48–51)
2. `0x41000020 result=1` (START ACK)
3. state=2 (MOWING)
4. `0x41000003` (mow command)
5. state=6 → state=7 error=16 (lift on box)

### HOME pressed (60–63)
6. `0x41000003` (HOME? same cmd as mow?)
7. state=6 → state=7 error=16 again

### Error handling (ESP→MB, later)
8. `0x10000007`, `0x10000002`, `0x10000001` — key commands (START/HOME → OK); `0x10000001` at 28.35 s follows OK after START (D2), then MB answers err 16

## Key Insight
`0x41000003` appears for BOTH mow start AND HOME — it's likely a generic "execute action" command that triggers whatever mode button was pressed.

## Error 16 persists
Error 16 (E11 on display) is the lift/tilt sensor. The mower is on a box, wheel contact lost.

## ESP Error Commands (new)

| Cmd | Name | Direction | Meaning |
|-----|------|-----------|---------|
| `0x10000001` | KEY_START_CONFIRM | ESP→MB | OK after START |
| `0x10000002` | KEY_HOME_CONFIRM | ESP→MB | OK after HOME |
| `0x10000007` | KEY_SELECT | ESP→MB | START or HOME pressed |

These likely acknowledge different error types or stages of error handling.
