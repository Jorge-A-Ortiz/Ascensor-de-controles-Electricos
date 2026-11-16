# Hallazgos de ingeniería inversa de la UI original

Fuente: `firmware/backup_esp32p4_16MB.bin` (app `factory` @0x10000). Herramientas en `tools/reverse/`.
Mapa de memoria de la app: rodata (strings, fuentes) en `0x40080020`, código en `0x40000020`.

## Fuentes (extraídas, idénticas bit a bit → `main/ui/fonts_orig.c`)
Formato `lv_font_conv`, 4 bpp, kerning por clases, rango 0x20-0x7F + 0xA0-0xFF (acentos y ñ).
Tipografía display cuadrada y negrita (estilo Rajdhani/Saira), NO Montserrat.

| Copia RAM | Fuente | line_height | Fallback |
|---|---|---|---|
| 0x4ff164b4 | `font_ui_xs`  | 19 | Montserrat 14 |
| 0x4ff16498 | `font_ui_s`   | 21 | Montserrat 16 |
| 0x4ff1647c | `font_ui_m`   | 25 | Montserrat 20 |
| 0x4ff16460 | `font_ui_l`   | 30 | Montserrat 24 |
| 0x4ff16444 | `font_ui_xl`  | 38 | Montserrat 32 |
| 0x4ff16428 | `font_ui_xxl` | 59 | Montserrat 48 |
| (directa)  | `font_ui_digits` | 66 | solo "0-9" y "P" |

Al arrancar, el original copia cada `lv_font_t` a RAM y le asigna `fallback` = Montserrat
(para los símbolos LV_SYMBOL_*), ver 0x400163ec-0x40016578.

## Helpers de UI (todo posicionado en píxeles absolutos, sin flex)
- `make_label(parent,x,y,w,h,texto,fuente,color24)` @0x40013708: label wrap, color con lv_color_hex.
- `make_button(parent,x,y,w,h,texto,cb,id)` @0x40014998: bg RGB565 0x1106, pressed 0x21a9, borde 0x29a8 1px,
  radio 12, sin sombra, texto 0xef9e, fuente F21 (ui_s), eventos CLICKED(7) y PRESSED(1).
- `make_panel(parent,x,y,w,h)` @0x40014d24: no scrollable, bg 0x10c4, borde 0x29a8 1px, radio 16, padding 0.
- Pie de página: `'<  Desliza para cambiar de pantalla  >'` en (40, 978) 520x22 fuente ui_xs, centrado,
  más tres puntos indicadores de pantalla (panel 8x8) en y=948.
- Colores de estilos son RGB565 crudos: 0x1106=#102031, 0x10c4=#101c26(aprox), 0x29a8=#2b3541(aprox).

## Rotación/pantalla
Aviso en log: `PPA: Rotación RGB565 por hardware; doble framebuffer con sincronización`, tarea `capture_task`,
`FRAME565`/`END565` por UART a 921600 (captura de pantalla por serie, solo depuración).

Traza completa con llamadas y argumentos: `docs/reverse/ui_trace.txt`.
