# TRASPASO — AscensorCrowPanel (ESP32-P4 + CrowPanel)

Documento de continuidad. Con esto otra IA (o una persona) puede seguir el
trabajo sin contexto previo. Última actualización: 2026-11-16.

---

## Actualización de programación (2026-11-16)

Esta actualización sustituye las referencias a modo demo y bloques GPIO del
resto del documento, que conserva el contexto histórico de recuperación.

- Eliminado el avance simulado y el selector demo; posición exclusivamente GPIO.
- Bloques `rules_v2`: botones virtuales de 300 ms, piso, dirección, puertas y
  llamadas pendientes; acciones GPIO y llamadas desde entradas físicas.
- Recogida colectiva con llamadas interiores/exteriores por sentido, incluidas
  paradas nuevas durante el recorrido. Vista desplazable «Ver pisos».
- Sensores contradictorios retienen fallo y llevan las reglas a reposo.
- Los timers del controlador comparten el mutex LVGL con las ediciones de UI.
- Pruebas host con sanitizadores y sintaxis UI con cabeceras LVGL 8.4 verificadas.
  No se compiló el firmware ESP-IDF ni se grabó/probó la placa en esta sesión.
- Cambios realizados en la copia local; ver `docs/PROGRAMACION_Y_FUNCIONAMIENTO.md`
  para ejemplos, alcance y funciones físicas todavía pendientes.

## 1. Qué es este proyecto

Reconstrucción del firmware de un **ascensor de controles eléctricos** que se
perdió como código fuente y solo sobrevivía flasheado en una placa
**Elecrow CrowPanel ESP32-P4**. El binario original no se puede descompilar a
código legible, así que el proyecto se reescribió en C con ESP-IDF, copiando la
arquitectura y los textos que se pudieron recuperar del binario y de los logs.

- **Pantalla:** 7" DSI, panel **EK79007**, 1024x600 nativa, usada en vertical
  (600x1024) con rotación por software de LVGL.
- **Táctil:** **GT911** por I2C.
- **Funcionalidad:** UI de 3 pantallas (principal, pisos, configuración),
  máquina de estados del ascensor, motor de reglas por bloques (lógica tipo
  PLC reconstruida de los tags originales) y persistencia en NVS.

---

## 2. Backup del firmware original (lo más importante)

| Dato | Valor |
|---|---|
| Ruta | `C:\Users\yenni\Documents\.Github Repositorios\Ascensor-de-controles-Electricos\firmware\backup_esp32p4_16MB.bin` |
| Tamaño | 16 MB (0x1000000) |
| SHA256 | `ade0742d81e76f7d41a270973dc8601cf64865915465cfe45a4eab23af2724f5` |
| Incluido en el repo | Sí (única excepción a la regla `*.bin` del `.gitignore`) |

Es la imagen **completa** de la flash (0x0–0x1000000) leída de la placa
original, con su NVS incluida (6 pisos, modo demo). Restaura el equipo al
estado de fábrica/taller exacto.

**Restaurar** (destructivo: borra el firmware reconstruido y la config actual):

```powershell
python -m esptool --chip esp32p4 -p COM10 --before default-reset --after hard-reset `
  write-flash 0x0 firmware/backup_esp32p4_16MB.bin
```

Antes de restaurar, confirmar el puerto (ver punto 4) y que la placa es la
**rev v1.3**. Nunca restaurar este backup en otra placa.

---

## 3. Repositorio y estado de git

- Carpeta local: `C:\Users\yenni\Documents\.Github Repositorios\Ascensor-de-controles-Electricos`
- Remoto: `https://github.com/Niko-Vanetti/Ascensor-de-controles-Electricos.git`
- Visibilidad: **privado**
- Rama: `main`
- Último commit publicado: `b231abc` — "Compila con ESP-IDF 6.1: corrige API DSI/LVGL8, bootloader y NVS"
- Identidad git local: `Niko-Vanetti` / `129996541+Niko-Vanetti@users.noreply.github.com`

**Cambios sin commitear** (importantes, son los arreglos de hardware):

```
M main/board/board.c      -> táctil sin swap_xy + flags de rotación del panel
M main/main.c             -> diagnóstico temporal del táctil (punto rojo + log)
M sdkconfig.defaults      -> revisión de chip v1.x y CPU a 360 MHz
```

`sdkconfig` está en `.gitignore` (se regenera desde `sdkconfig.defaults`).

---

## 4. Hardware: hay DOS placas (no confundirlas)

| Placa | Chip | MAC | Puerto cuando se documentó | Estado |
|---|---|---|---|---|
| **CrowPanel original** (la del proyecto) | ESP32-P4 **rev v1.3** | `30:ed:a0:e2:e1:df` | **COM10** (CH340K) | Es la placa buena, con UI funcionando |
| Otra placa / devkit | ESP32-P4 **rev v3.2** | `80:f1:b2:d8:0d:b8` | COM7 (CH340) | **NO** flashear con este firmware |

- Los nombres de puerto **cambian** al desconectar/reconectar (ya pasó de COM4
  a COM10). Nunca asumir el COM: identificarlo por revisión de chip.
- Identificar sin flashear:

```powershell
python -m esptool --chip esp32p4 -p COM10 --before default-reset --after hard-reset flash_id
```

Debe decir `revision v1.3`, `Detected flash size: 16MB`.

- Consola: UART0 a **115200 8N1** por el CH340K (GPIO 38/37). No es el
  USB-Serial/JTAG.
- El firmware actual está compilado **solo para rev v1.0–v1.99**; esptool
  rechaza grabarlo en la placa v3.2 (protección, no es un bug).

---

## 5. Entorno de compilación

ESP-IDF **v6.1** instalado en `C:\Users\yenni\esp\esp-idf` (Python 3.12.10,
entorno `idf6.1_py3.12_env`).

Preparar la shell (cada sesión nueva de PowerShell):

```powershell
$env:PYTHONUTF8=1
chcp 65001 | Out-Null
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. "$env:USERPROFILE\esp\esp-idf\export.ps1" | Out-Null
Set-Location "C:\Users\yenni\Documents\.Github Repositorios\Ascensor-de-controles-Electricos"
```

Compilar y grabar:

```powershell
idf.py build
idf.py -p COM10 flash
idf.py -p COM10 monitor      # salir con Ctrl+]
```

Dependencias gestionadas (no editar a mano, están en `managed_components/`):

| Componente | Versión |
|---|---|
| lvgl | 8.4.0 |
| esp_lvgl_port | 2.9.0 |
| esp_lcd_ek79007 | 1.0.4 |
| esp_lcd_touch | 1.2.1 |
| esp_lcd_touch_gt911 | 1.2.1 |
| idf | 6.1.0 |

Aviso: la macro `EK79007_1024_600_PANEL_60HZ_CONFIG` del componente usa campos
de IDF 5 y **no** compila en IDF 6.1. La configuración del panel se arma a mano
en `main/board/board.c`.

---

## 6. Configuración crítica (por qué está así)

Todo esto costó encontrar; no cambiarlo sin motivo.

1. **Revisión de chip v1.x** (`sdkconfig` + `sdkconfig.defaults`)
   ```ini
   CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
   CONFIG_ESP32P4_REV_MIN_100=y
   CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y
   ```
   IDF 6.1 apunta por defecto a rev v3.1+. Las ramas <3.0 y >=3.0 son
   mutuamente excluyentes.

2. **CPU a 360 MHz**: las revisiones < 3.0 **no admiten 400 MHz**. Con 400 la
   placa entra en bucle:
   `assert failed: esp_clk_init clk.c:105 (res)`.

3. **Bootloader sin logs** (`CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y`): con logs
   INFO el bootloader de IDF 6.1 mide 0x6140 y no cabe en los 0x6000 bytes
   antes de la tabla de particiones (0x8000). Desactivando logs entra de sobra
   y se conserva el layout de flash original:
   `0x2000` bootloader, `0x8000` tabla, `0xd000` ota_data, `0x10000` app.

4. **Flash:** 16 MB, modo QIO en sdkconfig pero esptool graba con
   `--flash-mode dio --flash-freq 80m` (valores por defecto del proyecto).

5. **Rotación vertical:** se hace con `sw_rotate` de LVGL
   (`lv_disp_set_rotation(s_disp, LV_DISP_ROT_90)`), **no** por hardware: el
   panel DPI no implementa `swap_xy` y cualquier llamada registra
   `E lcd_panel: esp_lcd_panel_swap_xy(60): swap_xy is not supported by this panel`
   (ruido inofensivo, esperado).
   Los flags `.rotation = { swap_xy=true, mirror_x=true, mirror_y=false }` de
   `lvgl_port_display_cfg_t` están elegidos para que, en ROT_90, el callback de
   rotación del puerto resulte en `swap_xy(false)` + `mirror(false,false)` y no
   toque el panel.

6. **Orden de arranque de la retroiluminación:** `ledc_fade_func_install()`
   **después** de `ledc_timer_config()` (al revés falla).

7. **NVS:** si no hay configuración guardada, se crean valores por defecto
   (6 pisos, modo demo). El arranque encadena errores con contexto en vez de
   usar `ESP_ERROR_CHECK` a secas.

---

## 7. Estado verificado en hardware (placa rev v1.3)

Confirmado por log serie y por el usuario:

- Arranque limpio: PSRAM 32 MB @200 MHz, `cpu freq: 360000000 Hz`,
  `Chip rev: v1.3`, `Min chip rev v1.0 / Max v1.99`.
- NVS carga configuración existente (6 pisos, modo demo).
- Panel EK79007 inicializa; GT911 detectado (`TouchPad_ID:0x39,0x31,0x31`).
- UI creada: `Interfaz lista: CrowPanel P4 V1.0, vertical 600x1024, 6 pisos.`
- **El usuario confirmó que la pantalla se ve bien en vertical.**

Pendiente de verificar: **que el táctil acierte** (ver punto 8).

---

## 8. Problema abierto: coordenadas del táctil

### Síntoma
La imagen se ve correcta, pero al tocar responde en otro sitio ("el táctil no
coincide").

### Diagnóstico
Se estaba rotando **dos veces**:

1. `esp_lcd_touch_config_t.flags.swap_xy = 1` en el GT911 intercambiaba los
   ejes (x=rawY, y=rawX), entregando ya coordenadas verticales 600x1024.
2. LVGL 8, en `indev_pointer_proc()` (`src/core/lv_indev.c`), aplica **su
   propia** rotación a los puntos de entrada cuando `disp_drv.rotated` está
   puesto:
   ```c
   /* ROT_90 */
   x = drv->ver_res - rawY - 1;   /* 599 - rawY */
   y = rawX;
   ```
   Con las coordenadas ya rotadas, el resultado salía fuera de rango
   (x negativo cuando y > 599).

### Corrección aplicada (en el código, compilada)
En `main/board/board.c`:

```c
.x_max = LCD_PANEL_H_RES,   /* 1024, orientación nativa */
.y_max = LCD_PANEL_V_RES,   /* 600  */
.flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
```

Es decir: el GT911 entrega coordenadas **nativas** (1024x600) y LVGL hace la
rotación. `x_max`/`y_max` solo se usan para el espejado por software, no para
escalar.

### Cómo verificar (instrumentación temporal ya incluida)
`main/main.c` tiene `#define TOUCH_DEBUG 1`, que:

- imprime por serie `TOUCHDBG punto LVGL x=.. y=..` al tocar;
- dibuja un **punto rojo de 24 px** en `lv_layer_top()` que sigue el dedo.

Procedimiento: grabar, deslizar el dedo por la pantalla y mirar si el punto rojo
va bajo el dedo. Alternativa numérica: tocar las 4 esquinas en orden
(sup-izq, sup-der, inf-der, inf-izq) y leer los valores del log.

### Si el punto rojo NO coincide (casos restantes)
La rotación ya es correcta; solo puede faltar un espejo. En el GT911 los flags
actúan sobre los ejes **crudos**, y tras la rotación de LVGL quedan así:

| Si el punto aparece… | Ajuste en `board.c` |
|---|---|
| espejado **horizontal** (toco derecha, aparece izquierda) | `.mirror_y = 1` |
| espejado **vertical** (toco arriba, aparece abajo) | `.mirror_x = 1` |
| invertido 180° | `.mirror_x = 1, .mirror_y = 1` |

Tras validar, **poner `#define TOUCH_DEBUG 0`** (o borrar el bloque) para que no
quede el punto rojo ni el log en producción.

### Cómo capturar la serie (sin monitor interactivo)

```powershell
$port = New-Object System.IO.Ports.SerialPort("COM10",115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 500
$port.Open()
$port.DtrEnable = $false
$port.RtsEnable = $true; Start-Sleep -Milliseconds 300; $port.RtsEnable = $false  # reset
$sb = New-Object System.Text.StringBuilder
$end = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $end) {
  try { $s = $port.ReadExisting(); if ($s) { [void]$sb.Append($s) } } catch {}
  Start-Sleep -Milliseconds 80
}
$port.Close()
$sb.ToString() -split "`r?`n" | Where-Object { $_ -match '^(I|W|E) \(' }
```

Cerrar el puerto antes de flashear (si no: `Could not open COM10, the port is
busy`).

---

## 9. Limitaciones conocidas (documentadas en el README)

- **Textos sin acentos ni símbolos raros**: la fuente Montserrat de LVGL no
  incluye glifos fuera de ASCII; se usan textos ASCII a propósito.
- **Audio (NS4168) sin implementar**: `board_sound()` solo registra el evento y
  parpadea el LED. El codec I2S quedó como stub.
- **Rotación por software**: más lenta que la de hardware, pero el panel DPI no
  admite `swap_xy`.
- La NVS original tenía configuración de ascensor real; la reconstrucción la
  respeta y no la borra.

---

## 10. Mapa del código

```
main/main.c              app_main: NVS -> config -> hardware -> UI -> timers
                         motion_tick 100 ms (elevator_tick + ui_refresh)
                         rules_tick  10 ms  (motor de reglas, filtro 30 ms)
                         touch_tick 100 ms  (SOLO diagnóstico, TOUCH_DEBUG)
main/board/board.c       I2C, LDO (ch3/ch4 2500 mV), backlight (GPIO31),
                         panel DSI EK79007 (reset GPIO41), GT911 (RST 40/INT 42),
                         puerto LVGL, rotación y mutex (board_lock/unlock)
main/include/board_config.h  pines y resoluciones:
                         LCD_PANEL_H_RES=1024/V_RES=600 (nativa)
                         LCD_H_RES=600/V_RES=1024 (lógica, vertical)
main/ui/ui.c             UI LVGL de 3 pantallas, botones de piso, estilos
main/elevator/elevator.c máquina de estados (parado/subiendo/bajando/puertas),
                         modelo demo y lectura de GPIO reales
main/rules/rules.c       motor de 32 bloques reconstruido de los tags originales
main/storage/storage.c   NVS: carga/guarda configuración (pisos, modo demo)
main/app.c/h             puente entre storage y elevator (app_load/app_save)
partitions.csv           tabla de particiones original (app 0x3f0000)
sdkconfig.defaults       configuración reproducible del proyecto
firmware/                backup del firmware original (ver punto 2)
```

---

## 11. Próximos pasos concretos

1. Grabar en **COM10** el build actual (`idf.py -p COM10 flash`) y probar el
   táctil con el punto rojo.
2. Si hace falta, ajustar `mirror_x` / `mirror_y` del GT911 (tabla del punto 8)
   y volver a probar.
3. Poner `TOUCH_DEBUG 0` y recompilar.
4. `git add -A && git commit` con los arreglos de hardware (chip rev, 360 MHz,
   orden LEDC, rotación, táctil) y `git push`.
5. Actualizar el README con el estado verificado final y las diferencias
   conocidas frente al firmware original.
6. Opcional: implementar el audio NS4168 y añadir acentos con una fuente LVGL
   ampliada (requiere generar la fuente con `lv_font_conv`).

---

## 12. Reglas de trabajo que ya se acordaron con el usuario

- El repositorio se queda **privado**.
- El backup del firmware **se conserva** dentro del repo y no se sobrescribe.
- El usuario valida siempre lo visual/táctil: el agente no puede "ver" la
  pantalla, solo el log serie. Preguntar antes de dar algo por bueno.
- Confirmar el puerto y la revisión del chip antes de grabar: hay dos placas
  conectadas y el firmware solo sirve para la rev v1.3.
