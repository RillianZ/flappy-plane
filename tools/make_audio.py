# Synthesizes all Flappy Plane v3 audio from scratch (no samples, no licensing issues).
import numpy as np, wave, subprocess, os
SR = 44100
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'resources')
rng = np.random.default_rng(7)

def save_wav(name, x, stereo=False):
    x = np.clip(x, -1, 1)
    data = (x * 32767).astype(np.int16)
    with wave.open(os.path.join(OUT, name), 'wb') as w:
        w.setnchannels(2 if stereo else 1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(data.tobytes())

def env(n, a=0.005, d=0.2, s=0.0, total=None):
    t = np.arange(n) / SR
    e = np.minimum(t / max(a, 1e-4), 1.0) * np.exp(-np.maximum(t - a, 0) / max(d, 1e-4))
    return e + s
def saw(f, t): return 2 * ((t * f) % 1.0) - 1
def sq(f, t, duty=0.5): return np.where((t * f) % 1.0 < duty, 1.0, -1.0)
def lowpass(x, cutoff):
    # one-pole lowpass, cutoff can be array
    a = np.exp(-2 * np.pi * np.asarray(cutoff) / SR) * np.ones_like(x)
    y = np.zeros_like(x); prev = 0.0
    for i in range(len(x)):
        prev = (1 - a[i]) * x[i] + a[i] * prev; y[i] = prev
    return y

# --- flap: airy filtered-noise swoosh with rising tone (paper plane)
n = int(0.22 * SR); t = np.arange(n) / SR
noise = rng.standard_normal(n)
sw = lowpass(noise, np.linspace(800, 4500, n)) * env(n, 0.01, 0.07)
tone = np.sin(2*np.pi*np.cumsum(np.linspace(500, 1100, n))/SR) * env(n, 0.003, 0.05) * 0.25
save_wav('flap.wav', (sw * 1.6 + tone) * 0.7)

# --- jet thrust: detuned saw burst, sweeping filter
n = int(0.28 * SR); t = np.arange(n) / SR
f = np.linspace(110, 220, n)
x = (saw(1, np.cumsum(f)/SR) + saw(1, np.cumsum(f*1.01)/SR)) * 0.5 + rng.standard_normal(n) * 0.35
x = lowpass(x, np.linspace(5000, 900, n)) * env(n, 0.005, 0.12)
save_wav('thrust.wav', x * 1.1)

# --- score: bright two-note arpeggio blip
n = int(0.18 * SR); t = np.arange(n) / SR
f = np.where(t < 0.06, 1318.5, 1975.5)
x = sq(1, np.cumsum(f)/SR, 0.25) * 0.35 * env(n, 0.002, 0.08)
save_wav('score.wav', lowpass(x, 7000))

# --- crash / derez: falling saw + crunchy bitcrushed noise
n = int(0.9 * SR); t = np.arange(n) / SR
f = 440 * np.exp(-t * 4) + 40
x = saw(1, np.cumsum(f)/SR) * 0.5 + rng.standard_normal(n) * np.exp(-t * 6) * 0.6
x = np.round(x * 8) / 8   # bitcrush
x = lowpass(x, 3500) * env(n, 0.002, 0.35)
save_wav('crash.wav', x * 0.9)

# --- ui select blip
n = int(0.08 * SR); t = np.arange(n) / SR
x = sq(880, t, 0.5) * 0.25 * env(n, 0.001, 0.03)
save_wav('select.wav', lowpass(x, 6000))

# --- music: two looping synthwave tracks (bar-exact loops)
def track(bpm, root, prog, bars, bright, seed):
    r = np.random.default_rng(seed)
    beat = 60 / bpm; bar = beat * 4; n = int(round(bars * bar * SR)); t = np.arange(n) / SR
    L = np.zeros(n); R = np.zeros(n)
    def note(f): return root * 2 ** (f / 12)
    step = beat / 4  # 16ths
    for b in range(bars):
        chord = prog[b % len(prog)]
        # bass: 8th-note pulse on chord root
        for k in range(8):
            st = int((b * bar + k * beat / 2) * SR); ln = int(beat / 2 * 0.9 * SR)
            tt = np.arange(ln) / SR; f = note(chord[0] - 12)
            s = saw(f, tt) * np.exp(-tt * 6) * 0.32
            L[st:st+ln] += s; R[st:st+ln] += s
        # arpeggio 16ths
        for k in range(16):
            st = int((b * bar + k * step) * SR); ln = int(step * 0.95 * SR)
            tt = np.arange(ln) / SR; f = note(chord[k % len(chord)] + 12 * (1 + (k // 8) % 2))
            s = (sq(f, tt, 0.3) * 0.5 + saw(f * 1.005, tt) * 0.5) * np.exp(-tt * 14) * 0.11 * bright
            pan = 0.5 + 0.35 * np.sin(k)
            L[st:st+ln] += s * (1 - pan); R[st:st+ln] += s * pan
        # pad
        st = int(b * bar * SR); ln = int(bar * SR); tt = np.arange(ln) / SR
        pad = sum(np.sin(2 * np.pi * note(c) * tt + i) for i, c in enumerate(chord)) / len(chord)
        pad *= (np.minimum(tt / 0.4, 1) * np.minimum((bar - tt) / 0.4, 1)) * 0.09
        L[st:st+ln] += pad; R[st:st+ln] += pad * 0.9
        # drums
        for k in range(4):
            st = int((b * bar + k * beat) * SR); ln = int(0.25 * SR); tt = np.arange(ln) / SR
            kick = np.sin(2 * np.pi * np.cumsum(60 + 120 * np.exp(-tt * 30)) / SR) * np.exp(-tt * 12) * 0.5
            L[st:st+ln] += kick; R[st:st+ln] += kick
            if k % 2 == 1:
                sn = r.standard_normal(ln) * np.exp(-tt * 18) * 0.18
                L[st:st+ln] += sn; R[st:st+ln] += sn
        for k in range(8):
            st = int((b * bar + k * beat / 2 + beat / 4) * SR); ln = int(0.05 * SR); tt = np.arange(ln) / SR
            hh = np.diff(r.standard_normal(ln + 1)) * np.exp(-tt * 60) * 0.06
            L[st:st+ln] += hh * 0.8; R[st:st+ln] += hh
    L = lowpass(L, 9000); R = lowpass(R, 9000)
    m = max(np.abs(L).max(), np.abs(R).max())
    return np.stack([L, R], 1) / m * 0.8

for name, args in [('music_paper', (100, 110.0, [[0, 3, 7, 10], [-4, 0, 3, 7], [-2, 2, 5, 9], [-5, -2, 2, 5]], 8, 0.9, 1)),
                   ('music_jet',   (128, 98.0,  [[0, 3, 7, 12], [5, 8, 12, 15], [3, 7, 10, 14], [-2, 2, 5, 10]], 8, 1.2, 2))]:
    x = track(*args)
    save_wav(name + '.wav', x.reshape(-1), stereo=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', os.path.join(OUT, name + '.wav'), '-c:a', 'libvorbis', '-q:a', '4', os.path.join(OUT, name + '.ogg')], check=True)
    os.remove(os.path.join(OUT, name + '.wav'))
print(sorted(os.listdir(OUT)))
