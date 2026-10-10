#!/usr/bin/env python3
"""Decode SNK UART captures (sigrok VCD or .sr) into one time-ordered timeline.

Frames are `&{json}<crc8>#` at 230400 8N1. Unlike tools/decode_capture.py this keeps
both directions interleaved by time and also prints edges on button lines, which is
what shows cause and effect (e.g. START pressed -> ESP sends 0x10000007 ~75 ms later).

  la_decode.py CAPTURE --uart D0=MB,D1=ESP [--lines D2=START] [--all]

CAPTURE: *.vcd (text VCD) or *.sr (sigrok session zip, captures 07-10). --all keeps the periodic heartbeat/keepalive/poll frames.
Commands are printed as decimal JSON plus a hex cmd id.
"""
import argparse, bisect, json, re, sys, zipfile

NOISE = {0x40000011, 0x30000005, 0x300000A1, 0x30000021, 0x30000022}


def load_vcd(path):
    ids, edges, t = {}, {}, 0
    with open(path, 'r', errors='replace') as f:
        for line in f:
            line = line.strip()
            if line.startswith('$var'):
                p = line.split(); ids[p[3]] = p[4]
            elif line.startswith('#'):
                parts = line.split(); t = int(parts[0][1:])
                for v in parts[1:]:
                    edges.setdefault(ids[v[1:]], []).append((t, int(v[0])))
            elif line.startswith('$timescale'):
                pass
    return edges, 1e7      # sigrok VCD timescale 100 ns


def load_sr(path):
    z = zipfile.ZipFile(path)
    md = z.read('metadata').decode()
    m = re.search(r'samplerate=([\d.]+)\s*(\w+)', md)
    rate = float(m.group(1)) * {'MHz': 1e6, 'kHz': 1e3}.get(m.group(2), 1)
    probes = {name: int(i) - 1 for i, name in re.findall(r'probe(\d+)=(\S+)', md)}
    chunks = sorted((n for n in z.namelist() if n.startswith('logic-1-')), key=lambda s: int(s.rsplit('-', 1)[1]))
    data = b''.join(z.read(c) for c in chunks)
    edges = {}
    try:
        import numpy as np
        arr = np.frombuffer(data, dtype=np.uint8)
        for name, bit in probes.items():
            s = (arr >> bit) & 1
            idx = np.nonzero(np.diff(s))[0] + 1
            edges[name] = [(0, int(s[0]))] + [(int(i), int(s[i])) for i in idx]
    except ImportError:            # pure-Python fallback, slow on long captures
        for name, bit in probes.items():
            prev, ev, mask = None, [], 1 << bit
            for idx, byte in enumerate(data):
                v = 1 if byte & mask else 0
                if v != prev:
                    ev.append((idx, v)); prev = v
            edges[name] = ev
    return edges, rate


def uart_bytes(ed, rate, baud=230400):
    bw = rate / baud
    ts = [e[0] for e in ed]
    lvl = lambda t: ed[max(0, bisect.bisect_right(ts, t) - 1)][1]
    out, t_last = [], -1
    for k in range(1, len(ed)):
        t, v = ed[k]
        if v == 0 and t > t_last:
            b = sum((lvl(t + bw * (1.5 + j)) << j) for j in range(8))
            out.append((t, b)); t_last = t + bw * 9.5
    return out


def frames(bs):
    res, buf, t0 = [], b'', None
    for t, b in bs:
        if b == 0x26:
            buf, t0 = b'', t
        buf += bytes([b])
        if b == 0x23 and buf.startswith(b'&{') and len(buf) > 3:
            res.append((t0, buf[1:-2].decode('latin1')))
            buf = b''
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('capture')
    ap.add_argument('--uart', required=True, help='CH=label,... e.g. D0=MB,D1=ESP')
    ap.add_argument('--lines', default='', help='CH=label,... button/logic lines to show')
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    with open(a.capture, 'rb') as f:
        is_zip = f.read(2) == b'PK'
    edges, rate = (load_sr if is_zip else load_vcd)(a.capture)
    parse = lambda s: [x.split('=') for x in s.split(',') if x]
    ev = []
    for ch, label in parse(a.uart):
        for t, js in frames(uart_bytes(edges[ch], rate)):
            try:
                cmd = json.loads(js).get('cmd')
            except ValueError:
                cmd = None
            if not a.all and cmd in NOISE:
                continue
            ev.append((t / rate, '%-5s TX' % label, ('[0x%08x] ' % cmd if isinstance(cmd, int) else '') + js))
    for ch, label in parse(a.lines):
        for t, v in edges.get(ch, [])[1:]:
            ev.append((t / rate, '%-8s' % label, 'pressed (low)' if v == 0 else 'released'))
    for t, who, s in sorted(ev):
        print('%9.4f  %s  %s' % (t, who, s))


if __name__ == '__main__':
    main()
