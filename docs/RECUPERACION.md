# Recuperación del firmware — AscensorCrowPanel

Documento generado a partir del volcado de flash del ESP32-P4 conectado por USB.
**No es código fuente**: es la información que se puede extraer de un firmware compilado.

---

## 1. Hardware detectado

| Dato | Valor |
|---|---|
| Chip | ESP32-P4 (rev v1.3), doble núcleo + LP core, 400 MHz |
| Flash | 16 MB (fabricante `c8` = GigaDevice, device `4018`) |
| Cristal | 40 MHz |
| MAC | `30:ed:a0:e2:e1:df` |
| Puerto | COM4 — puente USB-Serial CH340K (`VID_1A86&PID_7522`) |
| Pantalla | CrowPanel P4 V1.0, **600x1024 vertical**, MIPI DSI |
| Táctil | **GT911** por I2C |
| Retroiluminación | LEDC (PWM) — `set_lcd_blight(70)` |
| Rotación | PPA por hardware, doble framebuffer RGB565 |
| Audio | Códec presente, con avisos y **grabación** (`Grabación: %s`) |

## 2. Firmware activo

| Dato | Valor |
|---|---|
| Proyecto | **`AscensorCrowPanel`** |
| Versión | `1` |
| Compilado | **30 de octubre de 2026, 21:01:04** |
| ESP-IDF | `v6.1-dirty` |
| SHA256 del ELF | `27919f3eefa6f1b10d213dfd626ce03c4e646611567683f51f315f464cbe6812` |
| Partición | `factory` en `0x010000` |
| Arranque | `otadata` vacía (0xFF) → arranca siempre `factory` |

### Segundo firmware encontrado en el flash

| Dato | Valor |
|---|---|
| Proyecto | `HMI_Code_FP` |
| Versión | `58a3200-dirty` |
| Compilado | 19 de agosto de 2026, 02:58:04 |
| ESP-IDF | `v5.5.4-dirty` |
| Ubicación | `0x210000` (imagen antigua, resto de un layout previo) |

## 3. Tabla de particiones

| Label | Tipo | Subtipo | Offset | Tamaño |
|---|---|---|---|---|
| nvs | data | nvs | `0x009000` | 16 K |
| otadata | data | ota_data | `0x00d000` | 8 K |
| phy_init | data | phy_init | `0x00f000` | 4 K |
| factory | app | factory | `0x010000` | 4032 K |
| ota_0 | app | ota_0 | `0x400000` | 4032 K |
| ota_1 | app | ota_1 | `0x7f0000` | 4032 K |

## 4. Claves de NVS (`namespaces` y claves reales)

Extraídas del volcado de la partición `nvs`:

- `elevator` (namespace principal)
- `floors` — cantidad de pisos
- `floor_gpio` — GPIO asignado a cada piso
- `floor_level` — nivel lógico que identifica cada piso
- `logic_blocks` — bloques de lógica guardados
- `rules_v1` — versión/esquema de reglas
- `base3` — ajuste interno (escala/posición)
- `sound` — ajuste de audio

## 5. Arquitectura del software (reconstruida de tags y strings)

**Tareas / timers FreeRTOS observados:** `main_task`, `taskLVGL`, `touch_poll`,
`motion_timer`, `capture_task`, `ipc_task`

**Tags de log (módulos):** `ASCENSOR`, `DISPLAY`, `TOUCH`, `SLIDE`, `RENDER`,
`ILLUMINATE`, `SOUND`, `PERF`

**Archivos del proyecto que aparecen en el binario:**
- `./main/main.c`
- `./peripheral/bsp_illuminate/bsp_illuminate.c`

**Componentes usados:**
- LVGL 8 + `espressif__esp_lvgl_port` (variante lvgl8)
- `esp_lcd_mipi_dsi` / panel DPI
- `esp_lcd_touch_gt911`
- PPA (rotación por hardware)
- `nvs_flash`, `esp_driver_gpio`, `esp_driver_ledc`
- Audio (códec tipo ES8311) con reproducción y grabación

**UART0 a 921600 baudios**, 1024 bytes de RX. Filtrado de entradas de 30 ms.

## 6. Funcionalidad (deducida de los textos de UI)

### Pantalla 1 — ASCENSOR / EXTERIOR
- "ESTÁS EN EL PISO", botones **Piso anterior / Piso siguiente**
- "EL ASCENSOR ESTÁ EN", "¿Hacia dónde vas?", botones **SUBIR / BAJAR**
- "Tu piso se ilumina al llegar"
- Indicadores: `SUBIENDO`, `BAJANDO`, `DETENIDO · PUERTAS CERRADAS`

### Pantalla 2 — ASCENSOR / INTERIOR
- "ESTÁS AQUÍ", "¿A qué piso quieres ir?"
- Llamadas por piso: `Llamada P%02u · subiendo/bajando`
- Avisos: "Ese piso ya está incluido en el recorrido", "La cabina ya está en este piso"
- "Parada P%02u agregada a la ruta", "Límite de recorrido alcanzado"

### Pantalla 3 — ASCENSOR / TÉCNICA
- **Cantidad de pisos**
- **Origen de posición**: `MODO DEMO` vs `GPIO REALES`
- **Regla por piso**: `SI entrada digital` → `GPIO 2/3/4/5/25/27/28/49/50/51/52`
- "Desactiva GPIO antes de cambiar los pisos", "Ese GPIO ya identifica otro piso"
- "Conflicto: hay varios sensores de piso activos", "Entre pisos: ningún sensor activo"
- "Sensor confirmado: luz del piso encendida"

### Editor de bloques (lógica programable)
- **Programación por bloques**, hasta **32 bloques compartidos** entre todos los pisos
- Estructura: `SI entrada digital` = `ES IGUAL A 0/1` → `ENTONCES salida digital` = `ESCRIBIR 0/1`
- Nivel de **reposo seguro = 0 / = 1** por bloque
- Validaciones implementadas:
  - Un GPIO no puede ser entrada y salida a la vez
  - Un GPIO de salida debe tener un único nivel de reposo
  - Dos bloques no pueden ordenar valores contrarios a la misma salida
  - `GPIO reservado o no disponible en el conector J7`
  - Entradas a 3,3 V
- Notas por bloque (máx. 64 bytes UTF-8)
- Al desactivar: "salidas en su nivel de reposo configurado"

### Diagnóstico / rendimiento
- Overlay `PERF frames=… elapsed=…ms fps=… render_avg=…ms`
- `PPA=…us LCD=…us VSYNC=…us`

## 7. Restricciones de GPIO incluidas como aserciones en el binario

```
allowed_input(2) && allowed_input(52) && !allowed_input(31) && !allowed_input(40)
```

## 8. Qué se puede y qué no se puede recuperar

| Se recuperó | No se puede recuperar |
|---|---|
| Nombre del proyecto, versión, fecha e IDF | Código fuente `.c`/`.h` |
| Layout de flash y particiones | Nombres de variables y funciones |
| Claves de NVS y su contenido | Comentarios y estructura de archivos |
| Todos los textos de la UI | Valores exactos de pines del panel |
| Tags, tareas y módulos | Constantes de calibración táctil |
| Librerías y versiones de port | Diseño exacto de las pantallas |

## 9. Archivos generados

| Archivo | Contenido |
|---|---|
| `firmware/backup_esp32p4_16MB.bin` | Respaldo completo del flash (16 MB) |
| `firmware/reporte_firmware.txt` | Reporte del analizador |
| `firmware/strings_app.txt` | 10 213 strings de la partición de la app |
| `firmware/strings_all.txt` | 5 859 strings ASCII de todo el flash |
| `herramientas/analizar_firmware.py` | Analizador reutilizable |

SHA256 del respaldo: `ade0742d81e76f7d41a270973dc8601cf64865915465cfe45a4eab23af2724f5`

> **Para restaurar el firmware tal cual:**
> `esptool -p COM4 write-flash 0x0 firmware/backup_esp32p4_16MB.bin`
