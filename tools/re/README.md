# Reverse-engineering tools

Pure Python, no Ghidra or Xtensa binutils needed.

```bash
python3 -m pip install 'capstone>=6'   # 6.x is the first release with Xtensa (ESP32) support
```

| Tool | For | What it does |
|------|-----|--------------|
| `esp32dis.py` | ESP32 app image (`esp32/firmware/ota_0.bin`) | Annotated Xtensa disassembly. Resolves `l32r` literals (values and strings) and `call` targets. Also finds strings, xrefs and callers. |
| `gd32dis.py` | GD32 raw flash dumps (`u13/…/u13_flash.bin`, `u16/…/u16_flash.bin`), mapped at `0x08000000` | Thumb disassembly with resolved literals. Finds `bl` callers with their argument setup, literal-pool constants and peripheral bases. |
| `la_decode.py` | Logic-analyser captures (`captures/**/capture.vcd`, `*.sr`) | One time-ordered timeline of both UART directions plus button-line edges. |

`../esp32_img2elf.py` converts the ESP32 image to an ELF (for objdump or Ghidra). It now parses the segment table from the image. The old hard-coded table pointed 8 bytes too early, which is what produced the previous, broken `disasm.s`.

## esp32dis.py

```bash
T=tools/re/esp32dis.py; IMG=esp32/firmware/ota_0.bin

python3 $T $IMG info                          # segments, entry point
python3 $T $IMG str "button init error"       # where a string lives and which code loads it
python3 $T $IMG dis 400daf7c 400db01c         # annotated range
python3 $T $IMG callers 4012cdd0              # who calls gpio_get_level
python3 $T $IMG xref 3ffc5b18                 # who loads a global (here: UI process context)
python3 $T $IMG dump esp32/firmware/disasm.s  # regenerate the whole listing (~21 MB)
```

Example line:

```
400daf93  call8    . +0x51f0c                   -> 4012ce9c
400e1025  l32r     a12, . 0x400d1520            [400d1520]=0x3f405688 "I (%s) %s: KeyNum = %d\n"
```

Things to know:

- **Disassembly is a linear sweep, resynchronised at every `entry a1,N` prologue.** Literal pools and padding between functions can decode as junk. If a branch target lands mid-junk, run `dis` from that exact address.
- **Narrow branches are decoded by the tool itself.** capstone 6.0 cannot decode `beqz.n`/`bnez.n`, so `esp32dis.py` handles them.
- **Function attribution (`in fn …`) is heuristic.** It takes the nearest preceding `entry`.
- **ESP-IDF functions are best identified by the strings in their error paths.** For example, `4012ce9c` logs `"gpio_set_direction"` and `4012ce0c` logs `"gpio_set_pull_mode"`.

Useful anchors in `ota_0.bin` (v3.02.02):

| Address | What |
|---------|------|
| `400d77d8` | BSP init (tube, buzzer, button, rain drivers) |
| `400daf7c` / `400daef8` | Button driver init / poll (GPIO22, GPIO21, GPIO19, pull-up, active low) |
| `400e27f4` | UI main loop (10 ms tick), key edge, long press and key-combo logic |
| `400e2194` | UI event dispatcher, sends `0x1000000x` key commands |
| `400e1a58` | `send_cmd(cmd)`: builds `{"cmd":…}` and sends it to the mainboard |
| `400e0bc4` | Factory test task (`ft-key-`, `KeyNum`) |
| `400e6da0` | `TubeInit`: SPI display, MOSI=25, SCLK=33, CS=32, 400 kHz |

## gd32dis.py

```bash
G=tools/re/gd32dis.py; U13=u13/firmware/u13_flash.bin

python3 $G $U13 periph | grep USART           # where USART bases sit in literal pools
python3 $G $U13 callers 0x805303e             # gpio_input_bit_get callers (port/pin args shown)
python3 $G $U13 dis 0x0804de88 0x0804dec8     # U13 button reads PE10 / PE11
python3 $G $U13 lit 0x40013800                # literal-pool hits for USART0
```

`gpio_input_bit_get` is found by its byte pattern `80 68 08 40 00 d0 01 20 70 47`. In the U13 dump it is at `0x0800ca9a` (bootloader) and `0x0805303e` (app).

## la_decode.py

```bash
L=tools/re/la_decode.py

# captures 01-06: D0 = MB→ESP, D1 = ESP→MB, D2 = START line (01, 02, 04)
python3 $L captures/04-return-home/capture.vcd --uart D0=MB,D1=ESP --lines D2=START

# captures/2026-06-21: D1 = ESP TX, D2 = MB TX, D3 = START, D4 = OK
# (their "capture.vcd" files are really sigrok .sr zips; the tool detects that)
python3 $L captures/2026-06-21/trzeci/trzeci.sr --uart D1=ESP,D2=MB --lines D3=START,D4=OK
```

Heartbeats, keepalives, polls and WiFi/BT status frames are hidden unless you pass `--all`. Output:

```
  16.7372  START     pressed (low)
  16.8128  ESP   TX  [0x10000007] {"cmd":268435463}
  17.6022  OK        pressed (low)
  17.6828  ESP   TX  [0x10000001] {"cmd":268435457}
  17.7364  MB    TX  [0x330000a0] {"cmd":855638176,"state":8}
```

The older `tools/decode_capture.py` writes `decoded.json` grouped per channel. That loses the cross-direction ordering, which is exactly what shows cause and effect.
