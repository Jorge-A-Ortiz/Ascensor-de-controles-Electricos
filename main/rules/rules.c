/**
 * @file rules.c
 * @brief Evaluacion de la logica por bloques con antirrebote de 30 ms.
 */

#include "rules.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "driver/gpio.h"

#include "board_config.h"
#include "elevator.h"

static const char *TAG = "REGLAS";

#define RULES_GPIO_TABLE_SIZE (USER_GPIO_MAX + 1)

static const int s_user_gpios[USER_GPIO_COUNT] = USER_GPIO_LIST;

static rule_block_t s_blocks[RULES_MAX_BLOCKS];
static size_t s_count;
static bool s_enabled;
static bool s_previous_match[RULES_MAX_BLOCKS];

static bool s_last_raw[RULES_GPIO_TABLE_SIZE];
static bool s_stable[RULES_GPIO_TABLE_SIZE];
static uint32_t s_change_ms[RULES_GPIO_TABLE_SIZE];
static bool s_configured[RULES_GPIO_TABLE_SIZE];

static char s_last_error[RULES_ERR_MAX];

/* ------------------------------------------------------------------ */

void rules_block_defaults(rule_block_t *block)
{
    if (block == NULL) {
        return;
    }
    memset(block, 0, sizeof(*block));
    block->enabled    = true;
    block->in_gpio    = 0xFF;   /* sin asignar: hay que elegir GPIO */
    block->in_level   = 1;
    block->out_gpio   = 0xFF;
    block->out_level  = 1;
    block->rest_level = 0;
}

bool rules_gpio_allowed(int gpio)
{
    if (gpio < 0 || gpio > USER_GPIO_MAX) {
        return false;
    }
    if (BSP_RESERVED_GPIO_MASK & (1ULL << gpio)) {
        return false;
    }
    for (int i = 0; i < USER_GPIO_COUNT; i++) {
        if (s_user_gpios[i] == gpio) {
            return true;
        }
    }
    return false;
}

bool rules_input_allowed(int gpio)
{
    return rules_gpio_allowed(gpio);
}

/* ------------------------------------------------------------------ */
/*  Validacion                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t fail(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_last_error, sizeof(s_last_error), fmt, ap);
    va_end(ap);
    if (err && err_len) snprintf(err, err_len, "%s", s_last_error);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t rules_validate(const rule_block_t *blocks, size_t count,
                         char *err, size_t err_len)
{
    if (blocks == NULL && count > 0) {
        return fail(err, err_len, "Sin datos de bloques.");
    }
    if (count > RULES_MAX_BLOCKS) {
        return fail(err, err_len,
                    "Capacidad completa: %d bloques compartidos entre todos los pisos.",
                    RULES_MAX_BLOCKS);
    }

    /* Un GPIO no puede ser entrada y salida a la vez */
    uint64_t in_mask = 0;
    uint64_t out_mask = 0;
    uint8_t out_level[RULES_GPIO_TABLE_SIZE];
    uint8_t rest_level[RULES_GPIO_TABLE_SIZE];
    bool out_seen[RULES_GPIO_TABLE_SIZE];
    memset(out_level, 0, sizeof(out_level));
    memset(rest_level, 0, sizeof(rest_level));
    memset(out_seen, 0, sizeof(out_seen));

    for (size_t i = 0; i < count; i++) {
        const rule_block_t *b = &blocks[i];

        if (b->source >= RULE_SOURCE_COUNT || b->floor > ELEVATOR_MAX_FLOORS)
            return fail(err, err_len, "Condicion o piso invalido.");
        bool digital = b->source == RULE_GPIO;
        if (b->action > 3 || (b->action &&
            (b->action_floor < 1 || b->action_floor > ELEVATOR_MAX_FLOORS)))
            return fail(err, err_len, "Accion o destino invalido.");
        if (b->action) {
            if (digital && !rules_input_allowed(b->in_gpio))
                return fail(err, err_len, "Entrada GPIO invalida.");
            if (b->in_level > 1) return fail(err, err_len, "Nivel invalido.");
            if (b->enabled && digital) in_mask |= 1ULL << b->in_gpio;
            continue;
        }
        if ((digital && b->in_gpio == 0xFF) || b->out_gpio == 0xFF) {
            return fail(err, err_len,
                        "Completa entrada y salida de cada bloque antes de activar "
                        "(bloque %u).", (unsigned)(i + 1));
        }
        if ((digital && b->in_gpio > USER_GPIO_MAX) || b->out_gpio > USER_GPIO_MAX) {
            return fail(err, err_len, "GPIO fuera de rango en el bloque %u.",
                        (unsigned)(i + 1));
        }
        if ((digital && !rules_gpio_allowed(b->in_gpio)) || !rules_gpio_allowed(b->out_gpio)) {
            return fail(err, err_len,
                        "GPIO reservado o no disponible en el conector J7 "
                        "(bloque %u).", (unsigned)(i + 1));
        }
        if (digital && b->in_gpio == b->out_gpio) {
            return fail(err, err_len,
                        "Un GPIO no puede ser entrada y salida a la vez "
                        "(bloque %u).", (unsigned)(i + 1));
        }
        if (b->in_level > 1 || b->out_level > 1 || b->rest_level > 1) {
            return fail(err, err_len,
                        "Los niveles solo pueden ser 0 o 1 (bloque %u).",
                        (unsigned)(i + 1));
        }

        /* Solo los bloques habilitados llegan al hardware */
        if (!b->enabled) {
            continue;
        }

        if (digital) in_mask |= (1ULL << b->in_gpio);
        out_mask |= (1ULL << b->out_gpio);
        /* Los sensores pertenecen al controlador de posicion. */
        if (elevator_sensor_gpio(b->out_gpio))
            return fail(err, err_len, "GPIO %u ya es sensor de piso.", b->out_gpio);

        if (out_seen[b->out_gpio]) {
            if (out_level[b->out_gpio] != b->out_level) {
                return fail(err, err_len,
                            "Dos bloques podrian ordenar valores contrarios a la "
                            "misma salida (GPIO %u).", b->out_gpio);
            }
            if (rest_level[b->out_gpio] != b->rest_level) {
                return fail(err, err_len,
                            "Un GPIO de salida debe tener un unico nivel de reposo "
                            "(GPIO %u).", b->out_gpio);
            }
        } else {
            out_seen[b->out_gpio]  = true;
            out_level[b->out_gpio] = b->out_level;
            rest_level[b->out_gpio] = b->rest_level;
        }
    }

    if ((in_mask & out_mask) != 0) {
        return fail(err, err_len,
                    "Un GPIO no puede ser entrada y salida a la vez.");
    }

    s_last_error[0] = '\0';
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Hardware                                                          */
/* ------------------------------------------------------------------ */

static void output_relax(uint8_t gpio, uint8_t level)
{
    gpio_set_level(gpio, level);
}

static void release_all(void)
{
    for (int gpio = 0; gpio <= USER_GPIO_MAX; gpio++) {
        if (s_configured[gpio]) {
            if (!elevator_sensor_gpio(gpio)) gpio_reset_pin(gpio);
            s_configured[gpio] = false;
        }
    }
}

void rules_stop(void)
{
    /* Primero dejamos las salidas en reposo, despues liberamos */
    for (size_t i = 0; i < s_count; i++) {
        if (s_blocks[i].enabled && !s_blocks[i].action) {
            output_relax(s_blocks[i].out_gpio, s_blocks[i].rest_level);
        }
    }
    release_all();
    s_enabled = false;
    ESP_LOGI(TAG, "Desactivado: salidas en su nivel de reposo configurado.");
}

esp_err_t rules_start(const rule_block_t *blocks, size_t count)
{
    char err[RULES_ERR_MAX] = { 0 };
    esp_err_t rc = rules_validate(blocks, count, err, sizeof(err));
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Bloques: %s", err);
        return rc;
    }

    rules_stop();  /* libera cualquier configuracion previa */

    if (count) memmove(s_blocks, blocks, count * sizeof(rule_block_t));
    memset(s_previous_match, 0, sizeof(s_previous_match));
    s_count = count;

    for (size_t i = 0; i < s_count; i++) {
        const rule_block_t *b = &s_blocks[i];

        if (!b->enabled) continue;
        if (b->source == RULE_GPIO) {
            gpio_config_t in_cfg = {
                .pin_bit_mask = (1ULL << b->in_gpio),
                .mode         = GPIO_MODE_INPUT,
                /* Entradas a 3,3 V: polarizacion contraria al nivel activo */
                .pull_up_en   = (b->in_level == 0) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
                .pull_down_en = (b->in_level == 1) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
                .intr_type    = GPIO_INTR_DISABLE,
            };
            if (!elevator_sensor_gpio(b->in_gpio)) ESP_ERROR_CHECK(gpio_config(&in_cfg));
            s_configured[b->in_gpio] = true;

        }
        if (!b->action) {
            /* La salida arranca siempre en reposo */
            gpio_config_t out_cfg = {
                .pin_bit_mask = (1ULL << b->out_gpio),
                /* INPUT_OUTPUT: gpio_get_level() lee el pin real (verificable) */
                .mode         = GPIO_MODE_INPUT_OUTPUT,
                .pull_up_en   = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type    = GPIO_INTR_DISABLE,
            };
            ESP_ERROR_CHECK(gpio_config(&out_cfg));
            s_configured[b->out_gpio] = true;
            output_relax(b->out_gpio, b->rest_level);
        }

        if (b->source == RULE_GPIO) {
            int raw = gpio_get_level(b->in_gpio);
            s_last_raw[b->in_gpio] = (raw != 0);
            s_stable[b->in_gpio]   = (raw != 0);
            s_change_ms[b->in_gpio] = 0;
        }
    }

    s_enabled = true;
    ESP_LOGI(TAG, "GPIO activos. Entradas digitales filtradas %d ms.",
             BOARD_INPUT_FILTER_MS);
    return ESP_OK;
}

void rules_set_enabled(bool enabled)
{
    if (!enabled) {
        rules_stop();
        return;
    }
    if (s_count > 0) {
        (void)rules_start(s_blocks, s_count);
    }
}

bool rules_enabled(void)
{
    return s_enabled;
}

const char *rules_last_error(void)
{
    return s_last_error;
}

size_t rules_active_count(void)
{
    size_t n = 0;
    for (size_t i = 0; i < s_count; i++) {
        if (s_blocks[i].enabled) {
            n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/*  Evaluacion periodica                                              */
/* ------------------------------------------------------------------ */

void rules_tick(void)
{
    if (!s_enabled) {
        return;
    }
    elevator_state_t safety;
    elevator_get_state(&safety);
    if (safety.fault) { rules_stop(); return; }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    /* 1. Muestreo con filtro de 30 ms */
    for (size_t i = 0; i < s_count; i++) {
        if (!s_blocks[i].enabled || s_blocks[i].source != RULE_GPIO) continue;
        uint8_t gpio = s_blocks[i].in_gpio;
        bool raw = (gpio_get_level(gpio) != 0);

        if (raw != s_last_raw[gpio]) {
            s_last_raw[gpio]  = raw;
            s_change_ms[gpio] = now;
        }
        if ((now - s_change_ms[gpio]) >= BOARD_INPUT_FILTER_MS) {
            s_stable[gpio] = raw;
        }
    }

    elevator_state_t st;
    elevator_get_state(&st);

    /* 2. Resolver la salida deseada de cada GPIO */
    bool  wanted_valid[RULES_GPIO_TABLE_SIZE];
    bool  wanted[RULES_GPIO_TABLE_SIZE];
    memset(wanted_valid, 0, sizeof(wanted_valid));

    for (size_t i = 0; i < s_count; i++) {
        const rule_block_t *b = &s_blocks[i];
        if (!b->enabled) {
            continue;
        }

        uint16_t mask = b->floor ? (1u << (b->floor - 1)) : 0xFFFF;
        bool condition = false;
        bool at_floor = st.position_known && (!b->floor || st.current_floor == b->floor);
        switch (b->source) {
        case RULE_GPIO: condition = s_stable[b->in_gpio]; break;
        case RULE_BUTTON_UP: condition = (st.up_buttons & mask) != 0; break;
        case RULE_BUTTON_DOWN: condition = (st.down_buttons & mask) != 0; break;
        case RULE_BUTTON_CABIN: condition = (st.cabin_buttons & mask) != 0; break;
        case RULE_AT_FLOOR: condition = at_floor; break;
        case RULE_MOVING_UP: condition = st.moving && st.dir == ELEV_DIR_UP; break;
        case RULE_MOVING_DOWN: condition = st.moving && st.dir == ELEV_DIR_DOWN; break;
        case RULE_DOORS_OPEN: condition = st.doors_open; break;
        case RULE_PENDING: condition = ((st.cabin_calls | st.up_calls | st.down_calls) & mask) != 0; break;
        }
        if (b->source == RULE_GPIO || b->source == RULE_MOVING_UP ||
            b->source == RULE_MOVING_DOWN || b->source == RULE_DOORS_OPEN)
            condition = condition && (!b->floor || at_floor);
        bool input_matches = (condition == (b->in_level != 0));
        if (b->action) {
            if (input_matches && !s_previous_match[i]) {
                elevator_dir_t dir = b->action == 1 ? ELEV_DIR_IDLE :
                                     b->action == 2 ? ELEV_DIR_UP : ELEV_DIR_DOWN;
                elevator_call(b->action_floor, dir);
            }
            s_previous_match[i] = input_matches;
            continue;
        }

        if (!wanted_valid[b->out_gpio]) wanted[b->out_gpio] = b->rest_level != 0;
        if (input_matches) wanted[b->out_gpio] = b->out_level != 0;
        wanted_valid[b->out_gpio] = true;
    }

    /* 3. Aplicar */
    for (size_t i = 0; i < s_count; i++) {
        const rule_block_t *b = &s_blocks[i];
        if (!b->enabled || b->action) {
            continue;
        }
        uint8_t value;
        if (wanted_valid[b->out_gpio]) {
            value = wanted[b->out_gpio] ? 1 : 0;
        } else {
            value = b->rest_level;
        }
        if (gpio_get_level(b->out_gpio) != value) {
            gpio_set_level(b->out_gpio, value);
        }
    }
}
