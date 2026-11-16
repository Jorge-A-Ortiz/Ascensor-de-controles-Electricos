"""Pruebas automaticas por UART del AscensorCrowPanel (consola de main/debug/cli.c).

Uso:  python tools/uart_test.py COM11

Cada prueba envia comandos, lee las respuestas y comprueba el resultado.
Las pruebas de GPIO comprueban el pin FISICO (lectura real del registro), no
solo el texto de la pantalla.
"""
import re
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM11"
ser = serial.Serial(PORT, 115200, timeout=0.2)
results = []


def reset():
    ser.dtr = False
    ser.rts = True
    time.sleep(0.15)
    ser.rts = False
    time.sleep(0.05)
    end = time.time() + 12
    buf = ""
    while time.time() < end:
        buf += ser.read(4096).decode("utf8", "replace")
        if "cli listo" in buf:
            return buf
    return buf


def cmd(line, wait=0.35):
    ser.reset_input_buffer()
    ser.write((line + "\n").encode())
    end = time.time() + wait
    out = ""
    while time.time() < end:
        out += ser.read(4096).decode("utf8", "replace")
    # solo lineas de respuesta (prefijos conocidos), sin logs
    keep = [l.strip() for l in out.splitlines()
            if re.match(r"^(OK|ERR|ST|UI|GPIO|RULES|PERF|HEAP|AUDIO)\b", l.strip())]
    return keep


def kv(line):
    return dict(re.findall(r"(\w+)=(-?[\w.]+)", line))


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail and not cond else ""))


def state():
    out = cmd("state")
    return kv(out[0]) if out else {}


def wait_state(pred, timeout=8.0):
    end = time.time() + timeout
    st = state()
    while time.time() < end:
        st = state()
        if pred(st):
            return st
        time.sleep(0.3)
    return st


print("== Reinicio ==")
boot = reset()
check("arranca y abre consola", "cli listo" in boot)
check("audio inicializado", "Avisos de audio listos" in boot)
check("sin panic/assert", "Guru Meditation" not in boot and "abort()" not in boot)

print("== Estado inicial ==")
time.sleep(1.0)
st = state()
for _ in range(3):
    if "floors" in st:
        break
    time.sleep(0.5)
    st = state()
check("estado legible", "floors" in st, st)
orig_floors = int(st.get("floors", 0))

print("== Ascensor en modo demo (ruta real, sin pantalla) ==")
cmd("click mode")  # asegurar demo si estaba en real
st = state()
if st.get("demo") != "1":
    cmd("click mode")
st = state()
check("modo demo", st.get("demo") == "1", st)
cmd("call 1")
st = wait_state(lambda s: s.get("moving") == "0" and s.get("doors") == "0", 12)
cmd("click up")  # llamada desde la UI (piso de usuario = P01)
r = cmd("call 3")
st = wait_state(lambda s: s.get("cur") == "3" and s.get("moving") == "0", 15)
check("la cabina llega al piso 3", st.get("cur") == "3", st)
ui = cmd("ui")
check("la pantalla refleja el piso 3", any("cabin=P03" in l for l in ui), ui)

print("== Botones de la UI -> callbacks reales ==")
cmd("click next")
ui = cmd("ui")
check("boton 'Piso siguiente' cambia piso de usuario", any("user=P02" in l for l in ui), ui)
cmd("click prev")
ui = cmd("ui")
check("boton 'Piso anterior' vuelve a P01", any("user=P01" in l for l in ui), ui)
cmd("click floor 2")
st = wait_state(lambda s: s.get("cur") == "2" and s.get("moving") == "0", 15)
check("boton de piso interior mueve la cabina", st.get("cur") == "2", st)

print("== Cantidad de pisos (cambia la config guardada) ==")
cmd("click plus")
st = state()
check("+ aumenta pisos", int(st.get("floors", 0)) == orig_floors + 1, st)
cmd("click minus")
st = state()
check("- disminuye pisos", int(st.get("floors", 0)) == orig_floors, st)

print("== GPIO fisicos: escritura y lectura real del pin ==")
for pin in (2, 3, 4, 5, 25, 27, 28, 49, 50, 51, 52):
    a = cmd(f"gpio drive {pin} 1")
    b = cmd(f"gpio drive {pin} 0")
    ok = (a and "read=1" in a[0]) and (b and "read=0" in b[0])
    check(f"GPIO{pin} salida sube y baja (lectura del pin)", ok, (a, b))
r = cmd("gpio drive 31 1")
check("GPIO31 (reservado) rechazado", r and r[0].startswith("ERR"), r)

print("== Regla de piso por GPIO real (sensor) ==")
# demo -> editar regla del piso 1: GPIO2 nivel 1 ; piso 2: GPIO3 nivel 1
cmd("dd tfloor 0 0")
cmd("dd tgpio 0 1")      # indice 1 = GPIO 2
cmd("dd tlevel 0 1")     # = 1
cmd("dd tfloor 0 1")
cmd("dd tgpio 0 2")      # GPIO 3
cmd("dd tlevel 0 1")
st = cmd("state")
check("reglas de piso guardadas", st and "P1=gpio2/lvl1" in st[1] and "P2=gpio3/lvl1" in st[1], st)
r = cmd("dd tgpio 0 1")  # GPIO2 ya usado por piso 1 -> rechazo
ui = cmd("ui")
check("GPIO repetido se rechaza", any("ya identifica otro piso" in l for l in ui), ui)

cmd("click mode")        # pasa a GPIO REALES
st = state()
check("modo GPIO reales", st.get("demo") == "0", st)
cmd("gpio pull 2 down")
cmd("gpio pull 3 down")
time.sleep(0.4)
cmd("gpio pull 2 up")    # inyecta nivel 1 en el sensor del piso 1 (pin flotante)
st = wait_state(lambda s: s.get("known") == "1" and s.get("cur") == "1", 5)
check("sensor GPIO2 -> piso 1 detectado", st.get("cur") == "1" and st.get("known") == "1", st)
cmd("gpio pull 2 down")
cmd("gpio pull 3 up")    # ahora el sensor del piso 2
st = wait_state(lambda s: s.get("cur") == "2" and s.get("known") == "1", 5)
check("sensor GPIO3 -> piso 2 detectado", st.get("cur") == "2", st)
ui = cmd("ui")
check("la pantalla muestra el piso detectado por GPIO", any("cabin=P02" in l for l in ui), ui)
cmd("gpio pull 3 down")
cmd("click mode")        # vuelve a demo
st = state()
check("vuelve a modo demo", st.get("demo") == "1", st)
cmd("gpio pull 2 none")
cmd("gpio pull 3 none")

print("== Bloques de logica: entrada -> salida real ==")
cmd("click edit")
# partir de cero: borra los bloques que hubiera guardados
for _ in range(40):
    r = cmd("rules")
    if not r or "count=0" in r[0]:
        break
    cmd("click del 0")
cmd("click add")
r = cmd("rules")
check("bloque agregado", r and "count=1" in r[0], r)
cmd("dd block 0 4")     # campo 0 (entrada) -> indice 4 = GPIO 5
cmd("dd block 1 1")     # campo 1: ES IGUAL A 1
cmd("dd block 2 5")     # campo 2 (salida) -> indice 5 = GPIO 25
cmd("dd block 3 1")     # ESCRIBIR 1
cmd("dd block 4 0")     # reposo 0
r = cmd("rules")
check("bloque configurado (in=GPIO5==1 -> GPIO25<=1)", any("gpio5==1 THEN gpio25<=1 REST 0" in l for l in r), r)
cmd("click blksw")      # interruptor "Programacion por bloques"
st = state()
check("reglas activadas (interruptor de la UI)", st.get("blocks_on") == "1", st)
cmd("gpio pull 5 down")
time.sleep(0.2)
r = cmd("gpio read 25")
check("salida GPIO25 en reposo (0) con entrada en 0", r and "25=0" in r[0], r)
cmd("gpio pull 5 up")    # entrada a 1 -> la regla debe escribir 1 en GPIO25
time.sleep(0.25)
r = cmd("gpio read 25")
check("regla: GPIO5=1 pone GPIO25=1 (pin fisico)", r and "25=1" in r[0], r)
cmd("gpio pull 5 down")
time.sleep(0.25)
r = cmd("gpio read 25")
check("al soltar la entrada, GPIO25 vuelve a reposo (0)", r and "25=0" in r[0], r)
cmd("gpio pull 5 up")
time.sleep(0.25)
cmd("click blksw")       # desactivar reglas: la salida queda en reposo
time.sleep(0.2)
r = cmd("gpio read 25")
st = state()
check("al desactivar reglas la salida queda en reposo",
      st.get("blocks_on") == "0" and r and "25=0" in r[0], (st, r))
cmd("gpio pull 5 none")
cmd("click back")
cmd("click del 0")
r = cmd("rules")
check("bloque eliminado", r and "count=0" in r[0], r)

print("== Audio: el I2S reproduce de verdad (reloj, pines, amplificador) ==")
time.sleep(4)   # deja que termine la cola de sonidos de las pruebas anteriores
a0 = kv((cmd("audiostat") or [""])[0])
cmd("tone 1000 600", 0.1)
mid = kv((cmd("audiostat", 0.25) or [""])[0])
check("el amplificador (GPIO30) esta habilitado al sonar", mid.get("pa") == "1", mid)
time.sleep(0.9)
a1 = kv((cmd("audiostat") or [""])[0])
check("se reproduce una secuencia mas", int(a1.get("jobs", 0)) == int(a0.get("jobs", 0)) + 1, (a0, a1))
check("sin errores de escritura I2S", a1.get("write_errors") == "0", a1)
dframes = int(a1.get("frames", 0)) - int(a0.get("frames", 0))
check("un tono de 600 ms escribe 9600 muestras (16 kHz)", 9500 <= dframes <= 9700, dframes)
check("duracion real cercana a 600 ms (cola DMA adelanta ~60 ms)", 480 <= int(a1.get("last_job_ms", 0)) <= 680, a1)
check("el amplificador sigue habilitado (como el original)", a1.get("pa") == "1", a1)
r = cmd("sampleio", 1.2)
edges = dict(re.findall(r"(\w+)\(GPIO\d+\)=(\d+)", r[0])) if r else {}
check("LRCK/WS (GPIO21) conmuta a ~16 kHz (630-650 flancos en 20 ms)",
      600 <= int(edges.get("WS", 0)) <= 680, r)
check("BCLK (GPIO22) y DOUT (GPIO23) con actividad",
      int(edges.get("BCLK", 0)) > 500 and int(edges.get("DOUT", 0)) > 500, r)

print("\n== Resumen ==")
ok = sum(1 for _, p, _ in results if p)
print(f"{ok}/{len(results)} pruebas correctas")
sys.exit(0 if ok == len(results) else 1)
