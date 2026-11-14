#pragma once

/**
 * @file app.h
 * @brief Estado global de la aplicacion y operaciones de alto nivel.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "elevator.h"
#include "rules.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    elevator_config_t cfg;
    rule_block_t      blocks[RULES_MAX_BLOCKS];
    size_t            block_count;
} app_state_t;

/** @brief Acceso al estado compartido (configuracion + bloques). */
app_state_t *app_state(void);

/** @brief Carga NVS, arranca el ascensor y, si procede, las reglas. */
esp_err_t app_load(void);

/** @brief Aplica y guarda la configuracion (pisos, sensores, modo demo). */
esp_err_t app_apply_config(void);

/** @brief Guarda los bloques y aplica la validacion. */
esp_err_t app_save_blocks(void);

/**
 * @brief Activa o desactiva la logica por bloques.
 * @param err  motivo del rechazo (puede ser NULL)
 */
esp_err_t app_set_blocks_enabled(bool enabled, char *err, size_t err_len);

/** @brief Agrega un bloque vacio si queda capacidad. */
esp_err_t app_add_block(rule_block_t **out);

/** @brief Elimina el bloque indicado. */
esp_err_t app_remove_block(size_t index);

#ifdef __cplusplus
}
#endif
