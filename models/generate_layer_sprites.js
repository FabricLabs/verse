#!/usr/bin/env node
// Generate three 32x32x1 voxel-layer sprites as .world files:
//  - layer_sphere.world (filled circle of stone)
//  - layer_grass.world (dithered grass/soil texture)
//  - layer_tree_trunk.world (vertical bark-like stripes)

const fs = require('fs');
const path = require('path');

// ---------------------------
// Colorful console helpers
// ---------------------------
const ANSI = {
  reset: '\x1b[0m',
  bold: '\x1b[1m',
  dim: '\x1b[2m',
  italic: '\x1b[3m',
  underline: '\x1b[4m',
  fg: {
    black: '\x1b[30m', red: '\x1b[31m', green: '\x1b[32m', yellow: '\x1b[33m', blue: '\x1b[34m', magenta: '\x1b[35m', cyan: '\x1b[36m', white: '\x1b[37m', gray: '\x1b[90m'
  },
  bg: {
    black: '\x1b[40m', red: '\x1b[41m', green: '\x1b[42m', yellow: '\x1b[43m', blue: '\x1b[44m', magenta: '\x1b[45m', cyan: '\x1b[46m', white: '\x1b[47m'
  }
};

function colorize (str, colorCode) {
  return `${colorCode}${str}${ANSI.reset}`;
}

function formatMeters (m) {
  return `${m.toFixed(3)}m`;
}

// World header encoding mirrors world_serialize in src/world.c
// [version:4 hex][width:8][height:8][depth:8][log_length:8][log_data...][voxels...]
function hexN (value, width) {
  return value.toString(16).toUpperCase().padStart(width, '0');
}

function voxelNibble (n) {
  const table = '0123456789ABCDEF';
  return table[n & 0xF];
}

function saveWorld (filePath, width, height, depth, voxelsAsHexChars, logStr = '') {
  if (voxelsAsHexChars.length !== width * height * depth) {
    throw new Error('Voxel buffer length mismatch');
  }
  // If voxel buffer is all AIR and we have a topDownTexture, auto-voxelize by nearest nibble color so older paths render
  const isAllAir = /^0+$/.test(voxelsAsHexChars);
  if (isAllAir && logStr && logStr.includes('"topDownTexture"')) {
    try {
      const ppos = logStr.indexOf('"pixelsHex":"');
      if (ppos > 0) {
        const start = ppos + '"pixelsHex":"'.length;
        const end = logStr.indexOf('"', start);
        if (end > start) {
          const pixelsHex = logStr.slice(start, end);
          if (pixelsHex.length >= width * height * 6) {
            // Local nibble color table consistent with world_voxel_type_color
            const nibRgb = {
              1:[30,144,255], 2:[139,90,43], 3:[34,139,34], 4:[244,164,96], 5:[50,50,50],
              6:[60,60,65], 7:[120,110,110], 8:[180,180,160], 9:[128,128,128], 10:[150,80,60],
              11:[120,90,60], 12:[110,100,80], 13:[100,60,30], 14:[120,80,40], 15:[90,170,50],
              16:[100,185,60], 17:[85,165,48], 18:[70,150,40], 19:[34,139,34], 20:[180,200,160],
              21:[50,120,60], 26:[255,90,20], 30:[220,100,180], 31:[139,90,43], 32:[205,190,150], 33:[110,75,45]
            };
            // Restrict to hex-storable, non-AIR types (avoid WATER/WOOD to reduce stray blues/browns): 3..15 inclusive
            const keys = [];
            for (let k = 3; k <= 15; k++) keys.push(k);
            let vb = '';
            for (let i = 0; i < width * height; i++) {
              const off = i * 6;
              const r = parseInt(pixelsHex.slice(off+0, off+2), 16) | 0;
              const g = parseInt(pixelsHex.slice(off+2, off+4), 16) | 0;
              const b = parseInt(pixelsHex.slice(off+4, off+6), 16) | 0;
              let bestK = 9, bestD = 1<<30;
              for (let k of keys) {
                const [tr,tg,tb] = nibRgb[k];
                const dr = r - tr, dg = g - tg, db = b - tb;
                const d = dr*dr + dg*dg + db*db;
                if (d < bestD) { bestD = d; bestK = k; }
              }
              vb += voxelNibble(bestK);
            }
            voxelsAsHexChars = vb;
          }
        }
      }
    } catch (e) {}
  }
  const versionHex = hexN(0, 4);
  const widthHex = hexN(width, 8);
  const heightHex = hexN(height, 8);
  const depthHex = hexN(depth, 8);
  const logLenHex = hexN(Buffer.byteLength(logStr, 'utf8'), 8);
  const out = versionHex + widthHex + heightHex + depthHex + logLenHex + logStr + voxelsAsHexChars;
  fs.writeFileSync(filePath, out);
  const info = (typeof globalThis !== 'undefined' && globalThis.gHumanScaleInfo) ? globalThis.gHumanScaleInfo : null;
  if (info && typeof info.entityPxHeight === 'number') {
    const pad = (info.padTopPx != null && info.padBottomPx != null) ? `, pad=${info.padTopPx}+${info.padBottomPx}` : '';
    const mpp = (info.metersPerPixel != null) ? info.metersPerPixel : 0;
    const mpv = (info.metersPerVoxel != null) ? info.metersPerVoxel : mpp;
    const wm = width * mpp;
    const hm = height * mpp;
    const dm = depth * mpv;
    const base = path.basename(filePath);
    const category =
      /grass/i.test(base) ? 'grass' :
      /trunk/i.test(base) ? 'wood' :
      /rock/i.test(base) ? 'rock' :
      /sphere/i.test(base) ? 'sphere' : 'generic';
    const icon = { grass: '🌿', wood: '🌲', rock: '🪨', sphere: '🟣', generic: '📦' }[category];
    const col = {
      grass: ANSI.fg.green,
      wood: ANSI.fg.yellow,
      rock: ANSI.fg.magenta,
      sphere: ANSI.fg.cyan,
      generic: ANSI.fg.white
    }[category];
    const sizesStr = `${formatMeters(wm)} × ${formatMeters(hm)} × ${formatMeters(dm)}`;
    const msg = `${colorize('Wrote', ANSI.fg.gray)} ${colorize(base, col)} ${colorize(icon, ANSI.fg.white)} ${ANSI.dim}${sizesStr}${ANSI.reset} ${ANSI.dim}(tile=${info.entityPxHeight}px${pad}, ${mpp.toFixed(4)} m/px)${ANSI.reset}`;
    console.log(msg);
  } else {
    console.log(`Wrote ${filePath}`);
  }
  // Write sidecar BMP preview of the generated texture alongside the world file
  try {
    writeSidecarBitmap(filePath, width, height, depth, voxelsAsHexChars, logStr);
  } catch (e) {
    // Silent on preview errors to keep generator robust
  }
}

// ---------------------------
// Palette + color utilities
// ---------------------------
function hslToRgb (h, s, l) {
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = l - c / 2;
  let r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; b = 0; }
  else if (h < 120) { r = x; g = c; b = 0; }
  else if (h < 180) { r = 0; g = c; b = x; }
  else if (h < 240) { r = 0; g = x; b = c; }
  else if (h < 300) { r = x; g = 0; b = c; }
  else { r = c; g = 0; b = x; }
  return [Math.round((r + m) * 255), Math.round((g + m) * 255), Math.round((b + m) * 255)];
}

function buildPalette255 () {
  const palette = [];
  // 0..31: deep grays
  for (let i = 0; i < 32; i++) {
    const v = Math.round(8 + i * (80 / 31));
    palette.push([v, v, v]);
  }
  // 32..63: warm stone grays
  for (let i = 0; i < 32; i++) {
    const v = Math.round(60 + i * (80 / 31));
    palette.push([v, Math.round(v * 0.95), Math.round(v * 0.90)]);
  }
  // 64..111: greens (48 entries)
  for (let i = 0; i < 48; i++) {
    const h = 110 + (i * 10) / 48; // 110..120
    const s = 0.55 + 0.35 * Math.sin(i / 48 * Math.PI);
    const l = 0.25 + 0.30 * (i / 48);
    palette.push(hslToRgb(h, s, l));
  }
  // 112..159: browns (48 entries)
  for (let i = 0; i < 48; i++) {
    const h = 25 + (i * 10) / 48; // 25..35
    const s = 0.55 + 0.25 * Math.sin(i / 48 * Math.PI);
    const l = 0.22 + 0.28 * (i / 48);
    palette.push(hslToRgb(h, s, l));
  }
  // 160..191: bluish basalt/steel (32 entries)
  for (let i = 0; i < 32; i++) {
    const h = 210 + (i * 10) / 32; // 210..220
    const s = 0.25 + 0.15 * Math.sin(i / 32 * Math.PI);
    const l = 0.15 + 0.25 * (i / 32);
    palette.push(hslToRgb(h, s, l));
  }
  // 192..223: limestone/beige (32 entries)
  for (let i = 0; i < 32; i++) {
    const h = 45 + (i * 10) / 32; // 45..55
    const s = 0.35 + 0.15 * Math.sin(i / 32 * Math.PI);
    const l = 0.60 + 0.20 * (i / 32);
    palette.push(hslToRgb(h, s, l));
  }
  // 224..254: accents (31 entries)
  for (let i = 0; i < 31; i++) {
    const h = (i * 360) / 31;
    const s = 0.65;
    const l = 0.55;
    palette.push(hslToRgb(h, s, l));
  }
  // Ensure length 255
  while (palette.length < 255) palette.push([255, 255, 255]);
  return palette.slice(0, 255);
}

function bytesToHex (u8) {
  const hex = [];
  for (let i = 0; i < u8.length; i++) {
    hex.push((u8[i] >>> 4).toString(16));
    hex.push((u8[i] & 0x0f).toString(16));
  }
  return hex.join('');
}

function rgbToHex6 (r, g, b) {
  const to2 = (v) => Math.max(0, Math.min(255, v)).toString(16).toUpperCase().padStart(2, '0');
  return to2(r) + to2(g) + to2(b);
}

// Lightweight string hash -> 32-bit unsigned
function hashStr (s) {
  let h = 2166136261 >>> 0;
  for (let i = 0; i < s.length; i++) {
    h ^= s.charCodeAt(i);
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h >>> 0;
}

// Enrich a palette index with occasional full-palette accents to ensure wide color use
function enrichPaletteIdx (idx, x, y, width, height, seedInt, rate = 0.06) {
  // Deterministic pseudo-random per pixel
  let h = (Math.imul(x + 1, 374761393) ^ Math.imul(y + 1, 668265263) ^ seedInt) >>> 0;
  // 0..999 threshold
  const r = (h >>> 12) % 1000;
  if (r < Math.floor(rate * 1000)) {
    // Walk indices so every tile uses most of the palette
    const p = (x + y * width) % 255;
    return p;
  }
  return idx;
}

// ---------------------------
// BMP writer (24-bit, uncompressed)
// ---------------------------
function writeBmp24 (filePath, width, height, getPixelRgb) {
  // BMP rows are bottom-up, 4-byte padded
  const rowStride = ((width * 3 + 3) & ~3);
  const pixelDataSize = rowStride * height;
  const fileSize = 14 + 40 + pixelDataSize; // BITMAPFILEHEADER + BITMAPINFOHEADER
  const buf = Buffer.alloc(fileSize);
  let o = 0;
  // FILE HEADER
  buf[o++] = 0x42; buf[o++] = 0x4D; // 'BM'
  buf.writeUInt32LE(fileSize, o); o += 4;
  buf.writeUInt16LE(0, o); o += 2; // reserved1
  buf.writeUInt16LE(0, o); o += 2; // reserved2
  buf.writeUInt32LE(14 + 40, o); o += 4; // pixel data offset
  // INFO HEADER
  buf.writeUInt32LE(40, o); o += 4; // header size
  buf.writeInt32LE(width, o); o += 4;
  buf.writeInt32LE(height, o); o += 4;
  buf.writeUInt16LE(1, o); o += 2; // planes
  buf.writeUInt16LE(24, o); o += 2; // bpp
  buf.writeUInt32LE(0, o); o += 4; // compression (BI_RGB)
  buf.writeUInt32LE(pixelDataSize, o); o += 4;
  buf.writeInt32LE(2835, o); o += 4; // X ppm (~72 DPI)
  buf.writeInt32LE(2835, o); o += 4; // Y ppm
  buf.writeUInt32LE(0, o); o += 4; // colors used
  buf.writeUInt32LE(0, o); o += 4; // important colors
  // Pixel data (BGR, bottom-up)
  for (let y = 0; y < height; y++) {
    const by = height - 1 - y;
    let rowOff = 14 + 40 + by * rowStride;
    for (let x = 0; x < width; x++) {
      const { r, g, b } = getPixelRgb(x, y);
      buf[rowOff++] = b & 255;
      buf[rowOff++] = g & 255;
      buf[rowOff++] = r & 255;
    }
    // padding zeros already 0 in Buffer
  }
  fs.writeFileSync(filePath, buf);
}

function writeSidecarBitmap (worldPath, width, height, depth, voxelsAsHexChars, logStr) {
  const base = worldPath.replace(/\.world$/i, '');
  const bmpPath = base + '.bmp';
  // Prefer embedded topDownTexture or animatedTexture for color, else map voxel nibbles to palette via type colors
  let pixelsHex = null;
  let framesHex = null;
  try {
    if (logStr && logStr.length > 0) {
      const hasTop = logStr.includes('"topDownTexture"');
      const hasAnim = logStr.includes('"animatedTexture"');
      if (hasTop) {
        // Extract pixelsHex
        const ppos = logStr.indexOf('"pixelsHex":"');
        if (ppos > 0) {
          const start = ppos + '"pixelsHex":"'.length;
          const end = logStr.indexOf('"', start);
          if (end > start) pixelsHex = logStr.slice(start, end);
        }
      } else if (hasAnim) {
        // First frame only for preview
        const fpos = logStr.indexOf('"pixelsHexFrames":[');
        if (fpos > 0) {
          const start = logStr.indexOf('"', fpos + 20) + 1;
          const end = logStr.indexOf('"', start);
          if (start > 0 && end > start) pixelsHex = logStr.slice(start, end);
        }
      }
    }
  } catch (e) {}
  if (pixelsHex && pixelsHex.length >= width * height * 6) {
    // Build from 24-bit hex
    writeBmp24(bmpPath, width, height, (x, y) => {
      const idx = (y * width + x) * 6;
      const r = parseInt(pixelsHex.slice(idx + 0, idx + 2), 16);
      const g = parseInt(pixelsHex.slice(idx + 2, idx + 4), 16);
      const b = parseInt(pixelsHex.slice(idx + 4, idx + 6), 16);
      return { r, g, b };
    });
    return;
  }
  // Fallback: map voxel nibble types to colors using a small built-in table consistent with world_voxel_type_color
  function nibbleToRgb (n) {
    const map = {
      // Keep in sync with src/world.c world_voxel_type_color
      0: [0,0,0],                   // AIR
      1: [30,144,255],              // WATER
      2: [139,90,43],               // WOOD
      3: [34,139,34],               // LEAVES (generic)
      4: [244,164,96],              // SAND
      5: [50,50,50],                // BEDROCK
      6: [60,60,65],                // ROCK_BASALT
      7: [120,110,110],             // ROCK_GRANITE
      8: [180,180,160],             // ROCK_LIMESTONE
      9: [128,128,128],             // STONE
      10:[150,80,60],               // DIRT_CLAY
      11:[120,90,60],               // DIRT_LOAM
      12:[110,100,80],              // DIRT_SILT
      13:[100,60,30],               // SOIL
      14:[120,80,40],               // DIRT
      15:[90,170,50],               // GRASS
      16:[100,185,60],              // GRASS_SHORT
      17:[85,165,48],               // GRASS_MEDIUM
      18:[70,150,40],               // GRASS_TALL
      19:[34,139,34],               // LEAVES_OAK
      20:[180,200,160],             // LEAVES_BIRCH
      21:[50,120,60],               // LEAVES_PINE
      26:[255,90,20],               // MAGMA
      30:[220,100,180],             // FLOWER
      31:[139,90,43],               // WOOD_OAK
      32:[205,190,150],             // WOOD_BIRCH
      33:[110,75,45]                // WOOD_PINE
    };
    const def = [64,64,64];
    const arr = map.hasOwnProperty(n) ? map[n] : def;
    return { r: arr[0], g: arr[1], b: arr[2] };
  }
  writeBmp24(bmpPath, width, height, (x, y) => {
    const n = voxelsAsHexChars[y * width + x];
    const v = parseInt(n, 16);
    return nibbleToRgb(isNaN(v) ? 0 : v);
  });
}

// ---------------------------
// Top-down 32px texture generators (full 255-color palette)
// ---------------------------
function generateTopDownTextureGrassHex24 (width, height, seed = 'layer_grass') {
  const palette = buildPalette255();
  const seedInt = hashStr(seed + ':enrich');
  const noise = makePerlin2D(seed + ':grass');
  const nHue = makePerlin2D(seed + ':hue');
  const nSoil = makePerlin2D(seed + ':soil');
  const nFlower = makePerlin2D(seed + ':flower');
  const scale0 = 1 / 18, scale1 = 1 / 9, scale2 = 1 / 4;
  const w0 = 0.55, w1 = 0.30, w2 = 0.15;
  const stutterCycle = [1, 0, -1];
  let out = '';
  for (let y = 0; y < height; y++) {
    const shift = stutterCycle[y % stutterCycle.length];
    for (let x = 0; x < width; x++) {
      const xs = (x + shift + width) % width;
      const ny = y;
      // Base grass value
      let c = w0 * noise(xs * scale0, ny * 0.03) + w1 * noise(xs * scale1 + 17.2, ny * 0.06) + w2 * noise(xs * scale2 - 4.1, ny * 0.12);
      c = Math.min(1, Math.max(0, (c - 0.30) / 0.50));
      // Hue shift across greens 64..111 with variation 110..125 degrees equivalent in palette index drift
      const hueVar = nHue(xs * 0.06, ny * 0.06) - 0.5; // [-0.5,0.5]
      const greenBase = 64 + Math.round(c * 47);
      let idx = greenBase + Math.round(hueVar * 8); // ±8 within greens band
      // Soil exposure patches from 112..159 browns
      const soil = nSoil(xs * 0.08 + 3.3, ny * 0.08 - 6.1);
      const soilOn = soil > 0.78 || (soil > 0.72 && c < 0.35);
      if (soilOn) {
        const brown = 112 + Math.round((0.4 + 0.6 * c) * 47);
        idx = brown;
      }
      // Occasional flowers accents from 224..254 (purples/yellows/reds)
      const ff = nFlower(xs * 0.20 + 9.9, ny * 0.20 + 1.7);
      const flowerOn = !soilOn && ff > 0.965; // ~3.5% accents
      if (flowerOn) {
        const which = ((x * 1103515245) ^ (y * 12345)) & 3;
        if (which === 0) idx = 224 + 2;     // warm yellow
        else if (which === 1) idx = 224 + 18; // magenta
        else if (which === 2) idx = 224 + 12; // red-orange
        else idx = 224 + 24;                 // bluish accent
      }
      // Clamp and enrich
      idx = Math.max(0, Math.min(254, idx));
      idx = enrichPaletteIdx(idx, x, y, width, height, seedInt, 0.05);
      let [r, g, b] = palette[idx];
      // Subtle sun shading from NW→SE
      const shade = 0.94 + 0.10 * ((x - y) / Math.max(1, width));
      r = Math.max(0, Math.min(255, Math.round(r * shade)));
      g = Math.max(0, Math.min(255, Math.round(g * shade)));
      b = Math.max(0, Math.min(255, Math.round(b * shade)));
      out += rgbToHex6(r, g, b);
    }
  }
  return out;
}

function generateTopDownTextureTrunkHex24 (width, height, species = 'oak') {
  const palette = buildPalette255();
  const seedInt = hashStr('trunk:' + species);
  let stripeCount, noiseAmt, creaseThreshold, knotEvery, knotWidth;
  switch (species) {
    case 'birch': stripeCount = 4; noiseAmt = 0.15; creaseThreshold = -0.05; knotEvery = 12; knotWidth = 2.0; break;
    case 'pine': stripeCount = 8; noiseAmt = 0.30; creaseThreshold = -0.12; knotEvery = 8; knotWidth = 1.5; break;
    case 'oak': default: stripeCount = 6; noiseAmt = 0.22; creaseThreshold = -0.10; knotEvery = 10; knotWidth = 2.5; break;
  }
  const stutterCycle = [1, 0, -1];
  const cx = (width - 1) / 2;
  let out = '';
  for (let y = 0; y < height; y++) {
    const shift = stutterCycle[y % stutterCycle.length];
    for (let x = 0; x < width; x++) {
      const xs = (x + shift + width) % width;
      const u = (xs + 0.5) / width;
      const s1 = Math.sin((u + 0.21) * stripeCount * Math.PI * 4) * 0.35;
      const s2 = Math.sin((u + 0.00) * stripeCount * Math.PI * 2);
      const ridge = s2 * 0.75 + s1 * 0.25;
      const h = ((xs * 374761393) ^ (y * 668265263)) >>> 0;
      const n = (((h ^ 0xB5297A4D) * 2654435761) >>> 24) / 255.0; // [0,1)
      const v = ridge + (n - 0.5) * noiseAmt;
      const dy = ((y + (species === 'birch' ? 3 : species === 'pine' ? 1 : 2)) % knotEvery) - knotEvery / 2;
      const dx = (xs - cx);
      const knot = (dx * dx) / (knotWidth * knotWidth) + (dy * dy) / ((knotWidth * 0.6) * (knotWidth * 0.6));
      const knotOn = knot < 0.6;
      const isCrease = (v < creaseThreshold) || knotOn;
      // Base brown
      const t = Math.max(0, Math.min(1, (v + 1) / 2));
      let idx = 112 + Math.max(0, Math.min(47, Math.round(t * 47)));
      if (isCrease) {
        // darker grooves from gray band (0..31) mixed with cool shadow (160..191)
        const dark = 5 + Math.round(n * 20);
        const cool = 160 + Math.round((1 - t) * 10);
        idx = (n < 0.5) ? dark : cool;
      } else {
        // Sap highlights and inner rings: occasional lighter limestone tint
        if ((h & 255) < 8) idx = 192 + 20 + ((h >> 8) & 3);
      }
      // Lichen patches (greens 70..90) sparsely
      const lich = (((h ^ 0x9E3779B1) * 1103515245) >>> 24) / 255.0;
      if (lich > 0.985) idx = 70 + ((h >> 12) & 12);
      // Clamp and enrich
      idx = Math.max(0, Math.min(254, idx));
      idx = enrichPaletteIdx(idx, x, y, width, height, seedInt, 0.03);
      const [r, g, b] = palette[idx];
      out += rgbToHex6(r, g, b);
    }
  }
  return out;
}

function generateTopDownTextureRockHex24 (width, height, kind = 'stone', seed = 'rock') {
  const palette = buildPalette255();
  const noise = makePerlin2D(seed + ':' + kind);
  const seedInt = hashStr('rock:' + kind);
  const nVein = makePerlin2D(seed + ':vein:' + kind);
  const nFleck = makePerlin2D(seed + ':fleck:' + kind);
  let baseBandStart, baseBandCount; // palette bands
  switch (kind) {
    case 'basalt': baseBandStart = 160; baseBandCount = 32; break; // bluish steel
    case 'granite': baseBandStart = 160; baseBandCount = 32; break; // mix later
    case 'limestone': baseBandStart = 192; baseBandCount = 32; break; // beige
    case 'stone': default: baseBandStart = 32; baseBandCount = 32; break; // warm stone grays
  }
  let out = '';
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const n0 = noise(x * (1 / 10), y * (1 / 10));
      const n1 = noise(x * (1 / 5) + 7.1, y * (1 / 5) - 3.4);
      const n2 = noise(x * (1 / 2.5) - 11.3, y * (1 / 2.5) + 5.6);
      let c = 0.5 * n0 + 0.3 * n1 + 0.2 * n2;
      // Base index
      let idx;
      if (kind === 'granite') {
        const tn = noise(x * (1 / 3) + 23.5, y * (1 / 3) - 11.1);
        if (tn < 0.2) idx = 192 + Math.round(c * 31); // limestone tint
        else if (tn < 0.5) idx = 32 + Math.round(c * 31); // warm gray
        else idx = 160 + Math.round(c * 31); // steelish
      } else {
        idx = baseBandStart + Math.max(0, Math.min(baseBandCount - 1, Math.round(c * (baseBandCount - 1))));
      }
      // Veins: alternate darker/lighter bands
      const v = nVein(x * 0.5, y * 0.2);
      if (v > 0.82) {
        // Light quartz vein
        idx = (kind === 'limestone') ? (192 + 28) : (32 + 28);
      } else if (v < 0.10) {
        // Dark crack
        idx = 5; // bedrock dark
      }
      // Mineral flecks: copper/silver/gold sparse
      const f = nFleck(x * 1.7 + 9.9, y * 1.3 - 2.2);
      if (f > 0.985) {
        const pick = ((x * 1103515245) ^ (y * 12345)) & 7;
        if (pick < 3) idx = 224 + 6;          // gold
        else if (pick < 5) idx = 224 + 10;    // coppery
        else idx = 224 + 2;                   // yellowish accent
      }
      idx = Math.max(0, Math.min(254, idx));
      idx = enrichPaletteIdx(idx, x, y, width, height, seedInt, 0.04);
      const [r, g, b] = palette[idx];
      out += rgbToHex6(r, g, b);
    }
  }
  return out;
}

function generateTopDownTextureSphereHex24 (width, height) {
  const palette = buildPalette255();
  const cx = (width - 1) / 2;
  const cy = (height - 1) / 2;
  const r = Math.min(width, height) * 0.45;
  const seedInt = hashStr('sphere');
  let out = '';
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const dx = x - cx;
      const dy = y - cy;
      const dist2 = dx * dx + dy * dy;
      const inside = dist2 <= r * r;
      let idx;
      if (inside) {
        // radial normal -> shading; add warm rimlight and cool core
        const dist = Math.sqrt(dist2) / r; // 0..1
        const shade = Math.max(0, Math.min(1, 1.0 - dist));
        const warm = 32 + Math.round(shade * 20);
        const cool = 160 + Math.round((1 - shade) * 12);
        idx = (x + y) % 2 === 0 ? warm : cool;
      } else {
        idx = 0;
      }
      idx = enrichPaletteIdx(idx, x, y, width, height, seedInt, 0.02);
      const [rr, gg, bb] = palette[idx];
      out += rgbToHex6(rr, gg, bb);
    }
  }
  return out;
}

// Minotaur ASCII-art texture (32x32) -> full-color hex24 using palette 255
function generateTopDownTextureMinotaurHex24 (width, height) {
  const palette = buildPalette255();
  // 32x32 ASCII; characters map to colors
  const art = [
    "         HHHHHHHHHHHHHHHHHH         ",
    "       HHHHHHHHHHHHHHHHHHHHHH       ",
    "      HHHHHHHHHHHHHHHHHHHHHHHH      ",
    "     HHHHHHHH          HHHHHHHH     ",
    "    HHHHHHHH            HHHHHHHH    ",
    "    HHHHHHH              HHHHHHH    ",
    "     HHHHH                HHHHH     ",
    "        MMMMMMMMMMMMMMMMMMMM        ",
    "       MMMMMMMMMMMMMMMMMMMMM       ",
    "      MMMMMMMMMMMMMMMMMMMMMMM      ",
    "     MMMM   MMMMMMMMMM   MMMM     ",
    "     MMM     MMMMMMMM     MMM     ",
    "     MMM  o    MMMM    o  MMM     ",
    "     MMM         MM         MMM     ",
    "     MMM        MMMM        MMM     ",
    "     MMM      MMMMMMMM      MMM     ",
    "     MMM     MMMMMMMMMM     MMM     ",
    "     MMM    MMMMMMMMMMMM    MMM     ",
    "     MMM     MMMMMMMMMM     MMM     ",
    "     MMM      MMMMMMMM      MMM     ",
    "     MMM        MMMM        MMM     ",
    "     MMM         OO         MMM     ",
    "     MMM         OO         MMM     ",
    "     MMM        OOOO        MMM     ",
    "     MMM       OOOOOO       MMM     ",
    "      MMM     OOOOOOOO     MMM      ",
    "       MMMM  OOOOOOOOOO  MMMM       ",
    "        MMMMOOOOOOOOOOOOMMMM        ",
    "          MMMMMMMMMMMMMMMM          ",
    "            MMMMMMMMMMMM            ",
    "               MMMMMM               ",
    "                 MM                 "
  ];
  const bgIdx = 0; // darkest
  // Horns: limestone/beige band 192..223; pick light range
  const hornIdx = 192 + 26; // light beige
  // Face: browns 112..159 mid
  const faceIdx = 112 + 30;
  // Eyes: dark bedrock-like 5
  const eyeIdx = 5;
  // Ring: gold accent from 224..254; pick warm yellow
  const ringIdx = 224 + 6;
  const pick = (ch) => {
    if (ch === 'H') return hornIdx;
    if (ch === 'M') return faceIdx;
    if (ch === 'o' || ch === 'O') return (ch === 'o') ? eyeIdx : ringIdx;
    return bgIdx;
  };
  let out = '';
  for (let y = 0; y < height; y++) {
    const ay = Math.max(0, Math.min(art.length - 1, Math.round((y / (height - 1)) * (art.length - 1))));
    const row = art[ay];
    for (let x = 0; x < width; x++) {
      const ax = Math.max(0, Math.min(row.length - 1, Math.round((x / (width - 1)) * (row.length - 1))));
      const ch = row[ax];
      const idx = pick(ch);
      const [r, g, b] = palette[idx];
      out += rgbToHex6(r, g, b);
    }
  }
  return out;
}

function generateMinotaurVoxelMask (width, height) {
  // AIR = 0; WOOD = 2; BEDROCK = 5; LIMESTONE = 8;
  const artW = 32, artH = 32;
  const art = [
    "         HHHHHHHHHHHHHHHHHH         ",
    "       HHHHHHHHHHHHHHHHHHHHHH       ",
    "      HHHHHHHHHHHHHHHHHHHHHHHH      ",
    "     HHHHHHHH          HHHHHHHH     ",
    "    HHHHHHHH            HHHHHHHH    ",
    "    HHHHHHH              HHHHHHH    ",
    "     HHHHH                HHHHH     ",
    "        MMMMMMMMMMMMMMMMMMMM        ",
    "       MMMMMMMMMMMMMMMMMMMMM       ",
    "      MMMMMMMMMMMMMMMMMMMMMMM      ",
    "     MMMM   MMMMMMMMMM   MMMM     ",
    "     MMM     MMMMMMMM     MMM     ",
    "     MMM  o    MMMM    o  MMM     ",
    "     MMM         MM         MMM     ",
    "     MMM        MMMM        MMM     ",
    "     MMM      MMMMMMMM      MMM     ",
    "     MMM     MMMMMMMMMM     MMM     ",
    "     MMM    MMMMMMMMMMMM    MMM     ",
    "     MMM     MMMMMMMMMM     MMM     ",
    "     MMM      MMMMMMMM      MMM     ",
    "     MMM        MMMM        MMM     ",
    "     MMM         OO         MMM     ",
    "     MMM         OO         MMM     ",
    "     MMM        OOOO        MMM     ",
    "     MMM       OOOOOO       MMM     ",
    "      MMM     OOOOOOOO     MMM      ",
    "       MMMM  OOOOOOOOOO  MMMM       ",
    "        MMMMOOOOOOOOOOOOMMMM        ",
    "          MMMMMMMMMMMMMMMM          ",
    "            MMMMMMMMMMMM            ",
    "               MMMMMM               ",
    "                 MM                 "
  ];
  let out = '';
  for (let y = 0; y < height; y++) {
    const ay = Math.max(0, Math.min(artH - 1, Math.round((y / (height - 1)) * (artH - 1))));
    const row = art[ay];
    for (let x = 0; x < width; x++) {
      const ax = Math.max(0, Math.min(artW - 1, Math.round((x / (width - 1)) * (artW - 1))));
      const ch = row[ax];
      const on = (ch !== ' ');
      out += voxelNibble(on ? 2 /* WOOD */ : 0 /* AIR */);
    }
  }
  return out;
}

// ---------------------------
// MAGMA animated texture (32x32xNUM_FRAMES), sheen moves along Z (time)
// ---------------------------
function generateMagmaAnimFramesHex24 (width, height, frames, seed = 'layer_magma') {
  const palette = buildPalette255();
  const seedInt = hashStr(seed);
  const nBase = makePerlin2D(seed + ':base');
  const nSheen = makePerlin2D(seed + ':sheen');
  const framesHex = [];
  for (let f = 0; f < frames; f++) {
    let out = '';
    const t = f / frames; // 0..1 phase
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        // Base magma: reds/oranges with dark cracks
        const nb = 0.6 * nBase(x * 0.18, y * 0.18) + 0.4 * nBase(x * 0.08 + 7.1, y * 0.08 - 3.4);
        const cracks = nBase(x * 0.55 - 11.3, y * 0.55 + 5.6);
        let idx;
        if (cracks > 0.84) {
          idx = 5; // bedrock dark crack
        } else {
          // Hot body: use accent band 224..254 weighted toward reds/yellows
          const hot = Math.max(0, Math.min(1, nb));
          const band = 224 + Math.round(8 + 10 * hot); // 232..242
          idx = Math.max(224, Math.min(254, band));
        }
        // Animated sheen: move bright line diagonally via time phase
        const s = nSheen((x + t * width) * 0.12, (y - t * height) * 0.12);
        if (s > 0.80 && idx !== 5) {
          // Boost toward white-hot
          idx = 224 + 28; // bright
        }
        idx = enrichPaletteIdx(idx, x, y, width, height, seedInt, 0.08);
        const [r, g, b] = palette[idx];
        out += rgbToHex6(r, g, b);
      }
    }
    framesHex.push(out);
  }
  return framesHex;
}

// Deterministic pseudo-random (xorshift32)
function makePRNG (seed) {
  let state = 0;
  for (let i = 0; i < seed.length; i++) state = (state * 31 + seed.charCodeAt(i)) >>> 0;
  if (state === 0) state = 0x12345678;
  return function next () {
    let x = state;
    x ^= x << 13; x >>>= 0;
    x ^= x >>> 17; x >>>= 0;
    x ^= x << 5; x >>>= 0;
    state = x >>> 0;
    return (state & 0xFFFFFFFF) / 0x100000000; // [0,1)
  };
}

// Lightweight 2D Perlin-like noise (deterministic from seed string)
function makePerlin2D (seedStr) {
  let seed = 0;
  for (let i = 0; i < seedStr.length; i++) seed = (seed * 31 + seedStr.charCodeAt(i)) >>> 0;
  if (seed === 0) seed = 0xBADC0DE;
  function hash (x, y) {
    let h = (Math.imul(x, 374761393) ^ Math.imul(y, 668265263) ^ seed) >>> 0;
    h ^= h >>> 13; h = Math.imul(h, 1274126177) >>> 0; h ^= h >>> 16;
    return h >>> 0;
  }
  function grad (h) {
    switch (h & 7) {
      case 0: return [ 1, 0];
      case 1: return [-1, 0];
      case 2: return [ 0, 1];
      case 3: return [ 0,-1];
      case 4: return [ 0.7071, 0.7071];
      case 5: return [-0.7071, 0.7071];
      case 6: return [ 0.7071,-0.7071];
      default:return [-0.7071,-0.7071];
    }
  }
  const fade = (t) => t * t * (3 - 2 * t);
  const lerp = (a, b, t) => a + (b - a) * t;
  return function noise (x, y) {
    const x0 = Math.floor(x), y0 = Math.floor(y);
    const xf = x - x0,      yf = y - y0;
    const u = fade(xf),     v = fade(yf);
    const g00 = grad(hash(x0,   y0));
    const g10 = grad(hash(x0+1, y0));
    const g01 = grad(hash(x0,   y0+1));
    const g11 = grad(hash(x0+1, y0+1));
    const n00 = g00[0]*xf     + g00[1]*yf;
    const n10 = g10[0]*(xf-1) + g10[1]*yf;
    const n01 = g01[0]*xf     + g01[1]*(yf-1);
    const n11 = g11[0]*(xf-1) + g11[1]*(yf-1);
    const nx0 = lerp(n00, n10, u);
    const nx1 = lerp(n01, n11, u);
    const n   = lerp(nx0, nx1, v);
    return 0.5 * (n + 1); // [-1,1] -> [0,1]
  };
}

function generateSphereLayer (width, height) {
  const cx = (width - 1) / 2;
  const cy = (height - 1) / 2;
  const r = Math.min(width, height) * 0.45; // nice margins
  let out = '';
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const dx = x - cx;
      const dy = y - cy;
      const inside = dx * dx + dy * dy <= r * r;
      // VOXEL_STONE = 9, AIR = 0
      out += voxelNibble(inside ? 9 : 0);
    }
  }
  return out;
}

function generateGrassTexture (width, height, seed = 'layer_grass') {
  // Richer grass side texture using multiple grass variants, subtle soil exposure, and rare flowers.
  // Uses 1 nibble per pixel: maps directly to VoxelType values used by the editor's per-type layer loader.
  const nHeight = makePerlin2D(seed + ':height');
  const nTuft = makePerlin2D(seed + ':tuft');
  const nSoil = makePerlin2D(seed + ':soil');
  const nFlower = makePerlin2D(seed + ':flower');
  const nDry = makePerlin2D(seed + ':dry');
  const stutterCycle = [1, 0, -1];
  let out = '';
  for (let y = 0; y < height; y++) {
    const shift = stutterCycle[y % stutterCycle.length];
    for (let x = 0; x < width; x++) {
      const xs = (x + shift + width) % width;
      // Base grass "height" from layered noise (0..1)
      const h0 = nHeight(xs * 0.08, y * 0.08);
      const h1 = nTuft(xs * 0.16 + 12.3, y * 0.16 - 7.7);
      let h = 0.6 * h0 + 0.4 * h1; // 0..1
      // Subtle directional bias for wind/combing
      h += 0.06 * Math.sin((xs * 0.2 - y * 0.12) * Math.PI);
      // Normalize
      h = Math.max(0, Math.min(1, h));

      // Choose a grass variant by height thresholding
      // VOXEL_GRASS_*: 15,16,17,18
      let voxel = 15; // GRASS
      if (h > 0.74) voxel = 18;        // TALL
      else if (h > 0.58) voxel = 17;   // MEDIUM
      else if (h < 0.36) voxel = 16;   // SHORT

      // Dry/thatch speckles: bias some to short
      const dry = nDry(xs * 0.35 + 4.2, y * 0.18 - 3.1);
      if (dry > 0.88 && voxel !== 18) {
        voxel = 16; // dry short patch
      }

      // Soil exposure patches. Prefer SOIL/DIRT family with a few sand flecks.
      const s = nSoil(xs * 0.22 + 3.3, y * 0.22 - 6.1);
      const near_low = h < 0.45;
      if (s > 0.82 || (s > 0.76 && near_low)) {
        const pick = ((xs * 1103515245) ^ (y * 12345)) & 7;
        if (pick === 0) voxel = 4;     // occasional sand speck
        else if (pick < 3) voxel = 13; // SOIL
        else if (pick < 5) voxel = 11; // DIRT_LOAM
        else voxel = 14;               // DIRT
      }

      // Sparse flowers
      const f = nFlower(xs * 0.55 + 9.9, y * 0.55 + 1.7);
      if (voxel >= 15 && voxel <= 18 && f > 0.965) {
        voxel = 30; // VOXEL_FLOWER
      }

      out += voxelNibble(voxel);
    }
  }
  return out;
}

function generateTreeTrunkTexture (width, height, seed = 'layer_trunk') {
  const rand = makePRNG(seed);
  let out = '';
  // Parameters for vertical stripes
  const stripeCount = 6; // number of bark ridges across 32px
  const stutterCycle = [1, 0, -1];
  for (let y = 0; y < height; y++) {
    const shift = stutterCycle[y % stutterCycle.length];
    for (let x = 0; x < width; x++) {
      const xs = (x + shift + width) % width;
      const u = (xs + 0.5) / width;
      // Vertical ridges using a few harmonics
      const s1 = Math.sin((u + 0.00) * stripeCount * Math.PI * 2);
      const s2 = Math.sin((u + 0.17) * stripeCount * Math.PI * 4) * 0.35;
      const ridge = s1 * 0.75 + s2 * 0.25;
      // Subtle vertical noise to break uniformity
      const h = ((xs * 374761393) ^ (y * 668265263)) >>> 0;
      const n = (((h ^ 0xB5297A4D) * 2654435761) >>> 24) / 255.0; // [0,1)
      const v = ridge + (n - 0.5) * 0.25;
      // Base wood (2), dark creases (use 5 = bedrock nibble for visually dark accent)
      const wood = 2; // VOXEL_WOOD
      const dark = 5; // VOXEL_BEDROCK (renders dark; used as bark crease)
      const ch = v < -0.1 ? dark : wood;
      out += voxelNibble(ch);
    }
  }
  return out;
}

// Wood species variations: tweak stripe frequency and noise
function generateTreeTrunkBySpecies (width, height, species = 'oak') {
  let stripeCount;
  let noiseAmt;
  let creaseThreshold;
  let knotEvery; // vertical spacing in pixels between knots
  let knotWidth; // half-width in pixels for knots
  switch (species) {
    case 'birch':
      stripeCount = 4;  // wider stripes
      noiseAmt = 0.15;  // smoother
      creaseThreshold = -0.05; // fewer dark creases
      knotEvery = 12; knotWidth = 2.0;
      break;
    case 'pine':
      stripeCount = 8;  // tighter stripes
      noiseAmt = 0.30;  // rougher
      creaseThreshold = -0.12; // more creases
      knotEvery = 8; knotWidth = 1.5;
      break;
    case 'oak':
    default:
      stripeCount = 6;  // medium
      noiseAmt = 0.22;
      creaseThreshold = -0.10;
      knotEvery = 10; knotWidth = 2.5;
      break;
  }
  let out = '';
  const cx = (width - 1) / 2;
  const stutterCycle = [1, 0, -1];
  for (let y = 0; y < height; y++) {
    const shift = stutterCycle[y % stutterCycle.length];
    for (let x = 0; x < width; x++) {
      const xs = (x + shift + width) % width;
      const u = (xs + 0.5) / width;
      const s1 = Math.sin((u + 0.21) * stripeCount * Math.PI * 4) * 0.35;
      const s2 = Math.sin((u + 0.00) * stripeCount * Math.PI * 2);
      const ridge = s2 * 0.75 + s1 * 0.25;
      const h = ((xs * 374761393) ^ (y * 668265263)) >>> 0;
      const n = (((h ^ 0xB5297A4D) * 2654435761) >>> 24) / 255.0; // [0,1)
      const v = ridge + (n - 0.5) * noiseAmt;
      const wood = 2; // VOXEL_WOOD
      const dark = 5; // VOXEL_BEDROCK for creases
      // Knot mask: periodic darker oval near center X
      const dy = ((y + (species === 'birch' ? 3 : species === 'pine' ? 1 : 2)) % knotEvery) - knotEvery / 2;
      const dx = (xs - cx);
      const knot = (dx * dx) / (knotWidth * knotWidth) + (dy * dy) / ((knotWidth * 0.6) * (knotWidth * 0.6));
      const knotOn = knot < 0.6; // inside oval
      const isCrease = (v < creaseThreshold) || knotOn;
      out += voxelNibble(isCrease ? dark : wood);
    }
  }
  return out;
}

// Rock texture helpers (basalt/granite/limestone/stone)
function generateRockTexture (width, height, kind = 'stone', seed = 'rock') {
  const noise = makePerlin2D(seed + ':' + kind);
  // Setup per-kind parameters: frequency, crack density, second pass layering
  let scale, crackDensity, layerMix;
  switch (kind) {
    case 'basalt':
      scale = 1 / 6; crackDensity = 0.20; layerMix = 0.10; break; // blocky
    case 'granite':
      scale = 1 / 9; crackDensity = 0.12; layerMix = 0.25; break; // speckled
    case 'limestone':
      scale = 1 / 12; crackDensity = 0.08; layerMix = 0.35; break; // smoother bands
    case 'stone':
    default:
      scale = 1 / 8; crackDensity = 0.15; layerMix = 0.18; break;
  }
  // Base type nibble mappings
  const typeNibble = {
    basalt: 6,      // VOXEL_ROCK_BASALT
    granite: 7,     // VOXEL_ROCK_GRANITE
    limestone: 8,   // VOXEL_ROCK_LIMESTONE
    stone: 9        // VOXEL_STONE
  }[kind] || 9;
  const dark = 5; // VOXEL_BEDROCK for cracks/veins

  let out = '';
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      // Layered noise to get marbling/patches
      const n0 = noise(x * scale, y * scale);
      const n1 = noise(x * scale * 1.9 + 7.1, y * scale * 1.7 - 3.4);
      const n2 = noise(x * scale * 3.1 - 11.3, y * scale * 2.9 + 5.6);
      let c = 0.6 * n0 + layerMix * n1 + (1 - 0.6 - layerMix) * n2;
      // Subtle bias by kind
      if (kind === 'granite') c = Math.pow(c, 0.85);
      if (kind === 'limestone') c = Math.pow(c, 1.15);

      // Crack mask from high-frequency thresholded noise
      const crack = noise(x * (1 / 2), y * (1 / 2));
      const crackOn = crack > (1 - crackDensity);

      if (crackOn) {
        out += voxelNibble(dark);
        continue;
      }

      // Color variation via palette mixing per kind
      // Use a decorrelated noise for tone selection
      const tn = noise(x * (scale * 2.7) + 23.5, y * (scale * 2.3) - 11.1);
      let vType = typeNibble;
      if (kind === 'basalt') {
        // 70% basalt, 15% granite, 10% stone, 5% peppered dark
        if (tn < 0.05) vType = dark;         // deep pepper
        else if (tn < 0.20) vType = 7;       // granite flecks
        else if (tn < 0.30) vType = 9;       // lighter stone flecks
        else vType = 6;                      // basalt
      } else if (kind === 'granite') {
        // 70% granite, 15% limestone, 10% stone, 5% basalt
        if (tn < 0.05) vType = 6;            // occasional darker basalt pixel
        else if (tn < 0.20) vType = 8;       // limestone highlights
        else if (tn < 0.30) vType = 9;       // neutral stone
        else vType = 7;                      // granite
      } else if (kind === 'limestone') {
        // 70% limestone, 15% stone, 10% granite, 5% basalt
        if (tn < 0.05) vType = 6;            // rare dark pepper
        else if (tn < 0.20) vType = 9;       // stone veins
        else if (tn < 0.30) vType = 7;       // granite flecks
        else vType = 8;                      // limestone base
      } else {
        // stone: 70% stone, 15% granite, 10% limestone, 5% basalt
        if (tn < 0.05) vType = 6;            // dark pepper
        else if (tn < 0.20) vType = 7;       // granite
        else if (tn < 0.30) vType = 8;       // limestone
        else vType = 9;                      // stone
      }

      out += voxelNibble(vType);
    }
  }
  return out;
}

function main () {
  const outDir = __dirname;
  const w = 32, h = 32, d = 1;
  // Human scale: consider the Z-axis (depth) of the human model as the vertical height in voxels
  // (consistent with other worlds). We target a 32px-tall texture tile and apply symmetric padding
  // so the ~30-voxel-tall human fits in 32px without rescaling content.
  (function reportHumanScale () {
    const humanFile = 'human.world';
    let humanWidth = 0, humanHeight = 0, humanDepth = 0;
    try {
      const s = fs.readFileSync(path.join(outDir, humanFile), 'utf8');
      humanWidth = parseInt(s.slice(4, 12), 16);
      humanHeight = parseInt(s.slice(12, 20), 16);
      humanDepth = parseInt(s.slice(20, 28), 16);
    } catch (e) {
      // Fallback if not present; assume ~30 vox tall on Z as per convention
      humanDepth = 30;
    }
    // Use Z as height in voxels
    const humanVoxTall = (humanDepth || 30);
    const entityPxHeight = 32; // sprite tile height in pixels
    // Content occupies humanVoxTall pixels (1px per voxel), pad to 32
    const contentPxHeight = Math.min(entityPxHeight, humanVoxTall);
    const missing = Math.max(0, entityPxHeight - contentPxHeight);
    const padTopPx = Math.floor(missing / 2);
    const padBottomPx = missing - padTopPx;
    // Derived proportion: how many human-heights per tile
    const humanHeightsPerTile = entityPxHeight / Math.max(1, humanVoxTall);
    // Physical scale: assume human is 1.8 meters tall (override via HUMAN_METERS env)
    const humanMeters = Number.isFinite(Number(process.env.HUMAN_METERS)) ? Number(process.env.HUMAN_METERS) : 1.8;
    const metersPerVoxel = humanMeters / Math.max(1, humanVoxTall);
    const metersPerPixel = metersPerVoxel; // 1 px maps to 1 voxel layer unit vertically for content
    const tileMetersHeight = entityPxHeight * metersPerPixel;
    const contentMetersHeight = contentPxHeight * metersPerPixel;
    globalThis.gHumanScaleInfo = {
      entityPxHeight,
      humanModelVoxTall: humanVoxTall,
      contentPxHeight,
      padTopPx,
      padBottomPx,
      humanHeightsPerTile,
      humanMeters,
      metersPerVoxel,
      metersPerPixel,
      tileMetersHeight,
      contentMetersHeight
    };
    console.log(
      `HumanScale: humanZ=${humanVoxTall} vox ≈ ${humanMeters.toFixed(3)} m → tile=32px (content=${contentPxHeight}px, pad=${padTopPx}+${padBottomPx}), scale=${metersPerPixel.toFixed(4)} m/px`
    );
  })();

  function buildLayerLog (layerName, meta = {}) {
    const info = globalThis.gHumanScaleInfo || {};
    const category =
      /grass/i.test(layerName) ? 'grass' :
      /trunk/i.test(layerName) ? 'wood_bark' :
      /rock/i.test(layerName) ? 'rock' :
      /sphere/i.test(layerName) ? 'sphere' : 'generic';
    // Generate top-down 32px texture using full 255-color palette
    let pixelsHex = '';
    if (category === 'grass') {
      pixelsHex = generateTopDownTextureGrassHex24(w, h);
    } else if (category === 'wood_bark') {
      const species = meta && meta.species ? meta.species : 'oak';
      pixelsHex = generateTopDownTextureTrunkHex24(w, h, species);
    } else if (category === 'rock') {
      const kind = meta && meta.rockKind ? meta.rockKind : 'stone';
      pixelsHex = generateTopDownTextureRockHex24(w, h, kind);
    } else if (category === 'sphere') {
      pixelsHex = generateTopDownTextureSphereHex24(w, h);
    }
    const out = {
      metaVersion: 1,
      asTexture: true,
      layerName,
      category,
      tilePx: { width: w, height: h },
      tileMeters: { width: (w * (info.metersPerPixel || 0)).toFixed(6), height: (h * (info.metersPerPixel || 0)).toFixed(6) },
      topDownTexture: {
        description: 'Top-down 32px render of visible voxels',
        encoding: 'hex24',
        width: w,
        height: h,
        metersPerPixel: info.metersPerPixel || 0,
        usesPalette255: true,
        pixelsHex
      },
      humanRef: {
        voxHeightZ: info.humanModelVoxTall,
        contentPxHeight: info.contentPxHeight,
        padTopPx: info.padTopPx,
        padBottomPx: info.padBottomPx,
        humanHeightsPerTile: info.humanHeightsPerTile,
        humanMeters: info.humanMeters,
        metersPerVoxel: info.metersPerVoxel,
        metersPerPixel: info.metersPerPixel,
        tileMetersHeight: info.tileMetersHeight,
        contentMetersHeight: info.contentMetersHeight
      },
      hints: Object.assign({}, meta, { perType: true })
    };
    return JSON.stringify(out);
  }

  const sphere = generateTopDownTextureSphereHex24(w, h);
  saveWorld(
    path.join(outDir, 'layer_sphere.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_sphere', { shape: 'disc', radiusPx: Math.round(Math.min(w, h) * 0.45) })
  );

  const grassHex = generateTopDownTextureGrassHex24(w, h);
  saveWorld(
    path.join(outDir, 'layer_grass.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_grass', { usage: 'terrain_top', notes: 'ordered_dithered_green' })
  );

  const trunkHex = generateTopDownTextureTrunkHex24(w, h, 'oak');
  saveWorld(
    path.join(outDir, 'layer_tree_trunk.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_tree_trunk', { species: 'generic', stripeCount: 6 })
  );

  // Wood species variants
  saveWorld(
    path.join(outDir, 'layer_tree_trunk_oak.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_tree_trunk_oak', { species: 'oak', stripeCount: 6 })
  );
  saveWorld(
    path.join(outDir, 'layer_tree_trunk_birch.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_tree_trunk_birch', { species: 'birch', stripeCount: 4 })
  );
  saveWorld(
    path.join(outDir, 'layer_tree_trunk_pine.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_tree_trunk_pine', { species: 'pine', stripeCount: 8 })
  );

  // Rock variants
  saveWorld(
    path.join(outDir, 'layer_rock_basalt.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_rock_basalt', { rockKind: 'basalt' })
  );
  saveWorld(
    path.join(outDir, 'layer_rock_granite.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_rock_granite', { rockKind: 'granite' })
  );
  saveWorld(
    path.join(outDir, 'layer_rock_limestone.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_rock_limestone', { rockKind: 'limestone' })
  );
  saveWorld(
    path.join(outDir, 'layer_rock_stone.world'),
    w, h, d,
    '0'.repeat(w * h),
    buildLayerLog('layer_rock_stone', { rockKind: 'stone' })
  );

  // Minotaur texture (full 255-color top-down + voxel mask for preview)
  const minotaurHex = generateTopDownTextureMinotaurHex24(w, h);
  const minotaurMask = '0'.repeat(w * h);
  saveWorld(
    path.join(outDir, 'layer_minotaur.world'),
    w, h, d,
    minotaurMask,
    buildLayerLog('layer_minotaur', { creature: 'minotaur' })
  );

  // MAGMA animated texture: produce N frames (kept in separate *_anim file), and also a static layer_magma (32x32x1) for editor per-type loader
  const NUM_FRAMES = 8;
  const magmaFrames = generateMagmaAnimFramesHex24(w, h, NUM_FRAMES);
  // Static magma layer as voxel-types (major MAGMA with cracks as BEDROCK) for 32x32x1 loader
  (function writeStaticMagmaLayer () {
    let vox = '';
    const noise = makePerlin2D('layer_magma:static');
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const cracks = noise(x * 0.35, y * 0.35);
        const t = (cracks > 0.82) ? 5 /* BEDROCK */ : 26 /* MAGMA */;
        vox += voxelNibble(t);
      }
    }
    saveWorld(path.join(outDir, 'layer_magma.world'), w, h, 1, vox, buildLayerLog('layer_magma', { animated: false }));
  })();
  // Animated frames stored in a sidecar world for tooling that understands it
  (function writeMagmaAnimFrames () {
    const info = globalThis.gHumanScaleInfo || {};
    const magmaVoxAnim = '0'.repeat(w * h * NUM_FRAMES);
    const magmaLogAnim = JSON.stringify({
      metaVersion: 1,
      asTexture: true,
      layerName: 'layer_magma_anim',
      category: 'magma',
      tilePx: { width: w, height: h },
      tileMeters: { width: (w * (info.metersPerPixel || 0)).toFixed(6), height: (h * (info.metersPerPixel || 0)).toFixed(6) },
      animatedTexture: {
        description: 'Top-down 32px animated magma sheen; Z indexes frames over time',
        encoding: 'hex24', width: w, height: h, frames: NUM_FRAMES,
        metersPerPixel: info.metersPerPixel || 0, usesPalette255: true,
        pixelsHexFrames: magmaFrames
      },
      humanRef: {
        voxHeightZ: info.humanModelVoxTall, contentPxHeight: info.contentPxHeight,
        padTopPx: info.padTopPx, padBottomPx: info.padBottomPx,
        humanHeightsPerTile: info.humanHeightsPerTile, humanMeters: info.humanMeters,
        metersPerVoxel: info.metersPerVoxel, metersPerPixel: info.metersPerPixel,
        tileMetersHeight: info.tileMetersHeight, contentMetersHeight: info.contentMetersHeight
      }
    });
    saveWorld(path.join(outDir, 'layer_magma_anim.world'), w, h, NUM_FRAMES, magmaVoxAnim, magmaLogAnim);
  })();

  // Additional per-voxel-type layers for the editor's per-type loader (32x32x1)
  function writeTypeLayer(name, generator) {
    const vox = generator(w, h);
    saveWorld(path.join(outDir, `layer_${name}.world`), w, h, 1, vox, buildLayerLog(`layer_${name}`, { perType: true }));
  }
  const makeNoise = (s) => makePerlin2D('layer:' + s);
  const genSolid = (typeNibble) => (W, H) => {
    let out = '';
    for (let i = 0; i < W * H; i++) out += voxelNibble(typeNibble);
    return out;
  };
  const genRockMix = (primary) => (W, H) => {
    const n = makeNoise('rock:' + primary);
    const mapping = {
      basalt: [6, 7, 9, 5],
      granite: [7, 8, 9, 6],
      limestone: [8, 9, 7, 6],
      stone: [9, 7, 8, 6]
    }[primary];
    let out = '';
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        const v = n(x * 0.2, y * 0.2);
        const t = (v < 0.05) ? mapping[3] : (v < 0.20) ? mapping[1] : (v < 0.35) ? mapping[2] : mapping[0];
        out += voxelNibble(t);
      }
    }
    return out;
  };
  // Per-type layer writers now use full-color topDownTexture in metadata; voxel buffer can stay blank (AIR)
  const genGrass = () => (W, H) => '0'.repeat(W * H);
  const genLeaves = (variant) => (W, H) => '0'.repeat(W * H);
  const genWood = (variant) => (W, H) => '0'.repeat(W * H);
  const genSoil = (kind) => (W, H) => '0'.repeat(W * H);
  // Write per-type tiles (voxels unused), rely on topDownTexture for colors
  writeTypeLayer('grass', genGrass());
  writeTypeLayer('rock_basalt', (W,H)=>'0'.repeat(W*H));
  writeTypeLayer('rock_granite', (W,H)=>'0'.repeat(W*H));
  writeTypeLayer('rock_limestone', (W,H)=>'0'.repeat(W*H));
  writeTypeLayer('stone', (W,H)=>'0'.repeat(W*H));
  writeTypeLayer('wood', genWood('generic'));
  writeTypeLayer('wood_oak', genWood('oak'));
  writeTypeLayer('wood_birch', genWood('birch'));
  writeTypeLayer('wood_pine', genWood('pine'));
  writeTypeLayer('leaves', genLeaves('generic'));
  writeTypeLayer('leaves_oak', genLeaves('oak'));
  writeTypeLayer('leaves_birch', genLeaves('birch'));
  writeTypeLayer('leaves_pine', genLeaves('pine'));
  writeTypeLayer('sand', (W,H)=>'0'.repeat(W*H));
  writeTypeLayer('bedrock', (W,H)=>'0'.repeat(W*H));
  writeTypeLayer('water', (W,H)=>'0'.repeat(W*H));
  writeTypeLayer('magma', (W,H)=>'0'.repeat(W*H));
  writeTypeLayer('dirt', genSoil('dirt'));
  writeTypeLayer('dirt_clay', genSoil('dirt_clay'));
  writeTypeLayer('dirt_loam', genSoil('dirt_loam'));
  writeTypeLayer('dirt_silt', genSoil('dirt_silt'));
  writeTypeLayer('soil', genSoil('soil'));

  // ---------------------------
  // Report: estimated texture heights for non-layer .world files
  // ---------------------------
  (function reportOtherWorldsEstimatedHeights () {
    const info = globalThis.gHumanScaleInfo || {};
    const mpp = info.metersPerPixel || 0.06;
    const mpv = info.metersPerVoxel || mpp;
    const baseDir = outDir;
    function* walk (dir) {
      const entries = fs.readdirSync(dir, { withFileTypes: true });
      for (const e of entries) {
        const fp = path.join(dir, e.name);
        if (e.isDirectory()) {
          yield* walk(fp);
        } else if (e.isFile() && e.name.endsWith('.world')) {
          yield fp;
        }
      }
    }
    const worlds = [];
    for (const filePath of walk(baseDir)) {
      const name = path.basename(filePath);
      if (/^layer_.*\.world$/i.test(name)) continue; // skip layer files
      try {
        const s = fs.readFileSync(filePath, 'utf8');
        const W = parseInt(s.slice(4, 12), 16) || 0;
        const H = parseInt(s.slice(12, 20), 16) || 0;
        const D = parseInt(s.slice(20, 28), 16) || 0; // Z is height
        const estPx = D; // 1 px per voxel at this scale
        const heightMeters = D * mpv;
        worlds.push({ name, filePath, W, H, D, estPx, heightMeters });
      } catch (e) {
        // ignore
      }
    }
    worlds.sort((a, b) => a.name.localeCompare(b.name));
    if (worlds.length) {
      const hdr = colorize('Other models: estimated voxel texture heights', ANSI.fg.cyan) +
        ` ${ANSI.dim}(scale=${mpp.toFixed(4)} m/px)${ANSI.reset}`;
      console.log(hdr);
      for (const wld of worlds) {
        const line = `${colorize('·', ANSI.fg.gray)} ${colorize(wld.name, ANSI.fg.white)} ${ANSI.dim}` +
          `(Z=${wld.D} vox → ~${wld.estPx}px tall, ${formatMeters(wld.heightMeters)} height)` +
          `${ANSI.reset}`;
        console.log(line);
      }
    }
  })();
}

if (require.main === module) {
  main();
}
