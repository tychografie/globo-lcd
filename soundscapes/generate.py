#!/usr/bin/env python3
"""GLOBO soundscape generator.

Synthesizes the eight ambience loops (44.1kHz mono, seamless ~87s loops),
encodes them to 80kbps MP3 with lame, and renders a spectrogram PNG per scape
for visual QA. Everything is procedural — no samples, no licensing.

  python3 generate.py            # writes ./out/*.mp3 + ./out/*_spec.png
  Upload:  for f in out/*.mp3; do curl -F "file=@$f" http://globo.local/api/upload; done
"""
import numpy as np
import os, subprocess, wave, sys

SR = 44100
DUR = 90.0
N = int(SR * DUR)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)
T = np.arange(N) / SR


# ── building blocks ──────────────────────────────────────
def spectral_noise(shape_fn, seed, n=N):
    """White noise shaped in the frequency domain — any bed in one pass."""
    rng = np.random.default_rng(seed)
    W = np.fft.rfft(rng.standard_normal(n))
    f = np.maximum(np.fft.rfftfreq(n, 1 / SR), 0.5)
    x = np.fft.irfft(W * shape_fn(f), n)
    return x / (np.abs(x).max() + 1e-12)

def lowpass(fc, k=4):      return lambda f: 1.0 / (1.0 + (f / fc) ** k)
def bandpass(fc, width):   return lambda f: np.exp(-0.5 * (np.log2(f / fc) / width) ** 2)
def slope(p):              return lambda f: f ** (-p)

def mul(*fns):
    return lambda f: np.prod([fn(f) for fn in fns], axis=0)

def wander(lo, hi, period_s, seed, n=N):
    """Slow random walk between lo..hi — mynoise-slider-like modulation."""
    rng = np.random.default_rng(seed)
    k = max(int(DUR / period_s) + 2, 4)
    keys = rng.uniform(lo, hi, k)
    keys[-1] = keys[0]                       # loops cleanly
    x = np.interp(np.linspace(0, k - 1, n), np.arange(k), keys)
    win = int(SR * period_s / 8) | 1
    ker = np.hanning(win); ker /= ker.sum()
    return np.convolve(x, ker, mode="same")

def place(buf, snippet, idx, gain=1.0):
    end = min(idx + len(snippet), len(buf))
    if end > idx:
        buf[idx:end] += snippet[: end - idx] * gain

def env_ad(n, attack_frac=0.1, curve=2.0):
    a = max(int(n * attack_frac), 1)
    e = np.ones(n)
    e[:a] = np.linspace(0, 1, a)
    e[a:] = np.linspace(1, 0, n - a) ** curve
    return e

def seamless(x, fade_s=3.0):
    """Crossfade tail into head, trim tail → loop point is inaudible."""
    F = int(SR * fade_s)
    w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, F))   # equal-ish power
    y = x[: len(x) - F].copy()
    y[:F] = x[:F] * w + x[len(x) - F:] * (1 - w)
    return y

def finish(name, x, gain=0.85):
    x = seamless(x)
    x = x / (np.abs(x).max() + 1e-12) * gain
    wav = os.path.join(OUT, name + ".wav")
    mp3 = os.path.join(OUT, name + ".mp3")
    with wave.open(wav, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())
    subprocess.run(["lame", "--quiet", "-m", "m", "-b", "80", wav, mp3], check=True)
    os.remove(wav)
    spectrogram(name, x)
    print(f"{name:8s} {os.path.getsize(mp3)/1e6:.2f} MB")

def spectrogram(name, x, fmax=9000):
    """Quick STFT → PPM → PNG (sips), log-ish color, for visual QA."""
    hop, win = 2048, 4096
    frames = (len(x) - win) // hop
    h = np.hanning(win)
    cols = min(frames, 900)
    step = max(frames // cols, 1)
    S = []
    for i in range(0, frames * hop, hop * step):
        seg = x[i:i + win]
        if len(seg) < win: break
        S.append(np.abs(np.fft.rfft(seg * h)))
    S = np.array(S).T                                   # freq x time
    fbins = int(fmax / (SR / 2) * S.shape[0])
    S = S[:fbins]
    S = np.log10(S + 1e-3)
    S = (S - S.min()) / (S.max() - S.min() + 1e-12)
    Simg = (S[::-1] * 255).astype(np.uint8)             # low freq at bottom
    hgt, wid = Simg.shape
    px = bytearray()
    for row in Simg:
        for v in row:
            px += bytes((v, int(v * 0.85), int(255 - v * 0.75) if v < 60 else v // 2))
    ppm = os.path.join(OUT, name + "_spec.ppm")
    with open(ppm, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (wid, hgt) + bytes(px))
    subprocess.run(["sips", "-s", "format", "png", ppm, "--out",
                    os.path.join(OUT, name + "_spec.png")], capture_output=True)
    os.remove(ppm)


# ── the scapes ───────────────────────────────────────────
def waves():
    swell = np.clip(np.sin(2 * np.pi * 0.055 * T) * 0.6 +
                    np.sin(2 * np.pi * 0.083 * T + 1.7) * 0.4, -1, 1)
    swell = (swell * 0.5 + 0.5) ** 1.6                     # long lulls, steep crests
    body = spectral_noise(mul(slope(0.9), lowpass(700)), 11)
    wash = spectral_noise(mul(bandpass(3200, 1.2), lowpass(8000)), 12)
    crest = np.clip(np.gradient(swell) * SR, 0, None)
    crest = np.convolve(crest, np.hanning(SR) / (np.hanning(SR).sum()), "same")
    crest /= crest.max() + 1e-12
    return body * (0.28 + 0.72 * swell) + wash * (0.05 + 0.5 * crest ** 1.3)

def rain():
    bed = spectral_noise(mul(slope(0.25), bandpass(1800, 2.2)), 21)
    x = bed * (0.55 + 0.06 * wander(-1, 1, 11, 22))
    rng = np.random.default_rng(23)
    tick = spectral_noise(bandpass(5200, 0.8), 24, n=256) * env_ad(256, 0.05, 3)
    for idx in rng.integers(0, N - 300, int(DUR * 26)):     # ~26 drops/s
        place(x, tick, int(idx), rng.uniform(0.05, 0.42))
    drip = spectral_noise(bandpass(900, 0.5), 25, n=2000) * env_ad(2000, 0.02, 4)
    for idx in rng.integers(0, N - 2200, int(DUR * 0.7)):
        place(x, drip, int(idx), rng.uniform(0.15, 0.3))
    return x

def thunder():
    x = rain() * 0.62
    rng = np.random.default_rng(31)
    t0 = 4.0
    while t0 < DUR - 12:
        L = rng.uniform(4, 8)
        n = int(SR * L)
        rum = spectral_noise(mul(slope(1.1), lowpass(140, 3)), int(t0 * 7) % 9999, n=n)
        e = env_ad(n, rng.uniform(0.04, 0.18), rng.uniform(1.6, 2.6))
        place(x, rum * e, int(t0 * SR), rng.uniform(0.5, 0.95))
        if rng.random() < 0.35:                              # double strike
            place(x, rum[: n // 2] * env_ad(n // 2, 0.06, 2), int((t0 + L * 0.55) * SR), 0.4)
        t0 += L + rng.uniform(6, 16)
    return x

def fire():
    bed = spectral_noise(mul(slope(1.0), lowpass(420, 3)), 41)
    flick = 0.55 + 0.25 * wander(-1, 1, 2.2, 42) + 0.2 * wander(-1, 1, 0.6, 43)
    x = bed * flick * 0.75
    hiss = spectral_noise(bandpass(2600, 1.4), 44) * 0.05
    x += hiss * (0.6 + 0.4 * wander(-1, 1, 3.0, 45))
    rng = np.random.default_rng(46)
    for idx in rng.integers(0, N - 700, int(DUR * 9)):       # crackles
        n = int(rng.uniform(60, 500))
        cr = spectral_noise(bandpass(rng.uniform(1400, 4200), 0.7), int(idx) % 9999, n=n)
        place(x, cr * env_ad(n, 0.02, 3.5), int(idx), rng.uniform(0.08, 0.5))
    for idx in rng.integers(0, N - 3000, int(DUR * 0.5)):    # pops
        n = 2400
        pop = spectral_noise(lowpass(300, 3), int(idx + 1) % 9999, n=n)
        place(x, pop * env_ad(n, 0.01, 4), int(idx), rng.uniform(0.3, 0.6))
    return x

def wind():
    gust = np.clip(0.45 + 0.4 * wander(-1, 1, 9, 51) + 0.25 * wander(-1, 1, 3.2, 52), 0.08, 1.1)
    x = np.zeros(N)
    for i, fc in enumerate((280, 550, 950, 1600)):           # wandering resonances
        band = spectral_noise(mul(bandpass(fc, 0.55), lowpass(2500)), 53 + i)
        x += band * np.clip(wander(-0.3, 1, 7 + 2 * i, 60 + i), 0, None)
    x = x / (np.abs(x).max() + 1e-12) * gust
    rustle = spectral_noise(bandpass(5200, 1.0), 58) * np.clip(gust - 0.45, 0, None) ** 2 * 1.7
    return x + rustle

def night():
    # A lusher bed than v1: warm LF air + a faint continuous insect shimmer,
    # so the named voices sit IN a night rather than over silence.
    bed = spectral_noise(mul(slope(1.0), lowpass(260, 3)), 61) * 0.55
    shimmer = spectral_noise(bandpass(5600, 0.35), 66) * 0.10
    x = bed * (0.7 + 0.3 * wander(-1, 1, 13, 62)) + shimmer * (0.8 + 0.2 * wander(-1, 1, 7, 67))
    rng = np.random.default_rng(63)
    # crickets: narrowband-noise chirps (soft, not sine-piercing), 3 layers
    for f0, rate, base in ((4300, 26, 64), (3800, 21, 65), (4700, 31, 68)):
        tone = spectral_noise(bandpass(f0, 0.12), base)     # continuous timbre
        t0 = rng.uniform(0, 1.2)
        while t0 < DUR:
            dur = rng.uniform(0.35, 0.8)
            n = int(SR * dur)
            i0 = int(t0 * SR)
            if i0 + n >= N: break
            tt = np.arange(n) / SR
            pulse = 0.5 - 0.5 * np.cos(np.clip(np.sin(2 * np.pi * rate * tt), 0, 1) * np.pi)
            ka, kd = max(n // 8, 1), max(n // 6, 1)
            e = np.ones(n); e[:ka] = np.linspace(0, 1, ka); e[n - kd:] = np.linspace(1, 0, kd)
            place(x, tone[i0:i0 + n] * pulse * e, i0, rng.uniform(0.28, 0.5))
            t0 += dur + rng.uniform(0.15, 0.8)
    # frogs: croak = short pulse train around 420Hz with a pitch drop
    t0 = 2.0
    while t0 < DUR:
        n = int(SR * 0.16)
        tt = np.arange(n) / SR
        f = 430 * (1 - 0.25 * tt / tt[-1]) * rng.uniform(0.85, 1.15)
        croak = np.sin(2 * np.pi * np.cumsum(f) / SR) * (np.sin(2 * np.pi * 34 * tt) > 0).astype(float)
        place(x, croak * env_ad(n, 0.15, 1.6), int(t0 * SR), rng.uniform(0.35, 0.65))
        if rng.random() < 0.5:                               # answer croak
            place(x, croak * env_ad(n, 0.15, 1.6), int((t0 + 0.35) * SR), rng.uniform(0.18, 0.35))
        t0 += rng.uniform(1.2, 4.5)
    return x

def birds():
    windbed = spectral_noise(mul(bandpass(500, 1.3), lowpass(1800)), 71) * 0.11
    x = windbed * (0.7 + 0.3 * wander(-1, 1, 12, 72))
    rng = np.random.default_rng(73)

    def chirp(f0, f1, dur, vib=0.0):
        n = int(SR * dur)
        tt = np.arange(n) / SR
        f = np.linspace(f0, f1, n) * (1 + vib * np.sin(2 * np.pi * 38 * tt))
        c = np.sin(2 * np.pi * np.cumsum(f) / SR)
        return c * env_ad(n, 0.25, 1.8)

    voices = [dict(base=rng.uniform(2100, 4600), d=rng.uniform(0.4, 1.0)) for _ in range(3)]
    t0 = 0.8
    while t0 < DUR:
        v = voices[rng.integers(0, len(voices))]
        pos = int(t0 * SR)
        for k in range(rng.integers(2, 6)):                  # a motif of chirps
            f0 = v["base"] * rng.uniform(0.85, 1.2)
            f1 = f0 * rng.uniform(0.75, 1.35)
            dur = rng.uniform(0.05, 0.22)
            c = chirp(f0, f1, dur, vib=rng.uniform(0, 0.06))
            place(x, c, pos, 0.24 * v["d"])
            place(x, c, pos + int(SR * 0.11), 0.055 * v["d"])   # soft echo
            pos += int(SR * (dur + rng.uniform(0.04, 0.18)))
        t0 += rng.uniform(0.7, 3.4)
    return x

def train():
    rumble = spectral_noise(mul(slope(1.15), lowpass(130, 3)), 81)
    x = rumble * (0.55 + 0.1 * wander(-1, 1, 5.5, 82))
    hiss = spectral_noise(bandpass(1400, 1.1), 83) * 0.09
    x += hiss
    rng = np.random.default_rng(84)
    # bogie hits: LF thump + mid knock + HF tick so the rhythm reads clearly
    thm   = spectral_noise(lowpass(240, 3), 85, n=1500) * env_ad(1500, 0.02, 3.2)
    knock = spectral_noise(bandpass(850, 0.6), 87, n=900) * env_ad(900, 0.015, 3.5)
    tick  = spectral_noise(bandpass(3200, 0.8), 86, n=350) * env_ad(350, 0.02, 3)
    t0 = 0.0
    period = 1.75                                            # bogie pattern period
    while t0 < DUR:
        for off in (0.0, 0.22, 0.86, 1.08):                  # da-dum ... da-dum
            idx = int((t0 + off + rng.uniform(-0.008, 0.008)) * SR)
            g = rng.uniform(0.8, 1.0)
            place(x, thm, idx, 1.15 * g)
            place(x, knock, idx, 0.5 * g)
            place(x, tick, idx, 0.22 * g)
        t0 += period
    for horn_t in (33.0, 68.5):                              # distant horn, twice
        n = int(SR * rng.uniform(2.2, 3.2))
        tt = np.arange(n) / SR
        fall = 1 - 0.012 * tt / tt[-1]
        tone = sum(np.sin(2 * np.pi * f * fall * np.cumsum(np.ones(n)) / SR +
                          0.25 * np.sin(2 * np.pi * 5.2 * tt))
                   for f in (311, 392, 466))
        e = np.minimum(np.linspace(0, 4, n), 1) * np.minimum(np.linspace(4, 0, n), 1)
        place(x, tone * e / 3, int(horn_t * SR), 0.3)
    return x


ALL = dict(waves=waves, rain=rain, thunder=thunder, fire=fire,
           wind=wind, night=night, birds=birds, train=train)

if __name__ == "__main__":
    names = sys.argv[1:] or list(ALL)
    for name in names:
        finish(name, ALL[name]())
    print("done →", OUT)
