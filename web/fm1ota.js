// SPDX-License-Identifier: GPL-3.0-only
// Finton web flasher: the FM-1's USB-MIDI update, in the browser (Web MIDI with SysEx).
// The same protocol as tools/fm1_install.py (Felucca's M-UPGRADE path): step 1, the running firmware
// reads parts of the package and starts the update loader; step 2, the loader reads the whole package
// and writes it. In both steps the device asks (SysEx read requests on the logical image) and we answer.
// Then the FM-1 restarts and its identity is checked.

export const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
export const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const FINISH_CHECK = 0xE0000000, FINISH_WRITE = 0xF0000000, MAXDATA = 512;
const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;
const PORT_RE = /fm-1|felucca|ota|composite|sinco|usb-midi/i;   // never probe other gear
const LOADER_MARK = new TextEncoder().encode("FELUCCA-LOADER-1");
export const DELAY = { start: 2000, reply: 10, loader: 3000, reboot: 3000, retry: 1000, hs: 1000,
                       idleCheck: 8000, idleWrite: 180000, waitLoader: 30000, waitReboot: 40000 };
const sleep = ms => new Promise(r => setTimeout(r, ms));

export class InstallError extends Error {
  constructor(code, msg) { super(msg); this.code = code; }
}

// ---------------------------------------------------------------- wire format
export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

export function response(addr, data, fl = 0) {
  const n = data.length;
  const body = [0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl,
                addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
                n & 0xFF, (n >> 8) & 0xFF, 0, ...data];
  let sum = 0;
  for (let i = 6; i < body.length; i++) sum += body[i];
  body.push(~sum & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

export function parseRequest(pkt) {            // -> {fl, addr, n} or null
  if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0x00 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let sum = 0;
  for (let i = 6; i < 14; i++) sum += u[i];
  if ((~sum & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | u[8] << 8 | u[9] << 16 | u[10] << 24) >>> 0, n: u[11] | u[12] << 8 | u[13] << 16 };
}

export function parseIdentity(pkt) {           // -> {text, model, version, loader} or null
  if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0x00 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  let txt = String.fromCharCode(...d.slice(6, 33)).replace(/\0+$/, "");
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  return m ? { text: txt, model: m[1], version: +m[2], loader: m[1].toLowerCase().startsWith("ota-") } : null;
}

// -------------------------------------------------------------------- package
export function productOf(raw) {               // "FM-1_950": a marker byte after each of the first 20 blocks
  if (raw.length < BLOCKS * BLK) throw new InstallError("badpkg", "not an FM-1 package (too short)");
  let s = "";
  for (let i = 0; i < BLOCKS; i++) {
    const m = raw[i * BLK + KEEP];
    if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
  }
  return s;
}

export function logicalImage(raw) {            // the package without the 20 marker bytes
  const out = new Uint8Array(raw.length - BLOCKS);
  let o = 0;
  for (let i = 0; i < BLOCKS; i++) { out.set(raw.subarray(i * BLK, i * BLK + KEEP), o); o += KEEP; }
  out.set(raw.subarray(BLOCKS * BLK), o);
  return out;
}

function crc16(data, crc = 0) {                // CRC-16/XMODEM
  for (const b of data) {
    crc ^= b << 8;
    for (let k = 0; k < 8; k++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
  }
  return crc;
}
function dec(b) {                              // JieLi ENC stream cipher (its own inverse)
  const out = Uint8Array.from(b); let key = 0xFFFF;
  for (let i = 0; i < out.length; i++) {
    out[i] ^= key & 0xFF;
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
  }
  return out;
}
const le16 = (a, i) => a[i] | a[i + 1] << 8, le32 = (a, i) => (a[i] | a[i + 1] << 8 | a[i + 2] << 16 | a[i + 3] << 24) >>> 0;

export function packageDamage(image) {         // what is wrong with the update image, or null
  if (image.length < 0x40) return "too short";
  const hdr = dec(image.subarray(0, 0x40));
  if (crc16(hdr.subarray(2, 0x40)) !== le16(hdr, 0)) return "header CRC";
  const count = le16(hdr, 8);
  if (count < 1 || count > 8 || image.length < 0x40 + 0x50 * count) return "file list";
  const lst = image.subarray(0x40, 0x40 + 0x50 * count);
  if (crc16(lst) !== le16(hdr, 2)) return "file list CRC";
  const files = [];
  for (let i = 0; i < count; i++) {
    const e = dec(lst.subarray(i * 0x50, (i + 1) * 0x50));
    const off = le32(e, 8), size = le32(e, 12);
    const name = String.fromCharCode(...e.subarray(0x40, 0x50)).split("\0")[0];
    files.push({ name, size });
    if (off + size > image.length) return `${name}: cut short`;
    if (crc16(image.subarray(off, off + size)) !== le16(e, 4)) return `${name}: CRC`;
  }
  return null;
}

function hasMark(raw) {
  outer: for (let i = 0; i + LOADER_MARK.length <= raw.length; i++) {
    for (let k = 0; k < LOADER_MARK.length; k++) if (raw[i + k] !== LOADER_MARK[k]) continue outer;
    return true;
  }
  return false;
}

export function loadPackage(raw) {             // -> {product, image}
  const product = productOf(raw);
  if (!/^[^_]+_\d+$/.test(product)) throw new InstallError("badpkg", `not an FM-1 package (identity ${product})`);
  if (!hasMark(raw)) throw new InstallError("badpkg", "no Felucca update loader in this package");
  const image = logicalImage(raw);
  const bad = packageDamage(image);
  if (bad) throw new InstallError("badpkg", `damaged package (${bad}); nothing was written`);
  return { product, image };
}

// ------------------------------------------------------------------- Web MIDI
class Link {                                   // one input/output pair with a SysEx queue
  constructor(midi, input, output) {
    this.midi = midi; this.input = input; this.output = output; this.q = []; this.waiters = [];
    this.onmsg = e => {
      const d = Array.from(e.data);
      if (d[0] !== 0xF0) return;
      const w = this.waiters.shift();
      if (w) w(d); else this.q.push(d);
    };
    input.addEventListener("midimessage", this.onmsg);
  }
  get lost() { return this.input.state === "disconnected"; }
  send(pkt) { try { this.output.send(pkt); return true; } catch { return false; } }
  read(ms) {
    if (this.q.length) return Promise.resolve(this.q.shift());
    return new Promise(res => {
      const w = d => { clearTimeout(t); res(d); };
      const t = setTimeout(() => { this.waiters = this.waiters.filter(x => x !== w); res(null); }, ms);
      this.waiters.push(w);
    });
  }
  drain() { this.q = []; }
  close() { this.input.removeEventListener("midimessage", this.onmsg); }
}

function pairOutput(midi, input) {
  const outs = [...midi.outputs.values()];
  const base = s => s.replace(/\s+\d+$/, "");
  return outs.find(o => o.name === input.name) || outs.find(o => base(o.name) === base(input.name)) ||
         (outs.length === 1 ? outs[0] : null);
}

async function handshake(link, tries = 3) {
  link.drain();
  for (let i = 0; i < tries; i++) {
    if (!link.send(HS_QUERY)) return null;
    const end = performance.now() + DELAY.hs;
    let left;
    while ((left = end - performance.now()) > 0) {
      const p = await link.read(left);
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

export class Flasher {
  constructor(midi, log = () => {}) { this.midi = midi; this.log = log; }

  candidates() { return [...this.midi.inputs.values()].filter(i => PORT_RE.test(i.name)); }

  async find(want = null) {                    // the first FM-1 port that answers (and want(id))
    for (const input of this.candidates()) {
      const output = pairOutput(this.midi, input);
      if (!output) continue;
      const link = new Link(this.midi, input, output);
      const id = await handshake(link, 2);
      if (id && (!want || want(id))) return { link, id, name: input.name };
      link.close();
    }
    return null;
  }

  async waitFor(want, ms) {
    const end = performance.now() + ms;
    while (performance.now() < end) {
      const dev = await this.find(want);
      if (dev) return dev;
      await sleep(DELAY.retry);
    }
    return null;
  }

  async serve(link, image, finish, idle, progress) {
    let served = 0, last = performance.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (performance.now() - last > idle) return { served, finished: false, lost: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = performance.now();
      if (r.addr === FINISH_CHECK || r.addr === FINISH_WRITE) {
        link.send(response(r.addr, [...new TextEncoder().encode("success"), 0]));
        if (r.addr === finish) return { served, finished: true, lost: false };
        continue;
      }
      if (r.n > MAXDATA || r.addr + r.n > image.length)
        throw new InstallError("badreq", `the device asked for 0x${r.addr.toString(16)}+${r.n}, outside the package`);
      await sleep(DELAY.reply);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.n)), r.fl)))
        return { served, finished: false, lost: true };
      served++;
      progress(served, r.addr + r.n);
    }
  }

  // dev from find(); step(kind, value) reports: start, check, loader, write (0..100), reboot, done
  async install(dev, image, product, step = () => {}) {
    const model = t => t.split("_")[0].replace(/^ota-/i, "");
    if (model(dev.id.text) !== model(product)) {
      dev.link.close();
      throw new InstallError("model", `the device is ${dev.id.text}, the package is for ${product}`);
    }
    if (dev.id.loader && !/^ota-FM-1_9\d\d$/i.test(dev.id.text)) {
      dev.link.close();
      throw new InstallError("foreign", `the FM-1 is in the update mode of another firmware (${dev.id.text}); finish that update with its own updater first`);
    }
    let ota = dev;
    if (!dev.id.loader) {                      // step 1: check the package, start the loader
      step("start", dev.id.text);
      dev.link.send(UPGRADE);
      await sleep(DELAY.start);
      const r = await this.serve(dev.link, image, FINISH_CHECK, DELAY.idleCheck, k => step("check", k));
      dev.link.close();
      if (!r.finished) throw new InstallError(r.lost ? "lost" : "stopped",
        `the FM-1 ${r.lost ? "was disconnected" : "stopped answering"} after ${r.served} requests; nothing was written`);
      step("loader");
      await sleep(DELAY.loader);
      ota = await this.waitFor(id => id.loader, DELAY.waitLoader);
      if (!ota) throw new InstallError("noloader", "the update loader did not appear. Replug the USB cable and flash again: the FM-1 stays in update mode until it is done");
    }
    step("write", 0);                          // step 2: the loader writes the package
    ota.link.send(UPGRADE);
    await sleep(DELAY.start);
    const w = await this.serve(ota.link, image, FINISH_WRITE, DELAY.idleWrite,
                               (k, end) => step("write", Math.min(99, Math.floor(end * 100 / image.length))));
    ota.link.close();
    if (!w.finished) throw new InstallError(w.lost ? "lost" : "stopped",
      `the loader ${w.lost ? "was disconnected" : "stopped answering"} after ${w.served} requests. Replug the USB cable and flash again to finish`);
    step("write", 100);
    step("reboot");                            // the new firmware should answer with the package's identity
    await sleep(DELAY.reboot);
    const back = await this.waitFor(id => !id.loader, DELAY.waitReboot);
    if (!back) throw new InstallError("noreturn", "the FM-1 did not come back: power-cycle it");
    back.link.close();
    if (back.id.text !== product) throw new InstallError("mismatch", `written, but the FM-1 reports ${back.id.text} (expected ${product})`);
    step("done", back.id.text);
    return back.id.text;
  }
}
