#!/usr/bin/env python3
"""GD32 (Cortex-M, Thumb-2) flash-dump helper for U13/U16, built on capstone.

  gd32dis.py BIN dis START END       Thumb disassembly, PC-relative LDR literals resolved
  gd32dis.py BIN callers ADDR        BL sites calling ADDR, with the preceding
                                     instructions and resolved literals (shows args)
  gd32dis.py BIN lit VALUE           4-byte-aligned literal-pool words equal to VALUE
  gd32dis.py BIN periph              literal-pool words that are USART/UART/GPIO bases

BIN is a raw dump mapped at 0x08000000 (dumps/u13/u13_flash.bin, dumps/u16/u16_flash.bin).
"""
import argparse, struct, sys
import capstone

BASE = 0x08000000
MD = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
PERIPH = {
    0x40013800: 'USART0', 0x40004400: 'USART1', 0x40004800: 'USART2',
    0x40004C00: 'UART3', 0x40005000: 'UART4',
    0x40010800: 'GPIOA', 0x40010C00: 'GPIOB', 0x40011000: 'GPIOC',
    0x40011400: 'GPIOD', 0x40011800: 'GPIOE', 0x40011C00: 'GPIOF', 0x40012000: 'GPIOG',
}


def load(path):
    return open(path, 'rb').read()


def u32(d, a):
    o = a - BASE
    return struct.unpack('<I', d[o:o + 4])[0] if 0 <= o <= len(d) - 4 else None


def lit_ann(d, i):
    if i.mnemonic.startswith('ldr') and '[pc' in i.op_str:
        off = int(i.op_str.split('#')[-1].rstrip(']'), 16) if '#' in i.op_str else 0
        v = u32(d, ((i.address + 4) & ~3) + off)
        if v is not None:
            return '=%08x%s' % (v, ' ' + PERIPH[v] if v in PERIPH else '')
    return ''


def bl_target(pc, b):
    h1, h2 = b[0] | b[1] << 8, b[2] | b[3] << 8
    if (h1 & 0xF800) != 0xF000 or (h2 & 0xD000) != 0xD000:
        return None
    S = (h1 >> 10) & 1
    I1 = 1 - (((h2 >> 13) & 1) ^ S)
    I2 = 1 - (((h2 >> 11) & 1) ^ S)
    off = (S << 24) | (I1 << 23) | (I2 << 22) | ((h1 & 0x3FF) << 12) | ((h2 & 0x7FF) << 1)
    if S:
        off -= 1 << 25
    return pc + 4 + off


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bin')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('dis'); p.add_argument('start'); p.add_argument('end')
    p = sub.add_parser('callers'); p.add_argument('addr')
    p = sub.add_parser('lit'); p.add_argument('value')
    sub.add_parser('periph')
    a = ap.parse_args()
    d = load(a.bin)
    h = lambda x: int(x, 16)

    if a.cmd == 'dis':
        s, e = h(a.start) & ~1, h(a.end)
        for i in MD.disasm(d[s - BASE:e - BASE], s):
            print('%08x  %-8s %-30s %s' % (i.address, i.mnemonic, i.op_str, lit_ann(d, i)))
    elif a.cmd == 'callers':
        t = h(a.addr) & ~1
        for o in range(0, len(d) - 4, 2):
            if bl_target(BASE + o, d[o:o + 4]) == t:
                pc = BASE + o
                ins = list(MD.disasm(d[o - 24:o + 4], pc - 24))[-7:]
                print('%08x: %s' % (pc, ' | '.join('%s %s%s' % (x.mnemonic, x.op_str,
                      (' ' + lit_ann(d, x)) if lit_ann(d, x) else '') for x in ins)))
    elif a.cmd == 'lit':
        v = struct.pack('<I', h(a.value))
        i = d.find(v)
        while i >= 0:
            if i % 4 == 0:
                print('%08x' % (BASE + i))
            i = d.find(v, i + 1)
    elif a.cmd == 'periph':
        for o in range(0, len(d) - 4, 4):
            v = struct.unpack('<I', d[o:o + 4])[0]
            if v in PERIPH:
                print('%08x %s' % (BASE + o, PERIPH[v]))


if __name__ == '__main__':
    main()
