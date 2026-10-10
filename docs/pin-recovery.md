# PIN recovery

The 4-digit PIN is stored by **U13** (main MCU) as env key `pwd` (uint32) on the external SPI NOR, and cached in RAM at `0x2000027C` while the mower runs. The ESP32 on the display board only forwards the PIN typed on the buttons; it does not store it. The ESPHome component needs the PIN in its configuration (`pin:`), so you need to know it.

| Method | Needs | Keeps settings | Status |
|---|---|---|---|
| Read the RAM over SWD | Opening the mower, SWD adapter | yes | Works |
| Reset to `0000` with `0x30000023` | ESPHome on the display board | yes | Untested |
| Reset to `0000` with cloud command 112 | Mower paired with the Sunseeker app | yes | Works on Brucke units |
| `FORMATFLASH.json` on a USB stick | USB stick | **no**, wipes the whole env | Untested |

## Read the PIN over SWD

Needs: a Raspberry Pi Pico with the [debugprobe](https://github.com/raspberrypi/debugprobe) firmware (or another CMSIS-DAP adapter), three wires, OpenOCD, and access to the P4 pads on the mainboard ([disassembly.md](disassembly.md)).

1. Connect the Pico to P4 (pins counted from the top: `3V3 DIO CLK JTDO RES GND`). **Do not connect 3.3 V**: the mower powers U13 from its battery.

   | Pico | Signal | P4 |
   |---|---|---|
   | pin 3 (GND) | GND | 6 (TP81) |
   | pin 4 (GP2) | SWCLK | 3 (TP77) |
   | pin 5 (GP3) | SWDIO | 2 (TP76) |

2. Switch the mower on and dump the RAM (from the repository root):

   ```bash
   openocd -f tools/swd/dump_full_ram.cfg      # writes dumps/u13/ram_full.bin
   ```

3. Read the PIN:

   ```bash
   python3 -c "import struct; r=open('dumps/u13/ram_full.bin','rb').read(); print('PIN = %04d' % struct.unpack('<I', r[0x27c:0x280])[0])"
   ```

On Linux the Pico needs a udev rule:

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="0666"' | \
  sudo tee /etc/udev/rules.d/99-pico-debugprobe.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Reset the PIN to `0000` with `0x30000023`

U13 has a "reset pwd" command. On `0x30000023` (ESP → MB, no fields) the handler at `0804650a` sets `pwd` = 0, clears the wrong-PIN counter, re-enables PIN entry and answers `0x33000023 {"result":true}`. Nothing else in the env changes and there is no state check.

- **From ESPHome** (untested): the link must be up, so configure the component with any PIN, then send the frame once, e.g. from a template button with `lambda: 'id(my_mower).send_raw_json("{\"cmd\":805306403}");'`. Watch the log for `0x33000023`, then set `pin: "0000"` and reflash.
- **From the cloud**: the original ESP32 firmware turns cloud command `{"cmd":112}` into `0x30000023`. Brucke owners have reset their PIN this way from the Sunseeker app. The ESP talks plain MQTT to `server.sk-robot.com:1883`, so a local broker reached through a DNS override could send it too, but the mower's WiFi must have been set up through the app first.

## Factory reset with `FORMATFLASH.json`

A non-empty file named `FORMATFLASH.json` on a FAT32 stick in the mainboard USB socket makes the bootloader erase the whole SPI NOR at power-on. This removes the PIN together with all settings, statistics, the event log and the product config (serial number, model type, feature flags), which may then need restoring. Details: [usb.md](usb.md#factory-reset-formatflashjson).

## What does not work

- **Searching the flash dumps**: the PIN is not in the U13, U16 or ESP32 flash.
- **Removing the RTC coin cell**: the PIN is in the SPI NOR, not in battery-backed RAM.
- **Reading the ESP32**: it never stores the PIN.
- **Guessing** is slow: 10 wrong PINs block entry for a while (tested; the manufacturer's FAQ says 10 minutes with the mower switched on), then you can try again.
