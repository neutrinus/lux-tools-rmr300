# UART captures

Logic-analyser recordings of the **original** display firmware talking to U13 on the J8 ribbon cable. The protocol they show is documented in [docs/protocol.md](../docs/protocol.md).

## Setup

- Analyser: FX2 clone (sigrok `fx2lafw`), 2 MHz (01–06) or 4 MHz (07–10).
- UART: 230400 8N1, standard polarity.
- Channels:

| Captures | MB → ESP | ESP → MB | Buttons |
|---|---|---|---|
| 01–06 (`capture.vcd`) | D0 | D1 | D2 = START (01, 02, 04) |
| 07–10 (`capture.sr`, sigrok session) | D2 | D1 | D3 = START, D4 = OK |

`decoded.json` (and `d0.json`/`d1.json` in 01) lists the frames per channel, without timing.

## Decoding

```bash
# one time-ordered timeline of both directions and the buttons
python3 tools/re/la_decode.py captures/02-boot-start-error/capture.vcd --uart D0=MB,D1=ESP --lines D2=START
python3 tools/re/la_decode.py captures/09-mow-stop-home/capture.sr --uart D1=ESP,D2=MB --lines D3=START,D4=OK

# frames per channel into decoded.json
python3 tools/decode_capture.py captures/01-boot/capture.vcd
```

## Index

| # | Directory | What happens |
|---|---|---|
| 01 | `01-boot` | Cold boot: handshake, device info, PIN sent automatically, lock → ready, clock heartbeat |
| 02 | `02-boot-start-error` | Boot, PIN, START → OK; the mower on the bench raises error 16 (no boundary signal) |
| 03 | `03-home-in-error` | HOME → OK pressed in the error state; display `E11` |
| 04 | `04-start-home-error` | Boot, PIN, START → OK, HOME → OK, error 16 |
| 05 | `05-shutdown` | Error state, date / time / mowing hours set in the menu, power off |
| 06 | `06-settings-all` | Every settings menu: date and time, start time, hours per day, days per week, rain sensor, PIN change |
| 07 | `07-pin-only` | Boot and PIN, no command |
| 08 | `08-start-error` | Boot, PIN, START → OK, mowing starts, error 16; STOP pressed |
| 09 | `09-mow-stop-home` | START → OK (mowing), STOP, HOME → OK (returning), STOP |
| 10 | `10-dock-charge` | HOME → OK in front of the station: returning, `station:true`, `0x41000007`, charging |

Timing reference from capture 09:

```
16.7372  START pressed
16.8128  ESP → MB 0x10000007            (+75 ms debounce)
17.6022  OK pressed
17.6828  ESP → MB 0x10000001
17.7145  MB → ESP 0x41000005            (departure)
17.7364  MB → ESP 0x330000A0 {"state":8}
```
