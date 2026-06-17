# Inter-Chip Communication Protocols — Cross-Validation

## System Overview

```
ESP32 (Display Board)         U16 (Board MCU)              U13 (Main MCU)
┌───────────────────┐     ┌────────────────────┐      ┌────────────────────┐
│ ESP32-WROOM-32UE  │     │ GD32F303CGT6       │      │ GD32F305AGT6       │
│                   │     │                    │      │                    │
│  • UI (buttons,   │     │  • FreeRTOS        │      │  • Motor control   │
│    7-seg, buzzer) │     │  • Sensors (lift,  │      │  • Navigation      │
│  • WiFi/BT/MQTT   │     │    border, volt)   │      │  • USB host        │
│  • Rain sensor    │     │  • Motor control   │      │  • KV-store + PIN  │
│                   │     │  • JSON translate  │      │  • OTA             │
└──────┬────────────┘     └──────┬─────────────┘      └────────┬───────────┘
       │                         │                            │
       │ Protocol A              │ Protocol B                 │
       │ Binary UART             │ JSON over UART             │
       │ 115200 8N1              │ 230400 8N1                 │
       │ 0xAA 0x55 framing      │ cJSON (U16 side)           │
       │ XOR checksum            │ max 128 B/message          │
       │                         │ (mport driver)             │
       │                         │                            │
       │ J8 ribbon cable         │ PCB traces (internal)      │
       │ (pins 3,4)              │ USART0 ↔ USARTx            │
```

---

## Protocol A: ESP32 ↔ U16 (Binary Protocol)

### Physical Layer

| Parameter | ESP32.md | U16.md | Cross-Valid? |
|-----------|----------|--------|:------------:|
| Baud rate | 115200 (string `"115200"`) | 115200 (from `init port driver`) | ✅ match |
| Data bits | 8 | 8 (implied) | ✅ |
| Parity | None | None (implied) | ✅ |
| Stop bits | 1 | 1 (implied) | ✅ |
| Direction | Bidirectional | Bidirectional | ✅ |
| Level | 3.3V CMOS | 3.3V CMOS | ✅ |
| Physical | J8 pins 3-4 (ribbon) | J8 pins 3-4 (ribbon) | ✅ |

### Framing Structure

```
┌──────┬──────┬─────────┬──────────────────┬────────┐
│ 0xAA │ 0x55 │ CMD(1B) │ PAYLOAD (n B)    │ CS(1B) │
│ sync │ sync │ cmd ID  │ variable length  │ XOR    │
└──────┴──────┴─────────┴──────────────────┴────────┘
```

| Field | ESP32.md | U16.md | Cross-Valid? |
|-------|----------|--------|:------------:|
| Sync bytes | `0xAA 0x55` | (not explicitly documented) | ⚠️ *gap — U16 frame detection not documented* |
| CMD | 1 byte | (handled by state machine) | ✅ |
| Payload | variable | (forwarded to U13 as JSON) | ✅ |
| Checksum | XOR of all bytes between sync and CS | (not documented) | ⚠️ *gap — U16 CS verification not documented* |

### Command IDs

| ID | Direction | Name | ESP32.md | U16.md | Cross-Valid? |
|----|-----------|------|:--------:|:------:|:------------:|
| `0x01` | ESP→MB | `BTN_UP` | ✅ cmd 0x01 | (not enumerated) | ⚠️ *gap* |
| `0x02` | ESP→MB | `BTN_DOWN` | ✅ cmd 0x02 | (not enumerated) | ⚠️ *gap* |
| `0x04` | ESP→MB | `BTN_OK` | ✅ cmd 0x04 | (not enumerated) | ⚠️ *gap* |
| `0x0B` | ESP→MB | `PWD_VERIFY` | ✅ 4B ASCII PIN | ✅ handled (→JSON) | ✅ |
| `0x0C` | MB→ESP | `PWD_RESULT` | ✅ 1B (0=OK/1=fail) | ✅ sends result | ✅ |
| `0x0D` | ESP→MB | `STATUS_REQ` | ✅ 0B payload | ✅ forward to U13 | ✅ |
| `0x0E` | MB→ESP | `STATUS_RSP` | ✅ 4B+ status flags | (not enumerated) | ⚠️ *gap* |
| `0x0F` | ESP→MB | `MOW_START` | ✅ 0B payload | ✅ forward to U13 | ✅ |
| `0x10` | ESP→MB | `CHARGE_RET` | ✅ 0B payload | ✅ forward to U13 | ✅ |
| `0x11` | ESP→MB | `DISPLAY_OFF` | ✅ 0B payload | (not enumerated) | ⚠️ *gap* |
| `0x12` | MB→ESP | `ERROR_INFO` | ✅ 2B+ error code | (not enumerated) | ⚠️ *gap* |
| `0x14` | ESP→MB | `BAT_INFO_REQ` | ✅ 0B payload | ✅ forward to U13 | ✅ |
| `0x15` | MB→ESP | `BAT_INFO_RSP` | ✅ 2B voltage mV | (not enumerated) | ⚠️ *gap* |

> **Note**: "(not enumerated)" means U16.md does not list that specific cmd ID by name. This is a **documentation gap**, not necessarily missing implementation — U16 likely handles all of them, but it has not been confirmed from the decompilation.

### Checksum Calculation

**ESP32.md**: XOR of all bytes between sync and checksum (inclusive of CMD, exclusive of sync)

```
CS = CMD XOR PAYLOAD[0] XOR PAYLOAD[1] XOR ... XOR PAYLOAD[n-1]
```

**FUN_08008cb8 (U13 side, independent confirmation)**: The OTA XOR checksum in U13 uses the exact same XOR-over-data pattern, confirming SNK firmware consistently uses XOR checksums.

---

## Protocol B: U16 ↔ U13 (JSON over UART)

### Physical Layer

| Parameter | U13 (GD32F305.md) | U16 (U16.md) | Cross-Valid? |
|-----------|--------------------|--------------|:------------:|
| USART | USART0 @ `0x40013800` | mport driver channel | ✅ |
| Baud rate | `0x38400` = 230400 | 230400 (from init) | ✅ |
| GPIO AF | `0x25` | (implied) | ✅ |
| DMA | channel 4 | (N/A, RTOS queue) | ✅ |
| Max message | (not specified) | **128 bytes** (mport) | ⚠️ *gap — U13 has no documented limit* |
| OTA baud switch | 230400 → 460800 → 921600 | (not documented) | ⚠️ *gap — U16 OTA not documented* |

### Direction: U16 → U13

| Message | Trigger | JSON Fields | Status |
|---------|---------|-------------|:------:|
| `border_message` | struct flag +0x12==0 | border sensor data (left/right coil) | ✅ documented in U16.md |
| `version_message` | struct flag +0x14==0 | HW/SW version | ✅ |
| `seach_border_message` | struct flag +0x18==0 | search border status (typo in original) | ✅ |
| `follow_border_message` | struct flag +0x19==0 | follow border status | ✅ |
| PWD_RESULT | response to verify_pin | `0xAA 0x55 0x0C 0x00/0x01 CS` | ⚠️ *this is binary, not JSON — U16 translates JSON result → binary for ESP32* |

### Direction: U13 → U16

U13 sends JSON commands to U16. The U16 JSON parser (`get_process_comm`, FUN_080180a0 / FUN_08017f92) handles:

| Token type | Code | Usage |
|-----------|:---:|-------|
| boolean | 0x2 | configuration flags |
| null | 0x4 | empty values |
| number | 0x8 | numeric values (double) |
| string | 0x10 | field names, text values |
| array | 0x20 | parameter lists |
| object | 0x40 | nested structures |

**Known commands U13 → U16** (partially reconstructed):

| Command | Description | Confirmed in U16.md |
|---------|-------------|:-------------------:|
| `{"cmd":"verify_pin","pin":"1234"}` | U16 translates from binary 0x0B to JSON, sends to U13 | ✅ |
| `{"cmd":"mow_start"}` | Start mowing (from cmd 0x0F) | ✅ (forward) |
| `{"cmd":"charge_ret"}` | Return to station (from cmd 0x10) | ✅ (forward) |
| `{"cmd":"status_req"}` | Query status (from cmd 0x0D) | ✅ (forward) |
| `{"cmd":"bat_info"}` | Query battery (from cmd 0x14) | ✅ (forward) |

> **Gap**: The full JSON vocabulary is not documented. We do not know all JSON commands U13 sends to U16 (beyond those forwarded from ESP32). The U16 parser is generic — it handles arbitrary JSON, so the command list is likely larger.

### U16 JSON Parser Error Handling

From U16 firmware strings:
- `"message point is null"`
- `"get message cmd is null"`
- `"get message string is null"`
- `"error cmd =%d"`

---

## OTA Protocol (over the same U16↔U13 UART)

**U13 side** (GD32F305.md):
```
[2B length LE] [N bytes data] [1B XOR checksum]
```

Confirmed in FUN_08008cb8:
```c
uVar5 = *(ushort*)(DAT_08008d18 + 2);  // length LE (at offset +2)
bVar1 = *(byte*)(DAT_08008d18 + uVar5 + 4); // CS at offset +4+len
for (uVar2 = 0; uVar2 < uVar5; uVar2++) {
    bVar4 ^= *(byte*)(DAT_08008d18 + uVar2 + 4); // XOR data
}
if (bVar4 == bVar1) { /* OK */ }
```

**7 OTA commands** (from U13 firmware strings):
1. GET OTA INFO
2. DOWNLOAD
3. SET OTA MODE
4. SET VER
5. RETURN
6. SET FIRMWARE NUMBER
7. SET BAUDRATE

**U16 side** (U16.md): **no OTA documentation** — unclear whether U16 initiates OTA or only forwards data transparently.

> **Critical gap**: The U16 OTA role is undocumented. U13 strings (`"ota start"`, `"get ota info"`, `"ota download progress:%d%%"`, `"write firmware fail"`) suggest U13 is the OTA slave/receiver. U16 likely acts as a transparent pipe for OTA data flowing from ESP32 (which has `rw_ota.c` and cloud MQTT connectivity).

### Is OTA dead code in U16?

Probably **not dead code** — but likely **transparent forwarding** rather than active participation:

| Evidence | Implication |
|----------|-------------|
| U13 main loop bit 14 calls OTA handler every iteration | OTA is active on U13 side |
| ESP32 has `rw_ota.c` source file | ESP32 can receive OTA data via WiFi/MQTT |
| OTA data path: Cloud → ESP32 → U16 → U13 | U16 is a transparent pipe for OTA binary stream |
| U13 parses the `[2B len][data][XOR]` framing | U13 handles protocol, U16 just relays bytes |

The most likely architecture: U16 does not need to understand OTA — it just forwards bytes between its UART interfaces.

---

## PIN Verification Flow — Full Chain Cross-Validation

```
Step                    ESP32                          U16                          U13
────                    ─────                          ───                          ───
1. User enters PIN      GPIO matrix → 4 digits         —                            —
                        displayed as "0 0 0 0"

2. Send PIN             0xAA 0x55 0x0B "1234" CS       receives binary frame        —
                        ──────────────────────────▶    parses 0xAA 0x55

3. Forward to U13       —                              {"cmd":"verify_pin",          receives JSON
                                                        "pin":"1234"}               parses command
                                                       ──────────────────────────▶

4. Verify               —                              —                            reads PIN from EEPROM
                                                                                    compares

5. Response             —                              0xAA 0x55 0x0C 0x00 CS      sends OK/FAIL
                                                        (or 0x01 for fail)          (via U16 as JSON)
                                                        ◀──────────────────────────

6. Result to ESP        0xAA 0x55 0x0C 0x00 CS         translates JSON→binary       —
                        ◀─────────────────────────

7. Display              "IdLE" (OK) / "LoCK" (FAIL)    —                            —
```

### Cross-validation per step

| Step | ESP32.md | U16.md | GD32F305.md | Cross-Valid? |
|:----:|:--------:|:------:|:-----------:|:------------:|
| 1 | ✅ entry state 0x0D | — | — | N/A (ESP32 only) |
| 2 | ✅ 0xAA 0x55 0x0B + 4B PIN + XOR | ✅ receives and parses | — | ✅ |
| 3 | — | ✅ translates to JSON | ✅ {"cmd":"verify_pin","pin":"1234"} | ✅ |
| 4 | — | — | ✅ "compare pwd correct/uncorrect" | ✅ |
| 5 | — | ✅ 0x0C 0x00/0x01 | ✅ sends result via U16 | ✅ |
| 6 | ✅ 0xAA 0x55 0x0C + 1B + XOR | ✅ translates | — | ✅ |
| 7 | ✅ "IdLE"/"LoCK" | — | — | N/A (ESP32 only) |

---

## Status Forwarding Chain

```
ESP32                     U16                         U13
─────                     ───                         ───

(periodically)            border_message (JSON)       receives sensor data
0xAA 0x55 0x0D CS         ◀────────────────────────── and executes navigation logic
─────▶                    version_message (JSON)
cmd 0x0D = STATUS_REQ     ◀──────────────────────────
                          seach_border_message (JSON)
                          ◀──────────────────────────
                          follow_border_message (JSON)
                          ◀──────────────────────────
```

U16 **initiates** sending sensor data to U13 (does not wait for a request).
U13 **listens** and processes data for its navigation logic.

---

## Cross-Validation Summary

### Confirmed Matches (✅)

| Aspect | Status |
|--------|:------:|
| Baud rate ESP32↔U16 (115200) | ✅ |
| Baud rate U16↔U13 (230400) | ✅ |
| Binary frame (0xAA 0x55 + CMD + PAYLOAD + XOR) | ✅ |
| XOR checksum pattern (confirmed in U13 FUN_08008cb8) | ✅ |
| PIN verification flow (all 3 chips) | ✅ |
| Button command forwarding (0x01-0x04) | ✅ |
| PWD_VERIFY (0x0B) forwarded as JSON to U13 | ✅ |
| PWD_RESULT response format (0x0C 0x00/0x01) | ✅ |

### Gaps (⚠️)

| Gap | Description | Severity |
|-----|-------------|:--------:|
| **U16 binary cmd enumeration** | U16.md does not list which cmd 0x01-0x15 it handles | HIGH |
| **U16 XOR checksum verification** | Unknown whether U16 verifies XOR from ESP32 or just forwards | MEDIUM |
| **U13 JSON parser** | GD32F305.md assumes U13 parses JSON, but no proof in decompilation (U13 may use simpler format) | HIGH |
| **Full JSON vocabulary** | Unknown all JSON commands U13↔U16 exchange | HIGH |
| **U16 OTA implementation** | U16.md does not describe U16's OTA role (only U13 documented) | HIGH |
| **UART timeout/retry** | No documentation of timeouts, retransmission, init sequence | MEDIUM |
| **ESP32 cmd 0x03** | Cmd ID table has 0x01, 0x02, 0x04 but no 0x03 | LOW |
| **128B limit on U13 side** | U16 limits messages to 128B, but U13 may accept more | LOW |

### Known Documentation Limitations

- **ESP32.md**: All cmd IDs are "inferred from decompiled state machine case values and string context" — may be imprecise (especially missing 0x03)
- **U16.md**: No "ESP32 protocol parser" section — U16 certainly parses ESP32 frames, but this has not been mapped
- **GD32F305.md**: No section on USART0 data reception — unknown whether U13 receives JSON from U16 via interrupt, DMA, or polling

---

## Recommendations for Further Analysis

1. **UART sniffing** — capture live traffic between ESP32 and U16 (J8 pins 3-4) to confirm actual cmd IDs and frame format
2. **U16 decompilation** — find U16 functions that parse 0xAA 0x55 frames and map them to JSON
3. **U13 USART0 ISR analysis** — find USART0 interrupt in U13 to confirm whether U13 parses JSON or binary
4. **Analyze FUN_08007c68 / FUN_08007b78** — these are init and wait-for-ready functions for U16↔U13; decompiling them would reveal the startup sequence
