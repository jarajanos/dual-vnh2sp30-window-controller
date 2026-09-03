#pragma once

// ─── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
#define WIFI_MAX_RETRIES    10

// ─── MQTT ─────────────────────────────────────────────────────────────────────
#define MQTT_BROKER_URI     "mqtt://192.168.1.100:1883"
#define MQTT_USERNAME       ""          // Empty string disables authentication
#define MQTT_PASSWORD       ""
#define MQTT_CLIENT_ID      "window-controller"
#define MQTT_KEEPALIVE_S    60

// Local configuration UI: http://<device-ip>/
#define WEB_CONFIG_PORT     80

// Command topics (payload: "open" | "close" | "stop")
#define TOPIC_CMD_M1        "home/greenhouse/window/motor1/cmd"
#define TOPIC_CMD_M2        "home/greenhouse/window/motor2/cmd"
#define TOPIC_CMD_ALL       "home/greenhouse/window/all/cmd"

// Retained state topics
#define TOPIC_STATE_M1      "home/greenhouse/window/motor1/state"
#define TOPIC_STATE_M2      "home/greenhouse/window/motor2/state"

// Alarm and diagnostic topics
#define TOPIC_ALARM_M1      "home/greenhouse/window/motor1/alarm"
#define TOPIC_ALARM_M2      "home/greenhouse/window/motor2/alarm"
#define TOPIC_DIAG          "home/greenhouse/window/diag"

// LWT — the broker publishes "offline" after an unexpected disconnect
#define TOPIC_LWT           "home/greenhouse/window/status"
#define LWT_ONLINE          "online"
#define LWT_OFFLINE         "offline"

// ─── GPIO ─────────────────────────────────────────────────────────────────────
#define PIN_INA1    5
#define PIN_INB1    6
#define PIN_PWM1    10
#define PIN_CS1     1       // ADC1 CH1

#define PIN_INA2    4
#define PIN_INB2    7
#define PIN_PWM2    3
#define PIN_CS2     0       // ADC1 CH0

#define PIN_SW1     18      // Motor 1 switch — active-low, pull-up
#define PIN_SW2     19      // Motor 2 switch — active-low, pull-up

// ─── LEDC PWM ─────────────────────────────────────────────────────────────────
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CH_M1          LEDC_CHANNEL_0
#define LEDC_CH_M2          LEDC_CHANNEL_1
#define LEDC_FREQ_HZ        20000       // 20 kHz — above the audible range
#define LEDC_DUTY_BITS      LEDC_TIMER_8_BIT  // 0–255
#define MOTOR_PWM_DUTY      128         // Shared default, 0–255

// ─── Motor protection ─────────────────────────────────────────────────────────
// Maximum continuous motor run time
#define MOTOR_TIMEOUT_MS    10000

// ADC sample averaging
#define CS_SAMPLE_COUNT     4

// Current measurement interval [ms]
#define CS_CHECK_MS         50

// Overcurrent relative to the exponential moving average. Evaluation starts
// after startup and only with a sufficiently strong signal to reject ADC noise.
#define CS_OVERCURRENT_MIN_RUN_MS   500
#define CS_OVERCURRENT_MIN_RAW      5
#define CS_OVERCURRENT_PERCENT      110
#define CS_OVERCURRENT_COUNT        2

// Moving-average weight: new = (old * 7 + sample) / 8.
#define CS_AVERAGE_HISTORY_WEIGHT   7

// Mechanical end-stop detection based on a sharp current drop. Evaluation
// starts after startup and only if the normal current reached
// CS_ENDSTOP_MIN_RAW. The drop must persist for several consecutive readings.
#define CS_ENDSTOP_MIN_RUN_MS       500
#define CS_ENDSTOP_MIN_RAW          5
#define CS_ENDSTOP_DROP_PERCENT     45
#define CS_ENDSTOP_COUNT            3

// ─── Per-motor defaults ───────────────────────────────────────────────────────
// These values are used when no saved configuration exists in NVS. Each one
// can be changed independently later through the web UI.
#define MOTOR1_PWM_DUTY                  MOTOR_PWM_DUTY
#define MOTOR1_TIMEOUT_MS                MOTOR_TIMEOUT_MS
#define MOTOR1_OVERCURRENT_MIN_RUN_MS    CS_OVERCURRENT_MIN_RUN_MS
#define MOTOR1_OVERCURRENT_MIN_RAW       CS_OVERCURRENT_MIN_RAW
#define MOTOR1_OVERCURRENT_PERCENT       CS_OVERCURRENT_PERCENT
#define MOTOR1_OVERCURRENT_COUNT         CS_OVERCURRENT_COUNT
#define MOTOR1_ENDSTOP_MIN_RUN_MS        CS_ENDSTOP_MIN_RUN_MS
#define MOTOR1_ENDSTOP_MIN_RAW           CS_ENDSTOP_MIN_RAW
#define MOTOR1_ENDSTOP_DROP_PERCENT      CS_ENDSTOP_DROP_PERCENT
#define MOTOR1_ENDSTOP_COUNT             CS_ENDSTOP_COUNT

#define MOTOR2_PWM_DUTY                  MOTOR_PWM_DUTY
#define MOTOR2_TIMEOUT_MS                MOTOR_TIMEOUT_MS
#define MOTOR2_OVERCURRENT_MIN_RUN_MS    CS_OVERCURRENT_MIN_RUN_MS
#define MOTOR2_OVERCURRENT_MIN_RAW       CS_OVERCURRENT_MIN_RAW
#define MOTOR2_OVERCURRENT_PERCENT       CS_OVERCURRENT_PERCENT
#define MOTOR2_OVERCURRENT_COUNT         CS_OVERCURRENT_COUNT
#define MOTOR2_ENDSTOP_MIN_RUN_MS        CS_ENDSTOP_MIN_RUN_MS
#define MOTOR2_ENDSTOP_MIN_RAW           CS_ENDSTOP_MIN_RAW
#define MOTOR2_ENDSTOP_DROP_PERCENT      CS_ENDSTOP_DROP_PERCENT
#define MOTOR2_ENDSTOP_COUNT             CS_ENDSTOP_COUNT

// ─── Switches ─────────────────────────────────────────────────────────────────
#define SW_DEBOUNCE_MS      50

// ─── Timers ───────────────────────────────────────────────────────────────────
#define DIAG_INTERVAL_MS    10000
