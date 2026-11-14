#pragma once

/**
 * @file ui.h
 * @brief Interfaz LVGL de tres pantallas: Exterior, Interior y Tecnica.
 */

#include "esp_err.h"

#include "elevator.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Construye toda la interfaz (toma el mutex de LVGL). */
esp_err_t ui_create(void);

/** @brief Refresca textos segun el estado del ascensor. */
void ui_refresh(void);

/** @brief Notifica un evento del ascensor en la pantalla activa. */
void ui_on_event(const elevator_event_t *ev);

/** @brief Muestra un aviso modal con boton CERRAR. */
void ui_show_message(const char *title, const char *text);

#ifdef __cplusplus
}
#endif
