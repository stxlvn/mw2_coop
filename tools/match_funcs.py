"""Maps the functions of the 64-bit game to those of the 32-bit game (the two builds are the same engine compiled twice, so the strings a function refers to and the shape
of the call graph survive). Input: two files from tools/ghidra/ExportFuncs.java. Output: one line per matched function
"<rva64> <rva32> <how> <score>", and with --lookup the candidates for the given 64-bit rvas.

  python -I tools/match_funcs.py f64.txt f32.txt out.txt [--lookup 0xbca00 0x300a0 ...]

how: s = unique shared string, c = callee by position of a matched caller, p = caller of a matched callee (unique), n = neighbours agree.
"""
import sys
from collections import defaultdict


def load(path):
    funcs = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split(" | ")
        if len(parts) < 4:
            continue
        head = parts[0].split()
        rva = int(head[0], 16)
        funcs[rva] = {
            "size": int(head[1]),
            "n": int(head[2]),
            "callees": [int(x, 16) for x in parts[1].split(",") if x],
            "strings": [s for s in parts[2].split("\x1f") if s],
            "imports": [x for x in parts[3].split(",") if x],
        }
    callers = defaultdict(list)
    for rva, f in funcs.items():
        for c in f["callees"]:
            callers[c].append(rva)
    return funcs, callers


def main():
    args = sys.argv[1:]
    lookup = []
    if "--lookup" in args:
        i = args.index("--lookup")
        lookup = [int(x, 16) for x in args[i + 1:]]
        args = args[:i]
    f64, up64 = load(args[0])
    f32, up32 = load(args[1])
    m64, m32, how = {}, {}, {}

    def link(a, b, why):
        if a in m64 or b in m32 or a not in f64 or b not in f32:
            return False
        m64[a], m32[b], how[a] = b, a, why
        return True

    # 1. strings that only one function refers to, in both builds
    idx64, idx32 = defaultdict(set), defaultdict(set)
    for r, f in f64.items():
        for s in f["strings"]:
            idx64[s].add(r)
    for r, f in f32.items():
        for s in f["strings"]:
            idx32[s].add(r)
    votes = defaultdict(lambda: defaultdict(int))
    for s, a in idx64.items():
        b = idx32.get(s)
        if b and len(a) == 1 and len(b) == 1:
            votes[next(iter(a))][next(iter(b))] += 1
    for a, vs in votes.items():
        best = max(vs, key=vs.get)
        if len(vs) == 1:
            link(a, best, "s")
    # 1b. strings several functions refer to: a pair of functions that share several of them and is each other's best partner
    pair = defaultdict(float)
    for s, a in idx64.items():
        b = idx32.get(s)
        if b and 1 < len(a) <= 6 and 1 < len(b) <= 6:
            for x in a:
                for y in b:
                    pair[(x, y)] += 1.0 / (len(a) * len(b)) ** 0.5
    best64, best32 = {}, {}
    for (x, y), v in pair.items():
        if v > best64.get(x, (0, 0))[0]:
            best64[x] = (v, y)
        if v > best32.get(y, (0, 0))[0]:
            best32[y] = (v, x)
    for x, (v, y) in best64.items():
        if v >= 1.2 and best32.get(y, (0, 0))[1] == x and 0.4 < f64[x]["n"] / max(1, f32[y]["n"]) < 2.5:
            link(x, y, "s")
    print("string anchors:", len(m64))

    # 2. propagate through the call graph until nothing new is found
    def sane(a, b):
        sa, sb = f64[a]["n"], f32[b]["n"]
        return sb > 0 and 0.5 < sa / sb < 2.2

    def align(ca, cb):
        """pairs (x, y) of callee lists: the anchors in order (LCS over already matched pairs), then the gaps between them filled when they have the same length"""
        n, m = len(ca), len(cb)
        lcs = [[0] * (m + 1) for _ in range(n + 1)]
        for i in range(n - 1, -1, -1):
            for j in range(m - 1, -1, -1):
                lcs[i][j] = lcs[i + 1][j + 1] + 1 if m64.get(ca[i]) == cb[j] else max(lcs[i + 1][j], lcs[i][j + 1])
        anchors, i, j = [], 0, 0
        while i < n and j < m:
            if m64.get(ca[i]) == cb[j]:
                anchors.append((i, j))
                i += 1
                j += 1
            elif lcs[i + 1][j] >= lcs[i][j + 1]:
                i += 1
            else:
                j += 1
        anchors = [(-1, -1)] + anchors + [(n, m)]
        out = []
        for (i0, j0), (i1, j1) in zip(anchors, anchors[1:]):
            ga = [x for x in ca[i0 + 1:i1] if x not in m64]
            gb = [y for y in cb[j0 + 1:j1] if y not in m32]
            if len(ga) == len(gb):
                out.extend(zip(ga, gb))
        return out

    changed = True
    rounds = 0
    while changed and rounds < 60:
        changed = False
        rounds += 1
        for a in list(m64):
            b = m64[a]
            for x, y in align(f64[a]["callees"], f32[b]["callees"]):
                if sane(x, y) and link(x, y, "c"):
                    changed = True
            pa = [x for x in up64.get(a, []) if x not in m64]
            pb = [y for y in up32.get(b, []) if y not in m32]
            if len(pa) == 1 and len(pb) == 1 and sane(pa[0], pb[0]) and link(pa[0], pb[0], "p"):
                changed = True
    print("after propagation:", len(m64), "of", len(f64), "64-bit functions in", rounds, "rounds")
    with open(args[2], "w") as out:
        for a in sorted(m64):
            out.write("%x %x %s\n" % (a, m64[a], how[a]))
    for a in lookup:
        fa = f64.get(a)
        if a in m64:
            b = m64[a]
            print("%#x -> %#x (%s), instructions %d vs %d" % (a, b, how[a], f64[a]["n"], f32[b]["n"]))
        else:
            print("%#x: no match%s" % (a, "" if fa else " (not a function in the 64-bit export)"))


main()
