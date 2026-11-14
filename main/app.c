/**
 * @file app.c
 * @brief Operaciones de alto nivel sobre el estado de la aplicacion.
 */

#include "app.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#define APP_ERR_MAX 160

static const char *TAG = "APP";

static app_state_t s_app;

app_state_t *app_state(void)
{
    return &s_app;
}

esp_err_t app_load(void)
{
    memset(&s_app, 0, sizeof(s_app));

    storage_load_config(&s_app.cfg);

    size_t capacity = RULES_MAX_BLOCKS;
    if (storage_load_blocks(s_app.blocks, &capacity) != ESP_OK) {
        s_app.block_count = 0;
    } else {
        s_app.block_count = capacity;
    }

    ESP_ERROR_CHECK(elevator_init(&s_app.cfg));

    if (s_app.cfg.blocks_enabled && s_app.block_count > 0) {
        char err[APP_ERR_MAX] = { 0 };
        esp_err_t rc = rules_validate(s_app.blocks, s_app.block_count,
                                      err, sizeof(err));
        if (rc == ESP_OK) {
            rc = rules_start(s_app.blocks, s_app.block_count);
        }
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "Bloques: %s", err);
            s_app.cfg.blocks_enabled = false;
            storage_save_config(&s_app.cfg);
        }
    }
    return ESP_OK;
}

esp_err_t app_apply_config(void)
{
    elevator_reload(&s_app.cfg);
    elevator_set_demo(s_app.cfg.demo_mode);
    return storage_save_config(&s_app.cfg);
}

esp_err_t app_save_blocks(void)
{
    return storage_save_blocks(s_app.blocks, s_app.block_count);
}

esp_err_t app_set_blocks_enabled(bool enabled, char *err, size_t err_len)
{
    if (enabled) {
        esp_err_t rc = rules_validate(s_app.blocks, s_app.block_count,
                                      err, err_len);
        if (rc != ESP_OK) {
            return rc;
        }
        if (s_app.block_count == 0) {
            if (err != NULL) {
                snprintf(err, err_len, "Agrega al menos un bloque completo.");
            }
            return ESP_ERR_INVALID_STATE;
        }
        rc = rules_start(s_app.blocks, s_app.block_count);
        if (rc != ESP_OK) {
            return rc;
        }
    } else {
        rules_stop();
    }

    s_app.cfg.blocks_enabled = enabled;
    storage_save_config(&s_app.cfg);
    return ESP_OK;
}

esp_err_t app_add_block(rule_block_t **out)
{
    if (s_app.block_count >= RULES_MAX_BLOCKS) {
        return ESP_ERR_NO_MEM;
    }
    rule_block_t *b = &s_app.blocks[s_app.block_count];
    rules_block_defaults(b);
    b->enabled = false;   /* se habilita al validar */
    s_app.block_count++;

    if (out != NULL) {
        *out = b;
    }
    return ESP_OK;
}

esp_err_t app_remove_block(size_t index)
{
    if (index >= s_app.block_count) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = index; i + 1 < s_app.block_count; i++) {
        s_app.blocks[i] = s_app.blocks[i + 1];
    }
    s_app.block_count--;
    return app_save_blocks();
}
