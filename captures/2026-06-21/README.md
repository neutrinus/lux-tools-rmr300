# Captures 2026-06-21 — LA UART sniffer (original firmware)

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

1. **START/STOP/HOME are physical buttons → U16** — do NOT go through UART
2. **ESP cannot send "start mowing" command** via UART — it does not exist
3. **MB sends** `0x41000020` (START_ACK) to ESP after physical start — ESP is only notified
4. **MB sends** `0x41000006` (RETURN_HOME) to ESP after physical HOME
5. **MB sends** `0x41000007` (DOCKED_CHARGE) to ESP after docking
6. ESP sends to MB: KEEPALIVE, POLL, RAIN, PIN, WiFi/BT status, ESP_INFO, error ACKs
7. To control via HA, GPIO ESP would need to be connected to button lines (but those go to U16, not ESP)
