#!/usr/bin/env python3
"""
analizar_firmware.py - Analiza un volcado de flash ESP32 (ESP-IDF).

Uso:
    python analizar_firmware.py backup_esp32.bin
    python analizar_firmware.py backup_esp32.bin --solo-strings
    python analizar_firmware.py backup_esp32.bin --grep motor,gpio,ascensor

Extrae:
  * Cabeceras de aplicacion (esp_app_desc_t): nombre del proyecto, version,
    fecha/hora de compilacion, version de ESP-IDF y SHA256 del ELF.
  * Tabla de particiones: layout completo (app / ota / nvs / spiffs / coredump).
  * Strings del binario, clasificados en tareas FreeRTOS, tags de log y textos.
  * Strings de la particion NVS (claves y valores guardados).

No recupera codigo fuente: el firmware es codigo maquina compilado.
"""

import argparse
import hashlib
import re
import struct
import sys

APP_MAGIC = 0xABCD5432
PART_MAGIC = b"\xaa\x50"
PART_ENTRY = struct.Struct("<2sBBII16sI")  # magic, type, subtype, offset, size, label, flags

APP_TYPES = {0x00: "factory", 0x10: "ota_0", 0x11: "ota_1", 0x20: "test"}
DATA_TYPES = {
    0x00: "ota_data",
    0x01: "phy_init",
    0x02: "nvs",
    0x03: "coredump",
    0x04: "nvs_keys",
    0x05: "efuse",
    0x06: "undefined",
    0x80: "esphttpd",
    0x81: "fat",
    0x82: "spiffs",
    0x83: "littlefs",
}

# Palabras que suelen delatar la estructura del sketch/proyecto
KEYWORDS = [
    "ascensor", "elevator", "piso", "floor", "planta", "motor", "stepper",
    "servo", "ledc", "pwm", "puerta", "door", "sensor", "boton", "button",
    "ir", "ultrason", "peso", "load", "display", "lcd", "oled", "seven",
    "gpio", "task", "wifi", "mqtt", "nvs", "uart", "i2c", "spi",
]


def read(path):
    with open(path, "rb") as f:
        return f.read()


def parse_app_descriptors(data):
    """Busca todas las cabeceras esp_app_desc_t del volcado."""
    found = []
    magic = struct.pack("<I", APP_MAGIC)
    start = 0
    while True:
        i = data.find(magic, start)
        if i < 0:
            break
        start = i + 4
        blob = data[i:i + 256]
        if len(blob) < 176:
            continue
        (magic_word, secure_version) = struct.unpack_from("<II", blob, 0)
        version = blob[16:48].split(b"\x00")[0].decode("utf-8", "replace")
        project = blob[48:80].split(b"\x00")[0].decode("utf-8", "replace")
        t_compile = blob[80:96].split(b"\x00")[0].decode("utf-8", "replace")
        d_compile = blob[96:112].split(b"\x00")[0].decode("utf-8", "replace")
        idf_ver = blob[112:144].split(b"\x00")[0].decode("utf-8", "replace")
        elf_sha = blob[144:176].hex()

        # Descartar falsos positivos: un descriptor real trae fecha e IDF legibles
        if not re.match(r"^\d{2}:\d{2}:\d{2}$", t_compile):
            continue
        if not d_compile or not idf_ver:
            continue

        found.append({
            "offset": i,
            "app_offset": i - 0x20,  # la cabecera vive 0x20 dentro de la particion app
            "project": project,
            "version": version,
            "compiled_at": f"{d_compile} {t_compile}",
            "idf": idf_ver,
            "secure_version": secure_version,
            "elf_sha256": elf_sha,
        })
    return found


def parse_partition_table(data):
    """Tabla de particiones en 0x8000, entradas de 32 bytes."""
    base = 0x8000
    if data[base:base + 2] != PART_MAGIC:
        return None, []
    parts = []
    for n in range(95):
        off = base + n * 32
        chunk = data[off:off + 32]
        if len(chunk) < 32 or chunk[0:2] != b"\xaa\x50":
            break
        _, ptype, subtype, p_off, p_size, label, flags = PART_ENTRY.unpack(chunk)
        label = label.split(b"\x00")[0].decode("utf-8", "replace")
        kind = "app" if ptype == 0 else "data" if ptype == 1 else f"tipo {ptype}"
        name = APP_TYPES.get(subtype) if ptype == 0 else DATA_TYPES.get(subtype)
        parts.append({
            "label": label,
            "type": kind,
            "subtype": name or f"0x{subtype:02x}",
            "offset": p_off,
            "size": p_size,
        })
    return base, parts


def printable_strings(data, min_len=4):
    """Runs de bytes imprimibles con su offset."""
    out = []
    for m in re.finditer(rb"[\x20-\x7e\xa0-\xff]{%d,}" % min_len, data):
        raw = m.group()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("latin-1", "replace")
        out.append((m.start(), text))
    return out


def classify(strings):
    tasks, tags, texts = [], [], []
    for off, s in strings:
        if re.match(r"^[a-z0-9_]{3,24}(_task|task|_tsk)$", s):
            tasks.append((off, s))
        elif re.match(r"^[A-Z][A-Z0-9_]{2,20}$", s):
            tags.append((off, s))
        else:
            texts.append((off, s))
    return tasks, tags, texts


def main():
    ap = argparse.ArgumentParser(description="Analiza un volcado de flash ESP32 (ESP-IDF)")
    ap.add_argument("bin", help="archivo .bin del volcado")
    ap.add_argument("--grep", help="filtrar strings por palabras (separadas por coma)")
    ap.add_argument("--solo-strings", action="store_true", help="omitir cabeceras y particiones")
    ap.add_argument("--min-len", type=int, default=4, help="longitud minima de string (default 4)")
    ap.add_argument("--out", help="guardar el reporte en un archivo")
    args = ap.parse_args()

    data = read(args.bin)
    lines = []

    def emit(txt=""):
        lines.append(txt)
        print(txt)

    emit("=" * 72)
    emit(f"ARCHIVO: {args.bin}")
    emit(f"TAMANO : {len(data):,} bytes ({len(data)/1024/1024:.2f} MB)")
    emit(f"SHA256 : {hashlib.sha256(data).hexdigest()}")
    emit("=" * 72)

    if not args.solo_strings:
        descs = parse_app_descriptors(data)
        emit("\n[1] CABECERAS DE APLICACION (esp_app_desc_t)")
        if not descs:
            emit("  No se encontro ninguna cabecera valida.")
        for d in descs:
            emit(f"  --- descriptor en 0x{d['offset']:06x} (particion app en 0x{d['app_offset']:06x}) ---")
            emit(f"  Proyecto        : {d['project'] or '(vacio)'}")
            emit(f"  Version         : {d['version'] or '(vacia)'}")
            emit(f"  Compilado       : {d['compiled_at']}")
            emit(f"  ESP-IDF         : {d['idf']}")
            emit(f"  SHA256 del ELF  : {d['elf_sha256']}")
            emit(f"  Secure version  : {d['secure_version']}")
        if len(descs) > 1:
            emit("\n  AVISO: hay mas de un firmware en el flash (posibles slots OTA).")

        base, parts = parse_partition_table(data)
        emit("\n[2] TABLA DE PARTICIONES")
        if base is None:
            emit("  No se encontro tabla de particiones en 0x8000.")
            parts = []
        else:
            emit(f"  {'LABEL':<16} {'TIPO':<6} {'SUBTIPO':<10} {'OFFSET':>10} {'TAMANO':>10}")
            for p in parts:
                emit(f"  {p['label']:<16} {p['type']:<6} {p['subtype']:<10} "
                     f"0x{p['offset']:08x} {p['size']/1024:>9.0f}K")

        nvs = [p for p in parts if p["type"] == "data" and p["subtype"] == "nvs"]
        for p in nvs:
            seg = data[p["offset"]:p["offset"] + p["size"]]
            strs = printable_strings(seg, 3)
            if strs:
                emit(f"\n[3] CONTENIDO DE NVS ({p['label']}) - posibles claves y valores")
                for off, s in strs[:80]:
                    emit(f"  0x{p['offset'] + off:06x}  {s!r}")
            else:
                emit(f"\n[3] NVS ({p['label']}): sin strings (posiblemente vacia)")

    filt = None
    if args.grep:
        filt = [w.strip().lower() for w in args.grep.split(",") if w.strip()]

    strs = printable_strings(data, args.min_len)
    tasks, tags, texts = classify(strs)

    emit(f"\n[4] STRINGS ({len(strs):,} encontrados)")
    if filt:
        sel = [(o, s) for o, s in strs if any(w in s.lower() for w in filt)]
        emit(f"  Filtro: {', '.join(filt)}  ->  {len(sel)} coincidencias")
        for off, s in sel[:300]:
            emit(f"  0x{off:06x}  {s}")
    else:
        emit(f"\n  -- Posibles nombres de tareas FreeRTOS ({len(tasks)}) --")
        for off, s in tasks[:60]:
            emit(f"  0x{off:06x}  {s}")
        emit(f"\n  -- Posibles tags de log ({len(tags)}) --")
        for off, s in tags[:60]:
            emit(f"  0x{off:06x}  {s}")
        hits = [(o, s) for o, s in strs
                if any(k in s.lower() for k in KEYWORDS)]
        emit(f"\n  -- Strings relacionados con el proyecto ({len(hits)}) --")
        for off, s in hits[:150]:
            emit(f"  0x{off:06x}  {s}")

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
        print(f"\nReporte guardado en {args.out}")


if __name__ == "__main__":
    sys.exit(main())
