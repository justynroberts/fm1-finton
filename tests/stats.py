#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Levels and spectrum of a host render: tests/stats.py build/host/apex.wav"""
import sys, wave, array, math
w = wave.open(sys.argv[1]); a = array.array('h', w.readframes(w.getnframes()))
L = a[0::2]; R = a[1::2]; n = len(L)
pk = max(max(abs(x) for x in L), max(abs(x) for x in R))
rms = math.sqrt(sum(x * x for x in L) / n)
dc = sum(L) / n
side = math.sqrt(sum((l - r) ** 2 for l, r in zip(L, R)) / n)
# crude band energies with one-pole splits
def bands(x):
    lp1 = lp2 = lp3 = 0.0; e = [0.0] * 4
    k1 = 1 - math.exp(-2 * math.pi * 150 / 44100); k2 = 1 - math.exp(-2 * math.pi * 1200 / 44100); k3 = 1 - math.exp(-2 * math.pi * 6000 / 44100)
    for v in x[::2]:
        lp1 += k1 * (v - lp1); lp2 += k2 * (v - lp2); lp3 += k3 * (v - lp3)
        e[0] += lp1 * lp1; e[1] += (lp2 - lp1) ** 2; e[2] += (lp3 - lp2) ** 2; e[3] += (v - lp3) ** 2
    t = sum(e) or 1
    return [round(100 * v / t) for v in e]
print(f"peak {pk} ({20*math.log10(max(pk,1)/32767):.1f} dBFS)  rms {rms:.0f} ({20*math.log10(max(rms,1)/32767):.1f} dBFS)  dc {dc:.0f}  side {side:.0f}  bands <150/150-1.2k/1.2-6k/>6k % {bands(L)}")
