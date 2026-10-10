#!/usr/bin/env python3
"""ESP32 (Xtensa LX6) app-image disassembler/xref helper built on capstone >= 6.

Works directly on an ESP-IDF app image (e.g. dumps/esp32/ota_0.bin), no ELF
or Xtensa binutils needed. Resolves L32R literals (values + strings) and CALLn
targets, and decodes BEQZ.N/BNEZ.N which capstone 6.0 does not.

  esp32dis.py IMG info                      segments + header
  esp32dis.py IMG dis START END             annotated disassembly of a range
  esp32dis.py IMG dump OUT.s                whole code (IRAM+IROM), annotated
  esp32dis.py IMG str TEXT                  find string(s) + code that loads them
  esp32dis.py IMG xref VALUE                code that loads VALUE via L32R
  esp32dis.py IMG callers ADDR              CALL4/8/12 sites targeting ADDR

Addresses are hex. Function attribution uses `entry a1,N` prologues (heuristic).
Linear sweep: data between functions can decode as junk, re-run `dis` from the
exact branch target when something looks misaligned.
"""
import argparse, bisect, struct, sys

try:
    import capstone
    MD = capstone.Cs(capstone.CS_ARCH_XTENSA, capstone.CS_MODE_XTENSA_ESP32)
except (ImportError, AttributeError):
    sys.exit("needs capstone >= 6.0 with Xtensa support: pip install 'capstone>=6'")


class Image:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        if self.d[0] != 0xE9:
            sys.exit('not an ESP image (magic 0xE9 missing)')
        self.entry = struct.unpack('<I', self.d[4:8])[0]
        self.segs = []          # (vaddr, length, file offset of DATA)
        off = 0x18              # 8-byte header + 16-byte extended header
        for _ in range(self.d[1]):
            a, l = struct.unpack('<II', self.d[off:off + 8])
            self.segs.append((a, l, off + 8))
            off += 8 + l
        self.code = [s for s in self.segs if 0x40070000 <= s[0] < 0x40400000]
        self._funcs = None

    def rd(self, addr, n):
        for a, l, o in self.segs:
            if a <= addr < a + l:
                return self.d[o + addr - a:o + addr - a + n]
        return None

    def u32(self, addr):
        b = self.rd(addr, 4)
        return struct.unpack('<I', b)[0] if b and len(b) == 4 else None

    def cstr(self, addr, maxlen=160):
        b = self.rd(addr, maxlen)
        if not b:
            return None
        s = b.split(b'\0')[0]
        if len(s) >= 2 and all(32 <= c < 127 or c in (9, 10) for c in s):
            return s.decode().replace('\n', '\\n')
        return None

    @property
    def funcs(self):
        if self._funcs is None:
            f = []
            for a, l, o in self.code:
                blob = self.d[o:o + l]
                for i in range((-a) % 4, l - 3, 4):
                    if blob[i] == 0x36 and blob[i + 1] & 0xF == 1:
                        f.append(a + i)
            self._funcs = sorted(f)
        return self._funcs

    def func_of(self, pc):
        i = bisect.bisect_right(self.funcs, pc) - 1
        return self.funcs[i] if i >= 0 else None


def l32r_target(pc, b):
    imm = b[1] | (b[2] << 8)
    return ((((pc + 3) & ~3) + ((imm | 0xFFFF0000) << 2)) & 0xFFFFFFFF)


def call_target(pc, b):
    v = (b[0] | (b[1] << 8) | (b[2] << 16)) >> 6
    if v & 0x20000:
        v -= 0x40000
    return ((pc & ~3) + (v << 2) + 4) & 0xFFFFFFFF


def dis(img, start, end):
    pc = start
    while pc < end:
        b = img.rd(pc, min(64, end - pc + 3))
        if not b:
            break
        got = False
        for i in MD.disasm(b, pc):
            got = True
            raw = bytes(i.bytes)
            ann = ''
            if i.mnemonic == 'l32r':
                t = l32r_target(i.address, raw)
                v = img.u32(t)
                ann = '[%08x]=%s' % (t, '%#x' % v if v is not None else '?')
                if v is not None and 0x3F400000 <= v < 0x3F800000:
                    s = img.cstr(v)
                    if s:
                        ann += ' "%s"' % s
            elif i.mnemonic in ('call0', 'call4', 'call8', 'call12'):
                ann = '-> %08x' % call_target(i.address, raw)
            yield i.address, i.mnemonic, i.op_str, ann
            pc = i.address + i.size
            if pc >= end:
                break
        if not got:
            b0, b1 = b[0], b[1] if len(b) > 1 else 0
            if b0 & 0xF == 0xC and (b0 >> 4) >= 8:   # BEQZ.N / BNEZ.N (RI6)
                t = b0 >> 4
                imm = ((t & 3) << 4) | (b1 >> 4)
                yield pc, 'beqz.n' if t < 0xC else 'bnez.n', 'a%d, %08x' % (b1 & 0xF, pc + 4 + imm), ''
                pc += 2
            else:
                yield pc, '.byte', '%#04x' % b0, ''
                pc += 1


def fmt(a, m, o, ann):
    return '%08x  %-8s %-28s %s' % (a, m, o, ann)


def lits_with(img, v):
    vb = struct.pack('<I', v)
    for a, l, o in img.code:
        blob = img.d[o:o + l]
        i = blob.find(vb)
        while i >= 0:
            if (a + i) % 4 == 0:
                yield a + i
            i = blob.find(vb, i + 1)


def l32r_refs(img, lit):
    for a, l, o in img.code:
        lo, hi = max(a, lit - 4), min(a + l - 3, lit + 0x40004)
        for pc in range(lo, hi):
            b = img.d[o + pc - a:o + pc - a + 3]
            if b[0] & 0xF == 1 and l32r_target(pc, b) == lit:
                yield pc


def xref(img, v):
    return [(pc, img.func_of(pc)) for lit in lits_with(img, v) for pc in l32r_refs(img, lit)]


def callers(img, t):
    res = []
    for a, l, o in img.code:
        lo, hi = max(a, t - 0x80000), min(a + l - 3, t + 0x80000)
        for pc in range(lo, hi):
            b = img.d[o + pc - a:o + pc - a + 3]
            if b[0] & 0xF == 5 and (b[0] >> 4) & 3:
                if call_target(pc, b) == t:
                    res.append((pc, img.func_of(pc)))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('info')
    p = sub.add_parser('dis'); p.add_argument('start'); p.add_argument('end')
    p = sub.add_parser('dump'); p.add_argument('out')
    p = sub.add_parser('str'); p.add_argument('text')
    p = sub.add_parser('xref'); p.add_argument('value')
    p = sub.add_parser('callers'); p.add_argument('addr')
    a = ap.parse_args()
    img = Image(a.image)
    h = lambda x: int(x, 16)

    if a.cmd == 'info':
        print('entry %#x' % img.entry)
        for v, l, o in img.segs:
            print('seg vaddr=%08x len=%#07x file_off=%#08x' % (v, l, o))
    elif a.cmd == 'dis':
        for row in dis(img, h(a.start), h(a.end)):
            print(fmt(*row))
    elif a.cmd == 'dump':
        fs = set(img.funcs)
        with open(a.out, 'w') as f:
            f.write('; %s  entry=%#x  (capstone %s, linear sweep, see tools/re/README.md)\n'
                    % (a.image, img.entry, capstone.__version__))
            for v, l, o in img.code:
                f.write('\n; ===== segment %08x..%08x =====\n' % (v, v + l))
                # resync the sweep at every `entry` prologue so a desync never spills over
                cuts = [v] + [x for x in img.funcs if v < x < v + l] + [v + l]
                for s, e in zip(cuts, cuts[1:]):
                    if s in fs:
                        f.write('\nfn_%08x:\n' % s)
                    for row in dis(img, s, e):
                        f.write(fmt(*row) + '\n')
    elif a.cmd == 'str':
        needle = a.text.encode()
        for v, l, o in img.segs:
            blob = img.d[o:o + l]
            i = blob.find(needle)
            while i >= 0:
                s = i
                while s > 0 and blob[s - 1] != 0:
                    s -= 1
                addr = v + s
                print('%08x "%s"' % (addr, img.cstr(addr) or ''))
                for pc, fn in xref(img, addr):
                    print('    loaded at %08x in fn %08x' % (pc, fn or 0))
                i = blob.find(needle, i + 1)
    elif a.cmd == 'xref':
        for pc, fn in xref(img, h(a.value)):
            print('%08x in fn %08x' % (pc, fn or 0))
    elif a.cmd == 'callers':
        for pc, fn in callers(img, h(a.addr)):
            print('%08x in fn %08x' % (pc, fn or 0))


if __name__ == '__main__':
    main()
