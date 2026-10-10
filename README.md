# SNK Mower — Lux Tools A-RMR-300-24 & clones

> **Korekta 2026-10-09** (dowody: [`20261009_claude_investigation.md`](20261009_claude_investigation.md)): ESP32 łączy się UART-em bezpośrednio z U13 (`dpport`, USART0); U16 nie jest mostem, tylko MCU czujników przewodu/podnoszenia na osobnym porcie U13 (`bdport`). Przyciski START/HOME/OK są na ESP32 GPIO22/21/19 (pull-up, aktywne niskim); `0x10000001/2/7` to komendy klawiszy (START+OK = start koszenia, HOME+OK = powrót), nie potwierdzenia błędów.

![A-RMR-300-24](img/a-rmr-300-24.png)

This project documents the **SNK OEM** robot mower platform — the same hardware
sold under many brands across Europe. It covers everything you need: PIN recovery,
firmware dumps, protocol analysis, and the current status of replacing the
original firmware with ESPHome.

| Brand | Model(s) | Sold at / Region |
|-------|----------|------------------|
| **Lux Tools** | A-RMR-300-24, Oryx 300 Vision A-RMR-300-26 | OBI (PL/DE) |
| **Scheppach** | BRMR300, BTRM300, RRMA300 | Bauhaus, Aldi, Blocket (DE/SE) |
| **Brucke** | RM500, RM501, RM800 | Finland |
| **Adano** | RM5 | Harald Nyborg, Schou (DK/SE) |
| **Gomag** | Go-MR300 | DE |
| **Grouw** | City 300² | Schou (Scandinavia) |
| **Smart** | 365 500m² | Schou (Scandinavia) |
| **Meec Tools** | 300 m² (art. 027415) | Jula (FI/SE) |
| **Julan** | 300 m² | Jula (FI/SE) |
| **Landxcape** | (referenced in firmware) | — |
| **Sunseeker** | V1 300m² Vision AI | Puuilo (FI) |

All share part numbers: mainboard `80102372-01`, display `80102373-01`.
OEM: SNK (also SK-Robot for MQTT cloud).

Interesting fact: every mower has WiFi and Bluetooth on the ESP32, and the original
firmware has a full MQTT cloud client (`server.sk-robot.com`, plain MQTT). Brucke and
Sunseeker owners use it with the Sunseeker app. Lux does not advertise it, and pairing a
Lux unit with the app has not been tried.

---

## Why are you here?

### I want to recover the PIN

Go to **[PIN.md](PIN.md)** — step-by-step SWD guide + alternative methods.

### I want to migrate to ESPHome

Go to **[ha.md](ha.md)** — current status, what works, what doesn't, integration architecture.

### I want to replace the battery

Go to **[BATTERY.md](BATTERY.md)** — compatible packs, replacement guide, DIY upgrade.

---

## Documentation

| File | Contents |
|------|----------|
| [HARDWARE.md](HARDWARE.md) | Boards, MCUs, pinouts, SWD ports, GPIO |
| [PROTOCOLS.md](PROTOCOLS.md) | Inter-chip communication protocols (verified by LA) |
| [captures/README.md](captures/README.md) | 64 decoded UART commands from 6 capture scenarios |
| [BATTERY.md](BATTERY.md) | 5S Li-Ion battery — specs, BMS, replacement guide |

### Per-processor

| Folder | Processor | Role | Contents |
|--------|-----------|------|----------|
| [esp32/](esp32/) | ESP32-WROOM-32UE | Display, WiFi/BT, UI | dumps, notes |
| [u13/](u13/) | GD32F305 (main MCU) | Motors, navigation, PIN, USB | dumps, decomp, notes, datasheet |
| [u16/](u16/) | GD32F303 (board MCU) | Border wire, lift/hall sensors (via U13 `bdport`) | dump, notes |

---

## System Overview

```
                   Display Board (SNK_DISPLAY_CP_V11)
                   ┌─────────────────────────────────────┐
                   │ ESP32: UI, WiFi/BT, rain, buzzer    │
                   │ 4-digit 7-seg LED + 4 buttons      │
                   └────────┬────────────────────────────┘
                            │ UART @230400 8N1, JSON
                            │ #&{"cmd":...}\n
                   ┌────────▼────────────────────────────┐
                   │ Main Board (SNK_MAINBOARD_CP_V11)    │
                   │                                      │
                   │ U16 (GD32F303) — sensors, motors,    │
                   │   link to U13, IEC 60730           │
                   │                                      │
                   │ U13 (GD32F305) — motors, navigation, │
                   │   USB host, KV-store, ★ PIN        │
                   │                                      │
                   │ W25Q64 SPI NOR — env: PIN, settings  │
                   └──────────────────────────────────────┘
```

---

## Repository Map

```
kosiarka/
├── README.md           ← this file
├── PIN.md              ← PIN recovery guide
├── ha.md               ← ESPHome — work-in-progress status
├── HARDWARE.md         ← hardware, pinouts, SWD
├── PROTOCOLS.md        ← communication protocols
├── BATTERY.md          ← 5S Li-Ion battery analysis
│
├── esp32/              ← ESP32: dumps, analysis notes
├── u13/                ← GD32F305: dumps, decomp, notes, eeprom
├── u16/                ← GD32F303: dump, notes
│
├── captures/           ← UART logic analyzer captures (6 scenarios)
├── components/         ← ESPHome custom component (snk_mower)
├── img/                ← PCB photographs
├── tools/              ← scripts, OpenOCD configs, stubs
├── docs/               ← datasheets, user manual, notes
├── ghidra_proj/        ← Ghidra project files
├── sw/                 ← third-party tools (ghidra, JDK, ghidra-cli)
├── class_scripts/      ← Java classes for Ghidra bridge
```

---

## External References

- [Brucke RM500/RM501/RM800 infopaketti (io-tech.fi)](https://bbs.io-tech.fi/threads/brucke-rm500-rm501-rm800-robottiruohonleikkurin-infopaketti.405186/)
  Finnish community thread covering the same SNK/Sunseeker platform. What it adds and what it
  corrects: [`20261010_forum_io-techfi.md`](20261010_forum_io-techfi.md).
- [OlliKantola/Sunseeker_LawnMower_Control](https://github.com/OlliKantola/Sunseeker_LawnMower_Control):
  cloud MQTT command list (`cmd 101` mode, `112` PIN reset, …), see [esp32/notes/ESP32.md](esp32/notes/ESP32.md).

---

*Documentation produced through reverse engineering for educational purposes.*

## Related projects

- [Sdahl1234/Sunseeker-lawn-mower](https://github.com/Sdahl1234/Sunseeker-lawn-mower): Home Assistant integration for the Sunseeker cloud. Our ESP32 speaks the same cloud protocol ("OLD" models: `mode`, `power`, `errortype`), see `20261009_claude_investigation.md` §9.
