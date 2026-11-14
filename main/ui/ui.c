/**
 * @file ui.c
 * @brief Tres pantallas deslizables (Exterior / Interior / Tecnica) en LVGL 8.
 *
 * La estructura reproduce la del firmware original: un tileview vertical con
 * tres paginas y el aviso "Desliza para cambiar de pantalla".
 *
 * Nota sobre acentos: las fuentes Montserrat incluidas en LVGL 8 solo traen
 * ASCII. Los textos van sin tildes a proposito para que se rendericen siempre.
 * Ver README para activar el rango extendido.
 */

#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "app.h"
#include "board.h"
#include "board_config.h"
#include "elevator.h"
#include "rules.h"

/* ------------------------------------------------------------------ */
/*  Paleta y constantes                                               */
/* ------------------------------------------------------------------ */

#define COL_BG      0x0D1117
#define COL_CARD    0x161B22
#define COL_BORDER  0x30363D
#define COL_ACCENT  0x2F81F7
#define COL_OK      0x3FB950
#define COL_WARN    0xD29922
#define COL_TEXT    0xE6EDF3
#define COL_MUTED   0x8B949E

#define SCREEN_W    LCD_H_RES
#define SCREEN_H    LCD_V_RES
#define PAD         18

static const int s_gpios[USER_GPIO_COUNT] = USER_GPIO_LIST;

static char s_gpio_options[220];

/* ------------------------------------------------------------------ */
/*  Widgets                                                           */
/* ------------------------------------------------------------------ */

static lv_obj_t *s_tv;

/* Exterior */
static lv_obj_t *s_ext_user_floor;
static lv_obj_t *s_ext_cabin_floor;
static lv_obj_t *s_ext_dir;
static lv_obj_t *s_ext_status;

/* Interior */
static lv_obj_t *s_int_current;
static lv_obj_t *s_int_status;
static lv_obj_t *s_int_grid;
static lv_obj_t *s_int_btn[ELEVATOR_MAX_FLOORS];
static uint8_t   s_int_btn_count;

/* Tecnica */
static lv_obj_t *s_tech_floors_value;
static lv_obj_t *s_tech_mode;
static lv_obj_t *s_tech_mode_sw;
static lv_obj_t *s_tech_blocks_status;
static lv_obj_t *s_tech_floor_list;
static lv_obj_t *s_tech_block_list;
static lv_obj_t *s_tech_blocks_sw;
static lv_obj_t *s_msgbox;

static lv_style_t s_st_card;
static lv_style_t s_st_btn;
static lv_style_t s_st_btn_accent;

static uint8_t s_user_floor = 1;
static elevator_state_t s_last_state;

/* ------------------------------------------------------------------ */
/*  Utilidades de estilo                                              */
/* ------------------------------------------------------------------ */

static lv_obj_t *card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &s_st_card, 0);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *text(lv_obj_t *parent, const char *txt,
                      const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *button(lv_obj_t *parent, const char *txt, lv_coord_t w,
                        lv_coord_t h, bool accent)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, accent ? &s_st_btn_accent : &s_st_btn, 0);
    lv_obj_set_size(btn, w, h);

    lv_obj_t *label = text(btn, txt, &lv_font_montserrat_20, COL_TEXT);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *column(lv_obj_t *parent)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_width(col, lv_pct(100));
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    return col;
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
/*  Mensajes                                                          */
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
    lv_obj_set_style_bg_color(s_msgbox, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_text_color(s_msgbox, lv_color_hex(COL_TEXT), 0);
    lv_obj_add_event_cb(s_msgbox, msgbox_close_cb, LV_EVENT_VALUE_CHANGED, NULL);
    board_unlock();
}

/* ------------------------------------------------------------------ */
/*  Pantalla 1: EXTERIOR                                              */
/* ------------------------------------------------------------------ */

static void ext_floor_prev_cb(lv_event_t *e)
{
    (void)e;
    if (s_user_floor > 1) {
        s_user_floor--;
    }
    ui_refresh();
}

static void ext_floor_next_cb(lv_event_t *e)
{
    (void)e;
    uint8_t floors = app_state()->cfg.floors;
    if (s_user_floor < floors) {
        s_user_floor++;
    }
    ui_refresh();
}

static void ext_call_up_cb(lv_event_t *e)
{
    (void)e;
    board_sound(BOARD_SOUND_PRESS);
    elevator_call(s_user_floor, ELEV_DIR_UP);
    ui_refresh();
}

static void ext_call_down_cb(lv_event_t *e)
{
    (void)e;
    board_sound(BOARD_SOUND_PRESS);
    elevator_call(s_user_floor, ELEV_DIR_DOWN);
    ui_refresh();
}

static void create_exterior(lv_obj_t *tile)
{
    lv_obj_t *col = column(tile);

    text(col, "ASCENSOR / EXTERIOR", &lv_font_montserrat_20, COL_MUTED);

    lv_obj_t *select = card(col, lv_pct(100), 190);
    lv_obj_set_flex_flow(select, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(select, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(select, 10, 0);

    text(select, "ESTAS EN EL PISO", &lv_font_montserrat_20, COL_MUTED);

    lv_obj_t *row = lv_obj_create(select);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 90);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *prev = button(row, "<", 90, 80, false);
    lv_obj_add_event_cb(prev, ext_floor_prev_cb, LV_EVENT_CLICKED, NULL);

    s_ext_user_floor = text(row, "P01", &lv_font_montserrat_36, COL_TEXT);

    lv_obj_t *next = button(row, ">", 90, 80, false);
    lv_obj_add_event_cb(next, ext_floor_next_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *status = card(col, lv_pct(100), 230);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(status, 8, 0);

    text(status, "EL ASCENSOR ESTA EN", &lv_font_montserrat_20, COL_MUTED);
    s_ext_cabin_floor = text(status, "P01", &lv_font_montserrat_36, COL_ACCENT);
    s_ext_dir = text(status, "DETENIDO - PUERTAS CERRADAS",
                     &lv_font_montserrat_20, COL_WARN);
    s_ext_status = text(status, "Tu piso se ilumina al llegar.",
                        &lv_font_montserrat_14, COL_MUTED);

    text(col, "HACIA DONDE VAS?", &lv_font_montserrat_20, COL_MUTED);

    lv_obj_t *row2 = lv_obj_create(col);
    lv_obj_remove_style_all(row2);
    lv_obj_set_size(row2, lv_pct(100), 100);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row2, 14, 0);

    lv_obj_t *up = button(row2, "SUBIR", lv_pct(49), 100, true);
    lv_obj_add_event_cb(up, ext_call_up_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *down = button(row2, "BAJAR", lv_pct(49), 100, true);
    lv_obj_add_event_cb(down, ext_call_down_cb, LV_EVENT_CLICKED, NULL);

    text(col, "<  Desliza para cambiar de pantalla  >",
         &lv_font_montserrat_14, COL_MUTED);
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

    for (uint8_t f = 1; f <= floors; f++) {
        char txt[8];
        snprintf(txt, sizeof(txt), "P%02u", f);
        lv_obj_t *btn = button(s_int_grid, txt, lv_pct(31), 90, false);
        lv_obj_add_event_cb(btn, int_floor_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)f);
        s_int_btn[f - 1] = btn;
    }
    s_last_state.current_floor = 0xFF;  /* fuerza refresco */
}

static void create_interior(lv_obj_t *tile)
{
    lv_obj_t *col = column(tile);

    text(col, "ASCENSOR / INTERIOR", &lv_font_montserrat_20, COL_MUTED);

    lv_obj_t *here = card(col, lv_pct(100), 150);
    lv_obj_set_flex_flow(here, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(here, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    text(here, "ESTAS AQUI", &lv_font_montserrat_20, COL_MUTED);
    s_int_current = text(here, "P01", &lv_font_montserrat_36, COL_ACCENT);

    text(col, "A QUE PISO QUIERES IR?", &lv_font_montserrat_20, COL_MUTED);

    s_int_grid = card(col, lv_pct(100), 430);
    lv_obj_set_flex_flow(s_int_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_int_grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_int_grid, 12, 0);
    lv_obj_add_flag(s_int_grid, LV_OBJ_FLAG_SCROLLABLE);

    s_int_status = text(col, "Elige un piso", &lv_font_montserrat_14, COL_MUTED);
}

/* ------------------------------------------------------------------ */
/*  Pantalla 3: TECNICA                                               */
/* ------------------------------------------------------------------ */

static void tech_floors_minus_cb(lv_event_t *e);
static void tech_floors_plus_cb(lv_event_t *e);
static void tech_mode_cb(lv_event_t *e);
static void tech_blocks_sw_cb(lv_event_t *e);
static void tech_add_block_cb(lv_event_t *e);
static void tech_floor_gpio_cb(lv_event_t *e);
static void tech_floor_level_cb(lv_event_t *e);
static void rebuild_floor_rows(void);
static void rebuild_block_rows(void);

/* Reconstrucciones diferidas: no se puede borrar el widget que origino el
 * evento mientras LVGL lo esta procesando. */
static void rebuild_floor_rows_async(void *unused)
{
    (void)unused;
    rebuild_floor_rows();
}

static void rebuild_block_rows_async(void *unused)
{
    (void)unused;
    rebuild_block_rows();
}

static void refresh_tech_labels(void)
{
    app_state_t *app = app_state();
    if (s_tech_floors_value != NULL) {
        lv_label_set_text_fmt(s_tech_floors_value, "%u", app->cfg.floors);
    }
    if (s_tech_mode != NULL) {
        lv_label_set_text(s_tech_mode, app->cfg.demo_mode
                                       ? "MODO DEMO" : "GPIO REALES");
    }
    if (s_tech_blocks_status != NULL) {
        lv_label_set_text_fmt(s_tech_blocks_status, "BLOQUES ACTIVOS %u/%u",
                              (unsigned)rules_active_count(),
                              (unsigned)RULES_MAX_BLOCKS);
    }
}

static void rebuild_floor_rows(void)
{
    if (s_tech_floor_list == NULL) {
        return;
    }
    lv_obj_clean(s_tech_floor_list);

    app_state_t *app = app_state();

    for (uint8_t f = 1; f <= app->cfg.floors; f++) {
        lv_obj_t *row = card(s_tech_floor_list, lv_pct(100), 84);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);

        char txt[16];
        snprintf(txt, sizeof(txt), "Piso %u", f);
        text(row, txt, &lv_font_montserrat_20, COL_TEXT);

        lv_obj_t *dd_gpio = lv_dropdown_create(row);
        lv_dropdown_set_options(dd_gpio, s_gpio_options);
        lv_dropdown_set_selected(dd_gpio,
                                 gpio_to_index(app->cfg.floor_gpio[f - 1]));
        lv_obj_set_width(dd_gpio, 170);
        lv_obj_set_style_text_font(dd_gpio, &lv_font_montserrat_14, 0);
        lv_obj_add_event_cb(dd_gpio, tech_floor_gpio_cb,
                            LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)f);

        lv_obj_t *dd_level = lv_dropdown_create(row);
        lv_dropdown_set_options(dd_level, "NIVEL 0\nNIVEL 1");
        lv_dropdown_set_selected(dd_level, app->cfg.floor_level[f - 1]);
        lv_obj_set_width(dd_level, 120);
        lv_obj_set_style_text_font(dd_level, &lv_font_montserrat_14, 0);
        lv_obj_add_event_cb(dd_level, tech_floor_level_cb,
                            LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)f);
    }
}

static void tech_floor_gpio_cb(lv_event_t *e)
{
    uint8_t floor = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_t *dd = lv_event_get_target(e);
    int sel = (int)lv_dropdown_get_selected(dd);

    app_state_t *app = app_state();
    app->cfg.floor_gpio[floor - 1] = (sel == 0) ? 0xFF : (uint8_t)s_gpios[sel - 1];

    /* Un GPIO solo puede identificar un piso */
    for (int i = 0; i < app->cfg.floors; i++) {
        if (i != (floor - 1) &&
            app->cfg.floor_gpio[i] == app->cfg.floor_gpio[floor - 1] &&
            app->cfg.floor_gpio[i] != 0xFF) {
            app->cfg.floor_gpio[floor - 1] = 0xFF;
            lv_async_call(rebuild_floor_rows_async, NULL);
            ui_show_message("GPIO repetido", "Ese GPIO ya identifica otro piso.");
            return;
        }
    }

    app_apply_config();
    ui_show_message("Regla GPIO guardada",
                    "Sensor de piso actualizado. Entradas a 3.3 V.");
    ui_refresh();
}

static void tech_floor_level_cb(lv_event_t *e)
{
    uint8_t floor = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_t *dd = lv_event_get_target(e);
    app_state_t *app = app_state();
    app->cfg.floor_level[floor - 1] = (uint8_t)lv_dropdown_get_selected(dd);
    app_apply_config();
}

static void tech_floors_minus_cb(lv_event_t *e)
{
    (void)e;
    app_state_t *app = app_state();
    if (app->cfg.blocks_enabled) {
        ui_show_message("Pisos bloqueados",
                        "Desactiva los bloques antes de usar sensores de piso.");
        return;
    }
    if (app->cfg.floors > ELEVATOR_MIN_FLOORS) {
        app->cfg.floors--;
        app_apply_config();
        rebuild_floor_rows();
        rebuild_interior_buttons();
        refresh_tech_labels();
        ui_refresh();
    }
}

static void tech_floors_plus_cb(lv_event_t *e)
{
    (void)e;
    app_state_t *app = app_state();
    if (app->cfg.blocks_enabled) {
        ui_show_message("Pisos bloqueados",
                        "Desactiva los bloques antes de usar sensores de piso.");
        return;
    }
    if (app->cfg.floors < ELEVATOR_MAX_FLOORS) {
        app->cfg.floors++;
        app_apply_config();
        rebuild_floor_rows();
        rebuild_interior_buttons();
        refresh_tech_labels();
        ui_refresh();
    }
}

static void tech_mode_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool demo = lv_obj_has_state(sw, LV_STATE_CHECKED);
    app_state_t *app = app_state();
    app->cfg.demo_mode = demo;
    app_apply_config();
    refresh_tech_labels();
    ui_refresh();
}

static void tech_blocks_sw_cb(lv_event_t *e)
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

static void tech_add_block_cb(lv_event_t *e)
{
    (void)e;
    app_state_t *app = app_state();
    if (app->cfg.blocks_enabled) {
        ui_show_message("Reglas activas",
                        "Desactiva las reglas antes de agregar bloques.");
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

static void block_delete_cb(lv_event_t *e)
{
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    app_state_t *app = app_state();
    if (app->cfg.blocks_enabled) {
        ui_show_message("Reglas activas", "Desactiva las reglas para editarlas.");
        return;
    }
    app_remove_block(idx);
    lv_async_call(rebuild_block_rows_async, NULL);
    refresh_tech_labels();
}

static void block_dd_cb(lv_event_t *e)
{
    uint32_t packed = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    size_t idx  = packed >> 8;
    uint8_t field = packed & 0xFF;
    lv_obj_t *dd = lv_event_get_target(e);
    int sel = (int)lv_dropdown_get_selected(dd);

    app_state_t *app = app_state();
    if (idx >= app->block_count) {
        return;
    }
    rule_block_t *b = &app->blocks[idx];

    switch (field) {
    case 0: b->in_gpio   = (sel == 0) ? 0xFF : (uint8_t)s_gpios[sel - 1]; break;
    case 1: b->in_level  = (uint8_t)sel; break;
    case 2: b->out_gpio  = (sel == 0) ? 0xFF : (uint8_t)s_gpios[sel - 1]; break;
    case 3: b->out_level = (uint8_t)sel; break;
    case 4: b->rest_level = (uint8_t)sel; break;
    default: break;
    }
    app_save_blocks();
}

static lv_obj_t *block_dropdown(lv_obj_t *parent, const char *options,
                                int selected, size_t idx, uint8_t field)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, selected);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(dd, block_dd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)((idx << 8) | field));
    return dd;
}

static void rebuild_block_rows(void)
{
    if (s_tech_block_list == NULL) {
        return;
    }
    lv_obj_clean(s_tech_block_list);

    app_state_t *app = app_state();

    for (size_t i = 0; i < app->block_count; i++) {
        rule_block_t *b = &app->blocks[i];

        lv_obj_t *box = card(s_tech_block_list, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, 8, 0);

        lv_obj_t *head = lv_obj_create(box);
        lv_obj_remove_style_all(head);
        lv_obj_set_size(head, lv_pct(100), 40);
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        char title[32];
        snprintf(title, sizeof(title), "Piso %u / Bloque %u",
                 (unsigned)(i + 1), (unsigned)(i + 1));
        text(head, title, &lv_font_montserrat_20, COL_TEXT);

        lv_obj_t *del = button(head, "ELIMINAR", 130, 40, false);
        lv_obj_add_event_cb(del, block_delete_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        lv_obj_t *row = lv_obj_create(box);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(row, 8, 0);
        lv_obj_set_style_pad_column(row, 6, 0);

        text(row, "SI", &lv_font_montserrat_14, COL_MUTED);
        lv_obj_t *in_dd = block_dropdown(row, s_gpio_options,
                                         gpio_to_index(b->in_gpio), i, 0);
        lv_obj_set_width(in_dd, 150);

        lv_obj_t *lvl = block_dropdown(row, "ES IGUAL A 0\nES IGUAL A 1",
                                       b->in_level, i, 1);
        lv_obj_set_width(lvl, 150);

        text(row, "ENTONCES", &lv_font_montserrat_14, COL_MUTED);
        lv_obj_t *out_dd = block_dropdown(row, s_gpio_options,
                                          gpio_to_index(b->out_gpio), i, 2);
        lv_obj_set_width(out_dd, 150);

        lv_obj_t *out_lvl = block_dropdown(row, "ESCRIBIR 0\nESCRIBIR 1",
                                           b->out_level, i, 3);
        lv_obj_set_width(out_lvl, 140);

        text(row, "REPOSO", &lv_font_montserrat_14, COL_MUTED);
        lv_obj_t *rest = block_dropdown(row, "REPOSO 0\nREPOSO 1",
                                        b->rest_level, i, 4);
        lv_obj_set_width(rest, 130);

        if (b->note[0] != '\0') {
            text(box, b->note, &lv_font_montserrat_14, COL_MUTED);
        }
    }

    if (app->block_count == 0) {
        text(s_tech_block_list, "Sin bloques. Usa + Agregar condicional.",
             &lv_font_montserrat_14, COL_MUTED);
    }
}

static void create_tecnica(lv_obj_t *tile)
{
    lv_obj_t *col = column(tile);
    lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(col, lv_pct(100));

    text(col, "ASCENSOR / TECNICA", &lv_font_montserrat_20, COL_MUTED);
    text(col, "Configura tu edificio", &lv_font_montserrat_14, COL_TEXT);

    /* Cantidad de pisos */
    lv_obj_t *floors = card(col, lv_pct(100), 90);
    lv_obj_set_flex_flow(floors, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(floors, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    text(floors, "Cantidad de pisos", &lv_font_montserrat_20, COL_TEXT);

    lv_obj_t *ctrl = lv_obj_create(floors);
    lv_obj_remove_style_all(ctrl);
    lv_obj_set_size(ctrl, 210, 60);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *minus = button(ctrl, "-", 60, 56, false);
    s_tech_floors_value = text(ctrl, "4", &lv_font_montserrat_28, COL_TEXT);
    lv_obj_t *plus = button(ctrl, "+", 60, 56, false);
    lv_obj_add_event_cb(minus, tech_floors_minus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(plus, tech_floors_plus_cb, LV_EVENT_CLICKED, NULL);

    /* Origen de posicion */
    lv_obj_t *mode = card(col, lv_pct(100), 90);
    lv_obj_set_flex_flow(mode, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mode, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *mode_col = lv_obj_create(mode);
    lv_obj_remove_style_all(mode_col);
    lv_obj_set_flex_flow(mode_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(mode_col, lv_pct(60));
    text(mode_col, "Origen de posicion", &lv_font_montserrat_20, COL_TEXT);
    s_tech_mode = text(mode_col, "MODO DEMO", &lv_font_montserrat_14, COL_MUTED);

    s_tech_mode_sw = lv_switch_create(mode);
    lv_obj_set_size(s_tech_mode_sw, 90, 50);
    lv_obj_add_event_cb(s_tech_mode_sw, tech_mode_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);

    /* Regla por piso */
    text(col, "Regla por piso", &lv_font_montserrat_20, COL_TEXT);
    s_tech_floor_list = lv_obj_create(col);
    lv_obj_remove_style_all(s_tech_floor_list);
    lv_obj_set_width(s_tech_floor_list, lv_pct(100));
    lv_obj_set_height(s_tech_floor_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_tech_floor_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_tech_floor_list, 8, 0);

    /* Bloques */
    lv_obj_t *blocks = card(col, lv_pct(100), 100);
    lv_obj_set_flex_flow(blocks, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(blocks, 6, 0);

    lv_obj_t *blocks_head = lv_obj_create(blocks);
    lv_obj_remove_style_all(blocks_head);
    lv_obj_set_size(blocks_head, lv_pct(100), 50);
    lv_obj_set_flex_flow(blocks_head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(blocks_head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    text(blocks_head, "Programacion por bloques",
         &lv_font_montserrat_20, COL_TEXT);

    s_tech_blocks_sw = lv_switch_create(blocks_head);
    lv_obj_set_size(s_tech_blocks_sw, 90, 50);
    lv_obj_add_event_cb(s_tech_blocks_sw, tech_blocks_sw_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    s_tech_blocks_status = text(blocks, "BLOQUES ACTIVOS 0/32",
                                &lv_font_montserrat_14, COL_MUTED);

    lv_obj_t *add = button(col, "+ Agregar condicional a este piso",
                           lv_pct(100), 70, true);
    lv_obj_add_event_cb(add, tech_add_block_cb, LV_EVENT_CLICKED, NULL);

    s_tech_block_list = lv_obj_create(col);
    lv_obj_remove_style_all(s_tech_block_list);
    lv_obj_set_width(s_tech_block_list, lv_pct(100));
    lv_obj_set_height(s_tech_block_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_tech_block_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_tech_block_list, 10, 0);
}

/* ------------------------------------------------------------------ */
/*  Creacion y refresco                                               */
/* ------------------------------------------------------------------ */

static void init_styles(void)
{
    lv_style_init(&s_st_card);
    lv_style_set_bg_color(&s_st_card, lv_color_hex(COL_CARD));
    lv_style_set_bg_opa(&s_st_card, LV_OPA_COVER);
    lv_style_set_radius(&s_st_card, 14);
    lv_style_set_border_width(&s_st_card, 1);
    lv_style_set_border_color(&s_st_card, lv_color_hex(COL_BORDER));
    lv_style_set_pad_all(&s_st_card, 14);

    lv_style_init(&s_st_btn);
    lv_style_set_bg_color(&s_st_btn, lv_color_hex(COL_CARD));
    lv_style_set_bg_opa(&s_st_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_st_btn, 12);
    lv_style_set_border_width(&s_st_btn, 1);
    lv_style_set_border_color(&s_st_btn, lv_color_hex(COL_BORDER));

    lv_style_init(&s_st_btn_accent);
    lv_style_set_bg_color(&s_st_btn_accent, lv_color_hex(COL_ACCENT));
    lv_style_set_bg_opa(&s_st_btn_accent, LV_OPA_COVER);
    lv_style_set_radius(&s_st_btn_accent, 12);
}

esp_err_t ui_create(void)
{
    if (!board_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    build_gpio_options();
    init_styles();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_tv = lv_tileview_create(scr);
    lv_obj_set_size(s_tv, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_tv, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_tv, PAD, 0);

    lv_obj_t *t1 = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_BOTTOM);
    lv_obj_t *t2 = lv_tileview_add_tile(s_tv, 0, 1, LV_DIR_TOP | LV_DIR_BOTTOM);
    lv_obj_t *t3 = lv_tileview_add_tile(s_tv, 0, 2, LV_DIR_TOP);

    create_exterior(t1);
    create_interior(t2);
    create_tecnica(t3);

    rebuild_interior_buttons();
    rebuild_floor_rows();
    rebuild_block_rows();
    refresh_tech_labels();

    if (app_state()->cfg.demo_mode) {
        lv_obj_add_state(s_tech_mode_sw, LV_STATE_CHECKED);
    }
    if (app_state()->cfg.blocks_enabled) {
        lv_obj_add_state(s_tech_blocks_sw, LV_STATE_CHECKED);
    }

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
        if ((f + 1) == st->current_floor) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT), 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
        }
    }
}

void ui_refresh(void)
{
    if (!board_lock(100)) {
        return;
    }

    elevator_state_t st;
    elevator_get_state(&st);

    char buf[48];

    /* Exterior */
    if (s_ext_user_floor != NULL) {
        snprintf(buf, sizeof(buf), "P%02u", s_user_floor);
        lv_label_set_text(s_ext_user_floor, buf);
    }
    if (s_ext_cabin_floor != NULL) {
        if (st.position_known) {
            snprintf(buf, sizeof(buf), "P%02u", st.current_floor);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(s_ext_cabin_floor, buf);
    }
    if (s_ext_dir != NULL) {
        const char *txt;
        if (st.doors_open) {
            txt = "DETENIDO - PUERTAS ABIERTAS";
        } else if (st.dir == ELEV_DIR_UP) {
            txt = "SUBIENDO";
        } else if (st.dir == ELEV_DIR_DOWN) {
            txt = "BAJANDO";
        } else {
            txt = "DETENIDO - PUERTAS CERRADAS";
        }
        lv_label_set_text(s_ext_dir, txt);
        lv_obj_set_style_text_color(s_ext_dir,
            lv_color_hex(st.dir == ELEV_DIR_IDLE ? COL_WARN : COL_OK), 0);
    }

    /* Interior */
    if (s_int_current != NULL) {
        if (st.position_known) {
            snprintf(buf, sizeof(buf), "P%02u", st.current_floor);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(s_int_current, buf);
    }
    update_floor_button_states(&st);

    int changed = memcmp(&st, &s_last_state, sizeof(st)) != 0;
    if (changed && !st.position_known && !st.demo_mode && s_int_status != NULL) {
        lv_label_set_text(s_int_status,
                          "Posicion sin confirmar: espera un sensor de piso.");
    }
    s_last_state = st;

    board_unlock();
}

void ui_on_event(const elevator_event_t *ev)
{
    if (ev == NULL) {
        return;
    }
    if (ev->message != NULL && s_int_status != NULL) {
        if (!board_lock(100)) {
            return;
        }
        lv_label_set_text(s_int_status, ev->message);
        board_unlock();
    }
    if (ev->type == ELEV_EVT_ARRIVED) {
        board_sound(BOARD_SOUND_ARRIVAL);
    } else if (ev->type == ELEV_EVT_POSITION) {
        board_sound(BOARD_SOUND_INTERMEDIATE);
    }
}
