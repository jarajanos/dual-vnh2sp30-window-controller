#pragma once

#include "esp_err.h"
#include "motor.h"

// Start the local configuration web server.
esp_err_t web_config_start(motor_t *motor1, motor_t *motor2);
