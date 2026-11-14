#pragma once

/**
 * @file board_config.h
 * @brief Pines y constantes del CrowPanel Advanced 7" (ESP32-P4).
 *
 * Los valores provienen del esquema publicado por Elecrow para la placa
 * CrowPanel Advanced 7" ESP32-P4 (panel EK79007). Si tu unidad es de otra
 * pulgada, solo hay que ajustar este archivo.
 */

#include "driver/gpio.h"

/* ------------------------------------------------------------------ */
/*  Pantalla MIPI DSI (EK79007)                                        */
/* ------------------------------------------------------------------ */

/* El panel es fisicamente 1024x600 (apaisado) y la app lo usa en vertical.
 * LCD_PANEL_* es la resolucion nativa del panel; LCD_H_RES/LCD_V_RES es la
 * resolucion logica que ve LVGL despues de rotar 90 grados. */
#define LCD_PANEL_H_RES         1024
#define LCD_PANEL_V_RES         600
#define LCD_H_RES               600
#define LCD_V_RES               1024

#define BSP_LCD_RST_GPIO        GPIO_NUM_41
#define BSP_LCD_BACKLIGHT_GPIO  GPIO_NUM_31

/* Canales LDO que alimentan el PHY del DSI y el tactil (2.5 V) */
#define BSP_LDO_DSI_CHAN        3
#define BSP_LDO_TOUCH_CHAN      4
#define BSP_LDO_VOLTAGE_MV      2500

/* ------------------------------------------------------------------ */
/*  Tactil GT911                                                       */
/* ------------------------------------------------------------------ */

#define BSP_I2C_PORT            I2C_NUM_0
#define BSP_I2C_SDA_GPIO        GPIO_NUM_45
#define BSP_I2C_SCL_GPIO        GPIO_NUM_46
#define BSP_I2C_FREQ_HZ         400000

#define BSP_TOUCH_RST_GPIO      GPIO_NUM_40
#define BSP_TOUCH_INT_GPIO      GPIO_NUM_42

/* ------------------------------------------------------------------ */
/*  Audio (NS4168 + microfono)                                         */
/* ------------------------------------------------------------------ */

#define BSP_LED_GPIO            GPIO_NUM_48

/* ------------------------------------------------------------------ */
/*  GPIO disponibles para el usuario (conector J7 / bornera)          */
/* ------------------------------------------------------------------ */

/*
 * El firmware original valida exactamente este conjunto y rechaza los
 * pines reservados por el panel:
 *     allowed_input(2) && allowed_input(52) &&
 *     !allowed_input(31) && !allowed_input(40)
 */
#define USER_GPIO_LIST          { 2, 3, 4, 5, 25, 27, 28, 49, 50, 51, 52 }
#define USER_GPIO_COUNT         11
#define USER_GPIO_MAX           52

/* Pines que nunca deben usarse como entrada/salida de usuario */
#define BSP_RESERVED_GPIO_MASK  ((1ULL << 31) | (1ULL << 40) | (1ULL << 41) | \
                                 (1ULL << 42) | (1ULL << 45) | (1ULL << 46) | \
                                 (1ULL << 48))

/* Tiempo de filtrado antirrebote de las entradas digitales (ms) */
#define BOARD_INPUT_FILTER_MS   30
