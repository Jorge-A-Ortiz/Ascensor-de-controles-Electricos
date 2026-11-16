#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Catalogo de sonidos interactivos (sintetizados, ver audio.c) */
typedef enum {
    AUDIO_SND_TAP = 0,      /* toque suave en cualquier control        */
    AUDIO_SND_PRESS,        /* pulsacion de boton principal            */
    AUDIO_SND_CALL_UP,      /* llamada SUBIR: notas ascendentes        */
    AUDIO_SND_CALL_DOWN,    /* llamada BAJAR: notas descendentes       */
    AUDIO_SND_ACCEPT,       /* parada aceptada en la ruta              */
    AUDIO_SND_MOVE,         /* arranque del motor                      */
    AUDIO_SND_BEEP,         /* ding al pasar por un piso (con nota)    */
    AUDIO_SND_ARRIVE,       /* campanilla de llegada (con nota)        */
    AUDIO_SND_DOOR_OPEN,
    AUDIO_SND_DOOR_CLOSE,
    AUDIO_SND_ERROR,        /* rechazo / conflicto                     */
    AUDIO_SND_LIMIT,        /* limite de recorrido                     */
    AUDIO_SND_SWIPE_NEXT,   /* cambio de pantalla hacia la derecha     */
    AUDIO_SND_SWIPE_PREV,
    AUDIO_SND_ON,           /* interruptor activado                    */
    AUDIO_SND_OFF,
    AUDIO_SND_OK,           /* configuracion guardada                  */
    AUDIO_SND_OPEN,         /* se abre una lista                       */
    AUDIO_SND_ADD,          /* bloque agregado                         */
    AUDIO_SND_REMOVE,       /* bloque eliminado                        */
    AUDIO_SND_POWER_ON,     /* reglas activadas                        */
    AUDIO_SND_POWER_OFF,
    AUDIO_SND_BOOT,         /* melodia de arranque                     */
    AUDIO_SND_PLUS,
    AUDIO_SND_MINUS,
    AUDIO_SND_RUN,          /* zumbido de motor durante el trayecto   */
    AUDIO_SND_ARRIVE_UP,    /* llegada subiendo: un ding               */
    AUDIO_SND_ARRIVE_DOWN,  /* llegada bajando: dos ding               */
    AUDIO_SND_COUNT
} audio_snd_t;

esp_err_t audio_init(uint8_t volume);

typedef struct {
    uint32_t jobs;          /* secuencias reproducidas */
    uint32_t frames;        /* muestras estereo escritas al I2S */
    uint32_t write_errors;  /* escrituras I2S fallidas */
    uint32_t last_job_ms;   /* duracion real de la ultima secuencia */
    uint32_t dropped;       /* sonidos descartados por cola llena */
    int      pa_level;      /* nivel del GPIO de habilitacion del amplificador */
    int      peak;          /* pico de la ultima muestra generada (0..32767) */
} audio_stats_t;

void audio_get_stats(audio_stats_t *out);

/* Tono de prueba: onda sinusoidal a volumen pleno (ignora el volumen de usuario) */
void audio_tone(uint16_t hz, uint16_t ms);
void audio_pa_force(int level);   /* -1 = automatico */

void audio_set_volume(uint8_t volume);
uint8_t audio_get_volume(void);

void audio_play(audio_snd_t snd);

/* Variantes con nota segun el piso: AUDIO_SND_BEEP, AUDIO_SND_ARRIVE o
 * cualquier otro valor (seleccion de piso). */
void audio_play_floor(audio_snd_t kind, int floor);

#ifdef __cplusplus
}
#endif
