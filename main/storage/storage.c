/**
 * @file storage.c
 * @brief Lectura y escritura de la configuracion del ascensor en NVS.
 */

#include "storage.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "rules.h"

static const char *TAG = "STORAGE";

void storage_defaults(elevator_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->floors       = 4;
    cfg->demo_mode    = true;
    cfg->blocks_enabled = false;
    cfg->base3        = 1;
    cfg->sound        = 70;

    for (int i = 0; i < ELEVATOR_MAX_FLOORS; i++) {
        cfg->floor_gpio[i]  = 0xFF;  /* sin asignar */
        cfg->floor_level[i] = 1;
    }
}

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS incompatible, borrando particion");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static nvs_handle_t open_ns(nvs_open_mode_t mode)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(STORAGE_NS_ELEVATOR, mode, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo abrir el namespace: %s", esp_err_to_name(err));
        return 0;
    }
    return h;
}

esp_err_t storage_load_config(elevator_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    storage_defaults(cfg);

    nvs_handle_t h = open_ns(NVS_READONLY);
    if (h == 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    uint8_t u8 = 0;
    if (nvs_get_u8(h, STORAGE_KEY_FLOORS, &u8) == ESP_OK) {
        cfg->floors = u8;
    }

    size_t len = sizeof(cfg->floor_gpio);
    if (nvs_get_blob(h, STORAGE_KEY_FLOOR_GPIO, cfg->floor_gpio, &len) != ESP_OK) {
        memset(cfg->floor_gpio, 0xFF, sizeof(cfg->floor_gpio));
    }

    len = sizeof(cfg->floor_level);
    if (nvs_get_blob(h, STORAGE_KEY_FLOOR_LEVEL, cfg->floor_level, &len) != ESP_OK) {
        memset(cfg->floor_level, 1, sizeof(cfg->floor_level));
    }

    if (nvs_get_u8(h, STORAGE_KEY_DEMO, &u8) == ESP_OK) {
        cfg->demo_mode = (u8 != 0);
    }
    if (nvs_get_u8(h, STORAGE_KEY_BASE3, &u8) == ESP_OK) {
        cfg->base3 = u8;
    }
    if (nvs_get_u8(h, STORAGE_KEY_SOUND, &u8) == ESP_OK) {
        cfg->sound = u8;
    }
    if (nvs_get_u8(h, STORAGE_KEY_BLOCKS_ON, &u8) == ESP_OK) {
        cfg->blocks_enabled = (u8 != 0);
    }

    nvs_close(h);

    if (cfg->floors < ELEVATOR_MIN_FLOORS || cfg->floors > ELEVATOR_MAX_FLOORS) {
        cfg->floors = ELEVATOR_MIN_FLOORS;
    }
    ESP_LOGI(TAG, "Configuracion cargada: %u pisos, %s",
             cfg->floors, cfg->demo_mode ? "modo demo" : "GPIO reales");
    return ESP_OK;
}

esp_err_t storage_save_config(const elevator_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h = open_ns(NVS_READWRITE);
    if (h == 0) {
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    err |= nvs_set_u8(h, STORAGE_KEY_FLOORS, cfg->floors);
    err |= nvs_set_blob(h, STORAGE_KEY_FLOOR_GPIO, cfg->floor_gpio,
                        sizeof(cfg->floor_gpio));
    err |= nvs_set_blob(h, STORAGE_KEY_FLOOR_LEVEL, cfg->floor_level,
                        sizeof(cfg->floor_level));
    err |= nvs_set_u8(h, STORAGE_KEY_DEMO, cfg->demo_mode ? 1 : 0);
    err |= nvs_set_u8(h, STORAGE_KEY_BLOCKS_ON, cfg->blocks_enabled ? 1 : 0);
    err |= nvs_set_u8(h, STORAGE_KEY_BASE3, cfg->base3);
    err |= nvs_set_u8(h, STORAGE_KEY_SOUND, cfg->sound);

    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo guardar la configuracion: %s",
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t storage_load_blocks(struct rule_block_s *blocks, size_t *count)
{
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h = open_ns(NVS_READONLY);
    if (h == 0) {
        *count = 0;
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t len = (blocks == NULL) ? 0 : (*count) * sizeof(rule_block_t);
    esp_err_t err = nvs_get_blob(h, STORAGE_KEY_BLOCKS, blocks, &len);

    char ver[16] = { 0 };
    size_t ver_len = sizeof(ver) - 1;
    if (nvs_get_str(h, STORAGE_KEY_RULES_VER, ver, &ver_len) != ESP_OK) {
        ver[0] = '\0';
    }
    nvs_close(h);

    if (err != ESP_OK) {
        *count = 0;
        return err;
    }

    *count = len / sizeof(rule_block_t);
    if (ver[0] != '\0' && strcmp(ver, STORAGE_RULES_VERSION) != 0) {
        ESP_LOGW(TAG, "Esquema de reglas '%s' distinto de '%s', se conserva",
                 ver, STORAGE_RULES_VERSION);
    }
    ESP_LOGI(TAG, "%u bloques de logica cargados", (unsigned)*count);
    return ESP_OK;
}

esp_err_t storage_save_blocks(const struct rule_block_s *blocks, size_t count)
{
    if (blocks == NULL && count > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h = open_ns(NVS_READWRITE);
    if (h == 0) {
        return ESP_FAIL;
    }

    esp_err_t err = nvs_set_blob(h, STORAGE_KEY_BLOCKS, blocks,
                                 count * sizeof(rule_block_t));
    err |= nvs_set_str(h, STORAGE_KEY_RULES_VER, STORAGE_RULES_VERSION);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se guardaron los bloques: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t storage_erase_all(void)
{
    nvs_handle_t h = open_ns(NVS_READWRITE);
    if (h == 0) {
        return ESP_FAIL;
    }
    esp_err_t err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
