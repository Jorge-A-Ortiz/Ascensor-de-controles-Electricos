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

#include "board_config.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"

#include "driver/i2c_master.h"
#include "driver/ledc.h"

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

static esp_err_t lvgl_init(void)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack    = 8192;
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    /* El panel es DSI (DPI): LVGL usa sus propios buffers y el puerto los
     * vuelca al framebuffer. La rotacion a vertical se hace por software
     * (sw_rotate), porque un panel DPI no acepta swap_xy por hardware. */
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = s_io,
        .panel_handle  = s_panel,
        .buffer_size   = LCD_PANEL_H_RES * 60,
        .double_buffer = true,
        .hres          = LCD_PANEL_H_RES,
        .vres          = LCD_PANEL_V_RES,
        .monochrome    = false,
        .flags = {
            .buff_dma     = true,
            .buff_spiram  = true,
            .sw_rotate    = true,
            .full_refresh = false,
            .direct_mode  = false,
        },
    };
    lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags.avoid_tearing = false,
    };
    s_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    ESP_RETURN_ON_FALSE(s_disp != NULL, ESP_FAIL, TAG, "lvgl disp");

    /* 90 grados: 1024x600 nativo -> 600x1024 logico. */
    lv_disp_set_rotation(s_disp, LV_DISP_ROT_90);

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = BSP_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io),
                        TAG, "touch io");

    esp_lcd_touch_handle_t tp = NULL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,   /* tras swap: eje X logico (600) */
        .y_max        = LCD_V_RES,   /* tras swap: eje Y logico (1024) */
        .rst_gpio_num = BSP_TOUCH_RST_GPIO,
        .int_gpio_num = BSP_TOUCH_INT_GPIO,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 1,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp),
                        TAG, "gt911");

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
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "ledc fade");
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    board_set_backlight(0);

    ESP_RETURN_ON_ERROR(i2c_init(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(ldo_init(), TAG, "ldo");
    ESP_RETURN_ON_ERROR(display_init(), TAG, "display");
    ESP_RETURN_ON_ERROR(lvgl_init(), TAG, "lvgl");

    board_set_backlight(70);
    ESP_LOGI(TAG, "Display y tactil listos (600x1024, GT911)");
    return ESP_OK;
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
    static const char *names[] = { "llegada", "pulsacion", "piso intermedio" };
    ESP_LOGI(TAG, "aviso de audio: %s", names[sound]);
    /* TODO: NS4168 por I2S (ver README, seccion Audio). */
}

void *board_i2c_bus(void)
{
    return s_i2c_bus;
}
