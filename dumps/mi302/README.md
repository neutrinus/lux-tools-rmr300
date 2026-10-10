# Firmware dumps from salonnikov/mower-stock-reverse

Factory dumps of a second unit of the same platform (`SNK_MAINBOARD_CP_V11` / `SNK_DISPLAY_CP_V11`, sold as VILLARTEC MI 302), copied from the `dist/` folder of [salonnikov/mower-stock-reverse](https://github.com/salonnikov/mower-stock-reverse) at commit `4f97999`. Their "chip1" is our U13 and "chip2" is our U16.

| File | Their name | Size | md5 | What it is |
|------|-----------|------|-----|------------|
| `u13_flash_mi302.bin` | `gd32-mainboard-dump-v1.bin` | 1 MB | `316927a42857b6f28bd7a0ad2d070de5` | U13 (GD32F305) flash at `0x08000000`, dumped over SWD on 2026-06-29. Another firmware version than ours (`dumps/u13/`); same structure, addresses shifted. Bootloader CRC (`0x08017FFC`) and application CRC (`0x080FFFFC`) both check out |
| `u16_flash_mi302.bin` | `gd32-mainboard-chip2-dump-v1.bin` | 256 KB | `ddbc6b5aea44252eef3c6a63c39d2010` | U16 (GD32F303) flash. Byte-identical to our `dumps/u16/u16_flash.bin` |
| `esp32_dump_mi302_v3.02.05.bin` | `esp32-display-dump-v1.bin` | 4 MB | `a9d7f453a85f3ba805e0d8d88af1b1f7` | Full ESP32 flash of the display board, dumped over J1 on 2026-08-09. `Display_esp32` **v3.02.05** in `ota_0` (ours is v3.02.02). The NVS partition holds that unit's factory Wi-Fi credentials and serial number |

Their decompilations, symbol lists and string tables are not copied; they are in their repository under `reverse-v2/chip1/`, `reverse-v2/chip2/` and `reverse-v2/esp32-display/`.

To use the ESP32 image with `tools/re/esp32dis.py`, cut out the app partition first:

```bash
python3 -c "b=open('dumps/mi302/esp32_dump_mi302_v3.02.05.bin','rb').read(); open('/tmp/ota_0_v30205.bin','wb').write(b[0x10000:0x180000])"
python3 tools/re/esp32dis.py /tmp/ota_0_v30205.bin info
```
