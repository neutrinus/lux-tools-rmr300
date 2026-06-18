# 06-settings-all — Full menu walkthrough

## Capture
- **File:** `capture.vcd` (1.9 MB, ~3 min @ 2 MHz)
- **Channels:** D0 (MB→ESP), D1 (ESP→MB)

## User actions (in order)
1. Boot → auto PIN → state=1 (ready)
2. START → state=2 → error (state=6→7, error=16)
3. **Set date/time** (START 5s) — year=2026, month=6, day=18, time=18:42
4. **Set start time** (START+OK 3s) — 9:30
5. **Set mowing time/day** (OK 3s) — 2h (already set)
6. **Set days/week to 3** (HOME+OK 3s) — day=3, then changed back to day=5
7. **Set days/week to 7** (HOME+OK 3s) — day=7
8. **Rain sensor** (HOME 3s) — ON, delay=180 (no change)
9. **Change PIN** (START+HOME 3s) — old=9633, new=9633 (same)
10. **Days/week to 5** (HOME+OK 3s) — day=5
11. Then error persisted, eventually shutdown

## Complete Settings Protocol

### ESP→MB commands

| Cmd | Name | Fields | Setting |
|-----|------|--------|---------|
| `0x30000006` | SETTING_MODE | — | Enter a settings submenu |
| `0x30000007` | SETTING_APPLY | — | Confirm/apply setting |
| `0x30000009` | PIN_OLD | `old` | Old PIN for change |
| `0x30000010` | PIN_NEW | `pwd` | New PIN |
| `0x30000011` | SET_YEAR | `year` | Year |
| `0x30000012` | SET_DATE | `month, day` | Month and day |
| `0x30000013` | SET_TIME | `hour, minute` | Time of day |
| `0x30000014` | SET_START_TIME | `hour, minute` | Daily mowing start time |
| `0x30000015` | SET_DAILY_HOURS | `hour` | Hours per day (1-24) |
| `0x30000017` | SET_RAIN | `rain_en, rain_delay` | Rain sensor config |
| `0x30000027` | SET_DAYS_WEEK | `day` | Days per week (3, 5, 7) |
| `0x31000016` | SETTING_START | — | Enter settings menu |
| `0x31000017` | SETTING_SUBMENU | — | Enter submenu |

### MB→ESP commands

| Cmd | Name | Fields | Setting |
|-----|------|--------|---------|
| `0x33000009` | SETTING_OK_09 | `result` | Unknown setting confirm |
| `0x33000010` | PIN_CHANGE_OK | `result` | PIN change confirmed |
| `0x33000011` | RTC_SET_OK | `result` | Year set confirmed |
| `0x33000012` | DATE_SET_OK | `result` | Date set confirmed |
| `0x33000013` | SCHED_SET_OK | `result` | Time set confirmed |
| `0x33000014` | START_TIME_OK | `result` | Start time confirmed |
| `0x33000015` | HOURS_SET_OK | `result` | Daily hours confirmed |
| `0x33000017` | RAIN_SET_OK | `result` | Rain config confirmed |
| `0x33000027` | DAYS_WEEK_OK | `result` | Days per week confirmed |
| `0x40000012` | START_TIME_QUERY | `hour, minute` | MB queries start time |
| `0x40000013` | CUT_TIME_QUERY | `len` | MB queries cut length (min) |
