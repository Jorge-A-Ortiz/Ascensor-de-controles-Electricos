/**
 * @file main.c
 * @brief Arranque de la aplicacion AscensorCrowPanel.
 *
 * Orden de arranque reconstruido del firmware original:
 *   NVS -> configuracion -> ascensor -> pantalla/tactil -> interfaz -> timers
 *
 * Timers:
 *   - motion_timer (100 ms): avanza la cabina y refresca la UI.
 *   - rules_timer  (10 ms) : evalua la logica por bloques (filtro de 30 ms).
 */

#include "esp_log.h"
#include "esp_timer.h"

#include "lvgl.h"

#include "app.h"
#include "board.h"
#include "board_config.h"
#include "elevator.h"
#include "rules.h"
#include "ui.h"

static const char *TAG = "MAIN";

static esp_timer_handle_t s_motion_timer;
static esp_timer_handle_t s_rules_timer;

/* ------------------------------------------------------------------ */
/*  Diagnostico temporal del tactil                                    */
/*  Imprime el punto logico que recibe LVGL (ya rotado a 600x1024).    */
/*  Sirve para verificar la calibracion del GT911 en la placa real.    */
/* ------------------------------------------------------------------ */
#define TOUCH_DEBUG 0

#if TOUCH_DEBUG
static esp_timer_handle_t s_touch_timer;
static lv_obj_t *s_dbg_dot;

/* Crea (una sola vez) un punto rojo en la capa superior que sigue al dedo.
 * Se usa para comprobar a simple vista si el tactil coincide con la imagen. */
static void touch_debug_dot_create(void)
{
    s_dbg_dot = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dbg_dot);
    lv_obj_set_size(s_dbg_dot, 24, 24);
    lv_obj_set_style_radius(s_dbg_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dbg_dot, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(s_dbg_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_dbg_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_dbg_dot, LV_OBJ_FLAG_HIDDEN);
}

static void touch_tick(void *arg)
{
    (void)arg;

    lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
    lv_point_t p = { .x = 0, .y = 0 };

    if (board_lock(10)) {
        if (s_dbg_dot == NULL) {
            touch_debug_dot_create();
        }

        lv_indev_t *indev = lv_indev_get_next(NULL);
        if (indev != NULL) {
            lv_indev_get_point(indev, &p);
            state = indev->proc.state;
        }

        if (s_dbg_dot != NULL) {
            if (state == LV_INDEV_STATE_PRESSED) {
                lv_obj_clear_flag(s_dbg_dot, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(s_dbg_dot, p.x - 12, p.y - 12);
            } else {
                lv_obj_add_flag(s_dbg_dot, LV_OBJ_FLAG_HIDDEN);
            }
        }
        board_unlock();
    }

    static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
    static lv_point_t last_p = { .x = 0, .y = 0 };

    if (state == LV_INDEV_STATE_PRESSED) {
        int dx = p.x - last_p.x;
        int dy = p.y - last_p.y;
        if (last_state != LV_INDEV_STATE_PRESSED ||
            dx > 4 || dx < -4 || dy > 4 || dy < -4) {
            ESP_LOGI("TOUCHDBG", "punto LVGL x=%d y=%d", (int)p.x, (int)p.y);
        }
    } else if (last_state == LV_INDEV_STATE_PRESSED) {
        ESP_LOGI("TOUCHDBG", "liberado (ultimo x=%d y=%d)",
                 (int)last_p.x, (int)last_p.y);
    }

    last_state = state;
    last_p = p;
}
#endif

static void on_elevator_event(const elevator_event_t *ev, void *ctx)
{
    (void)ctx;
    ui_on_event(ev);
}

static void motion_tick(void *arg)
{
    (void)arg;
    elevator_tick();
    ui_refresh();
}

static void rules_tick_cb(void *arg)
{
    (void)arg;
    rules_tick();
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(TAG, "=== AscensorCrowPanel ===");

    /* 1. Persistencia y configuracion */
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(app_load());

    elevator_set_callback(on_elevator_event, NULL);

    /* 2. Hardware */
    ESP_ERROR_CHECK(board_init());

    /* 3. Interfaz */
    ESP_ERROR_CHECK(ui_create());

    /* 4. Timers de control */
    const esp_timer_create_args_t motion_args = {
        .callback = motion_tick,
        .name     = "motion_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&motion_args, &s_motion_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_motion_timer, 100 * 1000));

    const esp_timer_create_args_t rules_args = {
        .callback = rules_tick_cb,
        .name     = "rules_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&rules_args, &s_rules_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_rules_timer, 10 * 1000));

#if TOUCH_DEBUG
    const esp_timer_create_args_t touch_args = {
        .callback = touch_tick,
        .name     = "touch_dbg",
    };
    ESP_ERROR_CHECK(esp_timer_create(&touch_args, &s_touch_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_touch_timer, 100 * 1000));
#endif

    elevator_state_t st;
    elevator_get_state(&st);

    ESP_LOGI(TAG, "Interfaz lista: CrowPanel P4 V1.0, vertical %dx%d, %u pisos.",
             LCD_H_RES, LCD_V_RES, st.floors);
    ESP_LOGI(TAG, "Origen de posicion: %s",
             st.demo_mode ? "MODO DEMO" : "GPIO REALES");
}
