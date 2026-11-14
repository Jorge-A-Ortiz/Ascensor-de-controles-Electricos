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

static const char *TAG = "REGLAS";

#define RULES_GPIO_TABLE_SIZE (USER_GPIO_MAX + 1)

static const int s_user_gpios[USER_GPIO_COUNT] = USER_GPIO_LIST;

static rule_block_t s_blocks[RULES_MAX_BLOCKS];
static size_t s_count;
static bool s_enabled;

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
    block->enabled    = false;
    block->in_gpio    = 2;
    block->in_level   = 1;
    block->out_gpio   = 3;
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
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
    snprintf(s_last_error, sizeof(s_last_error), "%s", err);
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

        if (b->in_gpio > USER_GPIO_MAX || b->out_gpio > USER_GPIO_MAX) {
            return fail(err, err_len, "GPIO fuera de rango en el bloque %u.",
                        (unsigned)(i + 1));
        }
        if (!rules_gpio_allowed(b->in_gpio) || !rules_gpio_allowed(b->out_gpio)) {
            return fail(err, err_len,
                        "GPIO reservado o no disponible en el conector J7 "
                        "(bloque %u).", (unsigned)(i + 1));
        }
        if (b->in_gpio == b->out_gpio) {
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

        in_mask  |= (1ULL << b->in_gpio);
        out_mask |= (1ULL << b->out_gpio);

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
            gpio_reset_pin(gpio);
            s_configured[gpio] = false;
        }
    }
}

void rules_stop(void)
{
    /* Primero dejamos las salidas en reposo, despues liberamos */
    for (size_t i = 0; i < s_count; i++) {
        if (s_blocks[i].enabled) {
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

    memcpy(s_blocks, blocks, count * sizeof(rule_block_t));
    s_count = count;

    for (size_t i = 0; i < s_count; i++) {
        const rule_block_t *b = &s_blocks[i];

        gpio_config_t in_cfg = {
            .pin_bit_mask = (1ULL << b->in_gpio),
            .mode         = GPIO_MODE_INPUT,
            /* Entradas a 3,3 V: polarizacion contraria al nivel activo */
            .pull_up_en   = (b->in_level == 0) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = (b->in_level == 1) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&in_cfg));
        s_configured[b->in_gpio] = true;

        /* La salida arranca siempre en reposo */
        gpio_config_t out_cfg = {
            .pin_bit_mask = (1ULL << b->out_gpio),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&out_cfg));
        s_configured[b->out_gpio] = true;
        output_relax(b->out_gpio, b->rest_level);

        int raw = gpio_get_level(b->in_gpio);
        s_last_raw[b->in_gpio] = (raw != 0);
        s_stable[b->in_gpio]   = (raw != 0);
        s_change_ms[b->in_gpio] = 0;
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

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    /* 1. Muestreo con filtro de 30 ms */
    for (size_t i = 0; i < s_count; i++) {
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

    /* 2. Resolver la salida deseada de cada GPIO */
    bool  wanted_valid[RULES_GPIO_TABLE_SIZE];
    bool  wanted[RULES_GPIO_TABLE_SIZE];
    uint8_t rest[RULES_GPIO_TABLE_SIZE];
    memset(wanted_valid, 0, sizeof(wanted_valid));

    for (size_t i = 0; i < s_count; i++) {
        const rule_block_t *b = &s_blocks[i];
        if (!b->enabled) {
            continue;
        }

        bool input_matches = (s_stable[b->in_gpio] == (b->in_level != 0));
        bool level = input_matches ? (b->out_level != 0) : (b->rest_level != 0);

        if (wanted_valid[b->out_gpio] && wanted[b->out_gpio] != level) {
            /* No deberia ocurrir: la validacion lo impide */
            snprintf(s_last_error, sizeof(s_last_error),
                     "Conflicto de salidas: desactivado y en reposo.");
            ESP_LOGE(TAG, "%s", s_last_error);
            rules_stop();
            return;
        }
        wanted_valid[b->out_gpio] = true;
        wanted[b->out_gpio]       = level;
        rest[b->out_gpio]         = b->rest_level;
    }

    /* 3. Aplicar */
    for (size_t i = 0; i < s_count; i++) {
        const rule_block_t *b = &s_blocks[i];
        if (!b->enabled) {
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
