"""Maps addresses of the official 32-bit iw4sp.exe (image dump image32.bin, base 0x400000) to the 1.0.159 iw4sp.exe that the iw4x-sp client loads
(the file on disk, unpacked). The two builds are the same code linked in another order, so a function body has the same bytes at another place, except for
absolute addresses and relative call/jump displacements: those are wildcarded.
  code address A:  the 64 bytes at A (or, for an address inside a function, 32 bytes on both sides) are searched in the 159 .text; one hit = the match
  data address D:  up to 40 instructions of the official .text that hold D as an operand are searched the same way in the 159 .text; the operand found at the same place
                   in the hits is the candidate, the most frequent one wins (agree/total is printed)
Usage: python -I tools/map159.py <image32.bin> <iw4sp-159.exe> code|data|auto <VA hex>...   (VA = base 0x400000 + RVA; prints one line per address)
       python -I tools/map159.py <image32.bin> <iw4sp-159.exe> file <path of a text file with "<kind> <VA>" lines>"""
import re
import struct
import sys

LO, HI = 0x400000, 0x2140000


def load_pe(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    n = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    sec = pe + 24 + opt
    secs = []
    for i in range(n):
        name, vs, va, rs, raw = struct.unpack_from('<8sIIII', d, sec + 40 * i)
        secs.append((name.rstrip(b'\0').decode(), base + va, vs, rs, raw))
    return d, base, secs


class Image:
    """The bytes by VA: a PE file (sections) or a flat dump laid out from the base."""

    def __init__(self, path, flat):
        if flat:
            self.d = open(path, 'rb').read()
            self.base = 0x400000
            self.secs = [('.text', 0x401000, 0x293000, 0x293000, 0x1000)]
            self.flat = True
        else:
            self.d, self.base, self.secs = load_pe(path)
            self.flat = False

    def text(self):
        for name, va, vs, rs, raw in self.secs:
            if name == '.text':
                if self.flat:
                    return va, self.d[va - self.base:va - self.base + vs]
                return va, self.d[raw:raw + min(vs, rs)]

    def read(self, va, n):
        if self.flat:
            return self.d[va - self.base:va - self.base + n]
        for name, sva, vs, rs, raw in self.secs:
            if sva <= va < sva + max(vs, rs):
                return self.d[raw + va - sva:raw + va - sva + n]
        return b''


def wildmask(buf, va0, wild_at=()):
    """Which bytes of buf (which starts at VA va0) are wildcards: absolute addresses, relative displacements, and the 4 bytes at each position in wild_at."""
    mark = [False] * len(buf)
    for p in wild_at:
        for k in range(4):
            if 0 <= p + k < len(buf):
                mark[p + k] = True
    for i in range(len(buf) - 3):
        v = struct.unpack_from('<I', buf, i)[0]
        if LO <= v < HI:
            for k in range(4):
                mark[i + k] = True
            continue
        rel = False
        if i >= 1 and buf[i - 1] in (0xE8, 0xE9):
            rel = True
        if i >= 2 and buf[i - 2] == 0x0F and 0x80 <= buf[i - 1] <= 0x8F:
            rel = True
        if rel:
            t = (va0 + i + 4 + struct.unpack_from('<i', buf, i)[0]) & 0xffffffff
            if LO <= t < HI:
                for k in range(4):
                    mark[i + k] = True
    return mark


def pattern(buf, va0, wild_at=()):
    """Regex bytes for buf: the wildcards of wildmask() match anything."""
    mark = wildmask(buf, va0, wild_at)
    out = b''
    i = 0
    while i < len(buf):
        if mark[i]:
            j = i
            while j < len(buf) and mark[j]:
                j += 1
            out += b'.{%d}' % (j - i)
            i = j
        else:
            out += re.escape(buf[i:i + 1])
            i += 1
    return re.compile(out, re.DOTALL)


def hits(rx, text, limit=3):
    res = []
    for m in rx.finditer(text):
        res.append(m.start())
        if len(res) >= limit:
            break
    return res


def map_code(off, new, text_off, text_new, va):
    """The 159 VA of the official VA va (inside .text), or None / 'ambiguous'."""
    # a window that starts at va, then one centred on it
    for pre, post in ((0, 64), (0, 32), (32, 32), (48, 48), (64, 64), (96, 96)):
        a = va - pre
        buf = off.read(a, pre + post)
        if len(buf) < pre + post:
            continue
        h = hits(pattern(buf, a), text_new[1])
        if len(h) == 1:
            return text_new[0] + h[0] + pre, 'code %d+%d' % (pre, post)
        if len(h) > 1:
            continue
    return None, 'no unique match'


def map_code_near(off, new, text_off, text_new, va, reach=0x4000, step=0x40):
    """For an address whose own window is ambiguous: the 159 VA from the delta of the nearest uniquely mapped addresses below and above it (a link block is moved as a whole),
    accepted only if the bytes around it match at the candidate."""
    deltas = []
    for sign in (-1, 1):
        found = None
        for d in range(step, reach, step):
            a = va + sign * d
            buf = off.read(a, 64)
            if len(buf) < 64:
                break
            h = hits(pattern(buf, a), text_new[1])
            if len(h) == 1:
                found = text_new[0] + h[0] - a
                break
        deltas.append(found)
    buf = off.read(va - 24, 48)
    rx = pattern(buf, va - 24)
    for delta in sorted(set(d for d in deltas if d is not None), key=lambda d: -deltas.count(d)):
        pos = va + delta - 24 - text_new[0]
        if pos >= 0 and rx.match(text_new[1], pos):
            return va + delta, 'code by the delta 0x%x of its neighbours %s' % (delta & 0xffffffff, ['%x' % (d & 0xffffffff) if d is not None else None for d in deltas])
    return None, 'no unique match, neighbour deltas %s do not fit' % deltas


def map_fuzzy(off, new, text_new, va, pre=40, post=40, k=5):
    """When the 159 code around va is not byte-identical (another build of the function): the candidate where most of the non-wildcard bytes of the window agree. The k-byte pieces of
    the window vote for an alignment; the best alignments are scored byte by byte. Returns (159 VA, score 0..1, runner-up score)."""
    a = va - pre
    buf = off.read(a, pre + post)
    mask = [not w for w in wildmask(buf, a)]
    text = text_new[1]
    votes = {}
    for i in range(0, len(buf) - k + 1):
        if not all(mask[i:i + k]):
            continue
        piece = buf[i:i + k]
        j = text.find(piece)
        n = 0
        while j >= 0 and n < 200:
            votes[j - i] = votes.get(j - i, 0) + 1
            j = text.find(piece, j + 1)
            n += 1
    best = []
    for start, v in sorted(votes.items(), key=lambda kv: -kv[1])[:20]:
        if start < 0 or start + len(buf) > len(text):
            continue
        same = sum(1 for i in range(len(buf)) if mask[i] and text[start + i] == buf[i])
        best.append((same / max(1, sum(mask)), start))
    best.sort(reverse=True)
    if not best:
        return None, 0, 0
    second = best[1][0] if len(best) > 1 else 0
    return text_new[0] + best[0][1] + pre, best[0][0], second


def translate_abs(buf, va0, anchors):
    """buf with each absolute data address replaced by the 159 address, when the mapped data anchors (official VA -> 159 VA) on both sides of it moved by the same amount; returns
    (bytes, wildcard marks of what could not be translated or is a relative displacement)."""
    out = bytearray(buf)
    mark = wildmask(buf, va0)
    keys = sorted(anchors)
    for i in range(len(buf) - 3):
        v = struct.unpack_from('<I', buf, i)[0]
        if not (LO <= v < HI) or not all(mark[i:i + 4]):
            continue
        lo = [k for k in keys if k <= v]
        hi = [k for k in keys if k >= v]
        if lo and hi and anchors[lo[-1]] - lo[-1] == anchors[hi[0]] - hi[0]:
            struct.pack_into('<I', out, i, v + anchors[lo[-1]] - lo[-1])
            for k in range(4):
                mark[i + k] = False
    return bytes(out), mark


def map_code_abs(off, new, text_new, va, anchors):
    """A window of the official code with its data addresses moved to their 159 places (anchors) and its relative calls wildcards; the unique hit in the 159 .text."""
    for pre, post in ((0, 16), (0, 24), (0, 32), (16, 16), (24, 24), (0, 64), (32, 32)):
        a = va - pre
        buf = off.read(a, pre + post)
        data, mark = translate_abs(buf, a, anchors)
        out = b''
        i = 0
        while i < len(data):
            if mark[i]:
                j = i
                while j < len(data) and mark[j]:
                    j += 1
                out += b'.{%d}' % (j - i)
                i = j
            else:
                out += re.escape(data[i:i + 1])
                i += 1
        h = hits(re.compile(out, re.DOTALL), text_new[1])
        if len(h) == 1:
            return text_new[0] + h[0] + pre, 'code with moved data addresses %d+%d' % (pre, post)
    return None, 'no unique match with moved data addresses'


def callers(text, target, limit=400):
    """VAs of the E8 calls in the .text (va, bytes) that call target."""
    va0, t = text
    out = []
    pos = t.find(b'\xe8')
    while pos >= 0 and len(out) < limit:
        if pos + 5 <= len(t) and va0 + pos + 5 + struct.unpack_from('<i', t, pos + 1)[0] == target:
            out.append(va0 + pos)
        pos = t.find(b'\xe8', pos + 1)
    return out


def map_func_by_callers(off, new, text_off, text_new, va, maxcalls=50):
    """A function whose own bytes differ: its callers in the official .text that map uniquely (their code is the same) call it in the 159 .text; the target they call there wins the vote."""
    votes = {}
    used = 0
    for c in callers(text_off, va):
        if used >= maxcalls:
            break
        r, _ = map_code(off, new, text_off, text_new, c)
        if r is None:
            continue
        t = text_new[1]
        o = r - text_new[0]
        if o < 0 or o + 5 > len(t) or t[o] != 0xE8:
            continue
        used += 1
        tgt = (r + 5 + struct.unpack_from('<i', t, o + 1)[0]) & 0xffffffff
        votes[tgt] = votes.get(tgt, 0) + 1
    if not votes:
        return None, 'no caller of it maps'
    best = max(votes, key=votes.get)
    return best, 'function by %d of %d callers' % (votes[best], used)


def map_data(off, new, text_off, text_new, va, maxrefs=40):
    t_va, t = text_off
    needle = struct.pack('<I', va)
    refs = []
    i = t.find(needle)
    while i >= 0:
        refs.append(i)
        i = t.find(needle, i + 1)
    if not refs:
        return None, 'no references in the official .text'
    step = max(1, len(refs) // maxrefs)
    votes = {}
    used = 0
    for p in refs[::step]:
        a = t_va + p - 24
        buf = off.read(a, 52)
        rx = pattern(buf, a, wild_at=(24,))
        h = hits(rx, text_new[1])
        if len(h) != 1:
            continue
        used += 1
        cand = struct.unpack_from('<I', text_new[1], h[0] + 24)[0]
        votes[cand] = votes.get(cand, 0) + 1
    if not votes:
        return None, 'no reference matched (%d refs)' % len(refs)
    best = max(votes, key=votes.get)
    return best, 'data %d of %d matched refs agree (%d refs)' % (votes[best], used, len(refs))


def main():
    off = Image(sys.argv[1], True)
    new = Image(sys.argv[2], False)
    kind = sys.argv[3]
    items = []
    if kind == 'file':
        for line in open(sys.argv[4]):
            p = line.split()
            if len(p) >= 2:
                items.append((p[0], int(p[1], 16)))
    else:
        items = [(kind, int(a, 16)) for a in sys.argv[4:]]
    text_off = off.text()
    text_new = new.text()
    for k, va in items:
        if k == 'auto':
            k = 'code' if text_off[0] <= va < text_off[0] + len(text_off[1]) else 'data'
        if k == 'code':
            r, why = map_code(off, new, text_off, text_new, va)
        else:
            r, why = map_data(off, new, text_off, text_new, va)
        print('%s 0x%x -> %s  (%s)' % (k, va, ('0x%x' % r) if r else '?', why), flush=True)


if __name__ == '__main__':
    main()
