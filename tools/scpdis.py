#!/usr/bin/env python3
"""Disassembler for the original game's scripts (assets/epic50.png), from the original loader/interpreter:

container  cResourceManager::LoadScript @ghidra 000ddb54 / LoadScriptSub @000dd9f4
  +0x0C s32 count, then count x 12-byte chunk headers {u32 file offset, ?, u32 (copied to script+4), ...}
  chunk at offset: u32 a (byte0 = method, a>>8 = stored size), u32 b (unpacked size), then the data;
  method 6 = LZMA "alone" (fnDecompress2 @0015f6f4: 5 property bytes, size at +5, stream at +0x0D)
script blob  cflScriptBase::MapScriptMem @0016c3a4: commands from +8 (after the loader's 8-byte header)
command      u32 header: opcode bits 0..9, operand word count bits 10..12 (cflScriptBase::NextCommand @0016c9d0),
             operand-source flags in the upper bits; opcode 0x01 starts a script (operand = script id)
Script id -> chunk: cflScriptBase::MapScriptToFile @0016c528 (ids >= 1000), file number n -> chunk n+1.

usage: scpdis.py <epic50.png> list            chunk summary (script ids per chunk)
       scpdis.py <epic50.png> dump <scriptId>  commands of one script
       scpdis.py <epic50.png> find <opcode>    scripts using an opcode (hex)
"""
import lzma, struct, sys

OPNAMES = {}  # filled from tools/scpops.txt when present: "<hex opcode> <name>"


def load_ops():
    import os
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scpops.txt')
    if os.path.exists(p):
        for line in open(p, encoding='utf-8'):
            line = line.split('#', 1)[0].split(None, 1)
            if len(line) == 2:
                OPNAMES[int(line[0], 16)] = line[1].strip()


def chunks(data):
    n = struct.unpack_from('<i', data, 0xC)[0]
    out = []
    for i in range(n):
        off, x1, x2 = struct.unpack_from('<3I', data, 0x10 + 12 * i)
        a, b = struct.unpack_from('<II', data, off)
        method, stored = a & 0xFF, a >> 8
        if method == 0:
            blob = data[off + 8: off + 8 + b]
        else:
            raw = data[off + 8: off + 8 + stored]
            props, size = raw[:5], struct.unpack_from('<Q', raw, 5)[0]
            hdr = props + struct.pack('<Q', b)
            blob = lzma.decompress(hdr + raw[13:], format=lzma.FORMAT_ALONE)[:b]
        out.append((i, off, x1, x2, blob))
    return out


def commands(blob, end=None):
    """command area = first (x2 & 0xFFFF) words of the script memory incl. its 8-byte header (MapScriptMem: strings
    start at mem + u16(mem+4)*4 + 4), i.e. blob[:words*4 - 4]; strings follow"""
    pos = 0
    end = len(blob) if end is None else end
    while pos + 4 <= end:
        h = struct.unpack_from('<I', blob, pos)[0]
        nw = (h & 0x1FFF) >> 10
        ops = list(struct.unpack_from('<%dI' % nw, blob, pos + 4)) if pos + 4 + 4 * nw <= len(blob) else []
        yield pos, h, ops
        pos += 4 + 4 * nw


def scripts(blob, x2=None):
    cur, res = None, {}
    end = (x2 & 0xFFFF) * 4 - 4 if x2 else None
    for pos, h, ops in commands(blob, end):
        if h == 0x401 and ops:
            cur = ops[0] & 0xFFFF; res[cur] = []  # low 16 bits = id (MapScriptMem stores (short)op)
        if cur is not None:
            res[cur].append((pos, h, ops))
    return res


def fmt(pos, h, ops):
    op = h & 0x3FF
    name = OPNAMES.get(op, '')
    return '%06x  op %03x %-22s hdr %08x  %s' % (pos, op, name, h, ' '.join('%08x' % o for o in ops))


def main():
    load_ops()
    data = open(sys.argv[1], 'rb').read()
    cmd = sys.argv[2]
    allc = chunks(data)
    if cmd == 'list':
        for i, off, x1, x2, blob in allc:
            ids = list(scripts(blob, x2).keys())
            print('chunk %d off %x size %d scripts %d: %s' % (i, off, len(blob), len(ids), ' '.join(str(x) for x in ids[:12])))
    elif cmd == 'dump':
        sid = int(sys.argv[3], 0)
        for i, off, x1, x2, blob in allc:
            sc = scripts(blob, x2)
            if sid in sc:
                print('# script %d in chunk %d' % (sid, i))
                for c in sc[sid]:
                    print(fmt(*c))
    elif cmd == 'find':
        opw = int(sys.argv[3], 16)
        for i, off, x1, x2, blob in allc:
            for sid, cl in scripts(blob, x2).items():
                hits = [c for c in cl if c[1] & 0x3FF == opw]
                if hits:
                    print('chunk %d script %d: %s' % (i, sid, ' | '.join(' '.join('%x' % o for o in c[2]) for c in hits[:4])))


if __name__ == '__main__':
    main()
