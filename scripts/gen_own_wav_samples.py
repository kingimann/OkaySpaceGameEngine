#!/usr/bin/env python3
"""Writes examples/own_wav/ima_tone.wav: 4 seconds of a 440 Hz sine (amplitude 0.5) at 22,050 Hz as mono IMA ADPCM
(WAVE format tag 0x11, 256-byte blocks of 505 samples). It is made only from our own signal with a plain IMA ADPCM encoder
written here from the public IMA recommended-practice description (the 89-entry step table and the index adjust table are the
standard published constants, the same ones engine/src/Audio/WavDecoder.cpp holds); no outside file or text is used.
The old WAV readers cannot read this format, so it tells the own decoder apart from the old ones (docs/try_it.md section 9).
Run from the repository root:  python3 scripts/gen_own_wav_samples.py
tests/test_own_wav_sample.cpp checks the committed file."""
import math, os, struct, sys

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
        118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
        1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
        6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767]
ADJ = [-1, -1, -1, -1, 2, 4, 6, 8]
assert len(STEP) == 89

RATE, BLOCK, PER_BLOCK, SECONDS = 22050, 256, 505, 4
blocks = -(-RATE * SECONDS // PER_BLOCK)
total = blocks * PER_BLOCK
signal = [int(round(16384 * math.sin(2 * math.pi * 440 * n / RATE))) for n in range(total)]

def encode_block(samples, index):
    """One block: the first sample and the step index go in the 4-byte header (the index carried on from the previous block, as
    real encoders do, so a loud tone does not have to catch its step size up at the start of every block)."""
    pred = samples[0]
    out = bytearray(struct.pack("<hBB", pred, index, 0))
    nibbles = []
    for s in samples[1:]:
        step = STEP[index]
        diff = s - pred
        nib = 0
        if diff < 0: nib = 8; diff = -diff
        t = step
        if diff >= t: nib |= 4; diff -= t
        t >>= 1
        if diff >= t: nib |= 2; diff -= t
        t >>= 1
        if diff >= t: nib |= 1
        vp = step >> 3
        if nib & 4: vp += step
        if nib & 2: vp += step >> 1
        if nib & 1: vp += step >> 2
        pred = pred - vp if nib & 8 else pred + vp
        pred = max(-32768, min(32767, pred))
        index = max(0, min(88, index + ADJ[nib & 7]))
        nibbles.append(nib)
    for i in range(0, len(nibbles), 2):       # the first sample of a pair is the LOW nibble
        out.append(nibbles[i] | (nibbles[i + 1] << 4))
    assert len(out) == BLOCK
    return bytes(out), index

parts, idx = [], 56   # start at a step size that suits a loud tone, so the first few samples are not far off
for b in range(blocks):
    blk, idx = encode_block(signal[b * PER_BLOCK:(b + 1) * PER_BLOCK], idx)
    parts.append(blk)
data = b"".join(parts)
fmt = struct.pack("<HHIIHHHH", 0x11, 1, RATE, RATE * BLOCK // PER_BLOCK, BLOCK, 4, 2, PER_BLOCK)
fact = struct.pack("<I", total)
body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"fact" + struct.pack("<I", 4) + fact + b"data" + struct.pack("<I", len(data)) + data
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("examples", "own_wav")
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "ima_tone.wav"), "wb") as f:
    f.write(b"RIFF" + struct.pack("<I", len(body)) + body)
print("wrote ima_tone.wav:", blocks, "blocks,", total, "frames,", len(body) + 8, "bytes")
