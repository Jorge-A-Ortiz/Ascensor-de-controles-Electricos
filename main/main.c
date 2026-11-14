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

#include "app.h"
#include "board.h"
#include "board_config.h"
#include "elevator.h"
#include "rules.h"
#include "ui.h"

static const char *TAG = "MAIN";

static esp_timer_handle_t s_motion_timer;
static esp_timer_handle_t s_rules_timer;

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

    elevator_state_t st;
    elevator_get_state(&st);

    ESP_LOGI(TAG, "Interfaz lista: CrowPanel P4 V1.0, vertical %dx%d, %u pisos.",
             LCD_H_RES, LCD_V_RES, st.floors);
    ESP_LOGI(TAG, "Origen de posicion: %s",
             st.demo_mode ? "MODO DEMO" : "GPIO REALES");
}
