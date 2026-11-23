/**
 * @file board.c
 * @brief Pantalla MIPI DSI (EK79007) + tactil GT911 + LVGL.
 *
 * La app original usa LVGL 8 a traves de esp_lvgl_port, rota el panel a
 * vertical y maneja doble framebuffer. Aqui se mantiene el mismo esquema,
 * delegando la rotacion en el puerto de LVGL.
 */

#include "board.h"

#include <string.h>

#include "audio.h"
#include "board_config.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_lcd_ek79007.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_gt911.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "BOARD";

#define LCD_BL_LEDC_TIMER      LEDC_TIMER_0
#define LCD_BL_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_CHANNEL    LEDC_CHANNEL_0
#define LCD_BL_LEDC_RES        LEDC_TIMER_10_BIT

static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static lv_disp_t *s_disp;
static esp_ldo_channel_handle_t s_ldo_dsi;
static esp_ldo_channel_handle_t s_ldo_touch;

static board_perf_t s_perf;
static int64_t s_perf_t0;

static void monitor_cb(lv_disp_drv_t *drv, uint32_t time_ms, uint32_t px)
{
    (void)drv;
    s_perf.refreshes++;
    s_perf.total_ms += time_ms;
    s_perf.total_px += px;
    if (time_ms > s_perf.max_ms) {
        s_perf.max_ms = time_ms;
    }
}

void board_perf_reset(void)
{
    memset(&s_perf, 0, sizeof(s_perf));
    s_perf_t0 = esp_timer_get_time();
}

void board_perf_get(board_perf_t *out)
{
    *out = s_perf;
    out->window_ms = (uint32_t)((esp_timer_get_time() - s_perf_t0) / 1000);
}

/* ------------------------------------------------------------------ */
/*  Retroiluminacion                                                   */
/* ------------------------------------------------------------------ */

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LCD_BL_LEDC_MODE,
        .duty_resolution = LCD_BL_LEDC_RES,
        .timer_num       = LCD_BL_LEDC_TIMER,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    ledc_channel_config_t ch = {
        .gpio_num   = BSP_LCD_BACKLIGHT_GPIO,
        .speed_mode = LCD_BL_LEDC_MODE,
        .channel    = LCD_BL_LEDC_CHANNEL,
        .timer_sel  = LCD_BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch);
}

void board_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (1023u * percent) / 100u;
    ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL);
}

/* ------------------------------------------------------------------ */
/*  I2C + LDO                                                          */
/* ------------------------------------------------------------------ */

static esp_err_t i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port          = BSP_I2C_PORT,
        .sda_io_num        = BSP_I2C_SDA_GPIO,
        .scl_io_num        = BSP_I2C_SCL_GPIO,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

static esp_err_t ldo_init(void)
{
    esp_ldo_channel_config_t dsi_cfg = {
        .chan_id    = BSP_LDO_DSI_CHAN,
        .voltage_mv = BSP_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&dsi_cfg, &s_ldo_dsi),
                        TAG, "ldo dsi");

    esp_ldo_channel_config_t touch_cfg = {
        .chan_id    = BSP_LDO_TOUCH_CHAN,
        .voltage_mv = BSP_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&touch_cfg, &s_ldo_touch),
                        TAG, "ldo touch");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Panel DSI + tactil + LVGL                                          */
/* ------------------------------------------------------------------ */

static esp_err_t display_init(void)
{
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = EK79007_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), TAG, "dsi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = EK79007_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io),
                        TAG, "dbi io");
    s_io = io;

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel     = 0,
        .dpi_clk_src         = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz  = 52,
        .in_color_format     = LCD_COLOR_FMT_RGB565,
        .out_color_format    = LCD_COLOR_FMT_RGB565,
        .num_fbs             = 2,   /* doble framebuffer, como el original */
        .video_timing = {
            .h_size            = LCD_PANEL_H_RES,
            .v_size            = LCD_PANEL_V_RES,
            .hsync_pulse_width = 10,
            .hsync_back_porch  = 160,
            .hsync_front_porch = 160,
            .vsync_pulse_width = 1,
            .vsync_back_porch  = 23,
            .vsync_front_porch = 12,
        },
    };

    ek79007_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus     = dsi_bus,
            .dpi_config  = &dpi_cfg,
            .lane_num    = 2,
        },
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_LCD_RST_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor_cfg,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ek79007(io, &panel_cfg, &s_panel),
                        TAG, "panel ek79007");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");

    /* La rotacion a vertical la hace el puerto de LVGL: los paneles DPI no
     * soportan swap_xy/mirror a nivel de panel. */
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Volcado con rotacion por hardware (PPA), como el original          */
/*                                                                     */
/*  LVGL dibuja en vertical 600x1024 en buffers pequenos de RAM        */
/*  interna; cada franja se rota 90 grados con el PPA directamente al  */
/*  framebuffer del panel (1024x600). La rotacion por software de      */
/*  LVGL costaba ~90 ms por pantalla completa; asi es casi gratis.     */
/*                                                                     */
/*  Correspondencia: logico (x, y) -> fisico (X = y, Y = 599 - x), la  */
/*  misma que LV_DISP_ROT_90 de LVGL 8 (el PPA gira en sentido         */
/*  antihorario, ver board_ppa_selftest()).                            */
/* ------------------------------------------------------------------ */

#define DRAW_BUF_LINES   48
#define FB_BYTES         (LCD_PANEL_H_RES * LCD_PANEL_V_RES * 2)

static ppa_client_handle_t s_ppa;
static SemaphoreHandle_t s_ppa_done;

static bool ppa_done_cb(ppa_client_handle_t client, ppa_event_data_t *event, void *user_data)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_ppa_done, &woken);
    return woken == pdTRUE;
}
static uint8_t *s_fb;
static bool s_landscape;
static esp_lcd_touch_handle_t s_tp;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;

/* El PPA se bloquea con areas de ancho/alto impares (p. ej. 425x67). Se
 * redondean las areas a multiplos de 8 pixeles (600 y 1024 lo son). */
static void rounder_cb(lv_disp_drv_t *drv, lv_area_t *a)
{
    a->x1 &= ~7;
    a->y1 &= ~7;
    a->x2 |= 7;
    a->y2 |= 7;
    if (a->x2 > (lv_coord_t)(drv->hor_res - 1)) {
        a->x2 = drv->hor_res - 1;
    }
    if (a->y2 > (lv_coord_t)(drv->ver_res - 1)) {
        a->y2 = drv->ver_res - 1;
    }
}

static void ppa_flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *map)
{
    uint32_t w = a->x2 - a->x1 + 1;
    uint32_t h = a->y2 - a->y1 + 1;

    ppa_srm_oper_config_t cfg = {
        .in = {
            .buffer = map,
            .pic_w = w, .pic_h = h,
            .block_w = w, .block_h = h,
            .block_offset_x = 0, .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = s_fb,
            .buffer_size = FB_BYTES,
            .pic_w = LCD_PANEL_H_RES, .pic_h = LCD_PANEL_V_RES,
            /* vertical: X = y, Y = 599 - x (giro 90); horizontal: copia directa */
            .block_offset_x = (uint32_t)(s_landscape ? a->x1 : a->y1),
            .block_offset_y = (uint32_t)(s_landscape ? a->y1 : LCD_PANEL_V_RES - 1 - a->x2),
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = s_landscape ? PPA_SRM_ROTATION_ANGLE_0 : PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_NON_BLOCKING,
    };
    xSemaphoreTake(s_ppa_done, 0);
    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa, &cfg);
    if (err == ESP_OK && xSemaphoreTake(s_ppa_done, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGE(TAG, "PPA sin respuesta (timeout) en area x%ld..%ld y%ld..%ld",
                 (long)a->x1, (long)a->x2, (long)a->y1, (long)a->y2);
        err = ESP_ERR_TIMEOUT;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PPA fallo (%s) en area %ld,%ld %lux%lu", esp_err_to_name(err),
                 (long)a->x1, (long)a->y1, (unsigned long)w, (unsigned long)h);
    }
    lv_disp_flush_ready(drv);
}

/* Comprueba que el PPA produce exactamente la misma rotacion que LVGL
 * (LV_DISP_ROT_90): dest[(w-1-x)*h + y] = src[y*w + x]. Devuelve el numero
 * de pixeles distintos (0 = correcto). */
int board_ppa_selftest(void)
{
    enum { W = 40, H = 24 };
    uint16_t *src = heap_caps_aligned_alloc(64, 64 * 32 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    uint16_t *dst = heap_caps_aligned_alloc(64, 64 * 64 * 2, MALLOC_CAP_SPIRAM);
    if (src == NULL || dst == NULL) {
        free(src);
        free(dst);
        return -1;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            src[y * W + x] = (uint16_t)(y * 64 + x + 1);
        }
    }
    memset(dst, 0, H * W * 2);
    ppa_srm_oper_config_t cfg = {
        .in = { .buffer = src, .pic_w = W, .pic_h = H, .block_w = W, .block_h = H,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = dst, .buffer_size = 64 * 64 * 2, .pic_w = H, .pic_h = W,
                 .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = 1.0f, .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    int bad = -2;
    if (ppa_do_scale_rotate_mirror(s_ppa, &cfg) == ESP_OK) {
        bad = 0;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                if (dst[(W - 1 - x) * H + y] != src[y * W + x]) {
                    bad++;
                }
            }
        }
    }
    free(src);
    free(dst);
    return bad;
}

static esp_err_t lvgl_init(void)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack    = 8192;
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    /* Framebuffer del panel DSI y cliente PPA */
    void *fb0 = NULL, *fb1 = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1),
                        TAG, "frame buffers");
    s_fb = fb0;
    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    ESP_RETURN_ON_ERROR(ppa_register_client(&ppa_cfg, &s_ppa), TAG, "ppa client");
    s_ppa_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_ppa_done != NULL, ESP_ERR_NO_MEM, TAG, "ppa sem");
    ppa_event_callbacks_t ppa_cbs = { .on_trans_done = ppa_done_cb };
    ESP_RETURN_ON_ERROR(ppa_client_register_event_callbacks(s_ppa, &ppa_cbs), TAG, "ppa cb");

    /* Buffers de dibujo en RAM interna (mucho mas rapida que la PSRAM) */
    size_t buf_px = (size_t)LCD_H_RES * DRAW_BUF_LINES;
    lv_color_t *buf1 = heap_caps_aligned_alloc(64, buf_px * sizeof(lv_color_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_aligned_alloc(64, buf_px * sizeof(lv_color_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "draw buffers");

    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_FAIL, TAG, "lvgl lock");
    lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2, buf_px);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res    = LCD_H_RES;   /* logico vertical 600x1024 */
    s_disp_drv.ver_res    = LCD_V_RES;
    s_disp_drv.flush_cb   = ppa_flush_cb;
    s_disp_drv.rounder_cb = rounder_cb;
    s_disp_drv.draw_buf   = &s_draw_buf;
    s_disp_drv.monitor_cb = monitor_cb;
    s_disp = lv_disp_drv_register(&s_disp_drv);
    lvgl_port_unlock();
    ESP_RETURN_ON_FALSE(s_disp != NULL, ESP_FAIL, TAG, "lvgl disp");
    board_perf_reset();

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = BSP_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io),
                        TAG, "touch io");

    /* El GT911 entrega coordenadas en la orientacion nativa del panel
     * (1024x600). NO se intercambian ejes aqui: LVGL 8 rota los puntos de
     * entrada por su cuenta en indev_pointer_proc() usando disp_drv.rotated,
     * asi que un swap en el controlador haria la rotacion dos veces. */
    esp_lcd_touch_handle_t tp = NULL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_PANEL_H_RES - 1,
        .y_max        = LCD_PANEL_V_RES - 1,
        .rst_gpio_num = BSP_TOUCH_RST_GPIO,
        .int_gpio_num = BSP_TOUCH_INT_GPIO,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 1,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp),
                        TAG, "gt911");
    s_tp = tp;

    lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = s_disp,
        .handle = tp,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg) != NULL, ESP_FAIL,
                        TAG, "lvgl touch");

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  API publica                                                        */
/* ------------------------------------------------------------------ */

esp_err_t board_init(void)
{
    /* LEDC exige un timer configurado antes de instalar el fade. */
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "ledc fade");
    board_set_backlight(0);

    ESP_RETURN_ON_ERROR(i2c_init(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(ldo_init(), TAG, "ldo");
    ESP_RETURN_ON_ERROR(display_init(), TAG, "display");
    ESP_RETURN_ON_ERROR(lvgl_init(), TAG, "lvgl");

    board_set_backlight(70);
    ESP_LOGI(TAG, "Display y tactil listos (600x1024, GT911)");
    return ESP_OK;
}

void board_set_landscape(bool landscape)
{
    if (landscape == s_landscape || s_disp == NULL) {
        return;
    }
    s_landscape = landscape;
    /* Tactil: el GT911 entrega coordenadas nativas (1024x600). En horizontal se
     * usan tal cual; en vertical se refleja Y y se intercambian los ejes. */
    if (s_tp != NULL) {
        esp_lcd_touch_set_swap_xy(s_tp, !landscape);
        esp_lcd_touch_set_mirror_x(s_tp, false);
        esp_lcd_touch_set_mirror_y(s_tp, !landscape);
    }
    s_disp_drv.hor_res = landscape ? LCD_V_RES : LCD_H_RES;
    s_disp_drv.ver_res = landscape ? LCD_H_RES : LCD_V_RES;
    lv_disp_drv_update(s_disp, &s_disp_drv);
    ESP_LOGI(TAG, "Pantalla %s (%dx%d)", landscape ? "horizontal" : "vertical",
             (int)s_disp_drv.hor_res, (int)s_disp_drv.ver_res);
}

bool board_is_landscape(void)
{
    return s_landscape;
}

bool board_lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void board_unlock(void)
{
    lvgl_port_unlock();
}

void board_sound(board_sound_t sound)
{
    switch (sound) {
    case BOARD_SOUND_ARRIVAL:      audio_play(AUDIO_SND_ARRIVE); break;
    case BOARD_SOUND_PRESS:        audio_play(AUDIO_SND_PRESS); break;
    case BOARD_SOUND_INTERMEDIATE: audio_play(AUDIO_SND_BEEP); break;
    }
}

void *board_i2c_bus(void)
{
    return s_i2c_bus;
}
