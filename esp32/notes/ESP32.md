# ESP32 Firmware Analysis — SNK Display Board

## Overview

The display board (`SNK_DISPLAY_CP_V11`) contains an **ESP32-WROOM-32UE** module (U5) that serves as the primary user interface controller. Despite the mower being sold without any advertised wireless connectivity, the ESP32 ships with a full ESP-IDF firmware (project `Display_esp32`, version `3.02.02`) containing functional Wi-Fi, Bluetooth, and MQTT stacks — strongly suggesting a "smart" variant exists on the same platform.

## Hardware Role

```
┌──────────────────────────────────────────────────────────────────────┐
│ Display Board (SNK_DISPLAY_CP_V11)                                   │
│                                                                       │
│   Buttons (K1-K4)                                                     │
│       │                                                               │
│       ├──→ Local GPIO on display board → ESP32 reads state           │
│       │                                                               │
│       ├──→ Ribbon cable (J8 pins 2,6,7) → Mainboard direct GPIO     │
│       │                                                               │
│   ESP32 (U5)                                                          │
│       │                                                               │
│       ├──UART (115200 8N1)──→ J8 pin 4 (←) ──ribbon──→ Mainboard J8  │
│       │                           J8 pin 3 (→)     → U16 → U13       │
│       │                                                               │
│       ├── J1 header (UART0, programming only, NOT to mainboard)       │
│       ├──→ Controls 4-digit 7-segment display (GD5643CPG-1)          │
│       ├──→ Drives buzzer (BU1)                                        │
│       ├── IPEX antenna (WiFi/BT, unused in stock)                     │
│       └── 3.3V from U2 (buck converter, input +5V from J8 pin 1)     │
└──────────────────────────────────────────────────────────────────────┘
```

### Key Functions
- **Button input processing** — reads the button matrix (S1-S14) via GPIO, debounces, and translates to commands
- **Display control** — drives the GD5643CPG-1 4-digit 7-segment LED via shift registers (U1, U3) or display driver (U4)
- **Buzzer control** — piezo buzzer (BU1) for audible feedback
- **UART communication** — bidirectional serial link to mainboard (U16 → U13)
- **Rain sensor** — reads J4 spring contacts for moisture detection
- **Optional WiFi/BT/MQTT** — fully implemented but disabled in retail configuration

## UART Communication Protocol

The ESP32 communicates with the mainboard (U16 → U13) via a bidirectional UART over the ribbon cable (J8 pins 3-4). **Note:** This UART is NOT the same as J1 — J1 is the ESP32's programming UART (UART0, GPIO1/GPIO3) and is only used for flash dumping. The mainboard UART uses remapped pins via the GPIO matrix: RX on **GPIO13** and TX on **GPIO15**.

The protocol is a custom binary framing format.

### Physical Layer

| Parameter | Value |
|-----------|-------|
| Baud rate | **115200** (confirmed via string `"115200"` in firmware) |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| Level | 3.3V CMOS UART |
| Direction | Bidirectional (half-duplex or full-duplex) |

### Protocol Strings Found in Firmware

| String | Likely Purpose |
|--------|----------------|
| `"Uart Env"` | UART environment/configuration sent to mainboard |
| `"send command"` | Function that transmits a command packet |
| `"pwd result cmd"` | Response to PIN verification request |
| `"btn status error"` | Button status reporting error |
| `"display off cmd"` | Command to turn off the mainboard display |
| `"mow"` | Start mowing command |
| `"charge"` | Return to charge/dock command |
| `"work status"` | Status request for current mowing mode |
| `"bat info"` | Battery voltage/status request |
| `"sensor info"` | Sensor data request |
| `"error info"` | Error code request |

### Framing Structure (Reconstructed)

Based on decompilation of the serialization functions around addresses `0x4015bf34`, `0x4015be24`, `0x4015bdd4`:

```
┌──────┬────────┬──────────┬────────────────┬────────┐
│ 0xAA │ 0x55   │ CMD (1B) │ PAYLOAD (n B)  │ CS (1B)│
│ Sync │ Sync   │ Command  │ Variable length │ XOR    │
│ Byte │ Byte   │ ID       │                 │ checksum│
└──────┴────────┴──────────┴────────────────┴────────┘
```

- **Sync bytes**: `0xAA 0x55` — frame start delimiter
- **Command ID**: Single byte identifying the command type (see table below)
- **Payload**: Variable length, command-specific data
- **Checksum**: XOR of all bytes between sync and checksum (inclusive of CMD, exclusive of sync)

### Command IDs (Preliminary)

| ID | Direction | Name | Payload | Description |
|----|-----------|------|---------|-------------|
| `0x01` | ESP→MB | `BTN_UP` | 1B (scancode) | Button press up |
| `0x02` | ESP→MB | `BTN_DOWN` | 1B (scancode) | Button press down |
| `0x04` | ESP→MB | `BTN_OK` | 1B (scancode) | Button press OK/select |
| `0x0B` | ESP→MB | `PWD_VERIFY` | 4B (ASCII PIN) | Verify entered PIN |
| `0x0C` | MB→ESP | `PWD_RESULT` | 1B (0=OK, 1=fail) | PIN verification result |
| `0x0D` | ESP→MB | `STATUS_REQ` | 0B | Request mower status |
| `0x0E` | MB→ESP | `STATUS_RSP` | 4B+ (status flags) | Mower status response |
| `0x0F` | ESP→MB | `MOW_START` | 0B | Start mowing command |
| `0x10` | ESP→MB | `CHARGE_RET` | 0B | Return to dock command |
| `0x11` | ESP→MB | `DISPLAY_OFF` | 0B | Turn off display |
| `0x12` | MB→ESP | `ERROR_INFO` | 2B+ (error code) | Error notification |
| `0x14` | ESP→MB | `BAT_INFO_REQ` | 0B | Battery info request |
| `0x15` | MB→ESP | `BAT_INFO_RSP` | 2B (voltage mV) | Battery voltage response |

> **Note**: Command IDs above are inferred from decompiled state machine case values and string context. Actual values need verification via UART sniffing or deeper analysis.

### State Machine Architecture

The main application loop around `0x40112801` (largest function, ~5KB) implements a state machine with the following identified states:

| State | Name | Description |
|-------|------|-------------|
| `WAIT_IDLE` | 0x00 | Idle — waiting for button press or mainboard message |
| `BTN_WAIT` | 0x0B | Debounce wait — button pressed, waiting for release |
| `BTN_SEND` | 0x0C | Sending button command to mainboard |
| `PWD_ENTRY` | 0x0D | PIN entry mode — digit selection/adjustment |
| `PWD_SEND` | 0x0E | Sending PIN digits to mainboard for verification |
| `PWD_WAIT` | 0x0F | Waiting for PIN verification result |
| `STATUS_DISP` | 0x10 | Displaying status (IDLE, MOW, CHARGE, LOCK, ERROR) |
| `CMD_WAIT` | 0x11 | Waiting for command response from mainboard |
| `ERROR_DISP` | 0x12 | Displaying error code on screen |
| `SLEEP` | 0x14 | Display off / power saving mode |

## PIN Handling Flow

```
User presses OK          ESP32 enters         ESP32 builds
4 times while 0          PIN entry mode       frame: 0xAA 0x55
blinks on display        (state 0x0D)         0x0B "0000" CS
       │                       │                     │
       ▼                       ▼                     ▼
┌──────────┐           ┌──────────────┐      ┌──────────────┐
│ Button   │──UART──▶  │ ESP32 reads  │───▶  │ UART frame   │
│ matrix   │           │ via GPIO     │      │ 0x0B + PIN   │
└──────────┘           └──────┬───────┘      └──────┬───────┘
                              │                     │
                              │          ┌──────────▼──────────┐
                              │          │  Mainboard U13       │
                              │          │  Reads PIN from U22  │
                              │          │  Compares & responds │
                              │          └──────────┬──────────┘
                              │                     │
                              │            ┌────────▼────────┐
                              │            │ 0xAA 0x55 0x0C  │
                              │            │ 0x00 (OK) CS    │
                              │            │ OR 0x01 (FAIL)  │
                              │            └────────┬────────┘
                              │                     │
                         ┌────▼────┐          ┌─────▼──────┐
                         │ Display │◀─────────│ ESP32      │
                         │ "IDLE"  │ (if OK)  │ receives   │
                         │ "Lock"  │ (if fail)│ response   │
                         └─────────┘          └────────────┘
```

**Key insight**: The ESP32 forwards the PIN entry to the mainboard (U13) for verification. The PIN itself is **never stored on the ESP32** — it is stored on the mainboard EEPROM U22. The ESP32 is merely a UART terminal for PIN input and result display.

## WiFi/MQTT Subsystem

Despite not being used in the retail product, the ESP32 firmware contains a complete MQTT client implementation:

### NVS Configuration

Stored in the NVS partition at offset `0x9000`:

| Key | Value | Purpose |
|-----|-------|---------|
| `robot_password` | `"88888888"` | WiFi/app password |
| `robot_ssid` | `"cy-public"` | WiFi SSID |
| `wifi_passwd` | `"88888888"` | Same as robot_password |
| `robot_name` | `"MyMower"` | Device display name |
| `robot_sn` | `"2312CGF250600035167"` | Serial number |
| `iot_mqtt_uri` | `"mqtt://server.sk-robot.com"` | MQTT broker URL |
| `pdt_ver` | — | Product version |
| `model` | — | Product model |

### MQTT Topics (Inferred from Firmware)

Based on the string `"server.sk-robot.com"` and MQTT-related strings in the firmware:

| Topic Pattern | Direction | Description |
|---------------|-----------|-------------|
| `snk/device/{sn}/status` | Device→Cloud | Status reports |
| `snk/device/{sn}/command` | Cloud→Device | Remote commands |
| `snk/device/{sn}/config` | Both | Configuration sync |
| `snk/device/{sn}/ota` | Cloud→Device | OTA firmware update |

### WiFi Configuration Flow

1. ESP32 boots, reads NVS
2. If `robot_ssid` is set → connects to that WiFi network as station
3. If connection fails → optionally starts AP mode with SSID `"MyMower"` for direct configuration (untested in this unit)
4. On WiFi connect → connects MQTT to `server.sk-robot.com`:
   - Username: `robot_sn` (serial number)
   - Password: `robot_password`
5. Subscribes to command topic
6. Relays commands between MQTT and mainboard UART

## Display Control

The 4-digit 7-segment LED (GD5643CPG-1) is controlled by the ESP32. Display patterns observed:

| Display | Meaning |
|---------|---------|
| `----` | Off/sleep |
| `IdLE` | Idle, waiting for input |
| `LoCK` | PIN locked |
| `Mow ` | Mowing |
| `HoME` | Returning to dock |
| `ChAr` | Charging |
| `Err ` + code | Error state |
| `0   ` (blinking) | PIN entry mode — digit 1 |
| `0 0 ` | PIN entry — digit 2 |
| `0 0 0` | PIN entry — digit 3 |
| `0 0 0 0` | PIN entry — digit 4 |

## Firmware File Structure

```
┌────────────────────────────────────────────────┐
│ esp32/firmware/esp32_dump.bin (4 MB)                           │
│                                                  │
│  0x0000  ┌──────────────────────────────────┐    │
│          │ Bootloader (ESP-IDF ROM boot)    │    │
│  0x1000  ├──────────────────────────────────┤    │
│          │ Partition Table                  │    │
│  0x9000  ├──────────────────────────────────┤    │
│          │ NVS (Non-Volatile Storage)       │    │
│          │ robot_password, robot_ssid, etc  │    │
│  0xD000  ├──────────────────────────────────┤    │
│          │ OTAData (OTA selection)          │    │
│  0xF000  ├──────────────────────────────────┤    │
│          │ PHY_INIT (WiFi PHY calibration)  │    │
│ 0x10000  ├──────────────────────────────────┤    │
│          │ ota_0 (APP partition #1, 1472K)  │    │
│          │   - Xtensa code at 0x400D0000+   │    │
│          │   - Application firmware         │    │
│          │   - Version 3.02.02              │    │
│ 0x180000 ├──────────────────────────────────┤    │
│          │ ota_1 (APP partition #2, 1472K)  │    │
│          │   (same as ota_0 in this dump)   │    │
│ 0x300000 ├──────────────────────────────────┤    │
│          │ (unused space)                   │    │
│ 0x400000 └──────────────────────────────────┘    │
└──────────────────────────────────────────────────┘
```

### Key Memory Mappings (ESP32)

| Region | Virtual Address | Physical |
|--------|----------------|----------|
| IROM | `0x400D0000` | Flash offset 0x10000 (ota_0) |
| DROM | `0x3F400000` | Flash offset 0x10000 (ota_0) |
| IRAM | `0x40080000` | Internal SRAM (entry point `0x400814EC`) |
| DRAM | `0x3FFB0000` | Internal SRAM |

## Development Artifacts

The firmware contains source file paths confirming the project structure:

| Path | Component |
|------|-----------|
| `../main/src/app/rw_display.c` | Display driver, main application |
| `../main/src/app/rw_ble.c` | BLE communication module |
| `../main/src/app/rw_uart.c` | UART communication module |
| `../main/src/app/rw_mqtt.c` | MQTT client module |
| `../main/src/app/rw_wifi.c` | WiFi manager |
| `../main/src/app/rw_key.c` | Button/key matrix handler |
| `../main/src/app/rw_timer.c` | Timer/counter |
| `../main/src/app/rw_env.c` | Environment/configuration |
| `../main/src/app/rw_ota.c` | OTA firmware update |
| `../main/src/app/gpio_key.c` | GPIO button input |

## UART Sniffing Recommendations

To confirm the protocol, sniff the UART between ESP32 and mainboard:

### Physical Access Points

The UART lines are on the **ribbon cable** between boards. Access them at the **J8 connector on the mainboard** (labeled pins):

| J8 Pin | Signal | Connect LA To |
|--------|--------|---------------|
| 3 (`→`) | Mainboard TX → ESP32 RX | CH1 |
| 4 (`←`) | ESP32 TX → Mainboard RX | CH2 |
| 5 | GND | GND |

**Do NOT use J1 for sniffing** — J1 is the programming/debug UART (UART0) and is NOT connected to the mainboard.

### Setup

```bash
# Connect logic analyzer to J8 on mainboard:
# - CH1 → J8 pin 3 (→) — mainboard→ESP32
# - CH2 → J8 pin 4 (←) — ESP32→mainboard
# - GND → J8 pin 5

# Use sigrok/pulseview or saleae logic for capture
# At 115200 baud, standard UART decoding
```

## Next Steps for Deeper Analysis

1. **IRAM extraction**: Extract the `.text` segment from the IRAM region (`0x40080000`-`0x40082000`) from the full dump and add as a memory block in Ghidra to resolve the entry point `0x400814EC`
2. **UART sniffing**: Capture live UART traffic between ESP32 and mainboard during PIN entry, mowing, and error conditions to confirm the protocol
3. **Examine `FUN_4015bf34`**: The suspected UART command builder — decompile to understand exact framing
4. **Examine `FUN_4015be24` / `FUN_4015bdd4`**: Suspected serialization helpers
5. **Analyze string cross-references**: Most application strings are called via tables, not directly — find the string table structure to match strings to functions
6. **WiFi enablement testing**: Modify NVS or inject WiFi configuration to test if the IoT/MQTT path actually works

## GPIO Pin Analysis

All ESP32 pins are now fully mapped and verified via high-resolution visual PCB reverse engineering. See [`./ESP32_GPIO_ANALYSIS.md`](./ESP32_GPIO_ANALYSIS.md) for:
- Full verified GPIO pinout tables (UART, display SPI, buttons, buzzer, rain sensor)
- Hardware VSPI configuration details
- Visual tracing paths and component references (resistors, ferrite beads, test points)
- Xtensa decompilation workflow and cross-validation
