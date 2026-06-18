# 05-shutdown — Settings + Shutdown

## Capture
- **File:** `capture.vcd` (482 KB, ~1 min @ 2 MHz)
- **Channels:** D0 (MB→ESP), D1 (ESP→MB)

## User actions
- Error state (E11) → pressed HOME (tried to clear) → set date/time → set daily cutting hours → powered off

## Sequence

### Error continued (0–47)
1. RTC heartbeat in error state (state=7, error=16)
2. HOME pressed: `0x41000003` → state=6 → error=16 → state=7
3. HOME again: state=6 → error=16 → state=7

### Settings query (48)
4. MB→ESP: **`0x40000013 len=120`** — asks about max cutting time (120 min)

### Schedule config (61–63)
5. MB→ESP: **`0x33000015 result=True`** — schedule setting confirmed
6. MB→ESP: SCHEDULE (full schedule block sent)

### Date/Time settings from ESP→MB (149–195)
7. ESP→MB: `0x31000016` — new cmd, maybe setting mode
8. ESP→MB: `0x30000006` — new cmd, maybe "entering settings"
9. ESP→MB: **`0x30000015 hour=2`** — set daily cutting hours to 2
10. ESP→MB: `0x30000007` — confirm/apply
11. MB→ESP: `0x33000011 result=True` — year set OK
12. MB→ESP: `0x33000012 result=True` — date set OK
13. MB→ESP: `0x33000013 result=True` — time set OK
14. ESP→MB: **`0x30000011 year=2026`** — set year
15. ESP→MB: **`0x30000012 month=6, day=18`** — set date
16. ESP→MB: **`0x30000013 hour=18, minute=38`** — set time

### Shutdown (98–99)
17. MB→ESP: **`0x41000008`** — SHUTDOWN command
18. MB→ESP: **`STATUS state=11`** — state 11 = POWER OFF

## New Commands

### ESP→MB (Settings)

| Cmd | Name | Fields | Meaning |
|-----|------|--------|---------|
| `0x30000006` | SETTING_MODE | — | Enter settings mode |
| `0x30000007` | SETTING_APPLY | — | Apply/confirm setting |
| `0x30000011` | SET_YEAR | `year` | Set year |
| `0x30000012` | SET_DATE | `month, day` | Set month and day |
| `0x30000013` | SET_TIME | `hour, minute` | Set time of day |
| `0x30000015` | SET_DAILY_HOURS | `hour` | Set daily mowing hours |
| `0x31000016` | SETTING_START | — | Enter settings menu |

### MB→ESP (Settings)

| Cmd | Name | Fields | Meaning |
|-----|------|--------|---------|
| `0x33000011` | TIME_SET_OK | `result` | Year/time set confirmed |
| `0x33000012` | DATE_SET_OK | `result` | Date set confirmed |
| `0x33000013` | SCHED_SET_OK | `result` | Schedule set confirmed |
| `0x33000015` | HOURS_SET_OK | `result` | Daily hours set confirmed |
| `0x40000013` | CUT_TIME_QUERY | `len` | Query max cutting time (minutes) |

### MB→ESP (Shutdown)

| Cmd | Name | Fields | Meaning |
|-----|------|--------|---------|
| `0x41000008` | SHUTDOWN | — | Power-off command |

## State Machine Update

| State | Value | Description |
|-------|-------|-------------|
| SHUTDOWN | 11 | Powering off / shutdown |

## Settings discovered
- User configured: **daily hours=2**, **date=2026-06-18**, **time=18:38**, **max cut time=120 min**
- Settings flow: ESP→MB sends values, MB confirms with `0x3300001x result=True`
