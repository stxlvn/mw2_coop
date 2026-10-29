"""Writes src/addr159.c: the table that turns the addresses of the official 32-bit build into those of the 1.0.159 iw4sp.exe (iw4x-sp profile). The addresses are
collected from the source: XR(0x..) / XV(0x..) in src/*.c, and the tables of the 32-bit native code (ranges, probes, aim_dvar_rvas32, place_rvas32, server_n_sites32,
*_site(s) arrays, the context hook table); tools/map159.py maps each by byte signatures. tools/addr159_manual.txt holds what it cannot find:
  0x525c34 -> 0x529a74        (official RVA -> 159 RVA)
  0x525c34 = 0x525bc0 + 0x74  (derived from another address of the table)
Usage: python -I tools/gen_addr159.py <image32.bin> <iw4sp-159.exe> [report]   ("report" prints the mapping and writes nothing)"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import map159  # noqa: E402

SRC = os.path.join(HERE, '..', 'src')
FILES = ['native.c', 'rumble.c', 'padopts.c', 'padglyphs.c', 'menus.c']
HEX = r'0x[0-9a-fA-F]+'


def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def w32_lines(text):
    """The lines that only the 32-bit build compiles (inside the #else of #ifdef _WIN64, or #ifndef _WIN64), comments stripped."""
    out, stack = [], []
    for line in text.split('\n'):
        m = re.match(r'\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)', line)
        if m:
            k, rest = m.group(1), m.group(2)
            if k in ('ifdef', 'ifndef', 'if'):
                neg = k == 'ifndef' or '!defined(_WIN64)' in rest
                stack.append(['_WIN64' in rest, neg, False])
            elif k in ('elif', 'else'):
                stack[-1][2] = True
            elif k == 'endif':
                stack.pop()
            out.append('')
            continue
        w32 = False
        for is64, neg, els in stack:
            if is64 and ((not els) == neg):
                w32 = True
        out.append(strip_comments(line) if w32 else '')
    return out


def collect():
    items = {}  # official rva -> where

    def add(rva, why):
        items.setdefault(rva, why)

    for f in FILES:
        path = os.path.join(SRC, f)
        if not os.path.exists(path):
            continue
        text = open(path, encoding='utf-8', errors='replace').read()
        code = strip_comments(text)
        for m in re.finditer(r'\bXR\((%s)\)' % HEX, code):
            add(int(m.group(1), 16), f)
        for m in re.finditer(r'\bXV\((%s)\)' % HEX, code):
            add(int(m.group(1), 16) - 0x400000, f)
        if f != 'native.c':
            continue
        w32 = '\n'.join(w32_lines(text))
        # ranges: the first member of each entry of the 32-bit table
        m = re.search(r'range_t ranges\[\] = \{(.*?)\};', w32, re.S)
        for e in re.finditer(r'\{\s*(%s)\s*,' % HEX, m.group(1)):
            add(int(e.group(1), 16), 'ranges')
        for name in ('probes', 'aim_dvar_rvas32', 'place_rvas32'):
            m = re.search(r'\b%s\[[^\]]*\]\s*=\s*\{(.*?)\};' % name, w32, re.S)
            for h in re.findall(HEX, m.group(1)):
                add(int(h, 16), name)
        m = re.search(r'server_n_sites32\[\] = \{(.*?)\n\};', w32, re.S)
        for e in re.finditer(r'\{\s*(%s)\s*,' % HEX, m.group(1)):
            add(int(e.group(1), 16) - 0x400000, 'server_n_sites32')
        for m in re.finditer(r'\b\w+_sites?\[\]\s*=\s*\{(.*?)\}', w32, re.S):
            for h in re.findall(HEX, m.group(1)):
                add(int(h, 16), 'site array')
        m = re.search(r'RVA_WAIT_ALPHA_SITE (%s)' % HEX, w32)
        add(int(m.group(1), 16), 'wait site')
        m = re.search(r'\} sites\[\] = \{(.*?)\n\t\};', w32, re.S)
        for e in re.finditer(r'\{\s*(%s)\s*,' % HEX, m.group(1)):
            add(int(e.group(1), 16), 'context hooks')
    return items


def manual():
    path = os.path.join(HERE, 'addr159_manual.txt')
    over, derived = {}, {}
    if os.path.exists(path):
        for line in open(path):
            line = line.split('#')[0].strip()
            if not line:
                continue
            m = re.match(r'(%s)\s*->\s*(%s)$' % (HEX, HEX), line)
            if m:
                over[int(m.group(1), 16)] = int(m.group(2), 16)
                continue
            m = re.match(r'(%s)\s*=\s*(%s)\s*\+\s*(%s)$' % (HEX, HEX, HEX), line)
            if m:
                derived[int(m.group(1), 16)] = (int(m.group(2), 16), int(m.group(3), 16))
                continue
            raise SystemExit('addr159_manual.txt: cannot read: ' + line)
    return over, derived


def main():
    off = map159.Image(sys.argv[1], True)
    new = map159.Image(sys.argv[2], False)
    report = len(sys.argv) > 3 and sys.argv[3] == 'report'
    text_off, text_new = off.text(), new.text()
    items = collect()
    over, derived = manual()
    for k in list(over) + list(derived):
        items.setdefault(k, 'manual list')
    table, how, bad = {}, {}, []

    def is_code(rva):
        return text_off[0] <= 0x400000 + rva < text_off[0] + len(text_off[1])

    def got(rva, r, why):
        table[rva] = r - 0x400000
        how[rva] = why
        if report:
            print('0x%x -> 0x%x  %s  [%s]' % (rva, r - 0x400000, why, items[rva]), flush=True)

    for rva in sorted(items):  # what is certain first: the manual list, the data (global variables referenced by instructions)
        if rva in over:
            table[rva] = over[rva]
            how[rva] = 'manual'
        elif rva not in derived and not is_code(rva):
            r, why = map159.map_data(off, new, text_off, text_new, 0x400000 + rva)
            if r is None:
                bad.append(rva)
            else:
                got(rva, r, why)
    for rva in list(bad):  # data between two mapped addresses that moved by the same amount (the block was moved as a whole)
        lo = [a for a in table if a < rva and not is_code(a)]
        hi = [a for a in table if a > rva and not is_code(a)]
        if lo and hi and table[max(lo)] - max(lo) == table[min(hi)] - min(hi):
            got(rva, 0x400000 + rva + table[max(lo)] - max(lo), 'data between 0x%x and 0x%x' % (max(lo), min(hi)))
            bad.remove(rva)
    anchors = {0x400000 + a: 0x400000 + table[a] for a in table if not is_code(a)}
    for rva in sorted(items):  # code
        if rva in table or rva in derived or not is_code(rva):
            continue
        va = 0x400000 + rva
        r, why = map159.map_code(off, new, text_off, text_new, va)
        if r is None:
            r, why = map159.map_code_abs(off, new, text_new, va, anchors)
        if r is None:
            r, why = map159.map_func_by_callers(off, new, text_off, text_new, va)
        if r is None:
            r, why = map159.map_code_near(off, new, text_off, text_new, va)
        if r is None:
            f1 = map159.map_fuzzy(off, new, text_new, va)
            f2 = map159.map_fuzzy(off, new, text_new, va, pre=16, post=48)
            if f1[0] and f1[0] == f2[0] and min(f1[1], f2[1]) >= 0.75 and min(f1[1], f2[1]) - max(f1[2], f2[2]) >= 0.2:
                r, why = f1[0], 'fuzzy, score %.2f / %.2f' % (f1[1], f2[1])
        if r is None:
            bad.append(rva)
        else:
            got(rva, r, why)
    for rva, (b, d) in derived.items():
        if b in table:
            table[rva] = table[b] + d
        else:
            bad.append(rva)
    for rva in bad:
        print('UNMAPPED 0x%x [%s]' % (rva, items[rva]))
    print('%d addresses, %d mapped, %d unmapped' % (len(items), len(table), len(bad)))
    if report or bad:
        sys.exit(1 if bad else 0)
    lines = ['// Generated by tools/gen_addr159.py from the source and tools/map159.py: do not edit.',
             '// The iw4x-sp profile (1.0.159 iw4sp.exe): official 32-bit RVA -> RVA in the 159 image, for XR() and XV() (src/game.h).',
             '#ifdef MW2COOP_IW4X', '#include "game.h"', '#include "log.h"', '',
             'static const struct {', '\tunsigned off, rva;', '} table[] = {']
    for rva in sorted(table):
        lines.append('\t{0x%x, 0x%x},' % (rva, table[rva]))
    lines += ['};', '',
              'unsigned addr159(unsigned rva)', '{',
              '\tunsigned lo = 0, hi = sizeof(table) / sizeof(table[0]);',
              '\twhile (lo < hi) {',
              '\t\tunsigned mid = (lo + hi) / 2;',
              '\t\tif (table[mid].off == rva)',
              '\t\t\treturn table[mid].rva;',
              '\t\tif (table[mid].off < rva)',
              '\t\t\tlo = mid + 1;',
              '\t\telse',
              '\t\t\thi = mid;',
              '\t}',
              '\tlog_printf("addr159: no 159 address for rva 0x%x", rva);',
              '\treturn 0;',
              '}', '#endif', '']
    open(os.path.join(SRC, 'addr159.c'), 'w', newline='\n').write('\n'.join(lines))


main()
