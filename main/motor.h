#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

typedef enum {
    MOTOR_UNKNOWN = 0,
    MOTOR_STOPPED,
    MOTOR_OPENING,
    MOTOR_CLOSING,
    MOTOR_OPENED,
    MOTOR_CLOSED,
} motor_state_t;

typedef enum {
    STOP_REASON_NONE = 0,
    STOP_REASON_COMMAND,
    STOP_REASON_TIMEOUT,
    STOP_REASON_OVERCURRENT,
    STOP_REASON_ENDSTOP,
} stop_reason_t;

typedef struct {
    uint32_t pwm_duty;
    uint32_t timeout_ms;

    uint32_t overcurrent_min_run_ms;
    uint32_t overcurrent_min_raw;
    uint32_t overcurrent_percent;
    uint32_t overcurrent_count;

    uint32_t endstop_min_run_ms;
    uint32_t endstop_min_raw;
    uint32_t endstop_drop_percent;
    uint32_t endstop_count;
} motor_config_t;

typedef struct {
    const char      *name;

    // GPIO
    gpio_num_t       pin_ina;
    gpio_num_t       pin_inb;
    gpio_num_t       pin_cs;    // ADC channel is selected through cs_channel

    // ADC
    adc_channel_t    cs_channel;
    adc_unit_t       cs_unit;

    // LEDC
    ledc_channel_t   ledc_ch;

    // Configured values and the immutable snapshot used for current movement.
    motor_config_t   config;
    motor_config_t   active_config;

    // State (private — do not modify directly)
    motor_state_t    state;
    stop_reason_t    stop_reason;
    int              cs_raw;            // Last measured value
    uint32_t         start_tick;        // FreeRTOS tick when movement started
    uint32_t         last_cs_tick;      // Tick of the last current measurement
    uint8_t          overcurrent_count; // Consecutive overcurrent readings
    uint8_t          endstop_count;     // Consecutive current-drop readings
    int              cs_average_raw;    // Exponential moving average of current
    motor_state_t    last_motion;       // Direction before a manual stop
    bool             state_changed;
    bool             alarm_pending;
} motor_t;

// Validate the complete configuration. If invalid, field/reason identify the
// first rejected value when the corresponding output pointer is non-NULL.
bool motor_config_validate(const motor_config_t *config,
                           const char **field, const char **reason);

// Thread-safe configuration access. A changed configuration takes effect on
// the next movement; a motor already moving keeps its active snapshot.
void motor_set_config(motor_t *m, const motor_config_t *config);
void motor_get_config(const motor_t *m, motor_config_t *config);

// Initialize the motor GPIO and LEDC channel, then apply the brake.
void motor_init(motor_t *m);

// Start movement in the requested direction; does nothing if already moving there.
void motor_open(motor_t *m);
void motor_close(motor_t *m);
void motor_stop(motor_t *m, stop_reason_t reason);

// One-button control: end position -> move, moving -> stop, stopped -> reverse.
void motor_toggle(motor_t *m);

// Call every ~10 ms from a FreeRTOS task to check timeout and current sensing.
void motor_update(motor_t *m);

// Read and average the current-sense pin; returns a raw ADC value (0–4095).
int  motor_read_cs(motor_t *m);

// Approximate current in amperes.
float motor_current_a(const motor_t *m);

const char *motor_state_name(motor_state_t s);
const char *motor_stop_reason_name(stop_reason_t r);

// Read and clear the corresponding pending flag.
bool motor_take_state_changed(motor_t *m);
bool motor_take_alarm(motor_t *m);
