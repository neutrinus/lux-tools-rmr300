# UART Sniffing Plan — SNK Mower J8 Ribbon Cable

## Goal

Capture live traffic between the display board (ESP32) and mainboard (U16→U13) to confirm the binary protocol format before flashing ESPHome firmware.

## Hardware Setup

### J8 Connector Pinout (on mainboard)

| Pin | Label | Signal | Direction | Logic Analyzer |
|:---:|:-----:|--------|:---------:|:--------------:|
| 1 | `+5V` | Power to display | MB → Display | ❌ DO NOT PROBE |
| 2 | `ON` | Button K4 (direct GPIO) | Display → MB | CH3 (GPIO) |
| 3 | `→` | **UART TX from mainboard** | **MB → ESP32 RX** | **CH1 (UART)** |
| 4 | `←` | **UART TX from ESP32** | **ESP32 TX → MB** | **CH2 (UART)** |
| 5 | `GND` | Ground | — | GND |
| 6 | `Start` | Button K1 (direct GPIO) | Display → MB | CH4 (GPIO) |
| 7 | `OK` | Button K3 (direct GPIO) | Display → MB | CH5 (GPIO) |

### Probe Points

The easiest access is the **back of the mainboard** where J8 solder joints are exposed, or the display board's female header. Do NOT probe pin 1 (+5V).

### Recommended Logic Analyzer Settings

| Parameter | Value |
|-----------|-------|
| Sampling rate | ≥2 MHz (1 MHz minimum) |
| Channels | 5 (CH1–CH5) |
| Decode CH1 | Async Serial, 115200 baud, 8N1 |
| Decode CH2 | Async Serial, 115200 baud, 8N1 |
| Decode CH3–CH5 | GPIO (falling edge = button press) |
| Trigger | CH2 falling edge (first UART activity) |
| Voltage | 3.3V (⚠️ NOT 5V — may damage ESP32 GPIO) |

> **Voltage warning:** UART and GPIO lines are 3.3V. Most logic analyzers accept 5V tolerant input, but check your model. Use level shifters if unsure.

### Physical Probing

- **Ribbon cable:** Best option — carefully strip a tiny section of insulation on each wire, or use a breakout board. Do NOT cut the ribbon.
- **J8 pins on mainboard back:** Solder joints are accessible. Tack probe wires with blu-tack or kapton tape.
- **Display board header:** If you have a female-female dupont, you can slide probes into the header alongside the ribbon.

## Scenarios to Capture

Record each scenario as a separate capture file (`.sal`, `.vcd`, or `.csv`).

### 1. Boot Sequence (~15s)

```
Start state: Battery disconnected
Action:      Connect battery → press ON button
Capture:     Until display shows "IdLE"
```

What we learn:
- UART init sequence — does the display board send anything on boot?
- PIN verification flow — exact frame for PWD_VERIFY (0x0B) and PWD_RESULT (0x0C)
- Does mainboard send any unprompted messages?
- Startup timing — how long until the system is ready

Pinout timing to watch:
```
CH3 (ON) ━━━━━━━━━━━━━━━━━━━━━━┓___┓___ wait, ON is wired directly
CH1 (MB→ESP) ━━━━┓_____________┃___┃_  ← does mainboard send status after ON?
CH2 (ESP→MB) ━━━━┃___┃___┃______┃___┃  ← does ESP32 send PIN immediately?
```

### 2. Idle Steady-State (~30s)

```
Start state: Mower on, display shows "IdLE"
Action:      Touch nothing
Capture:     30 seconds of idle traffic
```

What we learn:
- Periodic polling interval — how often ESP32 sends STATUS_REQ (0x0D) and BAT_REQ (0x14)?
- Does mainboard send async status updates without being asked?
- Any keepalive/heartbeat from mainboard?
- Format of STATUS_RSP (0x0E) — **primary unknown** (flags? structure? length?)

Expected traffic pattern:
```
ESP→MB: AA 55 0D CS           (STATUS_REQ)
MB→ESP: AA 55 0E [flags...] CS (STATUS_RSP — NEED FORMAT)
(sleep ~2s)
ESP→MB: AA 55 14 CS           (BAT_REQ)
MB→ESP: AA 55 15 [mV_hi][mV_lo] CS (BAT_RSP)
```

### 3. Start Mowing (~15s)

```
Start state: Display shows "IdLE"
Action:      Press and release START (K1) once
Capture:     5s before → 10s after press
```

What we learn:
- **Critical:** Is START handled via direct GPIO (CH4 falling edge) or via UART command?
  - If mainboard starts mowing on CH4 edge alone → direct GPIO bypasses PIN
  - If CH4 edge AND ESP32 sends 0x0F on CH2 → ESP32 relay needed
- Does mainboard send a new status frame after receiving the start command?
- What does STATUS_RSP look like when mowing?

```
Expected direct GPIO path:
CH4 (START) ━━━━━━━━━━━┓_________________  ← falling edge = press
MB starts mowing ──── (no UART activity needed)

Expected UART path:
CH4 (START) ━━┓_________________________________
CH2 (ESP→MB) ━┃___AA_55_0F_CS_________________  ← cmd 0x0F
CH1 (MB→ESP) ─━━━━━AA_55_0E_01_CS______________  ← mowing status
```

### 4. Mowing Steady-State (~30s)

```
Start state: Mower is mowing
Action:      Let it run (block wheels if indoors)
Capture:     30 seconds
```

What we learn:
- Status polling continues during mowing — same interval?
- Does STATUS_RSP change between "mowing" and "returning" states?
- Battery response (0x15) while under load — voltage drop pattern

### 5. Press HOME While Mowing (~20s)

```
Start state: Mower is mowing
Action:      Press and release HOME (K2) once
Capture:     5s before → 15s after
```

What we learn:
- **HOME is NOT forked to mainboard** (K2 is display board only)
- So we should see: CH2 → cmd 0x10 (CHARGE_RET)
- Then status change: returning → docking → charging
- Verify the STATUS_RSP flags that correspond to each state

```
Expected:
CH2 (ESP→MB) ───AA_55_10_CS___________________  ← cmd 0x10
CH1 (MB→ESP) ────────AA_55_0E_[return_flag]_CS_  ← returning status
```

### 6. Lift Sensor / Error Condition (~15s)

```
Start state: Mower is mowing
Action:      Lift the mower (trigger hall sensor) or press STOP
Capture:     15 seconds
```

What we learn:
- ERROR_INFO (0x12) format — confirm 2-byte error code
- Does mainboard send 0x12 autonomously (async), or only in response to STATUS_REQ?
- Error code values (0x0022 = lifted? from community knowledge)

```
Expected:
CH1 (MB→ESP) ──AA_55_12_[code_hi][code_lo]_CS_  ← async error
```

### 7. Docking and Charging (~30s)

```
Start state: Mower returning to dock
Action:      Let it dock (or place on charger contacts manually)
Capture:     30 seconds
```

What we learn:
- Status transition: returning → docked → charging
- Does charging state come from STATUS_RSP flag or is it inferred from voltage?
- If we get STATUS_RSP with charging flag → confirmed
- If not → battery voltage > ~20V = charging heuristic

### 8. (Optional) Battery Voltage Range (~30s)

```
Start state: Mower idle
Action:      Disconnect charger → wait → reconnect
Capture:     30 seconds
```

What we learn:
- Voltage levels for charging vs idle vs low battery
- Can calibrate our voltage→percent lookup table
- Response delay after state change

## Expected Frame Reference

Keep this handy while decoding captures.

### Format: `AA 55 [CMD:1B] [PAYLOAD:n B] [CS:1B]`
### Checksum: `CS = CMD XOR payload[0] XOR payload[1] XOR ...`

| CMD | Dir | Payload | Name |
|:---:|:---:|---------|------|
| 0x01 | ESP→MB | 1B scancode | BTN_UP |
| 0x02 | ESP→MB | 1B scancode | BTN_DOWN |
| 0x04 | ESP→MB | 1B scancode | BTN_OK |
| 0x0B | ESP→MB | 4B ASCII | PWD_VERIFY |
| 0x0C | MB→ESP | 1B (0=OK, 1=fail) | PWD_RESULT |
| 0x0D | ESP→MB | — | STATUS_REQ |
| 0x0E | MB→ESP | **? (4B+ flags)** | STATUS_RSP — **UNKNOWN FORMAT** |
| 0x0F | ESP→MB | — | MOW_START |
| 0x10 | ESP→MB | — | CHARGE_RET |
| 0x11 | ESP→MB | — | DISPLAY_OFF |
| 0x12 | MB→ESP | 2B code | ERROR_INFO |
| 0x14 | ESP→MB | — | BAT_REQ |
| 0x15 | MB→ESP | 2B mV | BAT_RSP |

## Decoding Workflow

1. **Open capture** in PulseView/Saleae/Logic
2. **Apply UART decode** to CH1 (MB→ESP) and CH2 (ESP→MB) at 115200 8N1
3. **Export decoded data** as TXT/CSV
4. **Filter for `AA 55`** sync pattern
5. **Verify checksum** for each frame: XOR of all bytes between sync and last byte
6. **Map each frame** to expected command
7. **Note any UNEXPECTED frames** — these are gaps in our knowledge

## Data to Extract

For each scenario, document:

```
Scenario: [name]

--- Frame log ---
[t=0.000] ESP→MB: AA 55 0D CS                     (STATUS_REQ)
[t=0.008] MB→ESP: AA 55 0E [XX XX XX XX] CS       (STATUS_RSP — raw hex)
[t=2.100] ESP→MB: AA 55 14 CS                     (BAT_REQ)
[t=2.108] MB→ESP: AA 55 15 [XX XX] CS             (BAT_RSP — mV)
...

--- Key findings ---
- STATUS_RSP length: X bytes
- Bit mapping: [describe flags]
- Polling interval: X ms
- Error code for [condition]: 0xXXXX
```

## Tools

### PulseView / sigrok (recommended, free)

```bash
# Install
sudo apt install sigrok pulseview

# Or download from https://sigrok.org/wiki/Downloads
```

### Saleae Logic (if you have a Saleae clone)

Download from [saleae.com](https://www.saleae.com/downloads/) — version 2.x works with most clones.

### Command-line decode with Python (post-processing)

```bash
# Export from PulseView/Saleae as CSV, then:
python3 -c "
import csv
# parse CSV, find 'AA 55' patterns, decode frames
"
```

I can write a dedicated decode script once you have captures.

## Risk / Precautions

| Risk | Mitigation |
|------|------------|
| Probe slip shorts +5V to GND | Tape over pin 1; use probe clips |
| ESD damage to ESP32 | Ground yourself; work on anti-static mat |
| Ribbon cable damage | Use probe hooks, don't pierce insulation |
| Mower starts unexpectedly | Block wheels; remove blade |
| Logic analyzer ground loop | Share GND with mower only (not PC via USB) |
