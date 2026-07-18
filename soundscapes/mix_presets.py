#!/usr/bin/env python3
"""Build the ambience-mix beds: layered BBC recordings → 16kHz mono,
RMS-matched, seamless loop, headerless IMA-ADPCM (matches the firmware
decoder in globo_lcd.ino). ~360KB per 45s bed.

  python3 mix_presets.py           # builds out/mix_*.ima
"""
import numpy as np
import os, subprocess, wave, sys
from generate import seamless, OUT

SR_BED = 16000
BED_S  = 40
RAW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "raw")
os.makedirs(RAW, exist_ok=True)

# preset -> [(bbc_asset, start_s, gain), ...]  layered then normalized
PRESETS = {
    # jungle v2: a full menagerie (Tycho: "way more other animals")
    "jungle":   [("NHU05017128", 20, 1.0),   # rainforest dawn: insects + many birds
                 ("NHU05100001", 60, 0.70),  # distant howler monkeys
                 ("NHU05029076", 30, 0.32),  # macaw pair calls
                 ("07062008",    40, 0.45)], # Rwanda evening: tree frogs + insects
    "seaside":  [("07044003",    25, 1.0),   # gentle seawash + occasional gulls
                 ("07029153",    30, 0.25)], # seagull colony — present, not harsh
    "city":     [("07054142",    30, 1.0),   # Khatmandu street market: bells, horns, kids
                 ("07055081",    40, 0.25)], # Cairo old-town continuous hooters (taxi energy)
    "barista":  [("07052024",    20, 1.0)],  # Greek cafe, chatter + coffee machine
    "rainy":    [("NHU05061134",  8, 1.0),   # rain on leaves
                 ("07038333",    60, 0.5)],  # distant thunderstorm
    "garden":   [("07030052",    30, 1.0)],  # country garden: birdsong, distant traffic
    # street v2: 1976 London market — voices and commerce, zero birds (Tycho)
    "street":   [("07028153",    40, 1.0)],
    # Catalog consolidation: the standalone scapes become beds too — one list,
    # usable under the radio AND alone (via the silence carrier).
    # waves v4: sails carry the ship, timber barely whispers (Tycho: "more of
    # the sails and less of the wood squaking. the water is fine")
    "waves":    [("NHU05040122", 10, 1.0),   # deep evening surf
                 ("07054094",    20, 0.10),  # timber creaking — a whisper
                 ("07034026",     2, 0.85)], # sails flapping — the lead voice
    "thunder":  [("07038333",    15, 1.0)],  # thunderstorm, rumbles + claps
    # wind v4: pure rattling branches — the squeaking-trees layer cut (Tycho)
    "wind":     [("NHU05073124", 10, 1.0)],  # wind rattling branches
    "train":    [("07061102",    30, 1.0)],  # railway carriage interior
}

IMA_STEPS = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,
 66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
 494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
 2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
 10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
IMA_ADJ = [-1,-1,-1,-1,2,4,6,8]

def ima_encode(pcm16):
    """Headerless IMA-ADPCM, 2 samples/byte, low nibble first (matches device)."""
    pred, idx = 0, 0
    nibbles = []
    for s in pcm16:
        step = IMA_STEPS[idx]
        diff = int(s) - pred
        nib = 0
        if diff < 0: nib = 8; diff = -diff
        if diff >= step:      nib |= 4; diff -= step
        if diff >= step >> 1: nib |= 2; diff -= step >> 1
        if diff >= step >> 2: nib |= 1; diff -= step >> 2
        # reconstruct exactly like the decoder
        d = step >> 3
        if nib & 1: d += step >> 2
        if nib & 2: d += step >> 1
        if nib & 4: d += step
        pred += -d if nib & 8 else d
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IMA_ADJ[nib & 7]))
        nibbles.append(nib)
    if len(nibbles) & 1: nibbles.append(0)
    b = bytearray()
    for i in range(0, len(nibbles), 2):
        b.append(nibbles[i] | (nibbles[i + 1] << 4))
    return bytes(b)

def segment(asset, start):
    src = os.path.join(RAW, asset + ".mp3")
    if not os.path.exists(src):
        url = f"https://sound-effects-media.bbcrewind.co.uk/mp3/{asset}.mp3"
        subprocess.run(["curl", "-s", "--max-time", "120", "-o", src, url], check=True)
    wav = os.path.join(RAW, "_seg.wav")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-ss", str(start),
                    "-t", str(BED_S + 8), "-i", src, "-ac", "1", "-ar", str(SR_BED), wav], check=True)
    with wave.open(wav, "rb") as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768
    os.remove(wav)
    return x[: (BED_S + 8) * SR_BED]

def build(name, layers):
    n = (BED_S + 8) * SR_BED
    mix = np.zeros(n)
    for asset, start, gain in layers:
        seg = segment(asset, start)
        m = min(len(seg), n)
        rms = np.sqrt(np.mean(seg ** 2)) + 1e-12
        mix[:m] += seg[:m] / rms * gain          # per-layer RMS-relative gains
    mix = mix / (np.sqrt(np.mean(mix ** 2)) + 1e-12) * 0.18   # bed target RMS
    peak = np.abs(mix).max()
    if peak > 0.92:
        mix = np.tanh(mix / peak * 2.0) * 0.92 / np.tanh(2.0)
    # seamless() fades over 3s and trims — feed BED_S + fade
    global_fade = 3.0
    mix = mix[: int((BED_S + global_fade) * SR_BED)]
    looped = seamless_bed(mix, global_fade)
    data = ima_encode(np.round(looped * 32767).astype(np.int32))
    path = os.path.join(OUT, f"mix_{name}.ima")
    with open(path, "wb") as f:
        f.write(data)
    print(f"mix_{name:9s} {len(data)/1e3:.0f} KB  ({len(looped)/SR_BED:.1f}s)")

def seamless_bed(x, fade_s):
    F = int(SR_BED * fade_s)
    w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, F))
    y = x[: len(x) - F].copy()
    y[:F] = x[:F] * w + x[len(x) - F:] * (1 - w)
    return y

if __name__ == "__main__":
    for name in (sys.argv[1:] or PRESETS):
        build(name, PRESETS[name])
    print("done →", OUT)
