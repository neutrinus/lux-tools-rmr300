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

    segments = [
        (0x3f400020, 0x000262b4, 'drom',     PF_R),
        (0x3ffbdb60, 0x00006cd4, 'dram',     PF_R | PF_W),
        (0x40080000, 0x00003060, 'iram0',    PF_R | PF_X),
        (0x400d0020, 0x000e4004, 'irom',     PF_R | PF_X),
        (0x40083060, 0x000191f8, 'iram1',    PF_R | PF_X),
        (0x50000000, 0x00000010, 'rtc_data', PF_R),
    ]
    file_offsets = [
        0x00000018, 0x000262d4, 0x0002cfb0,
        0x00030018, 0x00114024, 0x0012d224,
    ]
    entry = 0x400814ec

    with open(img_path, 'rb') as f:
        img = f.read()

    seg_data = []
    for (vaddr, size, name, pflags), fo in zip(segments, file_offsets):
        if size == 0:
            continue
        data = img[fo:fo + size]
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
