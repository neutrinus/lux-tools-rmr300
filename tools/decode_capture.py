#!/usr/bin/env python3
"""Decode SNK mower JSON UART frames from sigrok VCD captures.

Uses sigrok-cli built-in UART decoder.
Protocol: JSON at 230400 8N1, standard polarity (not inverted).

Usage:
  python3 tools/decode_capture.py captures/01-boot/capture.vcd
  python3 tools/decode_capture.py captures/01-boot/capture.vcd --ch1 D1 --ch2 D0
"""

import sys
import json
import re
import subprocess
from pathlib import Path
from collections import OrderedDict

BAUDRATE = 230400
INVERT = False

DIR_MB = "MB→ESP"
DIR_ESP = "ESP→MB"

CMD_NAMES = OrderedDict([
    (0x20000001, "POWER_ON"),
    (0x20000004, "POWER_READY"),
    (0x22000000, "RAIN"),
    (0x30000005, "ESP_KEEPALIVE"),
    (0x30000021, "ESP_WIFI"),
    (0x30000022, "ESP_BT"),
    (0x30000028, "ESP_STATE"),
    (0x300000A1, "ESP_POLL"),
    (0x300000A6, "ESP_TRIM"),
    (0x300000A7, "ESP_RAIN_CFG"),
    (0x300000A8, "ESP_MULTIZONE"),
    (0x33000021, "PIN_RESULT"),
    (0x33000022, "PIN_RESULT2"),
    (0x330000A0, "STATUS"),
    (0x330000A1, "DEVICE_INFO"),
    (0x330000A2, "HW_VERSIONS"),
    (0x330000A6, "SCHEDULE"),
    (0x330000A7, "RAIN_CFG"),
    (0x330000A8, "MULTIZONE"),
    (0x330000AA, "UNKNOWN_AA"),
    (0x330000B0, "MAP_CFG"),
    (0x40000001, "ESP_INIT"),
    (0x40000004, "ESP_BOOT"),
    (0x40000006, "ESP_INFO"),
    (0x40000008, "BOOT_INIT"),
    (0x40000009, "BOOT_HEART"),
    (0x40000011, "RTC_HEARTBEAT"),
    (0x40000014, "UNKNOWN_14"),
    (0x40000020, "LIGHT"),
    (0x40000021, "BOOT_ACK"),
    (0x41000002, "LOCK"),
    (0x50000021, "BATTERY"),
])


def sigrok_uart_json(capture_file, rx_channel):
    """Extract UART bytes from sigrok capture at 230400 8N1."""
    inv = ":invert_rx=yes" if INVERT else ""
    cmd = [
        "sigrok-cli", "-i", str(capture_file),
        "-P", f"uart:baudrate={BAUDRATE}:data_bits=8:parity=none:stop_bits=1.0:rx={rx_channel}:format=hex{inv}",
        "-A", "uart=rx-data",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        return None, result.stderr

    bytes_out = []
    for line in result.stdout.split("\n"):
        line = line.strip()
        if not line:
            continue
        m = re.match(r"uart-\d+:\s*([0-9a-fA-F]+)", line)
        if m:
            bytes_out.append(int(m.group(1), 16))
    return bytes(bytes_out), result.stderr


def extract_json_objects(byte_data):
    """Parse JSON objects from byte stream (non-printable = frame boundaries)."""
    text = "".join(chr(b) if 32 <= b < 127 else "\n" for b in byte_data)
    frames = [f.strip() for f in text.split("\n") if f.strip()]

    objects = []
    for f in frames:
        for m in re.finditer(r"\{[^}]+\}", f):
            try:
                obj = json.loads(m.group())
                objects.append(obj)
            except json.JSONDecodeError:
                pass
    return objects


def cmd_name(cmd):
    return CMD_NAMES.get(cmd, f"0x{cmd:08X}")


def format_obj(obj):
    """Format a JSON command object for display."""
    cmd = obj.get("cmd", 0)
    name = cmd_name(cmd)
    extras = ", ".join(f"{k}={v}" for k, v in obj.items() if k != "cmd")
    if extras:
        return f"{name:<20}  {extras}"
    return name


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Decode SNK mower JSON UART frames")
    parser.add_argument("file", help="sigrok VCD capture")
    parser.add_argument("--ch1", default="D0", help="Channel MB→ESP (default D0)")
    parser.add_argument("--ch2", default="D1", help="Channel ESP→MB (default D1)")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    args = parser.parse_args()

    fp = Path(args.file)
    if not fp.exists():
        print(f"File not found: {fp}", file=sys.stderr)
        sys.exit(1)

    channels = [
        (args.ch1, DIR_MB),
        (args.ch2, DIR_ESP),
    ]

    all_objects = []
    for ch, direction in channels:
        data, err = sigrok_uart_json(fp, ch)
        if data is None:
            print(f"Error decoding {ch}: {err[:200]}", file=sys.stderr)
            continue
        objs = extract_json_objects(data)
        for obj in objs:
            obj["_ch"] = ch
            obj["_dir"] = direction
        all_objects.extend(objs)
        print(f"{direction} ({ch}): {len(objs)} JSON objects, {len(data)} bytes",
              file=sys.stderr)

    if args.json:
        print(json.dumps(all_objects, indent=2))
        return

    print(f"\n{'Dir':<10} {'Idx':>4} {'Name':<20} {'Fields'}")
    print(f"{'---':<10} {'---':>4} {'----':<20} {'------'}")
    for i, obj in enumerate(all_objects):
        d = obj.pop("_dir", "")
        extras = ", ".join(f"{k}={v}" for k, v in obj.items() if k != "cmd")
        name = cmd_name(obj.get("cmd", 0))
        print(f"{d:<10} {i:>4} {name:<20} {extras}")


if __name__ == "__main__":
    main()
