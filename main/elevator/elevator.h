#pragma once

/**
 * @file elevator.h
 * @brief Logica del ascensor: posicion, sentido, llamadas y puertas.
 *
 * Reconstruido de los textos del firmware original:
 *   "SUBIENDO", "BAJANDO", "DETENIDO - PUERTAS CERRADAS",
 *   "Piso detectado: %u", "Posicion sin confirmar",
 *   "Ese piso ya esta incluido en el recorrido",
 *   "Parada P%02u agregada a la ruta", "Limite de recorrido alcanzado".
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ELEV_DIR_IDLE = 0,
    ELEV_DIR_UP,
    ELEV_DIR_DOWN,
} elevator_dir_t;

typedef enum {
    ELEV_EVT_MOVING = 0,   /* empieza el recorrido */
    ELEV_EVT_ARRIVED,      /* llego a un piso */
    ELEV_EVT_DOORS,        /* puertas abren/cierran */
    ELEV_EVT_POSITION,     /* sensor confirmo posicion */
    ELEV_EVT_REJECTED,     /* llamada descartada, con motivo */
    ELEV_EVT_LIMIT,        /* limite de recorrido */
} elevator_event_type_t;

typedef struct {
    elevator_event_type_t type;
    uint8_t  floor;
    elevator_dir_t dir;
    bool     doors_open;
    const char *message;
} elevator_event_t;

typedef void (*elevator_event_cb_t)(const elevator_event_t *ev, void *ctx);

typedef struct {
    uint8_t  floors;
    uint8_t  current_floor;
    bool     position_known;
    elevator_dir_t dir;
    bool     doors_open;
    bool     doors_closing;  /* ultimos 700 ms del ciclo: la puerta se cierra (doors_open sigue true) */
    bool     demo_mode;
    bool     moving;
    uint16_t cabin_calls, up_calls, down_calls;
    uint16_t cabin_buttons, up_buttons, down_buttons;
    bool fault;
    elevator_dir_t observed_dir;   /* Last transition between distinct floor sensors. */
} elevator_state_t;

/**
 * @brief Inicializa la logica con la configuracion cargada de NVS.
 */
esp_err_t elevator_init(const elevator_config_t *cfg);

/** @brief Registra el callback de eventos (se invoca desde la tarea llamante). */
void elevator_set_callback(elevator_event_cb_t cb, void *ctx);

/** @brief Estado actual. */
void elevator_get_state(elevator_state_t *out);

/**
 * @brief Pide una parada.
 * @param floor  piso destino (1..floors)
 * @param dir    sentido del pasajero (ELEV_DIR_UP/DOWN) o IDLE si da igual
 */
esp_err_t elevator_call(uint8_t floor, elevator_dir_t dir);



/** @brief Recarga pisos y sensores sin reiniciar. */
esp_err_t elevator_reload(const elevator_config_t *cfg);

/** @brief true si queda alguna parada pendiente. */
bool elevator_has_pending(void);
bool elevator_sensor_gpio(int gpio);

/**
 * @brief Avanza la maquina de estados.
 * @note Llamar cada 100 ms.
 */
void elevator_tick(void);

#ifdef __cplusplus
}
#endif
