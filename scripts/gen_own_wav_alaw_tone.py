#!/usr/bin/env python3
# Writes examples/own_wav/alaw_tone.wav (run: python3 scripts/gen_own_wav_alaw_tone.py examples/own_wav/alaw_tone.wav): 2 s of a 440 Hz sine, amplitude 0.5, 22,050 Hz mono, 8-bit A-law.
# Our own signal and our own A-law encoder, written from ITU-T G.711 (13-bit magnitude, eight segments).
import math, struct, sys
def alaw(x16):
    s = x16 >> 3                      # 13-bit signed
    if s >= 0: mask = 0xD5
    else: mask = 0x55; s = -s - 1
    ends = [0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF]
    seg = next((i for i, e in enumerate(ends) if s <= e), 8)
    if seg >= 8: return 0x7F ^ mask
    v = seg << 4
    v |= ((s >> 1) if seg < 2 else (s >> seg)) & 0x0F
    return v ^ mask
rate, n = 22050, 44100
data = bytes(alaw(int(round(0.5 * 32767 * math.sin(2 * math.pi * 440 * i / rate)))) for i in range(n))
fmt = struct.pack('<HHIIHH', 6, 1, rate, rate, 1, 8) + struct.pack('<H', 0)
body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data
open(sys.argv[1], 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
