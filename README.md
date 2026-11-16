# Ascensor de controles Eléctricos

Reconstrucción del proyecto **`AscensorCrowPanel`** (ESP32-P4 + CrowPanel P4, LVGL 8), hecha a
partir del firmware que quedó grabado en la placa. El código fuente original se perdió con el
equipo donde se compiló, así que este repositorio se reconstruyó extrayendo del chip todo lo que
un binario puede contar: nombre de proyecto, fecha de compilación, particiones, claves de NVS,
librerías, tags de log y **todos los textos de la interfaz**.

| | |
|---|---|
| Placa | CrowPanel Advanced 7" ESP32-P4 (DSP: EK79007, 1024x600, pantalla usada en vertical 600x1024) |
| Táctil | GT911 por I2C (SDA 45 / SCL 46, reset 40, INT 42) |
| Chip | ESP32-P4 rev v1.3, 16 MB flash, 32 MB PSRAM |
| Firmware original | `AscensorCrowPanel` v1 — compilado el 30/10/2026 21:01:04 con ESP-IDF v6.1 |
| MAC de la placa | `30:ed:a0:e2:e1:df` |

El detalle completo de la recuperación está en [`docs/RECUPERACION.md`](docs/RECUPERACION.md).

---

## Qué hace la aplicación

Tres pantallas verticales que se cambian deslizando:

### 1. Ascensor / Exterior
- Muestra el piso donde está el pasajero y le permite subir/bajar de piso.
- Indica en qué piso está la cabina: `SUBIENDO`, `BAJANDO`, `DETENIDO - PUERTAS CERRADAS`.
- Botones **SUBIR** y **BAJAR** para llamar al ascensor.

### 2. Ascensor / Interior
- Muestra el piso actual de la cabina y una botonera con todos los pisos.
- Avisa cuando el piso ya está en el recorrido o cuando la cabina ya está ahí.

### 3. Ascensor / Técnica

Cambios de programación y revisión de funcionamiento real:
[Programación y funcionamiento](docs/PROGRAMACION_Y_FUNCIONAMIENTO.md).

- **Cantidad de pisos** (2 a 16).
- **Posición**: sensores GPIO exclusivamente; modo demo eliminado.
- **Ver pisos**: vista completa de sensores, posición y llamadas interiores/exteriores.
- **Regla por piso**: GPIO del sensor + nivel activo (0 o 1).
- **Programación por bloques**: hasta 32 reglas del tipo
  con condiciones GPIO, botones virtuales, piso, dirección, puertas y pedidos;
  acciones de salida GPIO o llamadas interiores/exteriores,
  con nivel de reposo seguro por bloque.

Validaciones incluidas (las mismas que anunciaba el firmware original):

- Un GPIO no puede ser entrada y salida a la vez.
- Un GPIO de salida debe tener un único nivel de reposo.
- Una salida compartida combina condiciones por OR, con valor activo y reposo coherentes.
- Un GPIO solo puede identificar un piso.
- Solo se permiten los GPIO del conector de usuario; los del panel están reservados.
- Las entradas se filtran 30 ms.

---

## Estructura

```
.
├── CMakeLists.txt
├── partitions.csv              # idéntica a la del firmware original
├── sdkconfig.defaults
├── main/
│   ├── main.c                  # arranque y timers
│   ├── app.c / app.h           # estado global y operaciones de alto nivel
│   ├── include/board_config.h  # pines del CrowPanel
│   ├── board/                  # DSI + GT911 + backlight + LVGL
│   ├── storage/                # NVS (claves del firmware original)
│   ├── elevator/               # máquina de estados del ascensor
│   ├── rules/                  # motor de lógica por bloques
│   └── ui/                     # las tres pantallas LVGL
├── docs/RECUPERACION.md        # informe de lo extraído del chip
├── tools/analizar_firmware.py  # analizador de volcados ESP32
└── firmware/                   # respaldo del flash + strings extraídos
```

## Compilar

Requiere ESP-IDF **v5.4 o superior** (el original usó v6.1).

```bash
# Linux / macOS
. $IDF_PATH/export.sh

# Windows (PowerShell)
. $env:IDF_PATH\export.ps1

idf.py set-target esp32p4
idf.py build
idf.py -p COM4 flash monitor
```

En Windows, `install.ps1` del instalador de ESP-IDF deja el entorno listo.

La primera compilación descarga los componentes declarados en `main/idf_component.yml`
(LVGL 8.4, `esp_lvgl_port`, `esp_lcd_ek79007`, `esp_lcd_touch_gt911`).

**Estado verificado:** compila sin errores con ESP-IDF v6.1 sobre `esp32p4`
(binario de ~686 KB, 83 % de la partición libre). Notas de la puesta a punto:

- `CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y`: con logs INFO el bootloader de IDF 6.1
  ocupa 0x6140 y no cabe en los 0x6000 disponibles antes de la tabla de
  particiones (0x8000). Desactivarlos conserva el layout original del flash.
- El panel DSI se configura a mano (campos `in_color_format`/`out_color_format`
  de IDF 6.x) porque la macro de `esp_lcd_ek79007` 1.0.4 todavía usa la API de IDF 5.
- La rotación vertical se hace por software (`sw_rotate`), ya que un panel DPI no
  admite `swap_xy` por hardware.

## Pines

Están todos en `main/include/board_config.h`:

| Función | GPIO |
|---|---|
| Retroiluminación (LEDC) | 31 |
| Reset del panel | 41 |
| I2C táctil SDA / SCL | 45 / 46 |
| Reset / INT del GT911 | 40 / 42 |
| LED de la placa | 48 |
| LDO DSI / táctil | canal 3 / canal 4 (2.5 V) |

GPIO disponibles para el usuario (conector J7): **2, 3, 4, 5, 25, 27, 28, 49, 50, 51, 52**.

## Diferencias conocidas con el original

La reconstrucción reproduce arquitectura, textos, estados y validaciones, pero hay tres puntos
que el binario no puede devolver:

1. **Acentos en pantalla.** Las fuentes Montserrat de LVGL 8 solo traen ASCII, así que los textos
   van sin tildes. El firmware original mostraba acentos porque usaba una fuente con rango
   extendido. Para recuperarlo: genera la fuente con
   `lv_font_conv --range 0x20-0x7F,0xA0-0xFF` y sustitúyela en `ui.c`.
2. **Audio.** El original tenía códec NS4168 con avisos y grabación. Aquí `board_sound()` solo
   registra el evento; falta el driver I2S (`bsp_audio`).
3. **Rotación.** El original rotaba con PPA y doble framebuffer propio (`bsp_illuminate.c`).
   Aquí se usa la rotación por software del puerto LVGL (`lv_disp_set_rotation` con
   `LV_DISP_ROT_90`). Si la imagen sale girada o espejada, cambia ese valor por
   `LV_DISP_ROT_270` en `main/board/board.c`.

## Respaldo y restauración

El flash completo de la placa está en `firmware/backup_esp32p4_16MB.bin`
(SHA256 `ade0742d81e76f7d41a270973dc8601cf64865915465cfe45a4eab23af2724f5`).

Para volver al firmware original tal como estaba:

```bash
esptool -p COM4 write-flash 0x0 firmware/backup_esp32p4_16MB.bin
```

## Herramientas

`tools/analizar_firmware.py` analiza cualquier volcado de flash ESP32/ESP-IDF y reporta
cabeceras de aplicación, tabla de particiones, NVS y strings clasificados:

```bash
python tools/analizar_firmware.py firmware/backup_esp32p4_16MB.bin
```

## Pendientes

- [ ] **Programación exterior por bloques.** Configurar desde el editor las llamadas SUBIR y BAJAR por piso y sus acciones.
- [ ] **Giro de pantalla.** Añadir un botón siempre visible que gire 90° entre vertical y horizontal. Adaptar el diseño y las coordenadas táctiles a ambas orientaciones.
- [ ] **Pestaña de animaciones.** Integrar las animaciones del [simulador AscensorControl](https://github.com/Jorge-A-Ortiz/AscensorControl) en una cuarta pestaña de la CrowPanel.
- [ ] **Indicador físico de puertas.** Leer la señal del PLC o de los sensores e indicar **Abriendo** y **Cerrando**. Definir pines y niveles según el cableado.
