#include "switch.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "SWITCH";

void switch_init(switch_t *sw, gpio_num_t pin, switch_cb_t cb, void *arg) {
    sw->pin              = pin;
    sw->cb               = cb;
    sw->cb_arg           = arg;
    sw->last_change_tick = xTaskGetTickCount();

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    sw->stable_state = gpio_get_level(pin);
    sw->last_raw     = sw->stable_state;

    ESP_LOGI(TAG, "GPIO%d initialized (pull-up, active-low)", pin);
}

void switch_update(switch_t *sw) {
    bool raw = gpio_get_level(sw->pin);
    uint32_t now = xTaskGetTickCount();

    if (raw != sw->last_raw) {
        sw->last_change_tick = now;
        sw->last_raw = raw;
    }

    uint32_t elapsed_ms = (now - sw->last_change_tick) * portTICK_PERIOD_MS;
    if (elapsed_ms >= SW_DEBOUNCE_MS && raw != sw->stable_state) {
        sw->stable_state = raw;
        bool pressed = (raw == 0);  // Active-low
        ESP_LOGI(TAG, "GPIO%d → %s", sw->pin, pressed ? "PRESSED" : "RELEASED");
        if (sw->cb) sw->cb(pressed, sw->cb_arg);
    }
}

bool switch_is_pressed(const switch_t *sw) {
    return sw->stable_state == 0;
}
