#!/usr/bin/env python3
"""Convert ESP32 firmware image (ota_0.bin) to an ELF with sections for objdump."""

import struct
import sys

ET_EXEC = 2
EM_XTENSA = 94
PT_LOAD = 1
SHT_PROGBITS = 1
SHT_STRTAB = 3
PF_R = 4
PF_W = 2
PF_X = 1
SHF_WRITE = 1
SHF_ALLOC = 2
SHF_EXECINSTR = 4

def main():
    if len(sys.argv) != 3:
        print(f'Usage: {sys.argv[0]} ota_0.bin output.elf', file=sys.stderr)
        sys.exit(1)

    img_path = sys.argv[1]
    elf_path = sys.argv[2]

    with open(img_path, 'rb') as f:
        img = f.read()

    # Parse the segment table from the image instead of hard-coding it.
    # Each segment = 8-byte header (load addr, length) followed by its data; the
    # data starts 8 bytes AFTER the header offset. (The previous hard-coded table
    # used header offsets as data offsets, shifting every section by 8 bytes and
    # producing the broken esp32/firmware/disasm.s.)
    if img[0] != 0xE9:
        sys.exit('not an ESP32 app image')
    entry = struct.unpack('<I', img[4:8])[0]
    seg_data = []
    off = 0x18                  # 8-byte image header + 16-byte extended header
    for _ in range(img[1]):
        vaddr, size = struct.unpack('<II', img[off:off + 8])
        data = img[off + 8:off + 8 + size]
        off += 8 + size
        if 0x3f400000 <= vaddr < 0x3f800000:
            name, pflags = 'drom%x' % vaddr, PF_R
        elif 0x3ff80000 <= vaddr < 0x40000000:
            name, pflags = 'dram%x' % vaddr, PF_R | PF_W
        elif 0x40070000 <= vaddr < 0x400c0000:
            name, pflags = 'iram%x' % vaddr, PF_R | PF_X
        elif 0x400d0000 <= vaddr < 0x40400000:
            name, pflags = 'irom%x' % vaddr, PF_R | PF_X
        else:
            name, pflags = 'rtc%x' % vaddr, PF_R
        if size:
            seg_data.append((vaddr, size, data, name, pflags))

    # Section name string table (shstrtab)
    shstrtab_names = [b'.shstrtab'] + [b'.' + s[3].encode() for s in seg_data]
    shstrtab = b'\x00' + b'\x00'.join(shstrtab_names) + b'\x00'

    # Calculate offset layout
    ehdr_size = 52
    phdr_size = 32
    shdr_size = 40
    num_phdrs = len(seg_data)
    num_sections = 1 + num_phdrs + 1   # null + segs + shstrtab section
    shstrtab_ndx = num_sections - 1

    phdr_off = ehdr_size
    shdr_off = phdr_off + num_phdrs * phdr_size
    data_off = shdr_off + num_sections * shdr_size

    # Build program headers
    phdrs = b''
    data_blob = b''
    seg_ndx = 1   # section index for first segment (0 is null)

    for vaddr, size, data, name, pflags in seg_data:
        padded = data
        pad = (4 - len(padded) % 4) % 4
        padded += b'\x00' * pad

        phdrs += struct.pack('<IIIIIIII',
            PT_LOAD,
            data_off + len(data_blob),
            vaddr,
            vaddr,
            len(data),
            len(data),
            pflags,
            4   # align
        )

        data_blob += padded
        seg_ndx += 1

    # Build section headers
    shdrs = b'\x00' * shdr_size   # null section

    # Compute name offsets in shstrtab (past initial null)
    cur = 1
    name_off_map = {}
    for nm in shstrtab_names:
        name_off_map[nm] = cur
        cur += len(nm)

    data_pos = 0
    for vaddr, size, data, name, pflags in seg_data:
        # Section flags
        sh_flags = 0
        if pflags & PF_W:
            sh_flags |= SHF_WRITE
        if pflags & (PF_R | PF_X):
            sh_flags |= SHF_ALLOC
        if pflags & PF_X:
            sh_flags |= SHF_EXECINSTR

        sh_type = SHT_PROGBITS
        shdrs += struct.pack('<IIIIIIIIII',
            name_off_map[b'.' + name.encode()],
            sh_type,
            sh_flags,
            vaddr,
            data_off + data_pos,
            len(data),
            0, 0, 4, 0
        )
        data_pos += (size + 3) & ~3  # padded

    # shstrtab section header
    shdrs += struct.pack('<IIIIIIIIII',
        name_off_map[b'.shstrtab'],
        SHT_STRTAB,
        0,
        0,
        data_off + data_pos,
        len(shstrtab),
        0, 0, 1, 0
    )

    # Append shstrtab data
    data_blob += shstrtab

    # ELF header
    ident = b'\x7fELF' + struct.pack('BBBB', 1, 1, 1, 0) + b'\x00' * 8
    ehdr = struct.pack('<16sHHIIIIIHHHHHH',
        ident,
        ET_EXEC,
        EM_XTENSA,
        1,
        entry,
        phdr_off,
        shdr_off,
        0,      # e_flags
        ehdr_size,
        phdr_size,
        num_phdrs,
        shdr_size,
        num_sections,
        shstrtab_ndx
    )

    elf = ehdr + phdrs + shdrs + data_blob

    with open(elf_path, 'wb') as f:
        f.write(elf)

    print(f'Wrote {elf_path} ({len(elf)} bytes, {num_phdrs} segments)')


if __name__ == '__main__':
    main()
