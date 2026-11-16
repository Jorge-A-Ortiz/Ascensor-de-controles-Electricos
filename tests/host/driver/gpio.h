#pragma once
#include <stdint.h>
typedef struct {uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_INPUT_OUTPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
int gpio_config(const gpio_config_t *c);
int gpio_get_level(int g);
int gpio_set_level(int g,int v);
int gpio_reset_pin(int g);
