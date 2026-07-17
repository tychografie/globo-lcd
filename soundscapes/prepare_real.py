#!/usr/bin/env python3
"""Fetch curated BBC Sound Effects recordings (RemArc licence — personal use),
cut a clean segment, level-match, craft a seamless loop, encode 80kbps mono.
Overwrites out/*.mp3 (same names the firmware expects) + refreshes the QA
spectrograms. The procedural generator (generate.py) remains as fallback.
"""
import numpy as np
import os, subprocess, wave, sys
from generate import seamless, spectrogram, OUT, SR

# scape -> (BBC asset id, start_s, dur_s)  — starts skip slates/level-settling
PICKS = {
    "waves":   ("NHU05040122", 10, 80),  # Evening. Heavy, deep waves onto beach
    "rain":    ("NHU05061134",  8, 80),  # Rain falling on leaves in woodland
    "thunder": ("07038333",    15, 80),  # Thunderstorm, heavy rain, rumbling + claps
    "fire":    ("NHU05008080",  6, 80),  # Small fire close-up, clear cracks
    "wind":    ("07005205",    20, 80),  # Wind in trees
    "night":   ("NHU05079182", 12, 80),  # Night with crickets and frogs
    "birds":   ("NHU05008110",  5, 80),  # Dawn chorus in wet forest
    "train":   ("07061102",    30, 80),  # Railway carriage interior, Germany
}
RAW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "raw")
os.makedirs(RAW, exist_ok=True)

TARGET_RMS = 0.10   # ambience-level match across the set
PEAK_CAP   = 0.90

def run(name, pick):
    asset, start, dur = pick
    src = os.path.join(RAW, asset + ".mp3")
    if not os.path.exists(src):
        url = f"https://sound-effects-media.bbcrewind.co.uk/mp3/{asset}.mp3"
        subprocess.run(["curl", "-s", "--max-time", "120", "-o", src, url], check=True)
    wav = os.path.join(RAW, name + "_seg.wav")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-ss", str(start), "-t", str(dur),
                    "-i", src, "-ac", "1", "-ar", str(SR), wav], check=True)
    with wave.open(wav, "rb") as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768
    os.remove(wav)

    rms = np.sqrt(np.mean(x ** 2))
    x *= TARGET_RMS / (rms + 1e-12)
    peak = np.abs(x).max()
    if peak > PEAK_CAP:                    # soft-limit crest (thunder claps)
        x = np.tanh(x / peak * 2.2) * PEAK_CAP / np.tanh(2.2)

    x = seamless(x, fade_s=4.0)
    out_wav = os.path.join(RAW, name + "_loop.wav")
    with wave.open(out_wav, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())
    mp3 = os.path.join(OUT, name + ".mp3")
    subprocess.run(["lame", "--quiet", "-m", "m", "-b", "80", out_wav, mp3], check=True)
    os.remove(out_wav)
    spectrogram(name, x)
    print(f"{name:8s} {asset}  {os.path.getsize(mp3)/1e6:.2f} MB  rms->{TARGET_RMS}")

if __name__ == "__main__":
    for name in (sys.argv[1:] or PICKS):
        run(name, PICKS[name])
    print("done →", OUT)
