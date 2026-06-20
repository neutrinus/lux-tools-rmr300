# Hardware Documentation — SNK Mower (Lux Tools A-RMR-300-24)

## Overview

The mower contains two PCBs connected via a ribbon cable/header:
1. **Mainboard** (`SNK_MAINBOARD_CP_V11`) — motor control, sensors, navigation logic
2. **Display Board** (`SNK_DISPLAY_CP_V11`) — UI, buttons, display, ESP32

Both are manufactured on the **SNK** platform, shared with **Adano RM5** (Harald Nyborg, Schou).

---

## Mainboard: `SNK_MAINBOARD_CP_V11`

![Mainboard top](img/mainboard_top.jpg) ![Mainboard bottom](img/mainboard_bottom.jpg)

### Identifiers
| Label | Value |
|-------|-------|
| Board model | `SNK_MAINBOARD_CP_V11` |
| Part number | `80102372-01` |
| Date code | `202311074577` |
| Certifications | `KCD E498693 KD-002`, `94V-0` |

### Microcontrollers

| Ref | Chip | Architecture | Role |
|-----|------|-------------|------|
| **U13** | `GD32F305 AGT6` (GigaDevice) | ARM Cortex-M4 | Main MCU — motors, BLDC control, navigation, boundary wire sensing, EEPROM access |
| **U16** | `GD32F303 CGT6` (GigaDevice) | ARM Cortex-M4 | Secondary MCU — UART bridge to display board, forwards button presses to U13 |

### Memory

| Ref | Package | Likely Type | Role |
|-----|---------|-------------|------|
| **U22** | SOIC-8 (left of U13) | I²C EEPROM (24C02/04) | **Stores PIN code**, schedule, working hours, ENV/KV config — entire PCB covered in protective coating, difficult to probe directly |
| **U12** | SOIC-8 (right of U13, below crystal) | SPI Flash (25xx) or 2nd EEPROM | Firmware update staging or additional logging |

### Power

| Ref | Function |
|-----|----------|
| **U7** | Buck converter — 20V battery → 3.3V/5V logic rails |
| **J5** (`BATTERY`) | Main 20V Li-Ion battery input (4-pin white connector — V+, GND, SDA, SCL or UART) |

### BLDC Motor Drivers

Three 3-phase brushless motors, controlled by MOSFET banks (bottom edge with heatsinks):

| Connector | Phases | Function |
|-----------|--------|----------|
| `LEFT` | `A B C` | Left drive wheel |
| `RIGHT` | `A B C` | Right drive wheel |
| `BLADE` | `A B C` | Cutting disc |
| `CHA` | `C- C+` | Charging contacts from docking station |

### Inter-Board Connector: J8 (Display Board ↔ Mainboard)

**J8** is the main 7-pin connector linking the display board to the mainboard via a ribbon cable. Pinout (as labeled on mainboard silkscreen):

| Pin | Label | Function | Direction |
|-----|-------|----------|-----------|
| 1 | `+5V` | Power to display board | Mainboard → Display |
| 2 | `ON` | Power button (K4) | Display → Mainboard (direct GPIO) |
| 3 | `→` | **UART TX from mainboard → RX to ESP32** | Mainboard → Display |
| 4 | `←` | **UART TX from ESP32 → RX to mainboard** | Display → Mainboard |
| 5 | `GND` | Ground | |
| 6 | `Start` | Start button (K1) | Display → Mainboard (direct GPIO) |
| 7 | `OK` | OK button (K3) | Display → Mainboard (direct GPIO) |

**Key architectural insight:** Only the OK button is shared between ESP32 (GPIO19) and mainboard (J8 pin 7).
ON (J8 pin 2) and START (J8 pin 6) connect to the mainboard but have **no confirmed** connection to the ESP32.

The **bidirectional UART** on pins 3-4 is the only digital communication channel between the two boards.

### Sensors & I/O Connectors

| Connector | Label | Function |
|-----------|-------|----------|
| `J10` / `H2` | `HALL +5V GND` | Hall effect sensor on front bumper — lift/tilt or collision detection |
| `J9` | — | Boundary wire loop coils (EM sensing, 2 coils under chassis) |
| `J7` | — | 4-pin UART diagnostic port (TX/RX/GND) — **unpopulated in this unit** |
| `U19` | `STOP` | Physical emergency stop button connector |

### USB Port
- **Type**: USB-A female (host), covered by rubber grommet on mower exterior
- **Function**: USB flash drive for log export and firmware update files
- **IC** (U3 area): USB host controller / power switch

### SWD Debug Ports

Both MCUs have accessible SWD ports. Pins are labeled on the **back** of the board.

#### P4 → U13 (GD32F305, Main MCU)

Through-hole pads on right edge, near USB port.

| Pin (top→bottom) | Label | TP Ref |
|:---:|:---:|:---:|
| 1 | `3V3` | TP74 |
| 2 | `DIO` (SWDIO) | TP76 |
| 3 | `CLK` (SWCLK) | TP77 |
| 4 | `JTDO` | TP78 |
| 5 | `RES` | TP80 |
| 6 | `GND` | TP81 |

#### P5 → U16 (GD32F303, Secondary MCU)

Black female header (pin socket), left side of board.

| Pin (top→bottom) | Label | TP Ref |
|:---:|:---:|:---:|
| 1 | `GND` | TP58 |
| 2 | `RES` | TP57 |
| 3 | `JTDO` | TP55 |
| 4 | `CLK` (SWCLK) | TP56 |
| 5 | `DIO` (SWDIO) | TP64 |
| 6 | `3V3` | — |

---

## Display Board: `SNK_DISPLAY_CP_V11`

![Display front](img/display_front.jpg) ![Display back](img/display_back.jpg)

### Identifiers
| Label | Value |
|-------|-------|
| Board model | `SNK_DISPLAY_CP_V11` |
| Part number | `80102373-01` |
| Date code | `20231020` |
| Laminate date | `2339` (week 39, 2023) |

### Wireless Module (Hidden Feature)

**U5**: `ESP32-WROOM-32UE` (Espressif)

Despite the mower being marketed as "simple, no wireless connectivity", the display board has a fully functional ESP32 with:
- Dual-core Tensilica Xtensa LX6
- Wi-Fi 802.11 b/g/n
- Bluetooth v4.2 BR/EDR + BLE
- External antenna via IPEX/U.FL connector (wire monopole, glued with white silicone)

Certifications:
- `FCC ID: 2AC7Z-ESPWROOM32UE`
- `CMIT ID: 2020DP10074(M)`
- `IC: 21098-ESPWROOMUE`

### Display & Buttons

| Component | Marking | Description |
|-----------|---------|-------------|
| Display | `GD5643CPG-1` (code `2335`) | 4-digit 7-segment LED, green/red, colon separator |
| K4 | `ON` | Power button (top) |
| K1 | `START` | Start mowing (2nd) |
| K2 | `HOME` | Return to dock (3rd) |
| K3 | `OK` | Confirm/select (bottom) |

### Driver ICs

| Ref | Package | Verified Type | Role |
|-----|---------|---------------|------|
| **U1, U3, U4** | SOP-16 | `74HC595` | Cascaded 3-stage shift registers for driving 4-digit 7-segment display (24 output bits total: segment select, digit select, and colon) |
| **U2** | — | Local 3.3V buck converter | Local 3.3V rail from ribbon cable +5V input (includes coil `3R3`) |
| **BU1** | — | Piezo buzzer | Driven via PWM and transistor driver |

### ESP32 GPIO Mapping

The ESP32 module (U5) is mapped to the display, buttons, sensors, and mainboard UART as follows:

| Pin / Function | ESP32 GPIO | ESP32 Pad | Circuit Path & Details |
|----------------|:----------:|:---------:|------------------------|
| **UART RX** (from MB) | **16** | Pad 17 | ESP32 Pad 17 → `FB3` → `R35` → `TP16` → `TVS5` → J8 Pin → MB TX |
| **UART TX** (to MB) | **17** | Pad 18 | ESP32 Pad 18 → `FB2` → `R32` → `TP15` → `TVS2` → J8 Pin → MB RX |
| **Display CS/Latch** | **5** | Pad 29 | VSPI CS: ESP32 Pad 29 → ST_CP (Pin 12) of U1/U3/U4 |
| **Display SCLK** | **18** | Pad 30 | VSPI SCLK: ESP32 Pad 30 → SH_CP (Pin 11) of U1/U3/U4 |
| **Display MOSI** | **23** | Pad 37 | VSPI MOSI: ESP32 Pad 37 → DS (Pin 14) of U1 |
| **Button K3** (`OK`) | **19** | Pad 8 | Potwierdzone testem — GPIO19 zmienia stan przy naciśnięciu OK |
| **Buzzer BU1** | **12** | Pad 14 | Buzzer PWM: ESP32 Pad 14 → `R29` → transistor driver → BU1 |
| **Rain Sensor J4** | **36** | Pad 4 | ADC Input (SENSOR_VP): J4 contact → input filtering → ESP32 Pad 4 |

**Uwaga:** Przyciski ON (K4), START (K1) i HOME (K2) nie zostały zidentyfikowane na żadnym GPIO ESP32.
Test polegający na skanowaniu wszystkich GPIO podczas naciskania każdego przycisku wykazał zmiany wyłącznie na GPIO19 (OK).
Pozostałe przyciski prawdopodobnie idą wyłącznie do mainboard przez złącze J8 i nie są podłączone do ESP32.

### Connectors

| Connector | Pins | Function |
|-----------|------|----------|
| **J1** | 6-pin female header | ESP32 **programming** UART (UART0): `3U3 T R GND GND P` (P = IO0/Prog) — used with FT232R + esptool.py to dump 4 MB flash at 921600 baud. **Not connected to mainboard.** |
| **J4** | 2 spring contacts | Rain/moisture detector (short when wet) |
| **Main header** | 7-pin white | Inter-board connector — mates with mainboard **J8**: `+5V ON → ← GND Start OK` |

---

## Experiments: Button GPIO Detection

We conducted a series of firmware tests to determine which physical buttons (K1–K4) are connected to the ESP32 and which pins they use.

### Test 1: GPIO Scan (pin_diag)

**Method:** Custom `pin_diag` mode in snk_mower component. Sets all 24 accessible GPIOs as inputs, polls every 100ms, logs any level change.

**Pins scanned:** `{0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39}`

**Result:**
- **GPIO19** — changes state when OK (K3) is pressed ✅
- All other GPIOs — no change when pressing any button (START, HOME, OK, ON)

### Test 2: ADC Scan (resistor ladder hypothesis)

**Hypothesis:** Buttons might be connected through a resistor ladder to a single ADC pin, where each button produces a different voltage level.

**Method:** ESPHome ADC sensors on all ADC1 pins (GPIO32–36, 39) with `raw: true` and `update_interval: 50ms`, using `delta: 30` filter to report only significant changes.

**Pins tested:**
| GPIO | ADC Unit | Result |
|------|----------|--------|
| 32 | ADC1_CH4 | Noisy (~2400–2700 raw), no change with buttons |
| 33 | ADC1_CH5 | Noisy (~1200–1400 raw), no change with buttons |
| 34 | ADC1_CH6 | 0 (pulled to GND), no change with buttons |
| 35 | ADC1_CH7 | ~980–1020 (noise only), no change with buttons |
| 36 | ADC1_CH0 | Rain sensor (short when wet), no change with buttons |
| 39 | ADC1_CH3 | 4095 (pulled to VCC), no change with buttons |

**Conclusion:** No resistor ladder. ADC pins show only noise — no voltage step when any button is pressed.

### Final Determination

| Button | Ref | Connection | How ESP32 detects it |
|--------|-----|------------|---------------------|
| OK | K3 | **GPIO19** (confirmed) + J8 pin 7 → mainboard | Direct GPIO read |
| START | K1 | J8 pin 6 → mainboard **only** | Via UART: `0x41000020` (START_ACK) then `0x41000003` (EXEC_ACTION) |
| HOME | K2 | Unknown — not on ESP32 GPIO or ADC, no unique UART command | Via UART: `0x41000003` (EXEC_ACTION — same as START!) |
| ON | K4 | J8 pin 2 → mainboard **only** | Not detectable by ESP32 |

**Key insight:** The mainboard's secondary MCU (U16, GD32F303) reads START and HOME directly, executes the action, and notifies the ESP32 via UART. The ESP32 cannot initiate or block these actions — it only receives status messages after the fact. The OK button is the only physical button directly accessible to the ESP32.

---

## SWD Debug Connections (RPi Pico)

Flash [debugprobe](https://github.com/raspberrypi/debugprobe) UF2 on RPi Pico.

### Pico Pinout for debugprobe

| GPIO | Function |
|:---:|:---:|
| GP2 | **SWCLK** |
| GP3 | **SWDIO** |

### Wiring to Mainboard

#### P4 → U13 (Main MCU, GD32F305)

| Pico Pin | Pico GPIO | SWD | P4 Pin |
|:---:|:---:|:---:|:---:|
| Pin 3 | GND | GND | 6 (bottom) — GND (TP81) |
| Pin 4 | GP2 | SWCLK | 3 — CLK (TP77) |
| Pin 5 | GP3 | SWDIO | 2 — DIO (TP76) |

#### P5 → U16 (Secondary MCU, GD32F303)

| Pico Pin | Pico GPIO | SWD | P5 Pin |
|:---:|:---:|:---:|:---:|
| Pin 3 | GND | GND | 1 (top) — GND |
| Pin 4 | GP2 | SWCLK | 4 — CLK |
| Pin 5 | GP3 | SWDIO | 5 — DIO |

> Do NOT connect Pico 3.3V. The mower powers itself from its battery.

### udev Rule

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/99-pico-debugprobe.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

