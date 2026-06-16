#!/usr/bin/env python3
# Find and disassemble code referencing FORMATFLASH.json and USB file ops
import struct, capstone, sys

BIN_FILE = "/home/marek/tmp/kosiarka/u13_flash.bin"
BASE = 0x08000000

with open(BIN_FILE, "rb") as f:
    data = f.read()

# ARM Cortex-M4 uses Thumb2
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
md.detail = True

# Key addresses from strings analysis
STR_ADDRS = {
    "FORMATFLASH.json": 0x3990,
    "env_read.json": 0x3010,
    "USB read env": 0x2b78,
    "USB read env %s": 0x2b98,
    "ready to format flash": 0x39a4,
    "format flash": 0x39bc,
    "env file read error": 0x3f40,
    "file open error": 0x34d5,
    "file read error": 0x34e5,
    "flash write error": 0x34f4,
    "into usb host mode": 0x45a5,
    "into usb device mode": 0x45b9,
    "to usb disk": 0x3dbc,
    "USB config finish": 0x1207d,
}

# Search for all LDR (PC-relative) instructions that reference our strings
# In Thumb2, LDR Rt, [PC, #imm] is encoded as:
# f8df xxxx  (where xxxx encodes the register and offset)
# or
# 480x        (LDR Rd, [PC, #imm]) short form

# Let's also search for literal pool values matching our string addresses
KEY_VALS = set(STR_ADDRS.values()) | set(v + BASE for v in STR_ADDRS.values())

print("=== Searching for literal pool references to key strings ===")
for i in range(0, len(data) - 4, 4):
    val = struct.unpack("<I", data[i:i+4])[0]
    if val in KEY_VALS:
        name = next((k for k, v in STR_ADDRS.items() if v == val or v + BASE == val), "?")
        print(f"  Literal 0x{val:08x} ({name}) at offset 0x{i:x}")

# Now disassemble around each found literal to understand the function
print("\n=== Disassembling code near literal references ===")
pos = 0
while pos < len(data) - 4:
    val = struct.unpack("<I", data[pos:pos+4])[0]
    if val in {v + BASE for v in STR_ADDRS.values()}:
        # Found a literal referencing one of our strings
        name = next((k for k, v in STR_ADDRS.items() if v + BASE == val), "?")
        # The LDR instruction should be within 4KB before this literal
        search_start = max(0, pos - 0x1000)
        search_end = max(0, pos - 2)
        # Search for LDR Rd, [PC, #imm] instructions
        for j in range(search_start, search_end):
            word_val = struct.unpack_from("<H", data, j)[0]
            # Thumb2 LDR.W Rd,[PC,#imm]: f8df XXrX where r = register encoding
            if (word_val & 0xFF00) == 0xf800 and ((word_val >> 8) & 0xE0) == 0xE0:
                # This might be a Thumb2 instruction - check full instruction
                instr_full = struct.unpack_from("<I", data, max(0, j))[0]
                if (instr_full & 0xFFFFFF00) == 0xF8DF0000:
                    # LDR.W Rd, [PC, #imm]
                    rt = (instr_full >> 24) & 0xF
                    imm12 = (instr_full >> 8) & 0xFFF
                    # PC is word-aligned: PC = (j & ~3) + 4 or PC = (j & ~3) + 2
                    pc_val = (j & ~0x3) + 4  # Thumb2: PC is (instruction_addr + 4) aligned
                    target = pc_val + imm12
                    if target >= pos - 4 and target <= pos + 4:
                        print(f"\n  LDR.W r{rt}, [PC, #{imm12}] @ 0x{j:x} -> literal at 0x{pos:x} = {name}")
                        # Print surrounding code
                        start_code = max(0, pos - 0x80)
                        print(f"  --- Code context ---")
                        for k, (addr, size, mnem, ops) in enumerate(md.disasm_lite(data[start_code:pos+0x20], BASE + start_code)):
                            if addr >= BASE + pos - 0x80 and addr < BASE + pos + 0x20:
                                print(f"  0x{addr:08x}: {mnem}\t{ops}")
                        break
        pos += 4
    else:
        pos += 1

# Let's also look for USB file operation functions by finding the "USB read env" function
print("\n=== Searching for USB file operations ===")
for kw_name, addr in [("USB read env", 0x2b78), ("USB read env %s", 0x2b98)]:
    print(f"\n--- {kw_name} @ 0x{addr:x} ---")
    # Look for LDR that points to this address
    target_va = addr + BASE
    for i in range(0, len(data) - 4):
        val = struct.unpack_from("<I", data, i)[0]
        if val == target_va:
            # Found literal - disassemble surrounding code
            start = max(0, i - 0x40)
            end = min(len(data), i + 0x40)
            count = 0
            for insn_addr, size, mnem, ops in md.disasm_lite(data[start:end], BASE + start):
                if count < 30:
                    print(f"  0x{insn_addr:08x}: {mnem}\t{ops}")
                    count += 1

print("\nDone.")
