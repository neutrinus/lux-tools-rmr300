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
| Pinout (assumed) | V+, GND, data (x2) — likely UART or I2C |

**NOTE:** HARDWARE.md previously listed J5 as 2-pin. It is **4-pin** (confirmed by visual inspection and replacement batteries).

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

### Communication protocol (partially known)

- U13 firmware contains **two drivers**: `driver_battery_snk_v1.c` and `v2.c`
- Both use **CRC-checked commands**: `battery connect failed, crc value=%d, receive crc=%d`
- Commands are sent and acknowledged: `send battery cmd failed, cansel cmd=%d`
- Periodic polling loop: `send_battery_state_loop`
- The protocol is **not reverse-engineered** (exact command bytes, baud rate, framing unknown)
- Physical layer likely UART or I2C on the two data pins of J5

### BMS log sample (from Brucke RM500 community)

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

The mower does not enforce a runtime limit — larger packs work without firmware changes.

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
