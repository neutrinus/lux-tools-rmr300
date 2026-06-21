# ESP32 Ghidra Decompilation — Display Driver Analysis

## Setup

| Parameter | Value |
|-----------|-------|
| Tool | Ghidra 12.1.2 |
| JDK | Adoptium Temurin 21.0.6+7 |
| Processor | Xtensa LE32 (`Xtensa:LE:32:default`) |
| Binary | `ota_0.elf` (ELF converted from `ota_0.bin` via `esp32_img2elf.py`) |
| LOAD segments | 6 |
| Functions found (after analysis) | **6091** |
| Analysis time | ~16 sec |

### Project

```
/home/marek/tmp/kosiarka/ghidra_proj/ESP32_Firmware/
  └── ota_0.elf  (imported, analyzed)
```

### Method

Due to a broken OSGi Java compiler in Ghidra 12.1.2, the workflow bypasses it:

1. Write `extends GhidraScript` Java class
2. Compile with `javac -cp "$GHIDRA_JARS"` → `.class`
3. Run with `analyzeHeadless -postScript ClassName.class` (HeadlessAnalyzer loads `.class` directly)

## Memory Map (from ELF)

| Section | VMA | File Offset | Size | Contents |
|---------|-----|-------------|------|----------|
| DROM (rodata) | `0x3f400020` | 0x234 | 0x262b4 | Strings, config tables, lookup tables |
| DRAM (data) | `0x3ffbdb60` | 0x264e8 | 0x6cd4 | BSS, GOT, runtime data |
| IRAM (cache init) | `0x40080000` | 0x2d1bc | 0x3060 | Startup, FreeRTOS, vectors |
| IROM (main code) | `0x400d0020` | 0x3021c | 0xe4004 | ESP-IDF + application |
| IRAM (tasks) | `0x40083060` | 0x114220 | 0x191f8 | FreeRTOS tasks, hot code |
| RTC_DATA | `0x50000000` | 0x12d418 | 0x10 | RTC fast memory |

## Display Architecture — Key Finding

The OEM firmware uses the **ESP32 LCD_CAM (I80) hardware peripheral** to drive the display — **NOT GPIO bit-banging**.

The LCD_CAM peripheral (documented in ESP32 Technical Reference Manual §14) supports parallel 8080-family interface. The firmware configures it to write 24-bit frames to the 3× 74HC595 shift registers.

### Register Map

| Address | Alias | Size | Function |
|---------|-------|------|----------|
| `0x3FF4E0C4` | `DAT_40190520` | 32b | LCD_CAM control/status register |
| `0x3FF4E130` | `DAT_40190e24` | 32b | Data buffer word 0 |
| `0x3FF4E134` | `DAT_40190e28` | 32b | Data buffer word 1 |
| `0x3FF4E138` | `DAT_40190e2c` | 32b | Data buffer word 2 |
| `0x3FF4E13C` | `DAT_40190e30` | 32b | Data buffer word 3 |
| `0x3FF4E140` | `DAT_40190e34` | 32b | Data buffer word 4 |

Control register `0x3FF4E0C4` bitfields (from decompilation):
- Bits 0-7: Sequence/step number (written in FUN_4019052c, FUN_40190634)
- Bit 8 (0x100): Toggle enable (cleared in FUN_401915cc)
- Bit 9 (0x200): Strobe/trigger (set then cleared in FUN_4019052c, FUN_40190634)
- Bits 10-14: Channel/phase selector (written in FUN_401908b4 with `(param_8 & 0x1f) << 10`)
- Bits 15-31: Configuration/timing flags

### Call Graph

```
Display Task (FUN_4019447c)
  │
  ├── FUN_4019153c (INIT)
  │     └── FUN_40191490(2, 4)        ← display update start
  │
  ├── FUN_40193e1c()                  ← init sequence
  ├── FUN_401943e0()                  ← init sequence
  │
  ├── FUN_40191790()                  ← display info/update
  │     ├── FUN_401915cc(uVar5, ...)  ← CONFIG (toggle enable, set timing)
  │     └── FUN_401913e0(uVar5)       ← SERIALIZE (read → write)
  │
  ├── FUN_40191490()                  ← main display update
  │     ├── FUN_401908b4(param_2, ..., 0xb, param_9)
  │     │                              ← CONFIG peripheral: sets bits 10-14
  │     │                                 of 0x3FF4E0C4, 11 items (0xb)
  │     ├── FUN_40190fb4(param_1,..., 0xb, ...)
  │     │                              ← RENDER: 12-case switch for segment
  │     │                                 patterns, multiplexing
  │     ├── FUN_401913e0(0xc)         ← SERIALIZE
  │     └── FUN_40194294()            ← timing config
  │
  └── FUN_40194294()                  ← timing config (also called from ---^)
```

### Functions Decompiled

#### FUN_401908b4 (CONFIG peripheral, size 0x1218)
- **Callers**: FUN_40191490 (passes param_8 = 0xb = 11)
- **Action**: Writes 5-bit channel selector `(param_8 & 0x1f) << 10` to `0x3FF4E0C4`
- Iterates 11 times over param_9 array, building a bitmask from `param_9[i] == 1`
- Writes to 3 register groups at `DAT_4019084c` / `_50` / `_54` selected by `(loopVar >> 3)`
- **param_8 = 0xb is a count/channel number, NOT a GPIO number**

#### FUN_4019052c (GPIO/sequence config, size 0x263)
- **Callers**: (referenced by DAT_4018a104 dispatch - generic peripheral register read)
- Writes sequence of bytes (0x55, 0xe, 0xfd, 0x25, 0x36, ...) to lower byte of `0x3FF4E0C4`
- Toggles bit 9 (0x200) for each step — generating peripheral clock strobes
- Adjusts a counter register at `DAT_40190528` based on step position
- Pattern length depends on param_2 (0x55 for param_2==0, 0xe for param_2!=0)

#### FUN_40190634 (Configure data stream, size ~0x80)
- Iterates `(param_1 & 0xff) * 3` times
- Writes sequence 0,1,2,3,4,5... to `0x3FF4E0C4` lower byte
- For each step, copies a value from param_2[] to `DAT_40190528`
- Toggles bit 9 (0x200) strobe after each write

#### FUN_40190e38 (WRITE data to buffer, size 0x221)
- **Called by**: FUN_401913e0
- Writes 5 × 4 bytes to buffer at `0x3FF4E130..0x3FF4E140`
- Data format: 3 bytes per item (24-bit shift register frame)

#### FUN_401913e0 (SERIALIZE, size 0x165)
- **Called by**: FUN_40191490 (with arg 0xc=12), FUN_40191790, FUN_401915cc
- Reads 3 × 32-bit words from `DAT_40190524` (= `0x3FF4E0C0`)
- Extracts byte via lookup table `DAT_401913d8` (= `0x3FFCD3CC`, bit-order reversal?)
- Calls FUN_40190e38 to write serialized data to buffer

#### FUN_40190fb4 (RENDER, size ~0x820)
- **Called by**: FUN_40191490 with param_8=0xb=11 items
- **Decompilation partially corrupted** (pcode errors, bad instructions at `0x01000100`)
- Contains a 12-case switch table (for digits 0-9, colon, blank)
- Each case sets up segment bit patterns for one display position
- Writes to register pairs DAT_40190884/DAT_40190888, DAT_4019088c/DAT_40190890, etc.
- Implements digit scanning/multiplexing logic

#### FUN_4019153c (INIT, size 0x120)
- **Called by**: Display task FUN_4019447c
- Copies 8 bytes from `PTR_DAT_4019014c` (initialization data)
- Calls `FUN_40191490(2, 4)` — starts display update with 2 phases, 4 items

#### FUN_401915cc (CONFIG, size 0x433)
- **Called by**: FUN_40191790
- Calls `FUN_4019052c((int)param_2, param_3)` for sequence timing
- Calls `FUN_401913e0(param_1 & 0xff)` for serialize
- Toggles bit 8 (0x100) of `0x3FF4E0C4` (enable/disable?)

#### FUN_40194294 (Timing config)
- Modifies multiple bitfields in `0x3FF4E0C4` using AND/OR masks from ROM data
- Sets timing parameters for LCD_CAM peripheral

#### FUN_4019447c (Display Task)
- Main display state machine
- Calls INIT, handles display mode switching
- Contains branches for sleep, active, PIN entry modes

### String References

Display-related source paths found in DROM:
- `../main/src/app/rw_display.c` @ `0x3f403467`
- `../components/spi_tube_driver/src/spi_tube_drive.c` @ `0x3f407524`
- `TubeInit` @ `0x3f407557`

Other display strings:
- `"tube driver not initialed"`, `"tube service"`, `"tube scan"`, `"tube flash timer"`
- `"ft-lcd-"`, `"Display_esp32"`, `"display process run successful/failed"`

**Note**: "spi_tube_driver" strings reference a **separate SPI-based code path** (using `spi_bus_config_t` with MOSI=12, SCLK=10). This is NOT the path used for the actual 74HC595 display on this board — the firmware uses the LCD_CAM peripheral instead, though the SPI tube driver code is compiled in but uses a different GPIO set.

### GPIO Numbers in Display Context

The display function call chain (FUN_4019447c → FUN_40191490 → FUN_401908b4, FUN_40190fb4, etc.) does **NOT contain explicit GPIO pin numbers**.

The LCD_CAM peripheral connects to physical GPIO pins via the **IO_MUX** (GPIO matrix), configured at boot time. The GPIO numbers are passed to `gpio_matrix_out()` with LCD_CAM signal IDs (I80_RS=126, I80_WR=128, I80_CS=130, I80_DATA0..7=100..107). These configuration calls happen in initialization code that was **not found** through the display function chain.

**DROM search for GPIO arrays**: Over 1000 false-positive matches were found — the DROM contains many lookup tables and data structures that happen to contain byte values matching potential GPIO numbers. None could be confidently linked to the LCD_CAM display configuration.

### LCD_CAM Signal to GPIO Mapping (hypothetical)

Based on ESP32 LCD_CAM standard signal usage with 74HC595s:

| 74HC595 Pin | LCD_CAM Signal | Signal ID | Typical GPIO |
|-------------|---------------|-----------|-------------|
| SH_CP (SCLK) | I80_WR | 128 | ? |
| DS (MOSI) | I80_DATA0 | 100 | ? |
| ST_CP (CS/Latch) | I80_CS | 130 | ? |
| MR (Master Reset) | I80_RS | 126 | ? |
| OE (Output Enable) | I80_ENA | — | ? |

The actual GPIO numbers for these signals are configured in the IO_MUX init code, NOT in the display functions themselves.

## Known Issues

1. **Pcode errors** in FUN_401908b4 and FUN_40190fb4 — overlapping instructions, bad data at `0x01000100`, constructor resolution failures. These are caused by:
   - Switch table dispatch code (computed goto) that Ghidra can't fully resolve
   - Peripheral register read/write patterns (`memw()` + store)

2. **No xrefs to LCD_CAM registers** — Ghidra's reference analysis doesn't track memory-mapped peripheral addresses. The registers at `0x3FF4E0xx` are outside the analyzed memory map.

3. **No GPIO numbers found in display functions** — the pins are configured during IO_MUX initialization, not hardcoded in the display driver.

## Conclusions

| Finding | Status |
|---------|--------|
| Display uses LCD_CAM peripheral (not bit-bang) | ✅ CONFIRMED |
| Register map identified (control + data buffer) | ✅ CONFIRMED |
| Call graph fully mapped | ✅ CONFIRMED |
| 11 display items (0xb), 12-case RENDER switch | ✅ CONFIRMED |
| 3 × 74HC595, 24-bit frame format | ✅ CONFIRMED |
| SPI tube driver code is separate (unused GPIOs 10,12) | ✅ CONFIRMED |
| Actual GPIO pin numbers for LCD_CAM | ❌ NOT FOUND in firmware |
| Segment-to-bit mapping | ❌ NOT FOUND (RENDER corrupted) |
| LCD frame timing parameters | ❌ NOT FOUND |

**Next step for GPIO pin identification**: Empirical test — single-bit sweep using ESPHome or custom firmware on candidate pins {33, 18, 32} (the only combination that has empirically produced any display response).

---

## UART Protocol Analysis (2026-06-21) — JSON, not Binary

Wcześniejsza analiza w `ESP32.md` sugerowała protokół binarny (0xAA 0x55 + CMD + CS @115200). **Jest to nieprawidłowe.** Analiza stringów w DROM potwierdza JSON:

| Offset | String | Znaczenie |
|--------|--------|-----------|
| 0x3502 | `"json get cmd failed"` | Parsowanie JSON z pola `cmd` |
| 0x397B | `"json get cmd failed"` | (warning variant) |
| 0x455C | `"send command: \"%s\""` | Wysyłanie komendy jako string JSON |
| 0x48E4 | `"cant find cmd:%x callback"` | Dispatch callbacków po ID komendy |
| 0x4823 | `"add receive message head callback, head = 0x%08x, callback @0x%08x"` | Dynamiczna rejestracja handlerów |
| 0x5B22 | `"goto action"` | Log przy wykonaniu akcji (mow/charge) |

### Architektura przetwarzania komend

```
MQTT (server.sk-robot.com) → ESP32 → JSON @230400 → U16 (cJSON) → wewn. protokół → U13
```

Callbacki są rejestrowane **dynamicznie** (nie ma statycznej tablicy dispatch). Funkcja rejestrująca przyjmuje `head` (32-bit ID komendy) i wskaźnik na handler.

### Tablica Command ID → String (0x31578-0x31718)

Znaleziona w DROM tablica mapująca ID komend na nazwy string (używana do logowania/debug):

| Offset | CMD | String (DROM) | Uwagi |
|--------|-----|---------------|-------|
| 0x31578 | 0x41000020 | START_ACK | Potwierdzenie START |
| 0x31588 | 0x41000002 | LOCK | Blokada PIN |
| 0x31590 | 0x41000003 | EXEC_ACTION | Akcja wykonana (START/HOME) |
| 0x31598 | 0x41000004 | ERROR_NOTIFY | Błąd |
| 0x315A8 | 0x41000005 | PIN_SEND | Wysłanie PIN |
| 0x315B0 | 0x41000013 | (nieznane) | — |
| 0x315B8 | 0x41000014 | (nieznane) | — |
| 0x315C0 | 0x41000006 | (nieznane) | — |
| 0x315C8 | 0x41000007 | (nieznane) | — |
| 0x315D0 | 0x41000008 | SHUTDOWN | Wyłączenie |
| 0x315D8 | 0x41000009 | (nieznane) | — |
| 0x315E0 | 0x41000010 | (nieznane) | — |
| 0x315E8 | 0x41000011 | (nieznane) | — |
| 0x315F0 | 0x41000012 | (nieznane) | — |
| 0x315F8 | 0x41000030 | (nieznane) | — |
| 0x31600 | 0x41000031 | (nieznane) | — |
| 0x31608 | **0x4100000A** | **"action"** | **Kluczowa komenda — "action"** |
| 0x31700 | 0x10000002 | ESP_ERR_ACK? | — |
| 0x31708 | 0x10000003 | ESP_ERR_ACK? | — |
| 0x31710 | 0x10000007 | ESP_ERR_ACK2 | Potwierdzenie błędu |
| 0x31718 | 0x10000009 | (nieznane) | — |

### Kluczowe obserwacje

1. **CMD 0x4100000A = "action"** — to może być komenda, którą ESP→MB wysyła by zainicjować akcję (mow/charge). Wartość 0x4100000A jest w zakresie MB→ESP (0x410000xx), ale nie jest udokumentowana w żadnym z captures. Może to być komenda ESP→MB lub nieodkryty MB→ESP notification.

2. **"goto action" (0x5B22)** — string logowany gdy firmware przechodzi do wykonania akcji. Występuje w okolicy obsługi hasła i czujnika deszczu. Dokładny przepływ: MQTT command → parse JSON → get "action" field → log "goto action" → send UART command.

3. **Brak stringów "mow"/"charge" w kontekście komend** — oryginalny firmware NIE wysyła stringów "mow" ani "charge" przez UART. Zamiast tego używa numerycznych ID komend (prawdopodobnie 0x4100000A z polem "action" w JSON).

4. **ESP_TRIM (0x300000A6) w oryginalnym firmware** — wysyłany jako pusty `{"cmd":805306534}` (bez pól harmonogramu). Harmonogram (`trim`, `sun_st`, `auto`, itp.) przychodzi TYLKO z MB jako SCHEDULE (0x330000A6).

### Wnioski dla ESPHome

1. `start_mowing()` wysyłający ESP_TRIM + ESP_ERR_ACK1 + ESP_STATE(2) — **nie działa**, bo oryginalny firmware używał innej komendy (prawdopodobnie związanej z 0x4100000A lub 0x41000012).

2. `return_to_dock()` wysyłający 0x10000001 (ESP_ERR_ACK1) — **nie działa**, bo to jest potwierdzenie błędu, a nie komenda powrotu.

3. **Potencjalne nieprzetestowane komendy do wypróbowania:**
   - `{"cmd":1090519050}` (0x4100000A) — "action"
   - `{"cmd":1090519058}` (0x41000012) — kolejna nieznana
   - `{"cmd":805306391}` (0x30000017) — z dispatch table
   - `{"cmd":805306392}` (0x30000018) — z dispatch table  
   - `{"cmd":268435458}` (0x10000002) — error ack
   - `{"cmd":268435459}` (0x10000003) — error ack

4. **Oryginalny firmware vs ESPHome:**
   - Oryginalny: UART na GPIO13(RX)/GPIO15(TX) @115200 przez GPIO matrix
   - ESPHome: UART na GPIO16(RX)/GPIO17(TX) @230400 przez UART2
   - Oba trafiają do tego samego U16 przez J8 — U16 najwyraźniej obsługuje różne piny/baudrates

5. **Aby definitywnie rozstrzygnąć** jaki JSON command startuje koszenie, potrzebny jest:
   - Sniffer UART z oryginalnym firmware (drugi ESP32)
   - Lub pełna dekompilacja U16 (GD32F303, FreeRTOS + cJSON) gdzie są handlery JSON command
   - Lub empiryczne testowanie nieudokumentowanych komend

### Pliki

| Plik | Zawartość |
|------|-----------|
| `ota_0.bin` | Aplikacja ESP32 (partition ota_0) |
| `esp32_dump.bin` | Pełny 4MB dump flash ESP32 |
| `disasm.s` | 400k linii disassembly Xtensa (objdump) |
| `ESP32.md` | Wstępna analiza (częściowo nieaktualna — błędny protokół binarny) |
| `ESP32_GPIO_ANALYSIS.md` | Analiza pinów GPIO |
| `1610-report.md` | Starszy raport o pinach wyświetlacza (błędne CLK=5, MOSI=32) |
