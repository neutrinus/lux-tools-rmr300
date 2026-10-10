# Battery Pack — SNK Mower

## Overview

The mower uses a **5S Li-Ion** smart battery pack (5 cells in series, 18–21V nominal).

Unlike a dumb battery, this pack has its own **BMS with digital communication** to the mainboard (U13). The mainboard queries it for voltage, per-cell voltage, temperature, cycle count, and health.

## Connector — J5 (on mainboard)

| Property | Detail |
|----------|--------|
| Label | `J5` / `BATTERY` |
| Type | 4-pin white connector |
| Mating plug | JST VLP-04VJST (or compatible VL-connector 4-polig) |
| Pinout | V+, GND and the BMS data line: a half-duplex UART to U13 (see below). Which J5 pin carries it has not been traced |

## BMS — on the battery pack

The battery pack contains a **smart BMS** (fuel gauge + protection) that communicates digitally with U13.

### What the BMS reports (from firmware strings)

| Data | Format |
|------|--------|
| Total voltage | `bat voltage=%dmV, ocv=%dmV` |
| Per-cell min/max | `battery cell max=%d, min=%d` |
| Temperature | `temp=%d` (NTC thermistor inside pack) |
| State of charge | `percent=%d` |
| Cycle count | `sony battery charge times =%d` |
| Health | `health=%d` |
| BMS model | `bms model=%d` |
| Battery ID | `battery id changed` |

### BMS protocol

- **U13 USART2 (`0x40004800`), 19200 8N1, half-duplex** on PD8/PD9 (init `0x080213c0`: GPIOD pin `0x100`, `mov.w r1,#0x4b00`). The driver switches PD8 between transmit and receive.
- Two drivers, `driver_battery_snk_v1.c` and `v2.c`, same protocol.
- Request: `1C A1 <LEN> <opcode> <args…> <CRC>`. Response: `3A A3 <LEN> <opcode> …<CRC>`. `LEN` counts the bytes after it (opcode + args + CRC). CRC is CRC-8/MAXIM (the same as on the display link) over opcode + args.
- The request templates are in the U13 image (`.data`, compressed); in our dump `1C A1 03 C1 01 2E` sits at `0x0808d2b9` and `1C A1 09 CE 55…` at `0x0808d2e7`.

| Opcode | Request | Purpose |
|--------|---------|---------|
| `C1` | `1C A1 03 C1 01 2E` | Connect / read voltage, current, temperature. Up to 4 tries at start-up |
| `C3` | `1C A1 03 C3 01 BF` | Pack info (type, capacity, status), polled periodically |
| `53` | `1C A1 05 53 00 02 1A 22` | Cell voltages, polled periodically |
| `CE` | `1C A1 09 CE 55 55 55 55 55 55 55 6E` | Wake / resynchronise the link; sent at start-up, on a timeout and on a CRC error |
| `B0` | `1C A1 03 B0 11 C1` | Charge mode / state commands ("send cmd into charge", "send cmd exit charge" and three state commands). Which B-opcode is which is not mapped |
| `B1` | `1C A1 03 B1 00 C6` | |
| `B2` | `1C A1 03 B2 00 93` | |
| `B3` | `1C A1 03 B3 55 B3` | |
| `B4` | `1C A1 03 B4 0F 78` | |

Start-up sequence: `CE`, then `C1` (retry with `CE` on timeout), then `C3` and `53`. After that the BMS task keeps polling `C3` + `53`. There is no "enable discharge" command: the "can not discharger" messages are U13's own safety decisions.

A response to `C1` captured over SWD on the MI 302: `3A A3 08 C1 01 19 0E …`. The parser checks `3A A3`, opcode `C1` and the CRC, then reads a status byte, current (u16) and voltage (u16).

U13 also measures the pack directly with ADC0: PC5 (channel 15) = pack voltage, `mV = raw × 5.4277` (about 20.16 V at 100%), PC4 (channel 14) = current.

Source: [salonnikov/mower-stock-reverse](https://github.com/salonnikov/mower-stock-reverse) `reverse-v2/factory-map/05-bms-pack.md`; the USART2 init and the frame templates were checked in our U13 dump.

### BMS log sample (Brucke RM500)

```
I/bms ... 5S1P_SONY_VTC4, id=xxxxxxxxx, voltage=20234, ocv=20234, percent=100, temp=16degree, charging times=113, discharge times=123, health=0
```

### Temperature sensing

Temperature is measured by an **NTC thermistor inside the battery pack**, read by the BMS, and reported digitally to the mainboard. The mainboard uses it for charge protection:

- `charging wait temp protect overtime, battery temp=%d`
- `battery temperature high=%d, change to err`

## Known pack configurations (from firmware)

| Pack | Configuration | Capacity |
|------|-------------|----------|
| `5S1P_SUMSANG_20R` | 5S1P Samsung 20R | 2000 mAh |
| `5S1P_SUMSANG_25R` | 5S1P Samsung 25R | 2500 mAh |
| `5S1P_SUMSANG_29E` | 5S1P Samsung 29E | 2900 mAh |
| `5S1P_SONY_VTC4` | 5S1P Sony VTC4 | 2100 mAh |
| `5S2P_SONY_VTC4` | 5S2P Sony VTC4 | 4200 mAh |
| `5S1P_EVE_2000` | 5S1P EVE 2000 | 2000 mAh |

The mower does not enforce a runtime limit — larger packs work without firmware changes, as long as the BMS is one the firmware recognises. The 4 Ah pack of the Brucke RM800 (`5S2P_SONY_VTC4`) works as is. A pack with an unknown BMS gives "battery type not define" and the mower switches off before the PIN prompt (community reports).

## Replacement / upgrade

### Third-party replacements

Brand **vhbw** (sold on eBay by ElectroPapa and others) offers compatible 5S 20V packs:

| Variant | Capacity | Price (approx) |
|---------|----------|---------------|
| Standard | 1.5 Ah | €26 |
| High-capacity | 2.5 Ah | €37 |

These use the same 4-pin JST connector and contain their own BMS. Compatible with Lux Tools, Practixx, Ferrex, Landxcape, Scheppach, Kress, Worx, Sunseeker, and other SNK-based rebrands.

### DIY upgrade — keep the BMS

If you want more capacity:

1. Buy a cheap vhbw pack (or keep your original)
2. Open it carefully, desolder the old 18650 cells
3. Keep the BMS powered (or re-apply 20V quickly) to avoid potential lock
4. Spot-weld new cells in 5S (5S2P for 2× capacity, or more)
5. Reuse the original connector housing

**Risk:** Some BMS ICs (e.g. TI BQ series) enter permanent failure lock if cells are fully disconnected. The vhbw/original BMS is likely a simpler Chinese design that tolerates cell swaps, but this is unconfirmed.

### Alternative — buy a used mower for parts

The mower (OBI Lux Tools A-RMR-300-24) regularly appears on OLX/eBay Kleinanzeigen for ~€50 broken — you get a second complete battery and spare parts.

## Charging

Charging is done by the mainboard via the **CHA** connector (C+, C− from the docking station). The mainboard talks to the BMS during charging:

- `bat full finish, vol =%d, temp=%d, percent=%d`
- `charging overtime but battery full, current=%d, vol=%d, temp=%d`
- `charging wait temp protect overtime, battery temp=%d`

The BMS controls charge termination based on voltage, current, temperature, and time.
