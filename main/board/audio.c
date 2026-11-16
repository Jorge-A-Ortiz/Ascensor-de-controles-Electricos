/**
 * @file audio.c
 * @brief Avisos sonoros por el amplificador NS4168 (I2S) del CrowPanel P4.
 *
 * Pines y formato recuperados del firmware original (tarea "sound"):
 *   BCLK = GPIO22, WS = GPIO21, DOUT = GPIO23, sin MCLK,
 *   16 kHz, 16 bits estereo, GPIO30 = habilitacion del amplificador.
 *
 * El original reproduce avisos; aqui los sonidos se sintetizan en tiempo de
 * ejecucion (sin muestras en flash): una tarea de baja prioridad recibe
 * secuencias de notas por una cola y las genera, asi no afecta a la UI.
 */

#include "audio.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "SOUND";

#define AUDIO_PA_GPIO     GPIO_NUM_30
/* Segun Elecrow (AUDIO_GPIO_CTRL, logica invertida) el amplificador se
 * habilita con el pin en BAJO. En alto queda apagado (silencio total). */
#define AUDIO_PA_ON       0
#define AUDIO_BCLK_GPIO   GPIO_NUM_22
#define AUDIO_WS_GPIO     GPIO_NUM_21
#define AUDIO_DOUT_GPIO   GPIO_NUM_23
#define AUDIO_RATE        16000
#define CHUNK_FRAMES      256
#define MAX_NOTES         6

typedef enum { W_SINE = 0, W_SQUARE, W_NOISE, W_TRI, W_BELL, W_THUMP, W_HUM, W_WHIRR } wave_t;

typedef struct {
    uint16_t f0;      /* Hz inicial (0 = silencio) */
    uint16_t f1;      /* Hz final: barrido lineal */
    uint16_t ms;
    uint8_t  vol;     /* 0..100 */
    uint8_t  wave;
} note_t;

typedef struct {
    note_t  notes[MAX_NOTES];
    uint8_t n;
} job_t;

static i2s_chan_handle_t s_tx;
static QueueHandle_t s_queue;
static volatile uint8_t s_volume = 70;
static bool s_ready;
static audio_stats_t s_stats;
static volatile int s_pa_force = AUDIO_PA_ON;   /* nivel del pin de control */
static volatile bool s_tone_full;

static int16_t s_sine[256];

/* ------------------------------------------------------------------ */
/*  Escala y utilidades                                               */
/* ------------------------------------------------------------------ */

/* Escala mayor desde Do5: 523 587 659 698 784 880 988 1047 ... */
static uint16_t scale_freq(int idx)
{
    static const uint8_t semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    if (idx < 0) {
        idx = 0;
    }
    int octave = idx / 7;
    int semi = semis[idx % 7] + 12 * octave;
    float f = 523.25f * powf(2.0f, semi / 12.0f);
    return (uint16_t)(f > 3800.0f ? 3800.0f : f);
}

#define N(f0, f1, ms, vol, w) { f0, f1, ms, vol, w }

/* ------------------------------------------------------------------ */
/*  Biblioteca de sonidos fijos                                       */
/* ------------------------------------------------------------------ */

static const note_t SND_TAP[]      = { N(1800, 1200, 18, 45, W_SINE) };
/* --- Sonidos de ascensor ------------------------------------------- */
/* boton de piso/llamada: pitido electronico corto */
static const note_t SND_PRESS[]    = { N(2000, 2000, 70, 55, W_SINE) };
/* llamada SUBIR / BAJAR: pitido + campanita ascendente / descendente */
static const note_t SND_CALL_UP[]  = { N(1700, 1700, 45, 50, W_SINE),
                                       N(0, 0, 20, 0, W_SINE),
                                       N(1319, 1319, 220, 60, W_BELL) };
static const note_t SND_CALL_DN[]  = { N(1700, 1700, 45, 50, W_SINE),
                                       N(0, 0, 20, 0, W_SINE),
                                       N(988, 988, 220, 60, W_BELL) };
/* parada aceptada en la ruta */
static const note_t SND_ACCEPT[]   = { N(1568, 1568, 260, 55, W_BELL) };
/* arranque: rele + zumbido de motor que acelera y se estabiliza */
static const note_t SND_MOVE[]     = { N(320, 170, 45, 65, W_THUMP),
                                       N(120, 200, 520, 50, W_HUM),
                                       N(200, 205, 260, 40, W_HUM) };
/* trayecto: zumbido continuo (se repite mientras la cabina se mueve) */
static const note_t SND_RUN[]      = { N(200, 205, 480, 38, W_HUM) };
/* pasa por un piso: "bip" suave de campana */
static const note_t SND_BEEP[]     = { N(1760, 1760, 230, 42, W_BELL) };
/* llegada: gong "ding-dong" clasico (mi - do) */
static const note_t SND_ARRIVE[]   = { N(1319, 1319, 520, 78, W_BELL),
                                       N(0, 0, 40, 0, W_SINE),
                                       N(988, 988, 700, 78, W_BELL) };
/* llegada subiendo: un "ding"; bajando: dos "ding" */
static const note_t SND_ARRIVE_UP[] = { N(1319, 1319, 750, 80, W_BELL) };
static const note_t SND_ARRIVE_DN[] = { N(1319, 1319, 380, 80, W_BELL),
                                        N(0, 0, 60, 0, W_SINE),
                                        N(1319, 1319, 700, 80, W_BELL) };
/* puertas: mecanismo + tope; al abrir termina con golpe suave */
static const note_t SND_DOOR_OPEN[] = { N(320, 170, 50, 65, W_THUMP),
                                        N(210, 330, 330, 38, W_WHIRR),
                                        N(220, 120, 120, 65, W_THUMP) };
static const note_t SND_DOOR_CLOSE[] = { N(320, 170, 50, 60, W_THUMP),
                                         N(330, 200, 340, 38, W_WHIRR),
                                         N(240, 120, 150, 70, W_THUMP) };
/* zumbador de error / sobrecarga */
static const note_t SND_ERROR[]    = { N(220, 220, 160, 80, W_SQUARE),
                                       N(0, 0, 50, 0, W_SINE),
                                       N(220, 220, 260, 80, W_SQUARE) };
static const note_t SND_LIMIT[]    = { N(220, 220, 140, 80, W_SQUARE),
                                       N(0, 0, 70, 0, W_SINE),
                                       N(220, 220, 140, 80, W_SQUARE),
                                       N(0, 0, 70, 0, W_SINE),
                                       N(220, 220, 300, 80, W_SQUARE) };

static const note_t SND_SWIPE_L[]  = { N(500, 1800, 130, 30, W_NOISE),
                                       N(700, 1300, 90, 40, W_SINE) };
static const note_t SND_SWIPE_R[]  = { N(1800, 500, 130, 30, W_NOISE),
                                       N(1300, 700, 90, 40, W_SINE) };
static const note_t SND_ON[]       = { N(700, 700, 55, 60, W_SINE),
                                       N(1050, 1050, 110, 65, W_SINE) };
static const note_t SND_OFF[]      = { N(1050, 1050, 55, 60, W_SINE),
                                       N(620, 620, 120, 60, W_SINE) };
static const note_t SND_OK[]       = { N(523, 523, 70, 65, W_SINE),
                                       N(659, 659, 70, 65, W_SINE),
                                       N(784, 784, 70, 65, W_SINE),
                                       N(1047, 1047, 180, 70, W_SINE) };
static const note_t SND_OPEN[]     = { N(500, 1000, 60, 40, W_SINE) };
static const note_t SND_ADD[]      = { N(400, 900, 70, 60, W_SINE),
                                       N(900, 1350, 90, 65, W_SINE) };
static const note_t SND_REMOVE[]   = { N(1200, 600, 80, 60, W_SINE),
                                       N(600, 250, 130, 60, W_TRI) };
static const note_t SND_POWER_ON[] = { N(262, 262, 90, 60, W_SINE),
                                       N(330, 330, 90, 60, W_SINE),
                                       N(392, 392, 90, 60, W_SINE),
                                       N(523, 523, 220, 70, W_SINE) };
static const note_t SND_POWER_OFF[] = { N(523, 523, 90, 60, W_SINE),
                                        N(392, 392, 90, 60, W_SINE),
                                        N(262, 262, 220, 60, W_SINE) };
static const note_t SND_BOOT[]     = { N(523, 523, 110, 60, W_SINE),
                                       N(659, 659, 110, 60, W_SINE),
                                       N(784, 784, 110, 60, W_SINE),
                                       N(1047, 1047, 110, 65, W_SINE),
                                       N(784, 1047, 260, 65, W_SINE) };
static const note_t SND_PLUS[]     = { N(800, 1100, 55, 55, W_SINE) };
static const note_t SND_MINUS[]    = { N(1100, 800, 55, 55, W_SINE) };

typedef struct { const note_t *notes; uint8_t n; } fixed_t;
#define FX(a) { a, (uint8_t)(sizeof(a) / sizeof(a[0])) }

static const fixed_t s_fixed[AUDIO_SND_COUNT] = {
    [AUDIO_SND_TAP]        = FX(SND_TAP),
    [AUDIO_SND_PRESS]      = FX(SND_PRESS),
    [AUDIO_SND_CALL_UP]    = FX(SND_CALL_UP),
    [AUDIO_SND_CALL_DOWN]  = FX(SND_CALL_DN),
    [AUDIO_SND_ACCEPT]     = FX(SND_ACCEPT),
    [AUDIO_SND_MOVE]       = FX(SND_MOVE),
    [AUDIO_SND_BEEP]       = FX(SND_BEEP),
    [AUDIO_SND_ARRIVE]     = FX(SND_ARRIVE),
    [AUDIO_SND_DOOR_OPEN]  = FX(SND_DOOR_OPEN),
    [AUDIO_SND_DOOR_CLOSE] = FX(SND_DOOR_CLOSE),
    [AUDIO_SND_ERROR]      = FX(SND_ERROR),
    [AUDIO_SND_LIMIT]      = FX(SND_LIMIT),
    [AUDIO_SND_SWIPE_NEXT] = FX(SND_SWIPE_L),
    [AUDIO_SND_SWIPE_PREV] = FX(SND_SWIPE_R),
    [AUDIO_SND_ON]         = FX(SND_ON),
    [AUDIO_SND_OFF]        = FX(SND_OFF),
    [AUDIO_SND_OK]         = FX(SND_OK),
    [AUDIO_SND_OPEN]       = FX(SND_OPEN),
    [AUDIO_SND_ADD]        = FX(SND_ADD),
    [AUDIO_SND_REMOVE]     = FX(SND_REMOVE),
    [AUDIO_SND_POWER_ON]   = FX(SND_POWER_ON),
    [AUDIO_SND_POWER_OFF]  = FX(SND_POWER_OFF),
    [AUDIO_SND_BOOT]       = FX(SND_BOOT),
    [AUDIO_SND_PLUS]       = FX(SND_PLUS),
    [AUDIO_SND_MINUS]      = FX(SND_MINUS),
    [AUDIO_SND_RUN]        = FX(SND_RUN),
    [AUDIO_SND_ARRIVE_UP]  = FX(SND_ARRIVE_UP),
    [AUDIO_SND_ARRIVE_DOWN] = FX(SND_ARRIVE_DN),
};

/* ------------------------------------------------------------------ */
/*  Sintesis                                                          */
/* ------------------------------------------------------------------ */

static void play_note(const note_t *nt, int16_t *buf)
{
    uint32_t total = (uint32_t)nt->ms * AUDIO_RATE / 1000;
    uint32_t attack = AUDIO_RATE / 400;                /* 2.5 ms */
    uint32_t release = total / 3;
    uint32_t phase = 0;
    static uint32_t rng = 0x1234567;
    uint32_t done = 0;

    while (done < total) {
        uint32_t n = total - done;
        if (n > CHUNK_FRAMES) {
            n = CHUNK_FRAMES;
        }
        for (uint32_t i = 0; i < n; i++) {
            uint32_t pos = done + i;
            int32_t s = 0;
            if (nt->f0 != 0 && nt->vol != 0) {
                float t = (float)pos / (float)total;
                float f = nt->f0 + (nt->f1 - nt->f0) * t;
                phase += (uint32_t)(f * (4294967296.0f / AUDIO_RATE));
                switch (nt->wave) {
                case W_SQUARE:
                    s = (phase & 0x80000000u) ? 18000 : -18000;
                    break;
                case W_TRI: {
                    int32_t p = (int32_t)(phase >> 16) - 32768;
                    s = (p < 0 ? -p : p) - 16384;
                    s = s * 5 / 8;
                    break;
                }
                case W_BELL: {
                    /* campana: fundamental + parciales inarmonicos (2.76x, 5.4x) */
                    uint32_t p2 = (uint32_t)(((uint64_t)phase * 2756) / 1000);
                    uint32_t p3 = (uint32_t)(((uint64_t)phase * 5404) / 1000);
                    s = s_sine[(phase >> 24) & 255] +
                        s_sine[(p2 >> 24) & 255] * 45 / 100 +
                        s_sine[(p3 >> 24) & 255] / 5;
                    s = s * 6 / 10;
                    break;
                }
                case W_THUMP:
                    s = s_sine[(phase >> 24) & 255];
                    break;
                case W_HUM: {
                    /* zumbido de motor: triangular + octava + 3er armonico */
                    int32_t a = (int32_t)(phase >> 16) - 32768;
                    int32_t b2 = (int32_t)((phase << 1) >> 16) - 32768;
                    a = (a < 0 ? -a : a) - 16384;
                    b2 = (b2 < 0 ? -b2 : b2) - 16384;
                    s = (a * 5 / 8) + (b2 / 4) + s_sine[((phase * 3) >> 24) & 255] / 6;
                    break;
                }
                case W_WHIRR: {
                    /* mecanismo de puertas: diente de sierra suave + ruido */
                    int32_t saw = (int32_t)(phase >> 17) - 16384;
                    rng = rng * 1664525u + 1013904223u;
                    s = saw / 2 + ((int32_t)(rng >> 19) - 8192) / 2;
                    break;
                }
                case W_NOISE:
                    rng = rng * 1664525u + 1013904223u;
                    s = ((int32_t)(rng >> 17) - 16384) / 2;
                    s += s_sine[(phase >> 24) & 255] / 5;
                    break;
                default:
                    s = s_sine[(phase >> 24) & 255];
                    break;
                }
                int32_t env = 256;
                if (pos < attack) {
                    env = (int32_t)(256 * pos / attack);
                }
                if (nt->wave == W_BELL || nt->wave == W_THUMP) {
                    int64_t rem = (int64_t)(total - pos);
                    env = env * (int32_t)(rem * rem * 256 / ((int64_t)total * total)) / 256;
                } else if (pos + release > total) {
                    env = env * (int32_t)(total - pos) / (int32_t)release;
                }
                s = s * env / 256;
                s = s * (nt->vol > 70 ? 70 : nt->vol) / 100 *
                    (s_tone_full ? 100 : s_volume) / 100;
            }
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            buf[2 * i]     = (int16_t)s;
            buf[2 * i + 1] = (int16_t)s;
            if ((s < 0 ? -s : s) > s_stats.peak) {
                s_stats.peak = s < 0 ? -s : s;
            }
        }
        size_t written = 0;
        if (i2s_channel_write(s_tx, buf, n * 4, &written, 200) != ESP_OK ||
            written != n * 4) {
            s_stats.write_errors++;
        }
        s_stats.frames += written / 4;
        done += n;
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t buf[CHUNK_FRAMES * 2];
    job_t job;

    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (s_volume == 0) {
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        for (uint8_t i = 0; i < job.n; i++) {
            play_note(&job.notes[i], buf);
        }
        s_stats.jobs++;
        s_stats.last_job_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        /* cola vacia: rellena con silencio y apaga el amplificador */
        memset(buf, 0, sizeof(buf));
        size_t w = 0;
        i2s_channel_write(s_tx, buf, sizeof(buf), &w, 100);
        s_tone_full = false;
    }
}

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */

esp_err_t audio_init(uint8_t volume)
{
    for (int i = 0; i < 256; i++) {
        s_sine[i] = (int16_t)(sinf(2.0f * 3.14159265f * i / 256.0f) * 26000.0f);
    }
    s_volume = volume > 100 ? 100 : volume;

    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << AUDIO_PA_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT,
    };
    esp_err_t err = gpio_config(&pa);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(AUDIO_PA_GPIO, AUDIO_PA_ON);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,
                                                            I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = CHUNK_FRAMES;
    err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_BCLK_GPIO,
            .ws   = AUDIO_WS_GPIO,
            .dout = AUDIO_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        return err;
    }

    s_queue = xQueueCreate(6, sizeof(job_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task, "sound", 4096, NULL, 2, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    ESP_LOGI(TAG, "Avisos de audio listos");
    return ESP_OK;
}

void audio_set_volume(uint8_t volume)
{
    s_volume = volume > 100 ? 100 : volume;
}

uint8_t audio_get_volume(void)
{
    return s_volume;
}

static void enqueue(const job_t *job, bool important)
{
    if (!s_ready || s_volume == 0) {
        return;
    }
    if (!important && uxQueueMessagesWaiting(s_queue) >= 3) {
        s_stats.dropped++;
        return;   /* no acumular clics si el audio va atrasado */
    }
    xQueueSend(s_queue, job, 0);
}

void audio_play(audio_snd_t snd)
{
    if (snd < 0 || snd >= AUDIO_SND_COUNT || s_fixed[snd].notes == NULL) {
        return;
    }
    job_t job = { .n = s_fixed[snd].n };
    memcpy(job.notes, s_fixed[snd].notes, job.n * sizeof(note_t));
    bool important = (snd != AUDIO_SND_TAP && snd != AUDIO_SND_OPEN &&
                      snd != AUDIO_SND_PLUS && snd != AUDIO_SND_MINUS);
    enqueue(&job, important);
}

void audio_play_floor(audio_snd_t kind, int floor)
{
    /* Cada piso tiene su nota: sube la escala al subir de piso */
    uint16_t f = scale_freq(floor - 1);
    job_t job = { 0 };

    switch (kind) {
    case AUDIO_SND_BEEP:      /* "ding" corto al pasar por un piso */
        job.n = 1;
        job.notes[0] = (note_t)N(f * 2 > 3800 ? 3800 : f * 2, f * 2 > 3800 ? 3800 : f * 2, 230, 45, W_BELL);
        break;
    case AUDIO_SND_ARRIVE:    /* llegada: campanilla en la nota del piso */
        job.n = 3;
        job.notes[0] = (note_t)N(f, f, 420, 80, W_BELL);
        job.notes[1] = (note_t)N(0, 0, 30, 0, W_SINE);
        job.notes[2] = (note_t)N(f * 3 / 4, f * 3 / 4, 600, 80, W_BELL);
        break;
    default:                  /* seleccion de piso */
        job.n = 2;
        job.notes[0] = (note_t)N(f, f, 60, 70, W_SINE);
        job.notes[1] = (note_t)N(f * 3 / 2 > 3800 ? 3800 : f * 3 / 2,
                                 f * 3 / 2 > 3800 ? 3800 : f * 3 / 2, 90, 60,
                                 W_SINE);
        break;
    }
    enqueue(&job, kind != AUDIO_SND_BEEP);
}

void audio_get_stats(audio_stats_t *out)
{
    *out = s_stats;
    out->pa_level = gpio_get_level(AUDIO_PA_GPIO);
}

void audio_tone(uint16_t hz, uint16_t ms)
{
    if (!s_ready) {
        return;
    }
    job_t job = { .n = 1 };
    job.notes[0] = (note_t)N(hz, hz, ms, 100, W_SINE);
    s_tone_full = true;
    xQueueSend(s_queue, &job, 0);
}

void audio_pa_force(int level)
{
    s_pa_force = level;
    if (level >= 0) {
        gpio_set_level(AUDIO_PA_GPIO, level ? 1 : 0);
    }
}
