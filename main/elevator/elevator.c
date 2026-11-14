/**
 * @file elevator.c
 * @brief Maquina de estados del ascensor.
 *
 * Dos origenes de posicion, igual que el firmware original:
 *   - MODO DEMO:   el recorrido se simula con temporizador.
 *   - GPIO REALES: la posicion solo se confirma con el sensor del piso.
 */

#include "elevator.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "driver/gpio.h"

#include "board_config.h"
#include "rules.h"

static const char *TAG = "ASCENSOR";

#define DEMO_MS_PER_FLOOR   1200   /* tiempo simulado entre pisos */
#define DOORS_OPEN_MS       3000   /* puertas abiertas */
#define SENSOR_FILTER_MS    30

static elevator_config_t s_cfg;
static elevator_state_t s_state;

static bool s_pending[ELEVATOR_MAX_FLOORS];
static bool s_has_target;
static uint8_t s_target;

static uint32_t s_move_accum_ms;
static uint32_t s_doors_deadline_ms;

/* Filtrado de sensores */
static bool     s_sensor_raw[ELEVATOR_MAX_FLOORS];
static bool     s_sensor_stable[ELEVATOR_MAX_FLOORS];
static uint32_t s_sensor_change_ms[ELEVATOR_MAX_FLOORS];

static elevator_event_cb_t s_cb;
static void *s_cb_ctx;

/* ------------------------------------------------------------------ */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void emit(elevator_event_type_t type, uint8_t floor, const char *msg)
{
    if (s_cb == NULL) {
        return;
    }
    elevator_event_t ev = {
        .type       = type,
        .floor      = floor,
        .dir        = s_state.dir,
        .doors_open = s_state.doors_open,
        .message    = msg,
    };
    s_cb(&ev, s_cb_ctx);
}

static void open_doors(void)
{
    if (!s_state.doors_open) {
        s_state.doors_open = true;
        s_doors_deadline_ms = now_ms() + DOORS_OPEN_MS;
        emit(ELEV_EVT_DOORS, s_state.current_floor, "Puertas abiertas");
        ESP_LOGI(TAG, "Llegada a P%02u - cabina detenida.", s_state.current_floor);
    }
}

static void clear_pending(uint8_t floor)
{
    if (floor >= 1 && floor <= ELEVATOR_MAX_FLOORS) {
        s_pending[floor - 1] = false;
    }
}

static bool any_pending_from(uint8_t floor, bool up)
{
    for (int f = 1; f <= s_cfg.floors; f++) {
        if (!s_pending[f - 1]) {
            continue;
        }
        if (up && f > floor) {
            return true;
        }
        if (!up && f < floor) {
            return true;
        }
    }
    return false;
}

static int pick_target(void)
{
    uint8_t cur = s_state.current_floor;

    if (s_state.dir == ELEV_DIR_UP && any_pending_from(cur, true)) {
        for (int f = cur + 1; f <= s_cfg.floors; f++) {
            if (s_pending[f - 1]) {
                return f;
            }
        }
    }
    if (s_state.dir == ELEV_DIR_DOWN && any_pending_from(cur, false)) {
        for (int f = cur - 1; f >= 1; f--) {
            if (s_pending[f - 1]) {
                return f;
            }
        }
    }

    /* Cambio de sentido */
    if (any_pending_from(cur, true)) {
        s_state.dir = ELEV_DIR_UP;
        for (int f = cur + 1; f <= s_cfg.floors; f++) {
            if (s_pending[f - 1]) {
                return f;
            }
        }
    }
    if (any_pending_from(cur, false)) {
        s_state.dir = ELEV_DIR_DOWN;
        for (int f = cur - 1; f >= 1; f--) {
            if (s_pending[f - 1]) {
                return f;
            }
        }
    }
    return -1;
}

static void plan_next(void)
{
    if (s_state.moving || s_state.doors_open) {
        return;
    }

    uint8_t cur = s_state.current_floor;

    if (s_pending[cur - 1]) {
        clear_pending(cur);
        open_doors();
        return;
    }

    int t = pick_target();
    if (t < 0) {
        s_state.dir = ELEV_DIR_IDLE;
        return;
    }

    s_target     = (uint8_t)t;
    s_has_target = true;
    s_state.moving = true;
    s_state.dir = (t > cur) ? ELEV_DIR_UP : ELEV_DIR_DOWN;
    s_move_accum_ms = 0;

    char msg[48];
    snprintf(msg, sizeof(msg), "Parada P%02u agregada a la ruta.", s_target);
    emit(ELEV_EVT_MOVING, s_target, msg);
    ESP_LOGI(TAG, "Recorrido hacia P%02u (%s)", s_target,
             s_state.dir == ELEV_DIR_UP ? "subiendo" : "bajando");
}

/* ------------------------------------------------------------------ */
/*  Modo demo                                                         */
/* ------------------------------------------------------------------ */

static void demo_step(void)
{
    if (!s_state.moving || !s_has_target) {
        return;
    }

    s_move_accum_ms += 100;   /* elevator_tick() se llama cada 100 ms */
    if (s_move_accum_ms < DEMO_MS_PER_FLOOR) {
        return;
    }
    s_move_accum_ms = 0;

    if (s_state.current_floor < s_target) {
        s_state.current_floor++;
    } else if (s_state.current_floor > s_target) {
        s_state.current_floor--;
    }

    s_state.position_known = true;
    emit(ELEV_EVT_POSITION, s_state.current_floor, "Posicion simulada");

    if (s_state.current_floor == s_target) {
        s_state.moving = false;
        s_has_target = false;
        s_state.dir = ELEV_DIR_IDLE;
        clear_pending(s_target);
        emit(ELEV_EVT_ARRIVED, s_target, "Cabina detenida");
        open_doors();
    }
}

/* ------------------------------------------------------------------ */
/*  Modo GPIO reales                                                  */
/* ------------------------------------------------------------------ */

static void read_sensors(void)
{
    uint32_t now = now_ms();
    int active_floor = 0;
    int active_count = 0;

    for (int f = 1; f <= s_cfg.floors; f++) {
        uint8_t gpio = s_cfg.floor_gpio[f - 1];
        if (gpio == 0xFF) {
            s_sensor_stable[f - 1] = false;
            continue;
        }

        bool raw = (gpio_get_level(gpio) == s_cfg.floor_level[f - 1]);
        if (raw != s_sensor_raw[f - 1]) {
            s_sensor_raw[f - 1]    = raw;
            s_sensor_change_ms[f - 1] = now;
        }
        if ((now - s_sensor_change_ms[f - 1]) >= SENSOR_FILTER_MS) {
            s_sensor_stable[f - 1] = raw;
        }
        if (s_sensor_stable[f - 1]) {
            active_count++;
            active_floor = f;
        }
    }

    if (active_count == 1) {
        if (s_state.current_floor != active_floor || !s_state.position_known) {
            s_state.current_floor = (uint8_t)active_floor;
            s_state.position_known = true;
            emit(ELEV_EVT_POSITION, (uint8_t)active_floor,
                 "Sensor confirmado: luz del piso encendida.");
            ESP_LOGI(TAG, "Piso detectado: %u", active_floor);
        }

        if (s_state.moving && s_has_target && active_floor == s_target) {
            s_state.moving = false;
            s_has_target = false;
            s_state.dir = ELEV_DIR_IDLE;
            clear_pending((uint8_t)active_floor);
            emit(ELEV_EVT_ARRIVED, (uint8_t)active_floor, "Cabina detenida");
            open_doors();
        }
        return;
    }

    if (active_count > 1) {
        s_state.position_known = false;
        emit(ELEV_EVT_REJECTED, 0, "Conflicto: hay varios sensores de piso activos.");
        ESP_LOGW(TAG, "Conflicto: hay varios sensores de piso activos.");
        return;
    }

    /* Ningun sensor activo: entre pisos, posicion sin confirmar */
    if (s_state.moving) {
        s_state.position_known = false;
    }
}

/* ------------------------------------------------------------------ */
/*  API publica                                                       */
/* ------------------------------------------------------------------ */

esp_err_t elevator_init(const elevator_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;

    memset(&s_state, 0, sizeof(s_state));
    memset(s_pending, 0, sizeof(s_pending));
    s_has_target = false;
    s_target = 0;

    s_state.floors      = cfg->floors;
    s_state.demo_mode   = cfg->demo_mode;
    s_state.current_floor = 1;
    s_state.position_known = cfg->demo_mode;  /* en demo arranca en P1 */
    s_state.dir = ELEV_DIR_IDLE;
    s_state.doors_open = false;

    /* Configura los GPIO de los sensores de piso (solo en modo real) */
    uint64_t mask = 0;
    for (int f = 0; f < s_cfg.floors; f++) {
        uint8_t gpio = s_cfg.floor_gpio[f];
        if (gpio != 0xFF && rules_input_allowed(gpio)) {
            mask |= (1ULL << gpio);
        }
    }
    if (mask != 0) {
        gpio_config_t io = {
            .pin_bit_mask = mask,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
    }

    ESP_LOGI(TAG, "Ascensor listo: %u pisos, %s", s_cfg.floors,
             s_cfg.demo_mode ? "modo demo" : "GPIO reales");
    return ESP_OK;
}

esp_err_t elevator_reload(const elevator_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Mantiene la cabina donde esta, pero reajusta pisos y sensores */
    elevator_state_t keep = s_state;
    esp_err_t err = elevator_init(cfg);
    if (err == ESP_OK) {
        s_state.current_floor = (keep.current_floor <= cfg->floors)
                                ? keep.current_floor : 1;
        s_state.doors_open = keep.doors_open;
    }
    return err;
}

void elevator_set_callback(elevator_event_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
}

void elevator_get_state(elevator_state_t *out)
{
    if (out != NULL) {
        *out = s_state;
    }
}

void elevator_set_demo(bool demo)
{
    s_state.demo_mode = demo;
    s_cfg.demo_mode = demo;
    s_state.position_known = demo;
    s_move_accum_ms = 0;
    ESP_LOGI(TAG, "Demo activa: %s", demo ? "posicion simulada" : "GPIO reales");
}

esp_err_t elevator_call(uint8_t floor, elevator_dir_t dir)
{
    if (floor < 1 || floor > s_cfg.floors) {
        emit(ELEV_EVT_LIMIT, floor, "Limite de recorrido alcanzado.");
        return ESP_ERR_INVALID_ARG;
    }

    if (floor == s_state.current_floor && !s_state.moving) {
        emit(ELEV_EVT_REJECTED, floor, "La cabina ya esta en este piso.");
        open_doors();
        return ESP_OK;
    }

    if (s_pending[floor - 1]) {
        emit(ELEV_EVT_REJECTED, floor,
             "Ese piso ya esta incluido en el recorrido.");
        return ESP_OK;
    }

    s_pending[floor - 1] = true;

    if (dir == ELEV_DIR_UP || dir == ELEV_DIR_DOWN) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Llamada P%02u - %s", floor,
                 dir == ELEV_DIR_UP ? "subiendo" : "bajando");
        emit(ELEV_EVT_MOVING, floor, msg);
    }

    plan_next();
    return ESP_OK;
}

bool elevator_has_pending(void)
{
    for (int f = 0; f < s_cfg.floors; f++) {
        if (s_pending[f]) {
            return true;
        }
    }
    return false;
}

void elevator_tick(void)
{
    uint32_t now = now_ms();

    if (s_state.doors_open && now >= s_doors_deadline_ms) {
        s_state.doors_open = false;
        emit(ELEV_EVT_DOORS, s_state.current_floor, "Puertas cerradas");
        plan_next();
    }

    if (s_state.demo_mode) {
        demo_step();
    } else {
        read_sensors();
    }

    if (!s_state.moving && !s_state.doors_open && elevator_has_pending()) {
        plan_next();
    }
}
