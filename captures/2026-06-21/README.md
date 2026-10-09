# Captures 2026-06-21 — LA UART sniffer (original firmware)

> **Korekta 2026-10-09** (dowody: [`20261009_claude_investigation.md`](../../20261009_claude_investigation.md)): wnioski 1–3 i 6–7 poniżej są błędne, patrz poprawiona lista.

## Setup

- **LA**: fx2lafw (Saleae Logic clone), 4MHz samplerate
- **Channels**: D1=ESP TX (←), D2=MB TX (→), D3=START, D4=OK
- **Connection**: parallel to J2 on display PCB
- **Issue**: ON (brown wire) interfered — detached
- **Baud**: 230400 8N1
- **Frame format**: `&{json}<CRC>#` (single `#`)

> **CORRECTION 2026-06-22**: Channel directions were REVERSED in the original version of this directory.
> See individual capture notes for verified direction assignments.

## Captures

| Capture | Duration | Action | Notes |
|---------|----------|--------|-------|
| [pierwszy/](pierwszy/) | ~43s | PIN entry + boot, no START | Full boot sequence, PIN `9633` sent by ESP |
| [drugi/](drugi/) | ~60s | PIN + START + error 16 | Mowing starts (state=2), then error 16 (out of wire) |
| [trzeci/](trzeci/) | ~60s | Full cycle: START→MOW→STOP→HOME→STOP | `0x41000006` RETURN_HOME confirmed, state:8 observed |
| [czwarty/](czwarty/) | ~60s | Docking: HOME+OK → charge | `0x41000007` DOCKED_CHARGE, state:10=CHARGING, `station:true` |

## Key conclusions (corrected directions)

1. **START/HOME/OK are read by the ESP32** (GPIO22/21/19); STOP is handled by the MB. Decode with `tools/re/la_decode.py ... --uart D1=ESP,D2=MB --lines D3=START,D4=OK`
2. **ESP starts mowing via UART**: START → `0x10000007` (+75 ms), OK → `0x10000001` (+80 ms) → MB `0x41000005`, `state:8` (trzeci 16.74–17.74 s)
3. `0x41000020` is the answer to the PIN command `0x41000005 {pwd}`, not a START ack
4. **MB sends** `0x41000006` (RETURN_HOME) to ESP after physical HOME
5. **MB sends** `0x41000007` (DOCKED_CHARGE) to ESP after docking
6. ESP sends to MB: KEEPALIVE, POLL, RAIN, PIN, WiFi/BT status, ESP_INFO, key commands `0x1000000x` (HOME→OK = `0x10000007`,`0x10000002`, czwarty 17.66/18.10 s)
7. To control via HA, send the same key commands from ESPHome (`start_mowing()` / `return_to_dock()`)
