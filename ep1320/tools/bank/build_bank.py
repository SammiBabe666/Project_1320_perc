#!/usr/bin/env python3
"""(1320: from medieval v0.2.3, unchanged but for its paths: run it from the plugin folder as
    python3 tools/bank/build_bank.py "<ep1320 sounds>" ; it writes md_bank.h there and bank.json beside itself.)

Build the sample bank (md_bank.h) from the EP-1320 WAV files, for the kits in kits.py.

    python3 tools/build_bank.py "<folder with the EP-1320 WAVs>" [-o md_bank.h] [--report bank.json]

Per sound:
  1. read the WAV (16-bit mono or stereo, any rate; the EP-1320 exports are 48 kHz mono), sum to mono
  2. trim the silence before the hit (2 ms of lead kept) and cut the tail where it falls 36 dB below the peak, at most
     MAX_LEN seconds; the last 15 % (at least 10 ms) is faded out, so every sound ends at exactly zero
  3. pick the lowest of 11.025 / 16 / 22.05 kHz that keeps 99.5 % of the sound's energy below 0.45 x the rate, resample
  4. normalise the peak to -1 dBFS (at most +6 dB of boost)
  5. encode as IMA ADPCM (4 bits per sample) in independent 64-sample blocks: a block header holds the block's first
     sample exactly and the decoder step index, so the engine can start, jump or play backwards from any block.
     tools/adpcm_enc.c (compiled here with cc) searches each block for the least error, about 3 dB cleaner than the
     greedy encoder below; --fast uses the greedy one. The search takes a few minutes, on every core.
The bank must fit the plugin file: the Workshop accepts .knl files up to 512 KiB, code and screen included.
Copyright (C) 2026 Sammi (Project Medieval). GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE, NOTICE).
"""
import argparse, glob, json, os, subprocess, sys, tempfile, wave
from multiprocessing import Pool
from math import gcd
import numpy as np
from scipy.signal import resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)                                    # kits.py beside it (1320: tools/bank/)
from kits import KITS, MAX_LEN   # noqa: E402

BLOCK = 64                      # samples per ADPCM block (+1: block k decodes samples 64k .. 64k+64)
BLOCK_BYTES = 4 + BLOCK // 2    # header (int16 sample, uint8 index, pad) + 64 nibbles
BANK_LIMIT = int(os.environ.get("MD_BANK_LIMIT", 440 * 1024))        # leave ~70 KiB of the 512 KiB file for code, tables and the screen
RATES = (11025, 16000, 22050)
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
        5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
        27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def find_wav(folder, num):
    hits = [f for f in glob.glob(os.path.join(folder, '*.wav')) if os.path.basename(f)[:3] == f'{num:03d}']
    if len(hits) != 1:
        sys.exit(f'sound {num:03d}: expected one file starting with "{num:03d}" in {folder}, found {len(hits)}')
    return hits[0]


def read_wav(path):
    with wave.open(path) as w:
        if w.getsampwidth() != 2:
            sys.exit(f'{path}: only 16-bit WAV files are supported')
        x = np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(np.float64) / 32768
        x = x.reshape(-1, w.getnchannels()).mean(axis=1)
        return x, w.getframerate()


def prepare(x, sr):
    pk = np.abs(x).max()
    if pk <= 0:
        sys.exit('silent sound')
    on = int(np.argmax(np.abs(x) > pk * 0.01))                 # -40 dB: the hit
    x = x[max(0, on - int(0.002 * sr)):]
    hop = max(1, sr // 100)
    env = np.maximum.reduceat(np.abs(x), np.arange(0, len(x), hop))
    above = np.nonzero(env > pk * 10 ** (-36 / 20))[0]
    n = min(len(x), (above[-1] + 1) * hop, int(MAX_LEN * sr))
    x = x[:n].copy()
    nf = max(int(0.010 * sr), int(0.15 * n))
    x[-nf:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, nf))
    spec = np.abs(np.fft.rfft(x)) ** 2
    cum = np.cumsum(spec) / spec.sum()
    f995 = np.fft.rfftfreq(len(x), 1 / sr)[np.searchsorted(cum, 0.995)]
    rate = next((r for r in RATES if f995 < 0.45 * r), RATES[-1])
    g = gcd(rate, sr)
    y = resample_poly(x, rate // g, sr // g)
    y *= min(10 ** (-1 / 20) / np.abs(y).max(), 2.0)
    return np.clip(np.round(y * 32767), -32768, 32767).astype(np.int32), rate


def adpcm_decode_block(blk):
    """the engine's decoder (medieval.c md_decode), for checking: 36 bytes -> 65 samples"""
    pred = int.from_bytes(blk[0:2], 'little', signed=True)
    idx = blk[2]
    out = [pred]
    for i in range(BLOCK):
        n = (blk[4 + i // 2] >> (4 * (i & 1))) & 15
        st = STEP[idx]
        d = st >> 3
        if n & 1: d += st >> 2
        if n & 2: d += st >> 1
        if n & 4: d += st
        pred = pred - d if n & 8 else pred + d
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + INDEX[n & 7]))
        out.append(pred)
    return out


def adpcm_encode(y):
    """-> bytes; blocks are independent, the step index carries over from the encoder so no block starts coarse"""
    nb = max(1, -(-(len(y) - 1) // BLOCK))
    y = np.concatenate([y, np.zeros(nb * BLOCK + 1 - len(y), np.int32)])
    out, idx = bytearray(), 0
    for b in range(nb):
        seg = y[b * BLOCK:b * BLOCK + BLOCK + 1]
        pred = int(seg[0])
        hdr = pred.to_bytes(2, 'little', signed=True) + bytes([idx, 0])
        nib = []
        for s in seg[1:]:
            st = STEP[idx]
            diff = int(s) - pred
            n = 8 if diff < 0 else 0
            diff = abs(diff)
            d = st >> 3
            if diff >= st: n |= 4; diff -= st; d += st
            if diff >= st >> 1: n |= 2; diff -= st >> 1; d += st >> 1
            if diff >= st >> 2: n |= 1; d += st >> 2
            pred = pred - d if n & 8 else pred + d
            pred = max(-32768, min(32767, pred))
            idx = max(0, min(88, idx + INDEX[n & 7]))
            nib.append(n)
        out += hdr + bytes(nib[i] | (nib[i + 1] << 4) for i in range(0, BLOCK, 2))
    return bytes(out), nb


ENC = None


def adpcm_encode_search(y):
    """-> bytes, nb: the same blocks as adpcm_encode, from tools/adpcm_enc.c's search"""
    out = subprocess.run([ENC], input=np.asarray(y, '<i2').tobytes(), capture_output=True, check=True).stdout
    return out, len(out) // BLOCK_BYTES


def build_sound(job):
    folder, num, fast = job
    path = find_wav(folder, num)
    x, sr = read_wav(path)
    y, rate = prepare(x, sr)
    enc, nb = adpcm_encode(y) if fast else adpcm_encode_search(y)
    return path, y, rate, enc, nb


def c_array(name, ctype, vals, per=16):
    rows = [', '.join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
    return f'static const {ctype} {name}[{len(vals)}] = {{\n    ' + ',\n    '.join(rows) + '\n};\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder')
    ap.add_argument('-o', '--out', default=os.path.join(os.path.dirname(os.path.dirname(HERE)), 'md_bank.h'))
    ap.add_argument('--report', default=os.path.join(HERE, 'bank.json'))
    ap.add_argument('--fast', action='store_true', help='greedy encoder (seconds instead of minutes, ~3 dB noisier)')
    a = ap.parse_args()
    global ENC
    if not a.fast:
        ENC = os.path.join(tempfile.mkdtemp(), 'adpcm_enc')
        subprocess.run(['cc', '-O2', '-o', ENC, os.path.join(HERE, 'adpcm_enc.c')], check=True)
    nums = list(dict.fromkeys(num for _, keys in KITS for num, _ in keys))
    with Pool() as pool:
        built = dict(zip(nums, pool.map(build_sound, [(a.folder, n, a.fast) for n in nums])))

    sounds, index, data, report = [], {}, bytearray(), []
    kits = []
    for kname, keys in KITS:
        if len(keys) != 24:
            sys.exit(f'kit {kname}: {len(keys)} keys, need 24')
        row = []
        for key, (num, tune) in enumerate(keys):
            if num not in index:
                path, y, rate, enc, nb = built[num]
                blocks = [adpcm_decode_block(enc[b * BLOCK_BYTES:(b + 1) * BLOCK_BYTES]) for b in range(nb)]
                dec = [s for blk in blocks for s in blk[:BLOCK]] + [blocks[-1][BLOCK]]
                err = np.array(dec[:len(y)]) - y
                snr = 10 * np.log10((y.astype(float) ** 2).sum() / max(1, (err.astype(float) ** 2).sum()))
                index[num] = len(sounds)
                sounds.append(dict(block=len(data) // BLOCK_BYTES, len=len(y), inc=round(65536 * rate / 44100)))
                name = os.path.basename(path)[3:-4].strip()
                report.append(dict(num=num, name=name, kit=kname, key=key, rate=rate,
                                   seconds=round(len(y) / rate, 3), bytes=len(enc), snr_db=round(snr, 1)))
                data += enc
            row.append((index[num], tune))
        kits.append((kname, row))
    if len(data) > BANK_LIMIT:
        sys.exit(f'bank is {len(data) / 1024:.0f} KiB, over the {BANK_LIMIT // 1024} KiB budget: shorten MAX_LEN or '
                 'use fewer sounds')
    if len(sounds) > 255:
        sys.exit('more than 255 sounds')

    with open(a.out, 'w') as f:
        f.write('/* md_bank.h: the EP-1320 sample bank, generated by tools/build_bank.py from kits.py. Do not edit.\n'
                f' * {len(sounds)} sounds, {len(kits)} kits, {len(data)} bytes of IMA ADPCM in {BLOCK}-sample blocks.\n'
                ' * The audio is Teenage Engineering EP-1320 factory content exported by the owner of the device: see\n'
                ' * README.md before sharing a build that contains it. */\n')
        f.write(f'#define MD_SOUNDS {len(sounds)}\n#define MD_KITS {len(kits)}\n#define MD_BLOCK {BLOCK}\n'
                f'#define MD_BLOCK_BYTES {BLOCK_BYTES}\n\n')
        f.write('typedef struct { uint32_t block; uint16_t len; uint16_t inc; } md_sound_t;   '
                '/* first block, length (samples), rate / 44100 (Q16) */\n')
        f.write('typedef struct { uint8_t snd; int8_t tune; } md_key_t;                         '
                '/* sound, tune (semitones) */\n\n')
        f.write(f'static const md_sound_t md_sound[MD_SOUNDS] = {{\n' +
                ',\n'.join(f'    {{ {s["block"]}, {s["len"]}, {s["inc"]} }}' for s in sounds) + '\n};\n')
        f.write('static const md_key_t md_kit[MD_KITS][24] = {\n' +
                ',\n'.join(f'    /* {n} */ {{ ' + ', '.join(f'{{ {s}, {t} }}' for s, t in row) + ' }'
                           for n, row in kits) + '\n};\n')
        f.write(c_array('md_adpcm', 'uint8_t', list(data), 24))
    with open(a.report, 'w') as f:
        json.dump(dict(bytes=len(data), sounds=report), f, indent=1)
    print(f'{len(sounds)} sounds in {len(kits)} kits: {len(data) / 1024:.1f} KiB of ADPCM -> {a.out}')
    worst = min(report, key=lambda r: r['snr_db'])
    print(f'lowest ADPCM SNR: {worst["snr_db"]} dB ({worst["num"]:03d} {worst["name"]})')


if __name__ == '__main__':
    main()
