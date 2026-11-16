/**
 * @file cli.c
 * @brief Consola de pruebas por UART0 (115200 8N1, lineas terminadas en \n).
 *
 * Permite probar TODA la cadena sin tocar la pantalla: los comandos "click"
 * y "dd" lanzan los mismos callbacks de LVGL que un toque real, y los
 * comandos "gpio" leen/escriben los pines fisicos para comprobar que los
 * cambios llegan al hardware y no solo a la pantalla.
 *
 * Todas las respuestas empiezan por OK, ERR o un prefijo de datos (UI, ST...)
 * para que un script pueda validarlas.
 */

#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app.h"
#include "audio.h"
#include "board.h"
#include "board_config.h"
#include "elevator.h"
#include "rules.h"
#include "ui.h"

static const int s_user_gpios[USER_GPIO_COUNT] = USER_GPIO_LIST;

static bool user_gpio_ok(int n)
{
    for (int i = 0; i < USER_GPIO_COUNT; i++) {
        if (s_user_gpios[i] == n) {
            return true;
        }
    }
    return false;
}

static void cmd_state(void)
{
    elevator_state_t st;
    elevator_get_state(&st);
    app_state_t *a = app_state();
    printf("ST floors=%u cur=%u known=%d dir=%d moving=%d doors=%d demo=%d "
           "blocks_on=%d nblocks=%u active=%u sound=%u pending=%d\n",
           st.floors, st.current_floor, st.position_known, st.dir, st.moving,
           st.doors_open, st.demo_mode, a->cfg.blocks_enabled,
           (unsigned)a->block_count, (unsigned)rules_active_count(),
           audio_get_volume(), elevator_has_pending());
    printf("ST rules:");
    for (int i = 0; i < a->cfg.floors; i++) {
        printf(" P%d=gpio%d/lvl%d", i + 1,
               a->cfg.floor_gpio[i] == 0xFF ? -1 : a->cfg.floor_gpio[i],
               a->cfg.floor_level[i]);
    }
    printf("\n");
}

static void cmd_gpio(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "dump")) {
        printf("GPIO");
        for (int i = 0; i < USER_GPIO_COUNT; i++) {
            printf(" %d=%d", s_user_gpios[i], gpio_get_level(s_user_gpios[i]));
        }
        printf("\n");
        return;
    }
    if (argc < 3) {
        printf("ERR uso: gpio dump | gpio read N | gpio drive N 0|1 | gpio pull N up|down|none\n");
        return;
    }
    int n = atoi(argv[2]);
    if (!user_gpio_ok(n)) {
        printf("ERR gpio %d no es de usuario\n", n);
        return;
    }
    if (!strcmp(argv[1], "read")) {
        printf("GPIO %d=%d\n", n, gpio_get_level(n));
    } else if (!strcmp(argv[1], "drive") && argc >= 4) {
        gpio_set_direction(n, GPIO_MODE_INPUT_OUTPUT);
        gpio_set_level(n, atoi(argv[3]) ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        printf("GPIO %d wrote=%d read=%d\n", n, atoi(argv[3]) ? 1 : 0,
               gpio_get_level(n));
    } else if (!strcmp(argv[1], "pull") && argc >= 4) {
        gpio_pullup_dis(n);
        gpio_pulldown_dis(n);
        if (!strcmp(argv[3], "up")) {
            gpio_pullup_en(n);
        } else if (!strcmp(argv[3], "down")) {
            gpio_pulldown_en(n);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        printf("GPIO %d pull=%s read=%d\n", n, argv[3], gpio_get_level(n));
    } else {
        printf("ERR gpio: subcomando desconocido\n");
    }
}

static void cmd_rules(void)
{
    app_state_t *a = app_state();
    printf("RULES count=%u enabled=%d err='%s'\n", (unsigned)a->block_count,
           rules_enabled(), rules_last_error());
    for (size_t i = 0; i < a->block_count; i++) {
        const rule_block_t *b = &a->blocks[i];
        printf("RULES [%u] en=%d IF gpio%u==%u THEN gpio%u<=%u REST %u\n",
               (unsigned)i, b->enabled, b->in_gpio, b->in_level, b->out_gpio,
               b->out_level, b->rest_level);
    }
}

static void cmd_perf(int argc, char **argv)
{
    board_perf_t p;
    if (argc >= 2 && !strcmp(argv[1], "reset")) {
        board_perf_reset();
        printf("OK perf reset\n");
        return;
    }
    board_perf_get(&p);
    printf("PERF refreshes=%lu avg_ms=%.1f max_ms=%lu avg_px=%lu window_ms=%lu fps=%.1f\n",
           (unsigned long)p.refreshes, p.refreshes ? (double)p.total_ms / p.refreshes : 0.0,
           (unsigned long)p.max_ms, p.refreshes ? (unsigned long)(p.total_px / p.refreshes) : 0,
           (unsigned long)p.window_ms,
           p.window_ms ? 1000.0 * p.refreshes / p.window_ms : 0.0);
}

static void handle_line(char *line)
{
    char *argv[8];
    int argc = 0;
    for (char *t = strtok(line, " \t\r\n"); t != NULL && argc < 8;
         t = strtok(NULL, " \t\r\n")) {
        argv[argc++] = t;
    }
    if (argc == 0) {
        return;
    }
    const char *c = argv[0];

    if (!strcmp(c, "help")) {
        printf("OK cmds: state call click dd goto ui gpio rules sound vol perf heap reboot ping\n");
    } else if (!strcmp(c, "ping")) {
        printf("OK pong t=%lld\n", (long long)(esp_timer_get_time() / 1000));
    } else if (!strcmp(c, "state")) {
        cmd_state();
    } else if (!strcmp(c, "call") && argc >= 2) {
        elevator_dir_t d = ELEV_DIR_IDLE;
        if (argc >= 3) {
            d = !strcmp(argv[2], "up") ? ELEV_DIR_UP
                : !strcmp(argv[2], "down") ? ELEV_DIR_DOWN : ELEV_DIR_IDLE;
        }
        printf("OK call -> %d\n", (int)elevator_call((uint8_t)atoi(argv[1]), d));
    } else if (!strcmp(c, "click") && argc >= 2) {
        bool ok = ui_test_click(argv[1], argc >= 3 ? atoi(argv[2]) : 0);
        printf(ok ? "OK click %s\n" : "ERR click %s\n", argv[1]);
    } else if (!strcmp(c, "dd") && argc >= 4) {
        bool ok = ui_test_dd(argv[1], atoi(argv[2]), atoi(argv[3]));
        printf(ok ? "OK dd %s\n" : "ERR dd %s\n", argv[1]);
    } else if (!strcmp(c, "goto") && argc >= 2) {
        ui_test_goto(atoi(argv[1]));
        printf("OK goto %s\n", argv[1]);
    } else if (!strcmp(c, "swipe") && argc >= 2) {
        ui_test_swipe(atoi(argv[1]));
        printf("OK swipe %s\n", argv[1]);
    } else if (!strcmp(c, "ui")) {
        ui_test_dump();
    } else if (!strcmp(c, "gpio")) {
        cmd_gpio(argc, argv);
    } else if (!strcmp(c, "rules")) {
        cmd_rules();
    } else if (!strcmp(c, "sound") && argc >= 2) {
        int id = atoi(argv[1]);
        if (id >= 0 && id < AUDIO_SND_COUNT) {
            if (argc >= 3) {
                audio_play_floor((audio_snd_t)id, atoi(argv[2]));
            } else {
                audio_play((audio_snd_t)id);
            }
            printf("OK sound %d\n", id);
        } else {
            printf("ERR sound id 0..%d\n", AUDIO_SND_COUNT - 1);
        }
    } else if (!strcmp(c, "vol") && argc >= 2) {
        audio_set_volume((uint8_t)atoi(argv[1]));
        printf("OK vol %u\n", audio_get_volume());
    } else if (!strcmp(c, "perf")) {
        cmd_perf(argc, argv);
    } else if (!strcmp(c, "ppatest")) {
        int bad = board_ppa_selftest();
        if (bad == 0) {
            printf("OK ppatest rotacion identica a LVGL\n");
        } else {
            printf("ERR ppatest bad=%d\n", bad);
        }
    } else if (!strcmp(c, "heap")) {
        printf("HEAP internal=%u psram=%u\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else if (!strcmp(c, "reboot")) {
        printf("OK reboot\n");
        fflush(stdout);
        esp_restart();
    } else {
        printf("ERR comando desconocido: %s\n", c);
    }
}

static void cli_task(void *arg)
{
    (void)arg;
    char line[96];
    size_t n = 0;

    for (;;) {
        uint8_t ch;
        int r = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
        if (r <= 0) {
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            if (n > 0) {
                line[n] = '\0';
                handle_line(line);
                n = 0;
            }
        } else if (n < sizeof(line) - 1) {
            line[n++] = (char)ch;
        }
    }
}

void cli_start(void)
{
    if (!uart_is_driver_installed(UART_NUM_0)) {
        uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    }
    xTaskCreate(cli_task, "cli", 6144, NULL, 3, NULL);
    printf("OK cli listo (escribe 'help')\n");
}
