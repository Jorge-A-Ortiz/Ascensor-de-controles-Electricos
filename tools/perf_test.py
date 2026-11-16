"""Mide el coste de renderizado (comando "perf" de la consola UART).

Uso: python tools/perf_test.py COM11
Reporta ms por ciclo de refresco y px redibujados en reposo, al cambiar de
pantalla con animacion y con la cabina en movimiento.
"""
import re
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM11"
s = serial.Serial(PORT, 115200, timeout=0.2)
s.dtr = False
s.rts = True
time.sleep(0.15)
s.rts = False
t = time.time()
b = ""
while time.time() - t < 12 and "cli listo" not in b:
    b += s.read(4096).decode("utf8", "replace")
time.sleep(1)


def cmd(c, w=0.6):
    s.reset_input_buffer()
    s.write((c + "\n").encode())
    t = time.time()
    o = ""
    while time.time() - t < w:
        o += s.read(4096).decode("utf8", "replace")
    return [l.strip() for l in o.splitlines()
            if re.match(r"^(OK|ERR|ST|UI|PERF|HEAP)", l.strip())]


def measure(name, action, wait):
    cmd("perf reset", 0.2)
    if action:
        cmd(action, 0.1)
    time.sleep(wait)
    r = cmd("perf", 0.3)
    print(f"{name:34s}", r[0] if r else "?")


measure("reposo 3 s", None, 3)
measure("cambio de pantalla 0->1", "swipe 1", 1.0)
measure("cambio de pantalla 1->2", "swipe 1", 1.0)
measure("cambio de pantalla 2->1", "swipe -1", 1.0)
measure("cambio de pantalla 1->0", "swipe -1", 1.0)
measure("cabina subiendo (call 4)", "call 4", 5)
measure("boton piso siguiente", "click next", 0.6)
