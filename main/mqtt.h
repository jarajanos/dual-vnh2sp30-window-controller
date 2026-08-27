#pragma once

#include "esp_err.h"

typedef void (*mqtt_cmd_cb_t)(int motor_idx, const char *cmd);
// motor_idx: 0 = motor1, 1 = motor2, 2 = both

esp_err_t mqtt_init(mqtt_cmd_cb_t cb);

// Publish helpers; esp-mqtt permits calls from any task.
void mqtt_publish_state(int motor_idx, const char *state_json);
void mqtt_publish_alarm(int motor_idx, const char *alarm_json);
void mqtt_publish_diag(const char *diag_json);
