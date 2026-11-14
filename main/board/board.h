#pragma once

/**
 * @file board.h
 * @brief Arranque del hardware: pantalla, tactil, retroiluminacion y LVGL.
 *
 * Modulos reconstruidos a partir de los tags que aparecen en el firmware
 * original: DISPLAY, TOUCH, SLIDE, RENDER, ILLUMINATE, SOUND.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_SOUND_ARRIVAL = 0,   /* "llegada"    */
    BOARD_SOUND_PRESS,         /* "pulsacion"  */
    BOARD_SOUND_INTERMEDIATE,  /* "piso intermedio" */
} board_sound_t;

/**
 * @brief Inicializa I2C, LDO, panel DSI, tactil GT911 y el puerto LVGL.
 */
esp_err_t board_init(void);

/**
 * @brief Toma el mutex de LVGL (obligatorio antes de tocar widgets).
 */
bool board_lock(int timeout_ms);

/** @brief Libera el mutex de LVGL. */
void board_unlock(void);

/**
 * @brief Ajusta la retroiluminacion.
 * @param percent 0..100 (el original arranca en 70).
 */
void board_set_backlight(uint8_t percent);

/**
 * @brief Reproduce un aviso corto.
 *
 * En la reconstruccion el codec no esta implementado: solo registra el
 * evento y usa el LED de la placa como parpadeo. Ver README.
 */
void board_sound(board_sound_t sound);

/** @brief Handle del bus I2C compartido (tactil y perifericos). */
void *board_i2c_bus(void);

#ifdef __cplusplus
}
#endif
