#pragma once

/**
 * @file rules.h
 * @brief Motor de logica por bloques: "SI entrada digital ... ENTONCES salida".
 *
 * Reconstruido de los textos del firmware original:
 *   "Programacion por bloques", "Hasta 32 bloques", "ES IGUAL A 0/1",
 *   "ENTONCES salida digital", "ESCRIBIR 0/1", "REPOSO seguro = 0/1",
 *   "GPIO reservado o no disponible en el conector J7",
 *   "Un GPIO no puede ser entrada y salida a la vez".
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RULES_MAX_BLOCKS   32
#define RULES_NOTE_MAX     64
#define RULES_ERR_MAX      160

typedef struct rule_block_s {
    bool    enabled;
    uint8_t in_gpio;      /* GPIO de entrada */
    uint8_t in_level;     /* 0 o 1: que nivel activa la condicion */
    uint8_t out_gpio;     /* GPIO de salida */
    uint8_t out_level;    /* 0 o 1: que se escribe cuando se cumple */
    uint8_t rest_level;   /* 0 o 1: nivel seguro cuando no se cumple */
    char    note[RULES_NOTE_MAX];  /* nota libre, max. 64 bytes UTF-8 */
} rule_block_t;

/** @brief Inicializa un bloque con valores validos por defecto. */
void rules_block_defaults(rule_block_t *block);

/** @brief true si el GPIO existe en el conector de usuario y no esta reservado. */
bool rules_gpio_allowed(int gpio);

/** @brief true si el GPIO puede usarse como entrada de usuario. */
bool rules_input_allowed(int gpio);

/**
 * @brief Valida el conjunto completo de bloques.
 * @param err      buffer donde se deja el motivo del rechazo
 * @param err_len  tamano del buffer
 */
esp_err_t rules_validate(const rule_block_t *blocks, size_t count,
                         char *err, size_t err_len);

/**
 * @brief Configura los GPIO y arranca la evaluacion de los bloques.
 * @note Deja una copia interna; la app puede liberar el arreglo original.
 */
esp_err_t rules_start(const rule_block_t *blocks, size_t count);

/** @brief Detiene las reglas y deja todas las salidas en su nivel de reposo. */
void rules_stop(void);

/** @brief Activa o desactiva la evaluacion sin perder la configuracion. */
void rules_set_enabled(bool enabled);

/** @brief Consulta si las reglas estan evaluando. */
bool rules_enabled(void);

/**
 * @brief Evalua entradas y aplica salidas.
 * @note Llamar periodicamente (p. ej. cada 10 ms). Internamente filtra 30 ms.
 */
void rules_tick(void);

/** @brief Ultimo error detectado en tiempo de ejecucion (o cadena vacia). */
const char *rules_last_error(void);

/** @brief Cantidad de bloques activos actualmente. */
size_t rules_active_count(void);

#ifdef __cplusplus
}
#endif
