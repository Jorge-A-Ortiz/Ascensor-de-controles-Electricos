#pragma once

/**
 * @file storage.h
 * @brief Persistencia en NVS.
 *
 * Reconstruido a partir de las claves que quedaron guardadas en la particion
 * NVS del firmware original:
 *
 *     elevator/floors, elevator/floor_gpio, elevator/floor_level,
 *     elevator/logic_blocks, elevator/rules_v1, elevator/base3,
 *     elevator/sound
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ELEVATOR_MAX_FLOORS     16
#define ELEVATOR_MIN_FLOORS     2

#define STORAGE_NS_ELEVATOR     "elevator"
#define STORAGE_KEY_FLOORS      "floors"
#define STORAGE_KEY_FLOOR_GPIO  "floor_gpio"
#define STORAGE_KEY_FLOOR_LEVEL "floor_level"
#define STORAGE_KEY_BLOCKS      "logic_blocks"
#define STORAGE_KEY_BLOCKS_ON   "blocks_on"
#define STORAGE_KEY_RULES_VER   "rules_v1"
#define STORAGE_KEY_BASE3       "base3"
#define STORAGE_KEY_SOUND       "sound"
#define STORAGE_KEY_DEMO        "demo_mode"

#define STORAGE_RULES_VERSION   "rules_v2"

/* Declaracion adelantada: la definicion vive en rules.h */
struct rule_block_s;

typedef struct {
    uint8_t floors;
    uint8_t floor_gpio[ELEVATOR_MAX_FLOORS];   /* GPIO del sensor de piso */
    uint8_t floor_level[ELEVATOR_MAX_FLOORS];  /* nivel activo: 0 o 1 */
    bool    demo_mode;                         /* legado: siempre false */
    bool    blocks_enabled;
    uint8_t base3;                             /* ajuste interno heredado */
    uint8_t sound;                             /* volumen/aviso */
} elevator_config_t;

/** @brief Valores de fabrica de la configuracion. */
void storage_defaults(elevator_config_t *cfg);

/** @brief Inicializa NVS (borra y reintenta si la particion es incompatible). */
esp_err_t storage_init(void);

/** @brief Carga la configuracion; si no existe, devuelve los valores por defecto. */
esp_err_t storage_load_config(elevator_config_t *cfg);

/** @brief Guarda la configuracion completa. */
esp_err_t storage_save_config(const elevator_config_t *cfg);

/**
 * @brief Carga los bloques de logica.
 * @param blocks  destino (puede ser NULL para solo consultar la cuenta)
 * @param count   [in,out] capacidad de entrada / cantidad real de salida
 */
esp_err_t storage_load_blocks(struct rule_block_s *blocks, size_t *count);

/** @brief Guarda los bloques de logica y la version del esquema. */
esp_err_t storage_save_blocks(const struct rule_block_s *blocks, size_t count);

/** @brief Borra todas las claves de la aplicacion. */
esp_err_t storage_erase_all(void);

#ifdef __cplusplus
}
#endif
