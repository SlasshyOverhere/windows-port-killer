// Generates src/app.ico + tools/icon-preview.png (no deps).
const fs = require('fs');
const zlib = require('zlib');
const path = require('path');

const SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256];

// ---- signed distance helpers (unit space 0..1) ----
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
function sdRoundBox(x, y, cx, cy, hw, hh, r) {
  const qx = Math.abs(x - cx) - (hw - r);
  const qy = Math.abs(y - cy) - (hh - r);
  return Math.hypot(Math.max(qx, 0), Math.max(qy, 0)) + Math.min(Math.max(qx, qy), 0) - r;
}
function sdCircle(x, y, cx, cy, r) { return Math.hypot(x - cx, y - cy) - r; }
function sdSegment(x, y, ax, ay, bx, by) {
  const pax = x - ax, pay = y - ay, bax = bx - ax, bay = by - ay;
  const h = clamp((pax * bax + pay * bay) / (bax * bax + bay * bay), 0, 1);
  return Math.hypot(pax - bax * h, pay - bay * h);
}
const lerp = (a, b, t) => a + (b - a) * t;
function mixColor(c1, c2, t) { return [lerp(c1[0], c2[0], t), lerp(c1[1], c2[1], t), lerp(c1[2], c2[2], t)]; }
const over = (dst, src, a) => [lerp(dst[0], src[0], a), lerp(dst[1], src[1], a), lerp(dst[2], src[2], a)];

const BG_TOP = [32, 44, 62];
const BG_BOT = [11, 14, 20];
const RING = [88, 148, 255];
const BOLT = [255, 96, 84];
const RING_HI = [130, 180, 255];

function shade(x, y) {
  // backdrop: rounded tile, gradient, subtle inner top highlight
  const d = sdRoundBox(x, y, 0.5, 0.5, 0.5, 0.5, 0.24);
  if (d > 0) return null;
  const t = clamp(y, 0, 1);
  let c = mixColor(BG_TOP, BG_BOT, t * t * 0.9 + t * 0.1);
  // top sheen
  c = over(c, [255, 255, 255], 0.06 * clamp(1 - y * 3.2, 0, 1));
  // accent glow behind ring
  const glow = clamp(1 - Math.hypot(x - 0.5, y - 0.47) / 0.55, 0, 1);
  c = over(c, [70, 120, 220], 0.20 * glow * glow);

  const rOuter = 0.295, rInner = 0.185;
  const ringD = Math.abs(sdCircle(x, y, 0.5, 0.47, (rOuter + rInner) / 2)) - (rOuter - rInner) / 2;
  const inRing = clamp(0.5 - ringD * 400, 0, 1);
  if (inRing > 0) {
    const tint = mixColor(RING, RING_HI, clamp(1 - (y - 0.18) / 0.6, 0, 1));
    c = over(c, tint, inRing);
  }

  // bolt: diagonal strike, rounded ends, click-cut notch
  const boltD = sdSegment(x, y, 0.265, 0.75, 0.735, 0.27) - 0.072;
  const inBolt = clamp(0.5 - boltD * 400, 0, 1);
  if (inBolt > 0) c = over(c, BOLT, inBolt);

  // inner dark cut on bolt to read as a kill mark
  const cutD = sdSegment(x, y, 0.4, 0.615, 0.6, 0.405) - 0.016;
  const inCut = clamp(0.5 + cutD * 400, 0, 1);
  if (inCut > 0) c = over(c, [120, 26, 24], inCut * 0.55);

  // hairline border
  const edge = clamp(1 - Math.abs(d + 0.006) * 400, 0, 1);
  if (edge > 0) c = over(c, [120, 150, 200], edge * 0.28);
  return c;
}

function render(size) {
  const SS = 4;
  const px = Buffer.alloc(size * size * 4);
  for (let py = 0; py < size; py++) {
    for (let pxi = 0; pxi < size; pxi++) {
      let r = 0, g = 0, b = 0, a = 0;
      for (let sy = 0; sy < SS; sy++) {
        for (let sx = 0; sx < SS; sx++) {
          const x = (pxi + (sx + 0.5) / SS) / size;
          const y = (py + (sy + 0.5) / SS) / size;
          const c = shade(x, y);
          if (c) { r += c[0]; g += c[1]; b += c[2]; a += 255; }
        }
      }
      const n = SS * SS;
      const o = (py * size + pxi) * 4;
      // premultiply not needed for ico (straight alpha)
      px[o] = clamp(Math.round(b / n), 0, 255);
      px[o + 1] = clamp(Math.round(g / n), 0, 255);
      px[o + 2] = clamp(Math.round(r / n), 0, 255);
      px[o + 3] = Math.round(a / n);
    }
  }
  return px;
}

function bmpEntry(size, bgra) {
  const hdr = Buffer.alloc(40);
  hdr.writeUInt32LE(40, 0);
  hdr.writeInt32LE(size, 4);
  hdr.writeInt32LE(size * 2, 8);
  hdr.writeUInt16LE(1, 12);
  hdr.writeUInt16LE(32, 14);
  hdr.writeUInt32LE(0, 16);
  hdr.writeUInt32LE(size * size * 4, 20);
  const xor = Buffer.alloc(size * size * 4);
  for (let y = 0; y < size; y++) bgra.copy(xor, y * size * 4, (size - 1 - y) * size * 4, (size - y) * size * 4);
  const andRow = Math.ceil(size / 32) * 4;
  const and = Buffer.alloc(andRow * size);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      if (bgra[(y * size + x) * 4 + 3] === 0) and[y * andRow + (x >> 3)] |= 0x80 >> (x & 7);
    }
  }
  return Buffer.concat([hdr, xor, and]);
}

const images = SIZES.map((s) => ({ s, data: bmpEntry(s, render(s)) }));
const header = Buffer.alloc(6);
header.writeUInt16LE(0, 0); header.writeUInt16LE(1, 2); header.writeUInt16LE(images.length, 4);
let offset = 6 + images.length * 16;
const entries = [];
for (const img of images) {
  const e = Buffer.alloc(16);
  e[0] = img.s >= 256 ? 0 : img.s;
  e[1] = img.s >= 256 ? 0 : img.s;
  e.writeUInt16LE(1, 4); e.writeUInt16LE(32, 6);
  e.writeUInt32LE(img.data.length, 8); e.writeUInt32LE(offset, 12);
  offset += img.data.length;
  entries.push(e);
}
const ico = Buffer.concat([header, ...entries, ...images.map((i) => i.data)]);
fs.writeFileSync(path.join(__dirname, '..', 'src', 'app.ico'), ico);

// preview PNG: sizes on a checker/dark strip
function png(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  }
  const idat = zlib.deflateSync(raw, { level: 9 });
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(require('zlib').crc32 ? zlib.crc32(td) : crc32(td));
    return Buffer.concat([len, td, crc]);
  };
  function crc32(buf) {
    let c, crc = 0xffffffff;
    for (let n = 0; n < buf.length; n++) {
      c = (crc ^ buf[n]) & 0xff;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crc = (crc >>> 8) ^ c;
    }
    return (crc ^ 0xffffffff) >>> 0;
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', idat), chunk('IEND', Buffer.alloc(0))]);
}
const P = 300, W = 1500, H = 340;
const canvas = Buffer.alloc(W * H * 4);
for (let i = 0; i < W * H; i++) { canvas[i * 4] = 24; canvas[i * 4 + 1] = 26; canvas[i * 4 + 2] = 32; canvas[i * 4 + 3] = 255; }
let xoff = 30;
for (const s of [256, 128, 64, 48, 32, 24, 20, 16]) {
  const px = images.find((i) => i.s === s).data;
  const bgra = px.subarray(40, 40 + s * s * 4);
  for (let y = 0; y < s; y++) for (let x = 0; x < s; x++) {
    const o = ((s - 1 - y) * s + x) * 4;
    const a = bgra[o + 3] / 255, d = ((y + 40) * W + (x + xoff)) * 4;
    canvas[d] = Math.round(bgra[o + 2] * a + 24 * (1 - a));
    canvas[d + 1] = Math.round(bgra[o + 1] * a + 26 * (1 - a));
    canvas[d + 2] = Math.round(bgra[o] * a + 32 * (1 - a));
    canvas[d + 3] = 255;
  }
  xoff += s + 26;
}
fs.writeFileSync(path.join(__dirname, 'icon-preview.png'), png(W, H, canvas));
console.log('wrote src/app.ico', ico.length, 'bytes');
