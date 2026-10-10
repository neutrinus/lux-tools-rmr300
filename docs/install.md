# Installing ESPHome on the mower

The ESPHome firmware replaces the original firmware of the **ESP32 on the display board**. The mainboard (U13, U16) keeps its original firmware; the ESP32 talks to it exactly like the original display firmware did, so the mower keeps working on its own (schedule, rain delay, boundary wire, STOP button) and gains Home Assistant control.

The first flash needs a cable to the ESP32. Every later update goes over WiFi (OTA).

## What you need

- The mower's **PIN**. If you do not know it, see [pin-recovery.md](pin-recovery.md).
- A **USB-to-serial adapter with 3.3 V logic** (CP2102, CH340, FT232R, …) and four or five Dupont wires.
- Screwdrivers to open the mower ([disassembly.md](disassembly.md)).
- ESPHome, either the Home Assistant ESPHome add-on or the command-line tool (`pip install esphome`). The configuration was tested with ESPHome 2026.9.
- WiFi coverage in the garden (2.4 GHz). The module has an external antenna.

## 1. Open the mower and find J1

Open the top shell as shown in [disassembly.md](disassembly.md) and locate the display board (`SNK_DISPLAY_CP_V11`). The 6-pin header **J1** next to the ESP32 module is its programming port:

| J1 pin | Label | Connect to the adapter |
|---|---|---|
| 1 | `3U3` | 3.3 V (see power, below) |
| 2 | `T` | RX |
| 3 | `R` | TX |
| 4, 5 | `GND` | GND |
| 6 | `P` | GND **while powering on**, to enter the bootloader |

Photos and the board pinout: [hardware.md](hardware.md#display-board-snk_display_cp_v11).

### Power

Unplug the ribbon cable (J8) from the display board and power the board from the adapter's 3.3 V pin. A mainboard that is switched on would switch the mower off after about 16 s anyway, because the ESP32 in its bootloader does not answer the mainboard's handshake.

Weak 3.3 V regulators on some adapters cannot supply the ESP32 when its WiFi starts, and the board resets (brownout). If that happens, power J1 from a separate 3.3 V supply (≥ 500 mA) with a common ground.

### Bootloader mode

Connect `P` to GND, then apply power. The ESP32 starts in download mode; `P` can be released once the flashing tool has connected. There is no reset pin on J1: to restart, remove and reapply power.

## 2. Back up the original firmware

Do this before anything else. It is the only way back to the original display firmware.

```bash
pip install esptool
esptool.py --port /dev/ttyUSB0 --baud 921600 read_flash 0 0x400000 display-original.bin
```

The image contains the factory WiFi settings and serial number of your unit; keep it private. To restore it later:

```bash
esptool.py --port /dev/ttyUSB0 --baud 921600 write_flash 0 display-original.bin
```

## 3. Create the configuration

Copy [`esphome/snk-mower.yaml`](../esphome/snk-mower.yaml) into your ESPHome configuration directory (rename it as you like) and create `secrets.yaml` next to it, starting from [`esphome/secrets.yaml.example`](../esphome/secrets.yaml.example):

```yaml
wifi_ssid: "your-network"
wifi_password: "your-wifi-password"
ap_password: "fallback-hotspot-password"
api_key: "32-byte base64 key, generate one with: openssl rand -base64 32"
mower_pin: "1234"     # the 4-digit PIN of your mower
```

The example configuration pulls the component from this repository on GitHub (`external_components`). Adjust `name`/`friendly_name` at the top. All options are described in [component.md](component.md).

## 4. Flash

With the board in bootloader mode:

```bash
esphome run snk-mower.yaml --device /dev/ttyUSB0
```

In the Home Assistant add-on, use **Install → Plug into this computer** (browser serial) or **Manual download** and flash the factory image with [ESPHome Web](https://web.esphome.io/).

## 5. Reassemble and start

1. Disconnect the adapter and the `P` bridge.
2. Plug the ribbon cable back in and close the mower (or leave it open for the first test).
3. Switch the mower on. The display shows `boot`, then the state (`IdLE` once the PIN has been accepted).
4. Add the device in Home Assistant (it is discovered automatically; enter the API key).

The log (`esphome logs snk-mower.yaml`, or the add-on's log view) should show the handshake and the PIN. The API usually connects 30–40 s after power-on, too late to see the boot; the component therefore prints its record of the first boot frames 60 s after boot. A healthy start looks like:

```
TX {"cmd":1073741828}                        ESP boot
RX {"cmd":536870916}                         link up
RX {"cmd":1090519042,"lock":1}               U13 asks for the PIN
TX {"cmd":1090519045,"pwd":1234}
RX {"cmd":1090519072,"result":1}             PIN accepted
RX {"cmd":855638176,"state":6}               ready
```

If the PIN is rejected (`"result":0`), fix `mower_pin` and update over OTA. Until the PIN is accepted, the mainboard ignores all commands.

## 6. Updates

Later updates go over WiFi: `esphome run snk-mower.yaml` (or **Install → Wirelessly** in the add-on). The mainboard keeps running during the update, and the ESP32 resynchronises with it after the restart without switching the mower off. The component is fetched from GitHub; `refresh` in `external_components` sets how often ESPHome checks for a newer version.

## Using it

Home Assistant gets:

| Entity | |
|---|---|
| **State** | `idle`, `mowing`, `trimming`, `returning`, `charging`, `docked`, `error`, `locked` |
| **Battery** | % |
| **Error Code** | 0 = no error; otherwise the mainboard's error bitmask (16 = no boundary signal, `E11` on the display) |
| **Raining** | Rain sensor on the display board |
| **Start Mowing**, **Return to Dock**, **Stop**, **Trim Edge** | Buttons. Edge trim works only from the station |
| Diagnostics | Cut/work area, operating time, battery health, rain delay, light level, model, serial number, mainboard firmware, WiFi signal (disabled by default) |

The front buttons work as before: START or HOME, then OK within 3 s. The display shows the state and the battery level, and dims from 20:00 to 06:00. The red STOP button and the power switch are handled by the mainboard.

## Troubleshooting

| Symptom | Cause |
|---|---|
| Mower switches off ~16 s after power-on | The ESP32 did not answer the handshake: not running ESPHome, wrong UART pins, or the ribbon cable is loose |
| Display stays on `boot`, log shows nothing received | No data from the mainboard: check the ribbon cable and `rx_pin: GPIO16` |
| Commands do nothing | PIN not accepted: look for `"result":0` in the log (after five rejections the state shows `locked`) and check `mower_pin` |
| Edge trim does nothing | It only works with the mower in the station |
| `Error Code` 16 | No boundary-wire signal: the mower is outside the wire, or the station is off |
