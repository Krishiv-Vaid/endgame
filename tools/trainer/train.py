#!/usr/bin/env python3
"""Trainer for endgame's NNUE: (768 -> H) x 2 -> 1, SCReLU.

    train.py --data 'data/shards/*.bin' --out nets/net-001 [options]

Data: PackedPosition shards written by `endgame datagen` (32-byte records
after a 32-byte header). Loss: MSE between sigmoid(eval / 400) and
    target = lambda * result + (1 - lambda) * sigmoid(score / 400)
from the side to move's point of view. Runs on Apple's MPS backend when
available, otherwise CUDA, otherwise CPU.

Writes <out>.pt (float checkpoint, resumable) and <out>.nnue (quantised,
endgame's EGNNUE01 format) after every superbatch.
"""
import argparse
import glob
import math
import os
import struct
import sys
import time

import numpy as np
import torch
import torch.nn.functional as F

INPUTS = 768
QA, QB, SCALE = 255, 64, 400
ARCH_ID = 1
# Weight clip keeps quantised values in range: |ft| * QA fits int16 and
# |out| * QB <= 128 (the engine's MAX_OUT_WEIGHT).
CLIP = 127 / 64  # 1.984375

RECORD = np.dtype([
    ("occ", "<u8"), ("pieces", "u1", 16), ("score", "<i2"), ("result", "u1"),
    ("stm", "u1"), ("ep", "u1"), ("castling", "u1"), ("rule50", "u1"), ("reserved", "u1"),
])
assert RECORD.itemsize == 32


def device():
    if torch.backends.mps.is_available():
        return torch.device("mps")
    if torch.cuda.is_available():
        return torch.device("cuda")
    return torch.device("cpu")


class Shards:
    """Memory-maps every shard; samples random batches across all of them.

    `weights` lets older data count less (sliding window): shards matching
    an --old glob get a lower sampling weight.
    """

    def __init__(self, paths, weights):
        self.maps, self.probs = [], []
        for p, w in zip(paths, weights):
            with open(p, "rb") as f:
                header = f.read(32)
            if header[:8] != b"EGDATA01":
                sys.exit(f"{p}: not an endgame shard")
            count = struct.unpack("<Q", header[8:16])[0]
            if count == 0:
                continue
            self.maps.append(np.memmap(p, dtype=RECORD, mode="r", offset=32, shape=(count,)))
            self.probs.append(count * w)
        if not self.maps:
            sys.exit("no training data")
        self.probs = np.array(self.probs) / sum(self.probs)
        self.total = sum(len(m) for m in self.maps)

    def batch(self, rng, n):
        counts = rng.multinomial(n, self.probs)
        parts = [m[np.sort(rng.integers(0, len(m), c))] for m, c in zip(self.maps, counts) if c]
        return np.concatenate(parts)


def decode(rec):
    """Records -> (stm features, nstm features) as (N, 32) int64 padded with
    INPUTS, plus stm-view score and result in [0, 1]."""
    n = len(rec)
    occ_bytes = np.ascontiguousarray(rec["occ"]).view(np.uint8).reshape(n, 8)
    bits = np.unpackbits(occ_bytes, axis=1, bitorder="little").astype(bool)  # (N, 64), a1 first
    nib = np.empty((n, 32), dtype=np.uint8)
    nib[:, 0::2] = rec["pieces"] & 15
    nib[:, 1::2] = rec["pieces"] >> 4
    rank = np.cumsum(bits, axis=1) - 1                     # index of each occupied square
    rows, sqs = np.nonzero(bits)
    piece = nib[rows, rank[rows, sqs]].astype(np.int64)
    color = piece >= 6
    ptype = piece % 6
    stm = rec["stm"].astype(bool)[rows]                    # True = black to move
    # Feature from a perspective: (relative colour * 6 + type) * 64 + square,
    # black's perspective mirrored vertically.
    white_f = ((color.astype(np.int64)) * 6 + ptype) * 64 + sqs
    black_f = ((~color).astype(np.int64) * 6 + ptype) * 64 + (sqs ^ 56)
    stm_f = np.where(stm, black_f, white_f)
    nstm_f = np.where(stm, white_f, black_f)
    slot = np.arange(len(rows)) - np.searchsorted(rows, rows)  # position within the row
    out_s = np.full((n, 32), INPUTS, dtype=np.int64)
    out_n = np.full((n, 32), INPUTS, dtype=np.int64)
    out_s[rows, slot] = stm_f
    out_n[rows, slot] = nstm_f
    sign = np.where(rec["stm"] == 1, -1.0, 1.0)
    score = rec["score"].astype(np.float32) * sign
    result = rec["result"].astype(np.float32) / 2.0
    result = np.where(rec["stm"] == 1, 1.0 - result, result)
    return out_s, out_n, score.astype(np.float32), result.astype(np.float32)


class Net(torch.nn.Module):
    def __init__(self, hidden):
        super().__init__()
        self.hidden = hidden
        # Row INPUTS is the padding row (kept at zero).
        self.ft = torch.nn.Parameter(torch.randn(INPUTS + 1, hidden) * 0.05)
        self.ft_bias = torch.nn.Parameter(torch.zeros(hidden))
        self.out = torch.nn.Parameter(torch.randn(2 * hidden) * 0.05)
        self.out_bias = torch.nn.Parameter(torch.zeros(1))

    def forward(self, stm_f, nstm_f):
        a = F.embedding_bag(stm_f, self.ft, mode="sum", padding_idx=INPUTS) + self.ft_bias
        b = F.embedding_bag(nstm_f, self.ft, mode="sum", padding_idx=INPUTS) + self.ft_bias
        a = torch.clamp(a, 0, 1) ** 2
        b = torch.clamp(b, 0, 1) ** 2
        return a @ self.out[: self.hidden] + b @ self.out[self.hidden:] + self.out_bias  # eval / SCALE

    def clip(self):
        with torch.no_grad():
            self.ft.clamp_(-CLIP, CLIP)
            self.out.clamp_(-CLIP, CLIP)
            self.ft[INPUTS].zero_()


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def export(net, path):
    """Quantise and write the engine's EGNNUE01 format."""
    ft = net.ft.detach().cpu().double().numpy()[:INPUTS]
    ftb = net.ft_bias.detach().cpu().double().numpy()
    out = net.out.detach().cpu().double().numpy()
    ob = float(net.out_bias.detach().cpu().double().numpy()[0])
    q_ft = np.clip(np.round(ft * QA), -32767, 32767).astype("<i2")
    q_ftb = np.clip(np.round(ftb * QA), -32767, 32767).astype("<i2")
    q_out = np.clip(np.round(out * QB), -128, 128).astype("<i2")
    q_ob = np.array([round(ob * QA * QB)], dtype="<i4")
    payload = q_ft.tobytes() + q_ftb.tobytes() + q_out.tobytes() + q_ob.tobytes()
    header = b"EGNNUE01" + struct.pack("<IIQ", ARCH_ID, net.hidden, fnv1a64(payload))
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(header + payload)
    os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", action="append", required=True, help="shard glob (repeatable)")
    ap.add_argument("--old", action="append", default=[], help="glob of older shards (lower weight)")
    ap.add_argument("--old-weight", type=float, default=0.5)
    ap.add_argument("--out", required=True, help="output prefix, e.g. nets/net-001")
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--superbatches", type=int, default=20)
    ap.add_argument("--batches-per-sb", type=int, default=1000)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--wd", type=float, default=0.01)
    ap.add_argument("--wdl", type=float, default=0.25, help="lambda: weight of the game result")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--resume", action="store_true")
    a = ap.parse_args()

    new = sorted({p for g in a.data for p in glob.glob(g)})
    old = sorted({p for g in a.old for p in glob.glob(g)} - set(new))
    data = Shards(new + old, [1.0] * len(new) + [a.old_weight] * len(old))
    dev = device()
    print(f"device {dev}, {len(new)} new + {len(old)} old shards, {data.total:,} positions, "
          f"hidden {a.hidden}, lambda {a.wdl}", flush=True)

    torch.manual_seed(a.seed)
    rng = np.random.default_rng(a.seed)
    net = Net(a.hidden).to(dev)
    opt = torch.optim.AdamW(net.parameters(), lr=a.lr, weight_decay=a.wd)
    total_steps = a.superbatches * a.batches_per_sb
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: 0.5 * (1 + math.cos(math.pi * min(s, total_steps) / total_steps)) * 0.99 + 0.01)
    start_sb = 0
    ckpt = a.out + ".pt"
    if a.resume and os.path.exists(ckpt):
        st = torch.load(ckpt, map_location=dev)
        net.load_state_dict(st["net"])
        opt.load_state_dict(st["opt"])
        sched.load_state_dict(st["sched"])
        start_sb = st["superbatch"]
        rng = np.random.default_rng(a.seed + start_sb)
        print(f"resumed at superbatch {start_sb}", flush=True)
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)

    for sb in range(start_sb, a.superbatches):
        t0, run = time.time(), 0.0
        for _ in range(a.batches_per_sb):
            s_f, n_f, score, result = decode(data.batch(rng, a.batch))
            s_f, n_f = torch.from_numpy(s_f).to(dev), torch.from_numpy(n_f).to(dev)
            score, result = torch.from_numpy(score).to(dev), torch.from_numpy(result).to(dev)
            target = a.wdl * result + (1 - a.wdl) * torch.sigmoid(score / 400)
            pred = torch.sigmoid(net(s_f, n_f) * SCALE / 400)
            loss = torch.mean((pred - target) ** 2)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            sched.step()
            net.clip()
            run += loss.item()
        dt = time.time() - t0
        pps = a.batches_per_sb * a.batch / dt
        print(f"superbatch {sb + 1}/{a.superbatches} loss {run / a.batches_per_sb:.6f} "
              f"lr {sched.get_last_lr()[0]:.2e} {pps:,.0f} pos/s", flush=True)
        torch.save({"net": net.state_dict(), "opt": opt.state_dict(), "sched": sched.state_dict(),
                    "superbatch": sb + 1, "args": vars(a)}, ckpt + ".tmp")
        os.replace(ckpt + ".tmp", ckpt)
        export(net, a.out + ".nnue")
    print(f"wrote {a.out}.nnue", flush=True)


if __name__ == "__main__":
    main()
