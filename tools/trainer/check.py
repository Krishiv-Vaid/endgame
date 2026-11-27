#!/usr/bin/env python3
"""Check that a .nnue file evaluates identically in NumPy (exact integer
re-implementation of the engine's inference) and in the engine itself.

    check.py <net.nnue> <endgame binary> [fen ...]
"""
import struct, subprocess, sys
import numpy as np

QA, QB, SCALE, INPUTS = 255, 64, 400, 768
PIECES = "PNBRQKpnbrqk"

def load(path):
    b = open(path, "rb").read()
    assert b[:8] == b"EGNNUE01", "bad magic"
    arch, hidden, _ = struct.unpack("<IIQ", b[8:24])
    o = 24
    ft = np.frombuffer(b, "<i2", INPUTS * hidden, o).reshape(INPUTS, hidden).astype(np.int64); o += INPUTS * hidden * 2
    ftb = np.frombuffer(b, "<i2", hidden, o).astype(np.int64); o += hidden * 2
    out = np.frombuffer(b, "<i2", 2 * hidden, o).astype(np.int64); o += 4 * hidden
    ob = int(np.frombuffer(b, "<i4", 1, o)[0])
    return hidden, ft, ftb, out, ob

def evaluate(net, fen):
    hidden, ft, ftb, out, ob = net
    board, stm = fen.split()[:2]
    acc = {c: ftb.copy() for c in "wb"}
    r, f = 7, 0
    for ch in board:
        if ch == "/": r, f = r - 1, 0
        elif ch.isdigit(): f += int(ch)
        else:
            p = PIECES.index(ch); sq = r * 8 + f; f += 1
            white = p < 6
            acc["w"] += ft[((0 if white else 1) * 6 + p % 6) * 64 + sq]
            acc["b"] += ft[((1 if white else 0) * 6 + p % 6) * 64 + (sq ^ 56)]
    us, them = (acc["w"], acc["b"]) if stm == "w" else (acc["b"], acc["w"])
    def dot(a, w):
        c = np.clip(a, 0, QA)
        return int(np.sum(c * w * c))
    s = dot(us, out[:hidden]) + dot(them, out[hidden:])
    v = int(s / QA) if s >= 0 else -int(-s / QA)          # C++ truncates toward zero
    v += ob
    num = v * SCALE
    return int(num / (QA * QB)) if num >= 0 else -int(-num / (QA * QB))

def engine_eval(exe, net_path, fen):
    cmds = f"setoption name EvalFile value {net_path}\nposition fen {fen}\neval\nquit\n"
    outp = subprocess.run([exe], input=cmds, capture_output=True, text=True).stdout
    for line in outp.splitlines():
        if line.startswith("nnue"):
            return int(line.split(":")[1].split()[0])
    raise RuntimeError("engine gave no nnue eval:\n" + outp)

if __name__ == "__main__":
    net_path, exe = sys.argv[1], sys.argv[2]
    fens = sys.argv[3:] or [
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "4k3/8/8/8/8/8/4P3/4K3 b - - 0 1",
    ]
    net = load(net_path)
    ok = True
    for fen in fens:
        a, b = evaluate(net, fen), engine_eval(exe, net_path, fen)
        print(f"{'OK  ' if a == b else 'DIFF'} numpy {a:6d} engine {b:6d}  {fen}")
        ok &= a == b
    sys.exit(0 if ok else 1)
