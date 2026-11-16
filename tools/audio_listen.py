"""Escucha con el microfono del PC si la placa realmente emite sonido.

Uso: python tools/audio_listen.py COM11 [indice_microfono]

Para cada sonido del catalogo graba el microfono, lo compara con el ruido de
fondo y comprueba que hay energia en las frecuencias esperadas (las notas que
sintetiza audio.c). Requiere: pip install sounddevice numpy pyserial
"""
import re
import sys
import time

import numpy as np
import serial
import sounddevice as sd

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM11"
MIC = int(sys.argv[2]) if len(sys.argv) > 2 else None
FS = 44100

# id -> (nombre, frecuencias dominantes esperadas en Hz, duracion aprox. s)
SOUNDS = {
    0: ("TAP", [1800, 1200], 0.05),
    2: ("SUBIR (sube)", [660, 880, 1320], 0.4),
    3: ("BAJAR (baja)", [1320, 880, 660], 0.4),
    7: ("LLEGADA", [988, 740], 0.6),
    11: ("LIMITE", [1175, 988, 784, 392], 0.6),
    16: ("OK guardado", [523, 659, 784, 1047], 0.5),
    22: ("ARRANQUE", [523, 659, 784, 1047], 0.7),
}

ser = serial.Serial(PORT, 115200, timeout=0.1)


def cmd(c):
    ser.write(("\n" + c + "\n").encode())


def band_energy(sig, f, bw=40):
    spec = np.abs(np.fft.rfft(sig * np.hanning(len(sig)))) ** 2
    freqs = np.fft.rfftfreq(len(sig), 1 / FS)
    m = (freqs > f - bw) & (freqs < f + bw)
    return spec[m].sum()


def record(seconds, trigger=None):
    rec = sd.rec(int(seconds * FS), samplerate=FS, channels=1, dtype="float32",
                 device=MIC)
    if trigger:
        time.sleep(0.4)
        trigger()
    sd.wait()
    return rec[:, 0]


cmd("vol 100")
time.sleep(0.3)
noise = record(1.5)
noise_rms = float(np.sqrt(np.mean(noise ** 2))) + 1e-9
print(f"ruido de fondo rms={noise_rms:.5f}")

ok_all = True
for sid, (name, freqs, dur) in SOUNDS.items():
    rec = record(dur + 1.2, lambda: cmd(f"sound {sid}"))
    # ventana con mayor energia
    win = int(FS * max(dur, 0.1))
    energies = [np.sqrt(np.mean(rec[i:i + win] ** 2)) for i in range(0, len(rec) - win, win // 4)]
    peak = max(energies)
    ratio = peak / noise_rms
    i0 = int(np.argmax(energies)) * (win // 4)
    seg = rec[i0:i0 + win]
    base = [band_energy(noise[:len(seg)] if len(noise) >= len(seg) else np.pad(noise, (0, len(seg) - len(noise))), f)
            for f in freqs]
    got = [band_energy(seg, f) for f in freqs]
    hits = sum(1 for g, b in zip(got, base) if g > 8 * b)
    heard = ratio > 2.0 and hits >= max(1, len(freqs) // 2)
    ok_all &= heard
    print(f"{'OIDO ' if heard else 'NO   '} {name:14s} nivel x{ratio:5.1f} sobre el ruido, "
          f"{hits}/{len(freqs)} notas detectadas")

cmd("vol 70")
sys.exit(0 if ok_all else 1)
