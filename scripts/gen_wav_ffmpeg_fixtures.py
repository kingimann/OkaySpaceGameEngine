#!/usr/bin/env python3
"""Make the WAV differential fixtures in tests/data/audio/wav_ffmpeg/ (test_wav_ffmpeg_diff).

Writes our own synthetic signal (two sines, a chirp, a click, a silent gap; the stereo channels differ),
encodes it with ffmpeg into each WAV coding the own decoder reads, and decodes each file with ffmpeg to
signed 16-bit samples (<name>.s16). ffmpeg is used only as an outside encoder and reference decoder,
as a program: none of its code is in the repository or was read. Run from the repository root:

    python3 -I scripts/gen_wav_ffmpeg_fixtures.py

The fixtures in the repository were made with ffmpeg 6.1.1 (Ubuntu 6.1.1-3ubuntu5).
"""
import math
import os
import struct
import subprocess
import sys
import tempfile

OUT = os.path.join("tests", "data", "audio", "wav_ffmpeg")
RATE = 11025
FRAMES = RATE // 4

# name, channels, ffmpeg codec arguments
CASES = [
    ("ms_mono", 1, ["-c:a", "adpcm_ms"]),
    ("ms_mono_b256", 1, ["-c:a", "adpcm_ms", "-block_size", "256"]),
    ("ms_stereo", 2, ["-c:a", "adpcm_ms"]),
    ("ms_stereo_b2048", 2, ["-c:a", "adpcm_ms", "-block_size", "2048"]),
    ("ima_mono", 1, ["-c:a", "adpcm_ima_wav"]),
    ("ima_stereo", 2, ["-c:a", "adpcm_ima_wav"]),
    ("alaw_stereo", 2, ["-c:a", "pcm_alaw"]),
    ("mulaw_mono", 1, ["-c:a", "pcm_mulaw"]),
    ("u8_mono", 1, ["-c:a", "pcm_u8"]),
    ("s24_stereo", 2, ["-c:a", "pcm_s24le"]),
    ("f32_mono", 1, ["-c:a", "pcm_f32le"]),
]


def signal():
    out = bytearray()
    for i in range(FRAMES):
        t = i / RATE
        left = 0.45 * math.sin(2 * math.pi * 440 * t) + 0.25 * math.sin(2 * math.pi * (200 + 3000 * t) * t)
        if 1000 <= i < 1010:
            left = 0.95 if i % 2 == 0 else -0.95          # a click
        if 2000 <= i < 2300:
            left = 0.0                                    # silence
        right = 0.6 * math.sin(2 * math.pi * 97 * t + 1.0) - 0.3 * math.sin(2 * math.pi * 1500 * t)
        for v in (left, right):
            out += struct.pack("<h", max(-32768, min(32767, int(round(v * 32767)))))
    return bytes(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "signal.raw")
        with open(raw, "wb") as f:
            f.write(signal())
        for name, ch, codec in CASES:
            wav = os.path.join(OUT, name + ".wav")
            s16 = os.path.join(OUT, name + ".s16")
            base = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
            subprocess.run(base + ["-f", "s16le", "-ar", str(RATE), "-ac", "2", "-i", raw,
                                   "-ac", str(ch)] + codec + ["-fflags", "+bitexact", "-flags", "+bitexact", wav], check=True)
            subprocess.run(base + ["-i", wav, "-f", "s16le", "-c:a", "pcm_s16le", s16], check=True)
            print(name, os.path.getsize(wav), os.path.getsize(s16))
    return 0


if __name__ == "__main__":
    sys.exit(main())
