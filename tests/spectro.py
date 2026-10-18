#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A log-frequency spectrogram of a host render as PNG: tests/spectro.py in.wav out.png [seconds]"""
import sys, wave
import numpy as np
from PIL import Image
w = wave.open(sys.argv[1]); a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2)[:, 0].astype(float)
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 6.0
a = a[: int(secs * 44100)]
N, hop = 4096, 256
win = np.hanning(N)
cols = [np.abs(np.fft.rfft(a[i:i + N] * win)) for i in range(0, len(a) - N, hop)]
S = 20 * np.log10(np.array(cols).T + 1e-3)
f = np.fft.rfftfreq(N, 1 / 44100)
rows = 300
lf = np.geomspace(30, 20000, rows)
idx = np.clip(np.searchsorted(f, lf), 0, len(f) - 1)
img = S[idx][::-1]
img = np.clip((img - img.max() + 90) / 90, 0, 1)
Image.fromarray((img * 255).astype(np.uint8)).resize((min(1600, img.shape[1]), rows * 2)).save(sys.argv[2])
print("spectrogram ->", sys.argv[2])
