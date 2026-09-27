#!/usr/bin/env python3
"""Convert an SKK-JISYO (EUC-JP) or a pinyin word list into the compact binary
dictionary used by the firmware IME.

Binary layout (little endian):
  'IMD1' magic | uint32 count | uint32 offsets[count] | entries...
  entry: reading UTF-8 NUL, candidates separated by '\\x1f', NUL
Readings are sorted bytewise so the firmware can binary-search them.
"""
import struct, sys, re

def load_skk(path):
    d = {}
    with open(path, 'rb') as f:
        raw = f.read().decode('euc_jp', 'ignore')
    for line in raw.splitlines():
        if not line or line.startswith(';;'):
            continue
        try:
            key, rest = line.split(' ', 1)
        except ValueError:
            continue
        cands = []
        for c in rest.strip('/').split('/'):
            if not c:
                continue
            c = c.split(';', 1)[0]           # drop annotations
            if c.startswith('(concat'):     # skip lisp entries
                continue
            if c and c not in cands:
                cands.append(c)
        if cands:
            d.setdefault(key, [])
            for c in cands:
                if c not in d[key]:
                    d[key].append(c)
    return d

def load_pinyin_c(path):
    """Parse LVGL's lv_ime_pinyin_def_dict[] table."""
    d = {}
    with open(path, encoding='utf-8') as f:
        for m in re.finditer(r'\{\s*"([a-z]+)"\s*,\s*"([^"]+)"\s*\}', f.read()):
            key, hz = m.group(1), m.group(2)
            d.setdefault(key, [])
            for ch in hz:
                if ch not in d[key]:
                    d[key].append(ch)
    return d

def write_bin(d, out):
    keys = sorted(d.keys(), key=lambda k: k.encode('utf-8'))
    blobs = []
    for k in keys:
        blobs.append(k.encode('utf-8') + b'\0' + '\x1f'.join(d[k]).encode('utf-8') + b'\0')
    header = 8 + 4 * len(keys)
    offs = []
    pos = header
    for b in blobs:
        offs.append(pos)
        pos += len(b)
    with open(out, 'wb') as f:
        f.write(b'IMD1' + struct.pack('<I', len(keys)))
        f.write(struct.pack('<%dI' % len(keys), *offs))
        for b in blobs:
            f.write(b)
    sys.stderr.write(f"{out}: {len(keys)} entries, {pos} bytes\n")

if __name__ == '__main__':
    kind, src, out = sys.argv[1:4]
    if kind == 'skk':
        write_bin(load_skk(src), out)
    elif kind == 'pinyin':
        write_bin(load_pinyin_c(src), out)
    else:
        raise SystemExit('usage: skk2bin.py skk|pinyin <src> <out.bin>')
