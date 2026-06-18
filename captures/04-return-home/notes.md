# 04-return-home — Full boot + PIN + START + HOME + error

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
8. `0x10000007`, `0x10000002`, `0x10000001` — ESP error handling commands

## Key Insight
`0x41000003` appears for BOTH mow start AND HOME — it's likely a generic "execute action" command that triggers whatever mode button was pressed.

## Error 16 persists
Error 16 (E11 on display) is the lift/tilt sensor. The mower is on a box, wheel contact lost.

## ESP Error Commands (new)

| Cmd | Name | Direction | Meaning |
|-----|------|-----------|---------|
| `0x10000001` | ESP_ERR_ACK1 | ESP→MB | Error handling |
| `0x10000002` | ESP_ERR_ACK2 | ESP→MB | Error handling |
| `0x10000007` | ESP_ERR_ACK7 | ESP→MB | Error handling |

These likely acknowledge different error types or stages of error handling.
