#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Put the built firmware next to the web flasher: web/firmware/finton.fwsc and its manifest.
#   tools/make_web.sh [build/finton-X.Y.fwsc]     (default: the newest build/finton*.fwsc)
#   python3 -m http.server -d web 8913             then http://localhost:8913 in Chrome or Edge
# Web MIDI needs a secure context: localhost or https (GitHub Pages serves web/ as it is).
set -e
cd "$(dirname "$0")/.."
PKG="${1:-$(ls -t build/finton*.fwsc | head -1)}"
[ -f "$PKG" ] || { echo "make_web.sh: no package (./build.sh first)"; exit 1; }
mkdir -p web/firmware
cp "$PKG" web/firmware/finton.fwsc
cp assets/logo.png web/logo.png
python3 - "$PKG" <<'PY'
import hashlib, json, re, sys, time
from pathlib import Path
raw = Path("web/firmware/finton.fwsc").read_bytes()
product = "".join(chr((raw[i * 0x30 + 0x2F] - i - 1) & 0xFF) for i in range(20) if raw[i * 0x30 + 0x2F] != 0x7D)
m = re.search(r"finton-([\d.]+[\w-]*)\.fwsc", sys.argv[1])
ver = m[1] if m else "dev"
Path("web/firmware/manifest.json").write_text(json.dumps({
    "file": "finton.fwsc", "product": product, "version": ver, "size": len(raw),
    "sha256": hashlib.sha256(raw).hexdigest(), "built": time.strftime("%Y-%m-%d")}, indent=2) + "\n")
print(f"web/firmware/finton.fwsc  {product}  v{ver}  {len(raw)} B")
PY
