# Capture: pierwszy — PIN entry + boot

**Duration**: ~43s (interrupted with Ctrl+C)
**Action**: Power on, enter PIN (9633), do NOT press START.

## Key events (corrected channels: D1=ESP TX, D2=MB TX)

| Direction | CMD | Description |
|-----------|-----|-------------|
| ESP→MB | `0x40000004` | BOOT handshake |
| ESP→MB | `0x40000001 {"init":3}` | INIT confirmation |
| ESP→MB | `0x30000005` | Keepalive (continuous) |
| ESP→MB | `0x30000028 {"state":0}` | ESP_STATE=idle |
| ESP→MB | `0x30000021 {"wifi":0,"str":0}` | WiFi status |
| ESP→MB | `0x30000022 {"bt":0,"str":0}` | BT status |
| ESP→MB | `0x22000000 {"rain":1}` | Rain sensor (on display board) |
| ESP→MB | `0x40000006 {"hv":...,"sv":...,"mac":"08-f9-e0-b3-da-70"}` | ESP info (MAC from ESP32) |
| ESP→MB | `0x300000A6` | ESP_TRIM |
| ESP→MB | `0x41000005 {"pwd":9633}` | **PIN sent** (ESP→MB) |
| MB→ESP | `0x20000001 {"action":0}` | POWER_ON, action=0 (idle) |
| MB→ESP | `0x40000009` | BOOT_HEART |
| MB→ESP | `0x50000021 {"bat":2}` | Battery |
| MB→ESP | `0x330000B0 {"map_sn":0,"area":300}` | Map / area |
| MB→ESP | `0x40000020 {"lv":255}` | Light level |
| MB→ESP | `0x330000A1 {"name":"MyMower",...}` | **Full device info** (name, sn, version) |

## Notes

- PIN 9633 appears as `0x41000005 {"pwd":9633}` on D1 (= ESP TX) → **ESP sends PIN to MB** ✓
- Button D3/D4 show no changes — weak probe contact
