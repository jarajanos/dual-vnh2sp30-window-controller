#pragma once

#include "esp_err.h"
#include "motor.h"

// Initialize NVS. Must be called once before Wi-Fi and settings access.
esp_err_t settings_init(void);

// Load one motor configuration. Missing, outdated, or invalid data leaves the
// supplied defaults in place.
esp_err_t settings_load_motor(int motor_idx, const motor_config_t *defaults,
                              motor_config_t *config);

// Persist both motor configurations in one NVS commit.
esp_err_t settings_save_all(const motor_config_t *motor1,
                            const motor_config_t *motor2);
