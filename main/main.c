#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"

#include "config.h"
#include "motor.h"
#include "switch.h"
#include "wifi.h"
#include "mqtt.h"

static const char *TAG = "MAIN";

// ─── Motors ───────────────────────────────────────────────────────────────────

static motor_t motor1 = {
    .name       = "Motor1",
    .pin_ina    = PIN_INA1,
    .pin_inb    = PIN_INB1,
    .pin_cs     = PIN_CS1,
    .cs_channel = ADC_CHANNEL_1,   // GPIO1 = ADC1_CH1 on ESP32-C3
    .cs_unit    = ADC_UNIT_1,
    .ledc_ch    = LEDC_CH_M1,
};

static motor_t motor2 = {
    .name       = "Motor2",
    .pin_ina    = PIN_INA2,
    .pin_inb    = PIN_INB2,
    .pin_cs     = PIN_CS2,
    .cs_channel = ADC_CHANNEL_0,   // GPIO0 = ADC1_CH0 on ESP32-C3
    .cs_unit    = ADC_UNIT_1,
    .ledc_ch    = LEDC_CH_M2,
};

// ─── Switches ─────────────────────────────────────────────────────────────────

static switch_t sw1, sw2;

// One press opens, stops, or closes the window based on its current state.
static void sw1_cb(bool pressed, void *arg) {
    if (pressed) motor_toggle(&motor1);
}

static void sw2_cb(bool pressed, void *arg) {
    if (pressed) motor_toggle(&motor2);
}

// ─── JSON helpers ─────────────────────────────────────────────────────────────

static void build_alarm_json(motor_t *m, char *buf, size_t size) {
    snprintf(buf, size,
             "{\"motor\":\"%s\",\"reason\":\"%s\",\"cs_raw\":%d,"
             "\"cs_avg_raw\":%d,\"current_A\":%.2f}",
             m->name,
             motor_stop_reason_name(m->stop_reason),
             m->cs_raw,
             m->cs_average_raw,
             motor_current_a(m));
}

static void build_diag_json(char *buf, size_t size) {
    snprintf(buf, size,
             "{\"online\":true,\"uptime_s\":%lu,"
             "\"motor1\":\"%s\",\"motor2\":\"%s\","
             "\"cs1_raw\":%d,\"cs2_raw\":%d,"
             "\"cs1_avg\":%d,\"cs2_avg\":%d,"
             "\"i1_A\":%.2f,\"i2_A\":%.2f}",
             (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000),
             motor_state_name(motor1.state),
             motor_state_name(motor2.state),
             motor1.cs_raw, motor2.cs_raw,
             motor1.cs_average_raw, motor2.cs_average_raw,
             motor_current_a(&motor1),
             motor_current_a(&motor2));
}

// ─── MQTT command callback ────────────────────────────────────────────────────

static void on_mqtt_cmd(int idx, const char *cmd) {
    // Normalize the command to lowercase. Whitespace is not accepted.
    char c[16] = {};
    strncpy(c, cmd, sizeof(c) - 1);
    for (int i = 0; c[i]; i++) {
        if (c[i] >= 'A' && c[i] <= 'Z') c[i] += 32;
    }

    motor_t *targets[2] = { NULL, NULL };
    int n = 0;

    if      (idx == 0) { targets[0] = &motor1; n = 1; }
    else if (idx == 1) { targets[0] = &motor2; n = 1; }
    else               { targets[0] = &motor1; targets[1] = &motor2; n = 2; }

    for (int i = 0; i < n; i++) {
        motor_t *m = targets[i];
        if      (strcmp(c, "open")  == 0) motor_open(m);
        else if (strcmp(c, "close") == 0) motor_close(m);
        else if (strcmp(c, "stop")  == 0) motor_stop(m, STOP_REASON_COMMAND);
        else ESP_LOGW(TAG, "Unknown command: \"%s\"", cmd);
    }
}

// ─── FreeRTOS tasks ───────────────────────────────────────────────────────────

// Motor update loop, switch polling, and state publishing
static void motor_task(void *arg) {
    char json[192];
    TickType_t last_diag = xTaskGetTickCount();

    while (1) {
        // Debounce switches
        switch_update(&sw1);
        switch_update(&sw2);

        // Motor timeout and current-sense protection
        motor_update(&motor1);
        motor_update(&motor2);

        // Motor 1 state changed
        if (motor_take_state_changed(&motor1)) {
            const char* state = motor_state_name(motor1.state);
            mqtt_publish_state(0, state);
        }
        if (motor_take_alarm(&motor1)) {
            build_alarm_json(&motor1, json, sizeof(json));
            mqtt_publish_alarm(0, json);
        }

        // Motor 2 state changed
        if (motor_take_state_changed(&motor2)) {
            const char* state = motor_state_name(motor2.state);
            mqtt_publish_state(1, state);
        }
        if (motor_take_alarm(&motor2)) {
            build_alarm_json(&motor2, json, sizeof(json));
            mqtt_publish_alarm(1, json);
        }

        // Diagnostics
        if ((xTaskGetTickCount() - last_diag) * portTICK_PERIOD_MS >= DIAG_INTERVAL_MS) {
            last_diag = xTaskGetTickCount();
            build_diag_json(json, sizeof(json));
            mqtt_publish_diag(json);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ─── LEDC timer shared by both channels ──────────────────────────────────────

static void ledc_timer_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_BITS,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    // Motor 1 channel
    ledc_channel_config_t ch1 = {
        .gpio_num   = PIN_PWM1,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH_M1,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch1));

    // Motor 2 channel
    ledc_channel_config_t ch2 = {
        .gpio_num   = PIN_PWM2,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH_M2,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch2));
}

// ─── app_main ─────────────────────────────────────────────────────────────────

void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════╗");
    ESP_LOGI(TAG, "║  Window Controller v1.0  ║");
    ESP_LOGI(TAG, "╚══════════════════════════╝");

    // 1) Shared LEDC timer
    ledc_timer_init();

    // 2) Motors
    motor_init(&motor1);
    motor_init(&motor2);

    // 3) Switches
    switch_init(&sw1, PIN_SW1, sw1_cb, NULL);
    switch_init(&sw2, PIN_SW2, sw2_cb, NULL);

    // 4) Wi-Fi (blocks until an IP address is assigned)
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi unavailable — MQTT will not work");
    }

    // 5) MQTT (esp-mqtt handles reconnection internally)
    ESP_ERROR_CHECK(mqtt_init(on_mqtt_cmd));

    // 6) Motor task with its own stack and a priority above IDLE
    xTaskCreate(motor_task, "motor_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System started.");
}
