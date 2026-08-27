#pragma once

#include <stdbool.h>
#include "driver/gpio.h"

typedef void (*switch_cb_t)(bool pressed, void *arg);

typedef struct {
    gpio_num_t  pin;
    switch_cb_t cb;
    void       *cb_arg;

    bool     stable_state;
    bool     last_raw;
    uint32_t last_change_tick;
} switch_t;

// Initialize the input with an internal pull-up.
void switch_init(switch_t *sw, gpio_num_t pin, switch_cb_t cb, void *arg);

// Call every ~5 ms from a task to debounce the switch.
void switch_update(switch_t *sw);

bool switch_is_pressed(const switch_t *sw);
