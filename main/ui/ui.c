/**
 * @file ui.c
 * @brief UI de AscensorCrowPanel reconstruida a partir del firmware original.
 *
 * Datos recuperados del binario (ver docs/reverse/HALLAZGOS_UI.md):
 *  - Tres pantallas independientes (Exterior / Interior / Tecnica) de 600x1024,
 *    todo en coordenadas absolutas, que se cambian con un gesto (swipe).
 *  - Tipografia propia (fonts_orig.c) con Latin-1 completo; Montserrat solo
 *    como "fallback" de simbolos, igual que el original.
 *  - Helpers make_label / make_button / make_panel con los mismos estilos.
 *  - Puntos indicadores en y=948 y aviso "Desliza para cambiar de pantalla".
 *
 * Lo unico que NO se pudo recuperar es el editor de bloques a pantalla
 * completa: esta es una version propia (misma funcionalidad y estilo).
 */

#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "app.h"
#include "board.h"
#include "board_config.h"
#include "elevator.h"
#include "fonts_orig.h"
#include "rules.h"

/* ------------------------------------------------------------------ */
/*  Paleta (RGB565 del original convertido a RGB888)                  */
/* ------------------------------------------------------------------ */

#define COL_BLACK       0x000000
#define COL_BTN         0x102031   /* 0x1106 */
#define COL_BTN_PRESS   0x21354A   /* 0x21a9 */
#define COL_BORDER      0x293542   /* 0x29a8 */
#define COL_BTN_TEXT    0xEFF3F7   /* 0xef9e */
#define COL_PANEL       0x101821   /* 0x10c4 */
#define COL_PANEL_EXT   0x101410   /* 0x10a2 */
#define COL_CALL        0x216952   /* 0x234a */
#define COL_OK          0x73D7A5   /* 0x76b4 */
#define COL_BLUE_BORDER 0x8CBEFF   /* 0x8dff */
#define COL_DOT_OFF     0x738284   /* 0x7410 */

/* Por pantalla (texto/acento) */
#define EXT_MUTED       0xA7B3AC
#define EXT_TEXT        0xF2F5F0
#define EXT_ACCENT      0x206A50
#define INT_MUTED       0x97ABA0
#define INT_ACCENT      0xCAEDB0
#define TEC_ACCENT      0x8EBEFF
#define TEC_TEXT        0xEEF2F6
#define TEC_MUTED       0x91A0B2
#define TEC_GREEN       0x76D6A2

#define SCREEN_W        LCD_H_RES
#define SCREEN_H        LCD_V_RES

enum {
    ID_PREV = 1, ID_NEXT, ID_UP, ID_DOWN, ID_MINUS, ID_PLUS,
    ID_MODE = 22, ID_EDIT = 30,
};

static const int s_gpios[USER_GPIO_COUNT] = USER_GPIO_LIST;
static char s_gpio_options[220];

/* ------------------------------------------------------------------ */
/*  Fuentes: copia en RAM con fallback Montserrat (como el original)  */
/* ------------------------------------------------------------------ */

static lv_font_t F19, F21, F25, F30, F38, F59, F66;

static void init_fonts(void)
{
    F19 = font_ui_xs;   F19.fallback = &lv_font_montserrat_14;
    F21 = font_ui_s;    F21.fallback = &lv_font_montserrat_16;
    F25 = font_ui_m;    F25.fallback = &lv_font_montserrat_20;
    F30 = font_ui_l;    F30.fallback = &lv_font_montserrat_24;
    F38 = font_ui_xl;   F38.fallback = &lv_font_montserrat_32;
    F59 = font_ui_xxl;  F59.fallback = &lv_font_montserrat_48;
    F66 = font_ui_digits;
}

/* ------------------------------------------------------------------ */
/*  Estado de widgets                                                 */
/* ------------------------------------------------------------------ */

static lv_obj_t *s_scr[3];
static lv_obj_t *s_scr_blocks;
static int       s_cur_scr;

/* Exterior */
static lv_obj_t *s_ext_user_floor;
static lv_obj_t *s_ext_cabin_floor;
static lv_obj_t *s_ext_arrow;
static lv_obj_t *s_ext_dir;
static lv_obj_t *s_ext_hint;

/* Interior */
static lv_obj_t *s_int_current;
static lv_obj_t *s_int_dir;
static lv_obj_t *s_int_grid;
static lv_obj_t *s_int_status;
static lv_obj_t *s_int_btn[ELEVATOR_MAX_FLOORS];
static uint8_t   s_int_btn_count;

/* Tecnica */
static lv_obj_t *s_tec_count;
static lv_obj_t *s_tec_mode_lbl;
static lv_obj_t *s_tec_mode_btn_lbl;
static lv_obj_t *s_tec_dd_floor;
static lv_obj_t *s_tec_dd_gpio;
static lv_obj_t *s_tec_dd_level;
static lv_obj_t *s_tec_read;
static lv_obj_t *s_tec_then;
static lv_obj_t *s_tec_note;
static uint8_t   s_sel_floor = 1;

/* Editor de bloques */
static lv_obj_t *s_blk_status;
static lv_obj_t *s_blk_sw;
static lv_obj_t *s_blk_list;
static lv_obj_t *s_msgbox;

static uint8_t s_user_floor = 1;
static elevator_state_t s_last_state;

/* ------------------------------------------------------------------ */
/*  Helpers (equivalentes a make_label / make_button / make_panel)    */
/* ------------------------------------------------------------------ */

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, int h,
                            const char *txt, const lv_font_t *font,
                            uint32_t color, lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(p, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 16, 0);
    return p;
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h,
                             const char *txt, lv_event_cb_t cb, intptr_t id,
                             lv_obj_t **out_label)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_BTN_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_text_color(b, lv_color_hex(COL_BTN_TEXT), 0);
    lv_obj_set_style_text_font(b, &F21, 0);
    if (cb != NULL) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)id);
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    if (out_label != NULL) {
        *out_label = l;
    }
    return b;
}

static void style_dropdown(lv_obj_t *dd, const lv_font_t *font);

static void dd_open_cb(lv_event_t *e)
{
    lv_obj_t *list = lv_dropdown_get_list(lv_event_get_target(e));
    if (list == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(list, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_text_color(list, lv_color_hex(COL_BTN_TEXT), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_text_font(list, &F25, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(COL_CALL),
                              LV_PART_SELECTED | LV_STATE_CHECKED);
}

static void style_dropdown(lv_obj_t *dd, const lv_font_t *font)
{
    lv_obj_set_style_bg_color(dd, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dd, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, 12, 0);
    lv_obj_set_style_text_color(dd, lv_color_hex(COL_BTN_TEXT), 0);
    lv_obj_set_style_text_font(dd, font, 0);
    lv_obj_add_event_cb(dd, dd_open_cb, LV_EVENT_READY, NULL);
}

static void build_gpio_options(void)
{
    strcpy(s_gpio_options, "Sin asignar");
    for (int i = 0; i < USER_GPIO_COUNT; i++) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "\nGPIO %d", s_gpios[i]);
        strcat(s_gpio_options, tmp);
    }
}

static int gpio_to_index(uint8_t gpio)
{
    for (int i = 0; i < USER_GPIO_COUNT; i++) {
        if ((uint8_t)s_gpios[i] == gpio) {
            return i + 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Navegacion entre pantallas (gesto horizontal)                     */
/* ------------------------------------------------------------------ */

static void goto_screen(int idx, bool from_right)
{
    if (idx < 0 || idx > 2 || idx == s_cur_scr) {
        return;
    }
    s_cur_scr = idx;
    lv_scr_load_anim(s_scr[idx],
                     from_right ? LV_SCR_LOAD_ANIM_OVER_LEFT
                                : LV_SCR_LOAD_ANIM_OVER_RIGHT,
                     200, 0, false);
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) {
        goto_screen(s_cur_scr + 1, true);
    } else if (dir == LV_DIR_RIGHT) {
        goto_screen(s_cur_scr - 1, false);
    }
}

static lv_obj_t *new_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s, lv_color_hex(COL_BLACK), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s, gesture_cb, LV_EVENT_GESTURE, NULL);
    return s;
}

/* Puntos indicadores + aviso inferior (0x40014dc8 del original) */
static void make_footer(lv_obj_t *scr, int active, uint32_t accent)
{
    for (int i = 0; i < 3; i++) {
        lv_obj_t *d = make_panel(scr, 270 + 24 * i, 948, 8, 8);
        lv_obj_set_style_bg_color(d, lv_color_hex(i == active ? accent
                                                              : COL_DOT_OFF), 0);
        lv_obj_set_style_border_width(d, 0, 0);
    }
    make_label(scr, 40, 978, 520, 22, "<  Desliza para cambiar de pantalla  >",
               &F19, accent, LV_TEXT_ALIGN_CENTER);
}

/* ------------------------------------------------------------------ */
/*  Mensajes modales (errores de reglas)                              */
/* ------------------------------------------------------------------ */

static void msgbox_close_cb(lv_event_t *e)
{
    lv_obj_t *mb = lv_event_get_current_target(e);
    lv_msgbox_close(mb);
    s_msgbox = NULL;
}

void ui_show_message(const char *title, const char *body)
{
    if (!board_lock(200)) {
        return;
    }
    if (s_msgbox != NULL) {
        lv_msgbox_close(s_msgbox);
        s_msgbox = NULL;
    }
    static const char *btns[] = { "CERRAR", "" };
    s_msgbox = lv_msgbox_create(NULL, title, body, btns, true);
    lv_obj_set_width(s_msgbox, 480);
    lv_obj_set_style_bg_color(s_msgbox, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_text_color(s_msgbox, lv_color_hex(COL_BTN_TEXT), 0);
    lv_obj_set_style_text_font(s_msgbox, &F25, 0);
    lv_obj_add_event_cb(s_msgbox, msgbox_close_cb, LV_EVENT_VALUE_CHANGED, NULL);
    board_unlock();
}

/* ------------------------------------------------------------------ */
/*  Pantalla 1: EXTERIOR                                              */
/* ------------------------------------------------------------------ */

static void ext_cb(lv_event_t *e)
{
    intptr_t id = (intptr_t)lv_event_get_user_data(e);
    uint8_t floors = app_state()->cfg.floors;

    switch (id) {
    case ID_PREV:
        if (s_user_floor > 1) {
            s_user_floor--;
        }
        break;
    case ID_NEXT:
        if (s_user_floor < floors) {
            s_user_floor++;
        }
        break;
    case ID_UP:
        board_sound(BOARD_SOUND_PRESS);
        elevator_call(s_user_floor, ELEV_DIR_UP);
        break;
    case ID_DOWN:
        board_sound(BOARD_SOUND_PRESS);
        elevator_call(s_user_floor, ELEV_DIR_DOWN);
        break;
    default:
        break;
    }
    ui_refresh();
}

static void create_exterior(lv_obj_t *scr)
{
    make_label(scr, 44, 42, 360, 25, "ASCENSOR / EXTERIOR", &F21, EXT_MUTED,
               LV_TEXT_ALIGN_LEFT);
    make_label(scr, 40, 120, 520, 32, "ESTÁS EN EL PISO", &F25, EXT_MUTED,
               LV_TEXT_ALIGN_CENTER);
    s_ext_user_floor = make_label(scr, 100, 182, 400, 144, "", &F66, EXT_TEXT,
                                  LV_TEXT_ALIGN_CENTER);
    make_button(scr, 90, 358, 190, 66, "Piso anterior", ext_cb, ID_PREV, NULL);
    make_button(scr, 320, 358, 190, 66, "Piso siguiente", ext_cb, ID_NEXT, NULL);

    lv_obj_t *p = make_panel(scr, 44, 470, 512, 170);
    lv_obj_set_style_bg_color(p, lv_color_hex(COL_PANEL_EXT), 0);
    make_label(p, 24, 20, 330, 26, "EL ASCENSOR ESTÁ EN", &F21, EXT_MUTED,
               LV_TEXT_ALIGN_LEFT);
    s_ext_cabin_floor = make_label(p, 24, 62, 180, 60, "", &F59, EXT_TEXT,
                                   LV_TEXT_ALIGN_LEFT);
    s_ext_arrow = make_label(p, 350, 46, 100, 65, "", &F59, EXT_TEXT,
                             LV_TEXT_ALIGN_LEFT);
    s_ext_dir = make_label(p, 225, 122, 265, 28, "", &F19, EXT_TEXT,
                           LV_TEXT_ALIGN_LEFT);

    make_label(scr, 40, 685, 520, 34, "¿Hacia dónde vas?", &F30, EXT_TEXT,
               LV_TEXT_ALIGN_CENTER);
    lv_obj_t *up = make_button(scr, 44, 746, 246, 112, LV_SYMBOL_UP "  SUBIR",
                               ext_cb, ID_UP, NULL);
    lv_obj_t *dn = make_button(scr, 310, 746, 246, 112, LV_SYMBOL_DOWN "  BAJAR",
                               ext_cb, ID_DOWN, NULL);
    lv_obj_set_style_bg_color(up, lv_color_hex(COL_CALL), 0);
    lv_obj_set_style_bg_color(dn, lv_color_hex(COL_CALL), 0);
    lv_obj_set_style_text_font(up, &F38, 0);
    lv_obj_set_style_text_font(dn, &F38, 0);

    s_ext_hint = make_label(scr, 40, 884, 520, 48, "Tu piso se ilumina al llegar.",
                            &F19, EXT_MUTED, LV_TEXT_ALIGN_CENTER);
    make_footer(scr, 0, EXT_ACCENT);
}

/* ------------------------------------------------------------------ */
/*  Pantalla 2: INTERIOR                                              */
/* ------------------------------------------------------------------ */

static void int_floor_cb(lv_event_t *e)
{
    uint32_t floor = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    board_sound(BOARD_SOUND_PRESS);
    elevator_call((uint8_t)floor, ELEV_DIR_IDLE);
    ui_refresh();
}

static void rebuild_interior_buttons(void)
{
    if (s_int_grid == NULL) {
        return;
    }
    lv_obj_clean(s_int_grid);
    memset(s_int_btn, 0, sizeof(s_int_btn));

    uint8_t floors = app_state()->cfg.floors;
    s_int_btn_count = floors;

    for (uint8_t n = 1; n <= floors; n++) {
        char txt[8];
        snprintf(txt, sizeof(txt), "P%02u", n);
        int col = (n - 1) % 3;
        int row = (n - 1) / 3;
        lv_obj_t *lbl = NULL;
        lv_obj_t *b = make_button(s_int_grid, col * 176, row * 106, 160, 90, txt,
                                  int_floor_cb, n, &lbl);
        lv_obj_set_style_text_font(b, &F38, 0);
        lv_obj_set_style_radius(b, 45, 0);
        s_int_btn[n - 1] = b;
    }
    s_last_state.current_floor = 0xFF;  /* fuerza refresco */
}

static void create_interior(lv_obj_t *scr)
{
    make_label(scr, 44, 42, 480, 28, "ASCENSOR / INTERIOR", &F21, INT_MUTED,
               LV_TEXT_ALIGN_LEFT);
    make_label(scr, 40, 111, 520, 30, "ESTÁS AQUÍ", &F25, INT_MUTED,
               LV_TEXT_ALIGN_CENTER);
    s_int_current = make_label(scr, 100, 165, 400, 144, "", &F66, INT_ACCENT,
                               LV_TEXT_ALIGN_CENTER);
    s_int_dir = make_label(scr, 40, 325, 520, 32, "", &F21, INT_ACCENT,
                           LV_TEXT_ALIGN_CENTER);
    make_label(scr, 40, 394, 520, 36, "¿A qué piso quieres ir?", &F30, TEC_TEXT,
               LV_TEXT_ALIGN_CENTER);

    s_int_grid = make_panel(scr, 44, 454, 512, 416);
    lv_obj_set_style_bg_opa(s_int_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_int_grid, 0, 0);
    lv_obj_add_flag(s_int_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_int_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_int_grid, LV_SCROLLBAR_MODE_OFF);

    s_int_status = make_label(scr, 40, 895, 520, 24,
                              "Tu piso se ilumina al llegar.", &F19, INT_MUTED,
                              LV_TEXT_ALIGN_CENTER);
    make_footer(scr, 1, INT_ACCENT);
}

/* ------------------------------------------------------------------ */
/*  Pantalla 3: TECNICA                                               */
/* ------------------------------------------------------------------ */

static void open_blocks_cb(lv_event_t *e);
static void rebuild_block_rows(void);

static void tec_note(const char *txt)
{
    if (s_tec_note != NULL) {
        lv_label_set_text(s_tec_note, txt);
    }
}

static void rebuild_floor_dropdown(void)
{
    if (s_tec_dd_floor == NULL) {
        return;
    }
    app_state_t *app = app_state();
    char opts[ELEVATOR_MAX_FLOORS * 10];
    opts[0] = '\0';
    for (uint8_t f = 1; f <= app->cfg.floors; f++) {
        char tmp[12];
        snprintf(tmp, sizeof(tmp), "%sPiso %u", f == 1 ? "" : "\n", f);
        strcat(opts, tmp);
    }
    if (s_sel_floor > app->cfg.floors) {
        s_sel_floor = app->cfg.floors;
    }
    lv_dropdown_set_options(s_tec_dd_floor, opts);
    lv_dropdown_set_selected(s_tec_dd_floor, s_sel_floor - 1);
}

static void refresh_tech_labels(void)
{
    app_state_t *app = app_state();
    char buf[64];

    if (s_tec_count != NULL) {
        snprintf(buf, sizeof(buf), "%u", app->cfg.floors);
        lv_label_set_text(s_tec_count, buf);
    }
    const char *mode = app->cfg.demo_mode ? "MODO DEMO" : "GPIO REALES";
    if (s_tec_mode_lbl != NULL) {
        lv_label_set_text(s_tec_mode_lbl, mode);
    }
    if (s_tec_mode_btn_lbl != NULL) {
        lv_label_set_text(s_tec_mode_btn_lbl, mode);
    }
    if (s_tec_dd_gpio != NULL) {
        lv_dropdown_set_selected(
            s_tec_dd_gpio, gpio_to_index(app->cfg.floor_gpio[s_sel_floor - 1]));
    }
    if (s_tec_dd_level != NULL) {
        lv_dropdown_set_selected(s_tec_dd_level,
                                 app->cfg.floor_level[s_sel_floor - 1] ? 1 : 0);
    }
    if (s_tec_then != NULL) {
        snprintf(buf, sizeof(buf), "ENTONCES piso %u + encender su luz",
                 s_sel_floor);
        lv_label_set_text(s_tec_then, buf);
    }
    if (s_tec_read != NULL) {
        bool has_gpio = app->cfg.floor_gpio[s_sel_floor - 1] != 0xFF;
        elevator_state_t st;
        elevator_get_state(&st);
        if (app->cfg.demo_mode) {
            snprintf(buf, sizeof(buf), "Lectura: modo demo");
        } else if (st.position_known) {
            snprintf(buf, sizeof(buf), "Piso detectado: %u · %s",
                     st.current_floor, has_gpio ? "GPIO activo" : "sin GPIO");
        } else {
            snprintf(buf, sizeof(buf), "Lectura: %s",
                     has_gpio ? "GPIO activo" : "sin GPIO");
        }
        lv_label_set_text(s_tec_read, buf);
    }
    if (s_blk_status != NULL) {
        snprintf(buf, sizeof(buf), "BLOQUES ACTIVOS %u/%u",
                 (unsigned)rules_active_count(), (unsigned)RULES_MAX_BLOCKS);
        lv_label_set_text(s_blk_status, buf);
    }
}

static void tec_floors_cb(lv_event_t *e)
{
    intptr_t id = (intptr_t)lv_event_get_user_data(e);
    app_state_t *app = app_state();

    if (!app->cfg.demo_mode) {
        tec_note("Desactiva GPIO antes de cambiar los pisos.");
        return;
    }
    if (app->cfg.blocks_enabled) {
        tec_note("Desactiva los bloques antes de usar sensores de piso.");
        return;
    }
    if (id == ID_MINUS && app->cfg.floors > ELEVATOR_MIN_FLOORS) {
        app->cfg.floors--;
    } else if (id == ID_PLUS && app->cfg.floors < ELEVATOR_MAX_FLOORS) {
        app->cfg.floors++;
    } else {
        return;
    }
    if (s_user_floor > app->cfg.floors) {
        s_user_floor = app->cfg.floors;
    }
    app_apply_config();
    rebuild_floor_dropdown();
    rebuild_interior_buttons();
    refresh_tech_labels();
    ui_refresh();
}

static void tec_mode_cb(lv_event_t *e)
{
    (void)e;
    app_state_t *app = app_state();

    if (app->cfg.demo_mode) {
        bool any = false;
        for (int i = 0; i < app->cfg.floors; i++) {
            if (app->cfg.floor_gpio[i] != 0xFF) {
                any = true;
            }
        }
        if (!any) {
            tec_note("Asigna al menos un GPIO a un piso.");
            return;
        }
    }
    app->cfg.demo_mode = !app->cfg.demo_mode;
    app_apply_config();
    tec_note(app->cfg.demo_mode ? "Demo activa: posición simulada."
                                : "GPIO reales activos. Filtrado de 30 ms.");
    refresh_tech_labels();
    ui_refresh();
}

static void tec_floor_sel_cb(lv_event_t *e)
{
    s_sel_floor = (uint8_t)(lv_dropdown_get_selected(lv_event_get_target(e)) + 1);
    refresh_tech_labels();
}

static void tec_gpio_cb(lv_event_t *e)
{
    app_state_t *app = app_state();
    int sel = (int)lv_dropdown_get_selected(lv_event_get_target(e));

    if (!app->cfg.demo_mode) {
        tec_note("Cambia a DEMO antes de editar las entradas.");
        refresh_tech_labels();
        return;
    }
    uint8_t gpio = (sel == 0) ? 0xFF : (uint8_t)s_gpios[sel - 1];
    for (int i = 0; i < app->cfg.floors; i++) {
        if (i != (s_sel_floor - 1) && gpio != 0xFF &&
            app->cfg.floor_gpio[i] == gpio) {
            tec_note("Ese GPIO ya identifica otro piso.");
            refresh_tech_labels();
            return;
        }
    }
    app->cfg.floor_gpio[s_sel_floor - 1] = gpio;
    app_apply_config();
    tec_note("Regla GPIO guardada.");
    refresh_tech_labels();
}

static void tec_level_cb(lv_event_t *e)
{
    app_state_t *app = app_state();
    if (!app->cfg.demo_mode) {
        tec_note("Cambia a DEMO antes de editar las entradas.");
        refresh_tech_labels();
        return;
    }
    app->cfg.floor_level[s_sel_floor - 1] =
        (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
    app_apply_config();
    tec_note("Regla GPIO guardada.");
}

static void create_tecnica(lv_obj_t *scr)
{
    make_label(scr, 44, 42, 500, 28, "ASCENSOR / TÉCNICA", &F21, TEC_ACCENT,
               LV_TEXT_ALIGN_LEFT);
    make_label(scr, 44, 110, 512, 48, "Configura tu edificio", &F30, TEC_TEXT,
               LV_TEXT_ALIGN_LEFT);

    lv_obj_t *p = make_panel(scr, 44, 184, 512, 232);
    make_label(p, 24, 20, 440, 30, "Cantidad de pisos", &F25, TEC_TEXT,
               LV_TEXT_ALIGN_LEFT);
    s_tec_count = make_label(p, 24, 64, 170, 56, "", &F59, TEC_ACCENT,
                             LV_TEXT_ALIGN_LEFT);
    make_button(p, 272, 58, 90, 64, "-", tec_floors_cb, ID_MINUS, NULL);
    make_button(p, 386, 58, 90, 64, "+", tec_floors_cb, ID_PLUS, NULL);
    make_label(p, 24, 154, 220, 24, "Origen de posición", &F21, TEC_MUTED,
               LV_TEXT_ALIGN_LEFT);
    s_tec_mode_lbl = make_label(p, 24, 182, 180, 32, "", &F30, TEC_TEXT,
                                LV_TEXT_ALIGN_LEFT);
    make_button(p, 248, 150, 230, 60, "MODO DEMO", tec_mode_cb, ID_MODE,
                &s_tec_mode_btn_lbl);

    make_label(scr, 44, 453, 290, 36, "Regla por piso", &F30, TEC_TEXT,
               LV_TEXT_ALIGN_LEFT);
    s_tec_dd_floor = lv_dropdown_create(scr);
    lv_obj_set_pos(s_tec_dd_floor, 350, 448);
    lv_obj_set_size(s_tec_dd_floor, 206, 52);
    style_dropdown(s_tec_dd_floor, &F25);
    lv_obj_add_event_cb(s_tec_dd_floor, tec_floor_sel_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *rule = make_panel(scr, 44, 526, 512, 180);
    lv_obj_set_style_border_color(rule, lv_color_hex(COL_BLUE_BORDER), 0);
    make_label(rule, 24, 12, 450, 27, "SI entrada digital", &F25, TEC_ACCENT,
               LV_TEXT_ALIGN_LEFT);
    s_tec_dd_gpio = lv_dropdown_create(rule);
    lv_dropdown_set_options(s_tec_dd_gpio, s_gpio_options);
    lv_obj_set_pos(s_tec_dd_gpio, 24, 53);
    lv_obj_set_size(s_tec_dd_gpio, 250, 52);
    style_dropdown(s_tec_dd_gpio, &F25);
    lv_obj_add_event_cb(s_tec_dd_gpio, tec_gpio_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tec_dd_level = lv_dropdown_create(rule);
    lv_dropdown_set_options(s_tec_dd_level, "= 0\n= 1");
    lv_obj_set_pos(s_tec_dd_level, 300, 53);
    lv_obj_set_size(s_tec_dd_level, 185, 52);
    style_dropdown(s_tec_dd_level, &F25);
    lv_obj_add_event_cb(s_tec_dd_level, tec_level_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);

    s_tec_read = make_label(rule, 24, 126, 460, 32, "", &F25, TEC_TEXT,
                            LV_TEXT_ALIGN_LEFT);

    lv_obj_t *then = make_panel(scr, 44, 725, 512, 83);
    lv_obj_set_style_border_color(then, lv_color_hex(COL_OK), 0);
    s_tec_then = make_label(then, 24, 26, 465, 32, "", &F25, TEC_GREEN,
                            LV_TEXT_ALIGN_LEFT);

    s_tec_note = make_label(scr, 40, 823, 520, 45, "", &F19, TEC_MUTED,
                            LV_TEXT_ALIGN_CENTER);
    make_button(scr, 44, 878, 512, 57, "EDITAR BLOQUES Y NOTAS", open_blocks_cb,
                ID_EDIT, NULL);
    make_footer(scr, 2, TEC_ACCENT);
}

/* ------------------------------------------------------------------ */
/*  Editor de bloques (pantalla propia, 32 bloques compartidos)       */
/* ------------------------------------------------------------------ */

static void rebuild_block_rows_async(void *unused)
{
    (void)unused;
    rebuild_block_rows();
}

static void close_blocks_cb(lv_event_t *e)
{
    (void)e;
    lv_scr_load_anim(s_scr[2], LV_SCR_LOAD_ANIM_OVER_RIGHT, 200, 0, false);
}

static void open_blocks_cb(lv_event_t *e)
{
    (void)e;
    refresh_tech_labels();
    lv_scr_load_anim(s_scr_blocks, LV_SCR_LOAD_ANIM_OVER_LEFT, 200, 0, false);
}

static bool blocks_editable(void)
{
    if (!app_state()->cfg.demo_mode) {
        ui_show_message("Bloques", "Cambia a DEMO antes de editar bloques.");
        return false;
    }
    if (app_state()->cfg.blocks_enabled) {
        ui_show_message("Reglas activas",
                        "Desactiva las reglas para editarlas.");
        return false;
    }
    return true;
}

static void blk_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    char err[RULES_ERR_MAX] = { 0 };
    if (app_set_blocks_enabled(on, err, sizeof(err)) != ESP_OK) {
        if (on) {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        }
        ui_show_message("No se pudo activar", err);
        return;
    }
    refresh_tech_labels();
}

static void blk_add_cb(lv_event_t *e)
{
    (void)e;
    if (!blocks_editable()) {
        return;
    }
    if (app_add_block(NULL) != ESP_OK) {
        ui_show_message("Capacidad completa",
                        "32 bloques compartidos entre todos los pisos.");
        return;
    }
    app_save_blocks();
    rebuild_block_rows();
    refresh_tech_labels();
}

static void blk_delete_cb(lv_event_t *e)
{
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (!blocks_editable()) {
        return;
    }
    app_remove_block(idx);
    lv_async_call(rebuild_block_rows_async, NULL);
    refresh_tech_labels();
}

static void blk_dd_cb(lv_event_t *e)
{
    uint32_t packed = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    size_t idx    = packed >> 8;
    uint8_t field = packed & 0xFF;
    int sel = (int)lv_dropdown_get_selected(lv_event_get_target(e));

    app_state_t *app = app_state();
    if (idx >= app->block_count) {
        return;
    }
    rule_block_t *b = &app->blocks[idx];

    switch (field) {
    case 0: b->in_gpio    = (sel == 0) ? 0xFF : (uint8_t)s_gpios[sel - 1]; break;
    case 1: b->in_level   = (uint8_t)sel; break;
    case 2: b->out_gpio   = (sel == 0) ? 0xFF : (uint8_t)s_gpios[sel - 1]; break;
    case 3: b->out_level  = (uint8_t)sel; break;
    case 4: b->rest_level = (uint8_t)sel; break;
    default: break;
    }
    app_save_blocks();
}

static lv_obj_t *blk_dropdown(lv_obj_t *parent, const char *options,
                              int selected, int w, size_t idx, uint8_t field)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, selected);
    lv_obj_set_width(dd, w);
    style_dropdown(dd, &F21);
    lv_obj_add_event_cb(dd, blk_dd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)((idx << 8) | field));
    return dd;
}

static lv_obj_t *blk_text(lv_obj_t *parent, const char *txt, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &F19, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

static void rebuild_block_rows(void)
{
    if (s_blk_list == NULL) {
        return;
    }
    lv_obj_clean(s_blk_list);
    app_state_t *app = app_state();

    for (size_t i = 0; i < app->block_count; i++) {
        rule_block_t *b = &app->blocks[i];

        lv_obj_t *box = make_panel(s_blk_list, 0, 0, 512, LV_SIZE_CONTENT);
        lv_obj_set_width(box, 512);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(box, 14, 0);
        lv_obj_set_style_pad_row(box, 10, 0);

        lv_obj_t *head = lv_obj_create(box);
        lv_obj_remove_style_all(head);
        lv_obj_set_size(head, lv_pct(100), 48);
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        char title[32];
        snprintf(title, sizeof(title), "Bloque %u", (unsigned)(i + 1));
        lv_obj_t *t = blk_text(head, title, TEC_TEXT);
        lv_obj_set_style_text_font(t, &F25, 0);
        make_button(head, 0, 0, 140, 44, "ELIMINAR", blk_delete_cb, (intptr_t)i,
                    NULL);
        lv_obj_set_pos(lv_obj_get_child(head, 1), 0, 0);

        lv_obj_t *row = lv_obj_create(box);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(row, 8, 0);
        lv_obj_set_style_pad_column(row, 8, 0);

        blk_text(row, "SI", TEC_MUTED);
        blk_dropdown(row, s_gpio_options, gpio_to_index(b->in_gpio), 170, i, 0);
        blk_dropdown(row, "ES IGUAL A 0\nES IGUAL A 1", b->in_level, 190, i, 1);
        blk_text(row, "ENTONCES", TEC_MUTED);
        blk_dropdown(row, s_gpio_options, gpio_to_index(b->out_gpio), 170, i, 2);
        blk_dropdown(row, "ESCRIBIR 0\nESCRIBIR 1", b->out_level, 190, i, 3);
        blk_text(row, "REPOSO", TEC_MUTED);
        blk_dropdown(row, "= 0\n= 1", b->rest_level, 110, i, 4);

        if (b->note[0] != '\0') {
            blk_text(box, b->note, TEC_MUTED);
        }
    }

    if (app->block_count == 0) {
        blk_text(s_blk_list, "Sin bloques. Usa + Agregar condicional.", TEC_MUTED);
    }
}

static void create_blocks_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BLACK), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_scr_blocks = scr;

    make_label(scr, 44, 42, 330, 28, "EDITAR BLOQUES Y NOTAS", &F21, TEC_ACCENT,
               LV_TEXT_ALIGN_LEFT);
    make_button(scr, 420, 30, 136, 50, "VOLVER", close_blocks_cb, 0, NULL);

    lv_obj_t *head = make_panel(scr, 44, 100, 512, 120);
    make_label(head, 24, 14, 330, 30, "Programación por bloques", &F25, TEC_TEXT,
               LV_TEXT_ALIGN_LEFT);
    s_blk_sw = lv_switch_create(head);
    lv_obj_set_pos(s_blk_sw, 400, 14);
    lv_obj_set_size(s_blk_sw, 90, 46);
    lv_obj_add_event_cb(s_blk_sw, blk_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_blk_status = make_label(head, 24, 70, 460, 28, "", &F21, TEC_MUTED,
                              LV_TEXT_ALIGN_LEFT);
    make_button(scr, 44, 234, 512, 60, "+ Agregar condicional a este piso",
                blk_add_cb, 0, NULL);

    s_blk_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_blk_list);
    lv_obj_set_pos(s_blk_list, 44, 308);
    lv_obj_set_size(s_blk_list, 512, 690);
    lv_obj_set_flex_flow(s_blk_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_blk_list, 12, 0);
    lv_obj_add_flag(s_blk_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_blk_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_blk_list, LV_SCROLLBAR_MODE_OFF);
}

/* ------------------------------------------------------------------ */
/*  Creacion y refresco                                               */
/* ------------------------------------------------------------------ */

esp_err_t ui_create(void)
{
    if (!board_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    init_fonts();
    build_gpio_options();

    for (int i = 0; i < 3; i++) {
        s_scr[i] = new_screen();
    }
    create_exterior(s_scr[0]);
    create_interior(s_scr[1]);
    create_tecnica(s_scr[2]);
    create_blocks_screen();

    rebuild_interior_buttons();
    rebuild_floor_dropdown();
    rebuild_block_rows();
    refresh_tech_labels();

    if (app_state()->cfg.blocks_enabled) {
        lv_obj_add_state(s_blk_sw, LV_STATE_CHECKED);
    }

    s_cur_scr = 0;
    lv_scr_load(s_scr[0]);

    board_unlock();
    return ESP_OK;
}

static void update_floor_button_states(const elevator_state_t *st)
{
    for (uint8_t f = 0; f < s_int_btn_count; f++) {
        lv_obj_t *btn = s_int_btn[f];
        if (btn == NULL) {
            continue;
        }
        bool here = (f + 1) == st->current_floor;
        lv_obj_set_style_bg_color(btn, lv_color_hex(here ? COL_CALL : COL_BTN), 0);
    }
}

void ui_refresh(void)
{
    if (!board_lock(100)) {
        return;
    }

    elevator_state_t st;
    elevator_get_state(&st);

    char buf[64];

    /* Exterior */
    snprintf(buf, sizeof(buf), "P%02u", s_user_floor);
    lv_label_set_text(s_ext_user_floor, buf);

    if (st.position_known) {
        snprintf(buf, sizeof(buf), "P%02u", st.current_floor);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_ext_cabin_floor, buf);

    const char *dir_txt;
    if (st.dir == ELEV_DIR_UP) {
        snprintf(buf, sizeof(buf), "%s · P%02u", "SUBIENDO", st.current_floor);
        dir_txt = buf;
    } else if (st.dir == ELEV_DIR_DOWN) {
        snprintf(buf, sizeof(buf), "%s · P%02u", "BAJANDO", st.current_floor);
        dir_txt = buf;
    } else {
        dir_txt = st.doors_open ? "DETENIDO · PUERTAS ABIERTAS"
                                : "DETENIDO · PUERTAS CERRADAS";
    }
    lv_label_set_text(s_ext_dir, dir_txt);
    lv_obj_set_style_text_color(
        s_ext_dir, lv_color_hex(st.dir == ELEV_DIR_IDLE ? EXT_MUTED : COL_OK), 0);

    lv_label_set_text(s_ext_arrow,
                      st.dir == ELEV_DIR_UP ? LV_SYMBOL_UP
                      : st.dir == ELEV_DIR_DOWN ? LV_SYMBOL_DOWN : "");
    lv_obj_set_style_text_color(s_ext_arrow, lv_color_hex(COL_OK), 0);

    /* Interior */
    if (st.position_known) {
        snprintf(buf, sizeof(buf), "P%02u", st.current_floor);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_int_current, buf);
    lv_label_set_text(s_int_dir,
                      st.dir == ELEV_DIR_UP ? "subiendo"
                      : st.dir == ELEV_DIR_DOWN ? "bajando" : "");
    update_floor_button_states(&st);

    int changed = memcmp(&st, &s_last_state, sizeof(st)) != 0;
    if (changed && !st.position_known && !st.demo_mode) {
        lv_label_set_text(s_int_status,
                          "Posición sin confirmar: espera un sensor de piso.");
    }
    s_last_state = st;

    board_unlock();
}

void ui_on_event(const elevator_event_t *ev)
{
    if (ev == NULL) {
        return;
    }
    if (ev->message != NULL) {
        if (!board_lock(100)) {
            return;
        }
        lv_label_set_text(s_int_status, ev->message);
        lv_label_set_text(s_ext_hint, ev->message);
        board_unlock();
    }
    if (ev->type == ELEV_EVT_ARRIVED) {
        board_sound(BOARD_SOUND_ARRIVAL);
    } else if (ev->type == ELEV_EVT_POSITION) {
        board_sound(BOARD_SOUND_INTERMEDIATE);
    }
}
