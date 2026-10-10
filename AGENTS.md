# AGENTS.md

Reverse-engineering repo for the **SNK OEM robot mower** (Lux Tools A-RMR-300-24 and ~15 rebrands; same PCBs `80102372-01` / `80102373-01`). Two largely independent workstreams — read the right doc first:

1. **Firmware / protocol** (reverse engineering + ESPHome replacement). Mostly wound down as of 2026-06-24.
2. **Used-mower price hunting** via `tools/search_mowers.py` (active).

## Read these first

- `README.md` — map of the whole repo and rebrand/model table.
- `FIRMWARE_MAP.md` — layout, modules, key functions and data structures of all three firmwares (ESP32, U13, U16).
- `ha.md` — firmware status + history. **§14 (H3) is the current root-cause conclusion.**
- `PROTOCOLS.md` — **authoritative** command directions and protocol.
- `captures/README.md` — logic-analyzer setup + scenario index.

## Commands

ESPHome firmware (config `snk-mower.yaml`, custom component `components/snk_mower/`):
```bash
esphome run snk-mower.yaml        # global ~/.local/bin/esphome 2026.6.0; run from repo root
```
- `esphome` is **not** in `.venv`. As of 2026-10-09 `esphome config`/`compile` crash on Python 3.14 with a protobuf `TypeError: Metaclasses with custom tp_new are not supported` — suspect an env/protobuf issue, don't assume the YAML is broken.
- `secrets.yaml` (gitignored, holds WiFi creds) is **required** by the YAML.

Offer scan (`.venv` exists only for this; Python is system 3.14):
```bash
source .venv/bin/activate
python3 tools/search_mowers.py            # OLX(API) + Kleinanzeigen + Blocket + Allegro
python3 tools/search_mowers.py --login    # one-time: solve Allegro DataDome captcha headful
python3 tools/search_mowers.py --all-prices --portals olx,ka
```
- Needs `curl_cffi` (installed) and Playwright Firefox. Allegro session persists in `.browser_profile_allegro/`.
- Playwright Firefox can fail to launch from a dangling lock: `rm -f ~/.cache/ms-playwright/firefox-1522/firefox/lock`.
- Stale shells `tools/search_mowers.sh`, `tools/olx_search.sh`, `tools/olx_weekly_search.sh` are superseded — use the Python script.

Other tools:
```bash
python3 tools/decode_capture.py captures/01-boot/capture.vcd   # sigrok-based UART decode
python3 tools/esp32_img2elf.py <image.bin>                     # ESP32 image -> ELF
# tools/re/: esp32dis.py, gd32dis.py, la_decode.py (capstone>=6), see tools/re/README.md
# tools/*.cfg = OpenOCD/SWD scripts for dumping u13/u16 (GD32) and EEPROM
```

## Protocol facts that docs get wrong

- Real link: **JSON over UART 230400 8N1**, frame `&{json}<CRC>#` (**single** `#`), Dallas/Maxim CRC-8 (poly 0x31) over the JSON bytes only. Bus: ESP32 ↔ **U13** directly (GD32F305 `dpport`, USART0; motors/PIN/EEPROM U22). U16 (GD32F303) is **not** a bridge: it is the border-wire/lift MCU on a separate U13 port (`bdport`). Evidence: `20261009_claude_investigation.md`.
- **Ignore banner direction tables in `ha.md` §2** (generated from constants; wrong). Use `PROTOCOLS.md`.
- **Ignore the binary protocol `0xAA 0x55` @115200 in `esp32/notes/ESP32.md`** — it is wrong.
- `captures/2026-06-21/README.md` has **reversed D1/D2 labels**; direction labels in `captures/README.md` (01–06) are correct.
- `0x10000001/2/7` are **key commands, not error ACKs** (START/HOME → `0x10000007`, then OK → `0x10000001` mow / `0x10000002` home).

## Hard-won constraints (don't re-litigate)

- **Corrected 2026-10-09:** mowing *can* be started over UART. The original firmware does it on START→OK: `0x10000007`, then `0x10000001` (capture `2026-06-21/trzeci`, MB answers `state:8`). Earlier attempts failed because they used MB→ESP commands. Buttons START/HOME/OK are on ESP32 GPIO22/21/19 (pull-up, active low), not on U16. Remote app commands map to `0x10000021` start, `0x10000022` home, `0x10000023` stop, `0x10000015` edge trim (firmware only, untested). See `20261009_claude_investigation.md`.
- **U13 shuts the mower off ~16 s after boot if the ESP misses the handshake**: answer every `0x40000009` with ESP_INFO and every `0x40000008` with ESP_INIT `{"init":3}`, within ~2.5 s / ~1 s. Never stay silent for >3 s while running. `0x10000004/14/24` is a **power-off** command. See `20261009_claude_investigation.md` §10.
- **PIN is not in the ESP32**; it lives in the U13 KV-store / EEPROM U22. The ESP only forwards an entered PIN.
- Root cause of the last failure: custom firmware never sent the `ESP_BOOT`/`ESP_KEEPALIVE`/`ESP_POLL`/`ESP_INIT` handshake, so the MB (U13) ignored it (`ha.md` §14).

## Git gotchas

The repo ignores a lot; new files silently won't be committed without `git add -f`:
- Global: `*.bin`, `*.pdf`, `*.log`, `*.elf`, `*.o`, `__pycache__/`, `.venv/`, `.esphome/`.
- Dirs: `esp32/`, `u13/decomp/`, `ghidra_proj/`, `sw/ghidra-*/`, `class_scripts/`, `tools/bridge_compile*/`.
- Env/sensitive: `secrets.yaml`, `.allegro_cookies.json`, `kosiarka-logs*.txt`, `.browser_profile/`, `.browser_profile_allegro/`, `offers/`, `results/`.
- Tracked files in ignored dirs stay tracked; only *new* files are ignored.

If you need vendored Ghidra tooling, `sw/ghidra-cli/` is a nested project with its own `AGENTS.md`/`CLAUDE.md` (read those there).
