# AGENTS.md

ESPHome replacement firmware and technical documentation for the **SNK OEM robot mower** (Lux Tools A-RMR-300-24 and rebrands; PCBs `80102372-01` / `80102373-01`). The component in `components/snk_mower/` is feature-complete and runs on the owner's mower. A separate, unrelated tool hunts used-mower prices (`tools/search_mowers.py`, `results/`); leave it alone unless asked.

## Rules for this repo

- **Everything committed is in English** (docs, comments, commit messages), even though the owner chats in Polish. Exception: `tools/search_mowers.py`, `results/` and `docs/reference/user-manual.txt` stay as they are.
- **Documentation describes the current technical state only**: no history, no "previously we thought", no dated investigation logs. Put new facts into the matching file in `docs/`. Mark uncertain claims **[I]** (inferred) next to **[F]** (from firmware) / **[C]** (seen on the wire or on the mower) where the file uses markers.
- The owner's Home Assistant builds its own config on another machine from `github://neutrinus/lux-tools-rmr300@main`: a component change reaches the mower only after **compile, commit and push**. Compile locally before pushing.

## Map

| Path | What |
|---|---|
| `README.md` | Overview, feature list, supported models, documentation index |
| `docs/install.md`, `docs/component.md` | Installation guide, component reference |
| `docs/protocol.md` | **Authoritative** display ↔ U13 protocol (commands, directions, states, sequences) |
| `docs/hardware.md` | Boards, pinouts, connectors |
| `docs/firmware-esp32.md`, `firmware-u13.md`, `firmware-u16.md` | Original firmware maps (addresses checked against `dumps/`) |
| `docs/usb.md`, `docs/battery.md`, `docs/pin-recovery.md`, `docs/disassembly.md`, `docs/reverse-engineering.md` | The rest |
| `esphome/snk-mower.yaml` | Example configuration (`esphome/secrets.yaml.example`) |
| `dumps/` | Firmware dumps (`esp32`, `u13` + Ghidra exports, `u16`, `mi302`) |
| `captures/` | LA captures of the original firmware, index in `captures/README.md` |
| `tools/re/` | `esp32dis.py`, `gd32dis.py`, `la_decode.py` (capstone ≥ 6), see `tools/re/README.md` |
| `tools/swd/` | OpenOCD dump scripts (run from the repo root) |

## Commands

Local compile (system Python 3.14 breaks ESPHome's protobuf; use the uv venv):
```bash
uv venv --python 3.13 .venv-esphome && VIRTUAL_ENV=.venv-esphome uv pip install esphome   # once
# copy esphome/snk-mower.yaml to a scratch dir, replace the github source with
#   - source: {type: local, path: <repo>/components}
# add a dummy secrets.yaml (keys from esphome/secrets.yaml.example), then:
.venv-esphome/bin/esphome compile <scratch>/snk-mower.yaml
```

Captures and dumps:
```bash
python3 tools/re/la_decode.py captures/09-mow-stop-home/capture.sr --uart D1=ESP,D2=MB --lines D3=START,D4=OK
python3 tools/re/gd32dis.py dumps/u13/u13_flash.bin dis 08063808 08063900
python3 tools/re/esp32dis.py dumps/esp32/ota_0.bin str "KeyNum"
```

Price search (`.venv`, Playwright Firefox, `curl_cffi`):
```bash
source .venv/bin/activate && python3 tools/search_mowers.py
```
A dangling Playwright lock: `rm -f ~/.cache/ms-playwright/firefox-1522/firefox/lock`.

## Facts that are easy to get wrong

- The ESP32 talks **directly to U13** (`dpport`, USART0). U16 is the boundary-wire/lift MCU on another U13 port, not a bridge.
- Frames are JSON `&{json}<CRC8>#` (single `#`) at 230400. The CRC byte can be `{`, so frames start at `&{`.
- `0x10000001/2/7` are **key commands** (START/HOME → `0x10000007`, OK → `0x10000001` mow / `0x10000002` home), not acks.
- `0x33000021/22` ack the ESP's WiFi/BT frames; the **PIN result is `0x41000020`**.
- **No command is acted on until U13 has accepted the PIN** (`{"lock":1}` → `0x41000005 {"pwd":N}` → `0x41000020 {"result":1}`).
- U13 switches the mower off ~16 s after boot if the handshake (`0x40000009` → ESP_INFO, `0x40000008` → ESP_INIT) is missed, and drops the link after 3 s without frames. `0x10000004/14/24` **power the mower off**.
- `state` 2 is a transient after unlock, not mowing; mowing is 8, edge trim 16.
- The PIN lives in U13's env on the SPI NOR, never on the ESP32.

## Git gotchas

`*.bin`, `*.pdf`, `*.elf`, `*.log` are ignored globally: new dumps or datasheets need `git add -f`. Tracked files stay tracked. `secrets.yaml` is ignored everywhere.
