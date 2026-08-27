#include "motor.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MOTOR";

static adc_oneshot_unit_handle_t adc1_handle = NULL;

// ─── Internal helpers ─────────────────────────────────────────────────────────

static void set_pins(motor_t *m, int ina, int inb, uint32_t duty) {
    gpio_set_level(m->pin_ina, ina);
    gpio_set_level(m->pin_inb, inb);
    ledc_set_duty(LEDC_MODE, m->ledc_ch, duty);
    ledc_update_duty(LEDC_MODE, m->ledc_ch);
}

// Active brake: INA=LOW, INB=LOW, PWM=0
static void apply_brake(motor_t *m) {
    set_pins(m, 0, 0, 0);
}

static bool motor_is_moving(const motor_t *m) {
    return m->state == MOTOR_OPENING || m->state == MOTOR_CLOSING;
}

static void finish_at_endstop(motor_t *m) {
    motor_state_t finished_state = m->state == MOTOR_OPENING
                                 ? MOTOR_OPENED
                                 : MOTOR_CLOSED;

    apply_brake(m);
    m->state         = finished_state;
    m->stop_reason   = STOP_REASON_ENDSTOP;
    m->state_changed = true;

    ESP_LOGI(TAG, "[%s] End position reached: %s", m->name,
             motor_state_name(finished_state));
}

// ─── Public API ───────────────────────────────────────────────────────────────

void motor_init(motor_t *m) {
    // Configure INA and INB as GPIO outputs
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << m->pin_ina) | (1ULL << m->pin_inb),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    // ADC current-sense channel
    if (adc1_handle == NULL) {
        adc_oneshot_unit_init_cfg_t adc_init_config = {
            .unit_id  = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };

        ESP_ERROR_CHECK(
            adc_oneshot_new_unit(&adc_init_config, &adc1_handle)
        );
    }

    adc_oneshot_chan_cfg_t adc_channel_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc1_handle,
            m->cs_channel,
            &adc_channel_config
        )
    );

    // The LEDC channel uses the shared timer initialized in main.c
    apply_brake(m);
    m->state           = MOTOR_UNKNOWN;
    m->stop_reason     = STOP_REASON_NONE;
    m->cs_raw          = 0;
    m->cs_average_raw  = 0;
    m->last_motion     = MOTOR_UNKNOWN;
    m->endstop_count   = 0;
    m->state_changed   = false;
    m->alarm_pending   = false;

    ESP_LOGI(TAG, "[%s] initialized", m->name);
}

void motor_open(motor_t *m) {
    if (m->state == MOTOR_OPENING || m->state == MOTOR_OPENED) return;
    ESP_LOGI(TAG, "[%s] OPEN", m->name);
    set_pins(m, 1, 0, MOTOR_PWM_DUTY);
    m->state             = MOTOR_OPENING;
    m->state_changed     = true;
    m->stop_reason       = STOP_REASON_NONE;
    m->start_tick        = xTaskGetTickCount();
    m->last_cs_tick      = m->start_tick;
    m->overcurrent_count = 0;
    m->endstop_count     = 0;
    m->cs_average_raw    = 0;
    m->last_motion       = MOTOR_OPENING;
}

void motor_close(motor_t *m) {
    if (m->state == MOTOR_CLOSING || m->state == MOTOR_CLOSED) return;
    ESP_LOGI(TAG, "[%s] CLOSE", m->name);
    set_pins(m, 0, 1, MOTOR_PWM_DUTY);
    m->state             = MOTOR_CLOSING;
    m->state_changed     = true;
    m->stop_reason       = STOP_REASON_NONE;
    m->start_tick        = xTaskGetTickCount();
    m->last_cs_tick      = m->start_tick;
    m->overcurrent_count = 0;
    m->endstop_count     = 0;
    m->cs_average_raw    = 0;
    m->last_motion       = MOTOR_CLOSING;
}

void motor_stop(motor_t *m, stop_reason_t reason) {
    if (!motor_is_moving(m)) return;
    ESP_LOGW(TAG, "[%s] STOP — reason: %s", m->name, motor_stop_reason_name(reason));
    m->stop_reason = reason;
    if (reason == STOP_REASON_OVERCURRENT || reason == STOP_REASON_TIMEOUT) {
        m->alarm_pending = true;
    }
    apply_brake(m);
    m->state         = MOTOR_STOPPED;
    m->state_changed = true;
}

void motor_toggle(motor_t *m) {
    switch (m->state) {
        case MOTOR_OPENED:
            motor_close(m);
            break;
        case MOTOR_CLOSED:
            motor_open(m);
            break;
        case MOTOR_OPENING:
        case MOTOR_CLOSING:
            motor_stop(m, STOP_REASON_COMMAND);
            break;
        case MOTOR_STOPPED:
            // After a manual stop, the next press moves in the opposite direction.
            if (m->last_motion == MOTOR_OPENING) motor_close(m);
            else                                motor_open(m);
            break;
        case MOTOR_UNKNOWN:
        default:
            // Position is unknown after restart; the first end stop calibrates it.
            motor_open(m);
            break;
    }
}

void motor_update(motor_t *m) {
    if (!motor_is_moving(m)) return;

    uint32_t now   = xTaskGetTickCount();
    uint32_t elapsed_ms = (now - m->start_tick) * portTICK_PERIOD_MS;

    // 1) Timeout
    if (elapsed_ms >= MOTOR_TIMEOUT_MS) {
        ESP_LOGW(TAG, "[%s] Timeout after %lu ms", m->name, (unsigned long)elapsed_ms);
        motor_stop(m, STOP_REASON_TIMEOUT);
        return;
    }

    // 2) Current monitoring
    uint32_t cs_elapsed_ms = (now - m->last_cs_tick) * portTICK_PERIOD_MS;
    if (cs_elapsed_ms >= CS_CHECK_MS) {
        m->last_cs_tick = now;
        int raw = motor_read_cs(m);
        m->cs_raw = raw;
        ESP_LOGD(TAG, "[%s] Current raw=%d, average=%d",
                 m->name, raw, m->cs_average_raw);

        if (m->cs_average_raw == 0) {
            m->cs_average_raw = raw;
        }

        bool overcurrent = elapsed_ms >= CS_OVERCURRENT_MIN_RUN_MS &&
                           m->cs_average_raw >= CS_OVERCURRENT_MIN_RAW &&
                           raw * 100 >= m->cs_average_raw * CS_OVERCURRENT_PERCENT;

        if (overcurrent) {
            m->overcurrent_count++;
            ESP_LOGW(TAG, "[%s] Overcurrent raw=%d, average=%d (%u/%u)",
                     m->name, raw, m->cs_average_raw,
                     (unsigned)m->overcurrent_count,
                     (unsigned)CS_OVERCURRENT_COUNT);
            if (m->overcurrent_count >= CS_OVERCURRENT_COUNT) {
                ESP_LOGE(TAG, "[%s] Stopping — current exceeded average by %d %%!",
                         m->name, CS_OVERCURRENT_PERCENT - 100);
                motor_stop(m, STOP_REASON_OVERCURRENT);
                return;
            }
        } else {
            m->overcurrent_count = 0;
        }

        // An end stop is the opposite event: a sharp drop against the same average.
        bool drop = elapsed_ms >= CS_ENDSTOP_MIN_RUN_MS &&
                    m->cs_average_raw >= CS_ENDSTOP_MIN_RAW &&
                    raw * 100 <= m->cs_average_raw * CS_ENDSTOP_DROP_PERCENT;

        if (drop) {
            m->endstop_count++;
            ESP_LOGW(TAG, "[%s] Current drop raw=%d from %d (%u/%u)",
                     m->name, raw, m->cs_average_raw,
                     (unsigned)m->endstop_count,
                     (unsigned)CS_ENDSTOP_COUNT);
            if (m->endstop_count >= CS_ENDSTOP_COUNT) {
                finish_at_endstop(m);
                return;
            }
        } else {
            m->endstop_count = 0;
        }

        // Do not include a suspicious spike or drop in the average. Otherwise,
        // the reference would drift toward the fault while it is being confirmed.
        if (!overcurrent && !drop) {
            m->cs_average_raw =
                (m->cs_average_raw * CS_AVERAGE_HISTORY_WEIGHT + raw) /
                (CS_AVERAGE_HISTORY_WEIGHT + 1);
        }
    }
}

int motor_read_cs(motor_t *m) {
    long sum = 0;
    int raw;
    for (int i = 0; i < CS_SAMPLE_COUNT; i++) {
        ESP_ERROR_CHECK(
            adc_oneshot_read(
                adc1_handle,
                m->cs_channel,
                &raw
            )
        );
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return (int)(sum / CS_SAMPLE_COUNT);
}

float motor_current_a(const motor_t *m) {
    return (float)m->cs_raw * 377.0f * 3.3f / 4095.0f;
}

const char *motor_state_name(motor_state_t s) {
    switch (s) {
        case MOTOR_UNKNOWN: return "unknown";
        case MOTOR_STOPPED: return "stopped";
        case MOTOR_OPENING: return "opening";
        case MOTOR_CLOSING: return "closing";
        case MOTOR_OPENED:  return "opened";
        case MOTOR_CLOSED:  return "closed";
        default:            return "unknown";
    }
}

const char *motor_stop_reason_name(stop_reason_t r) {
    switch (r) {
        case STOP_REASON_COMMAND:     return "command";
        case STOP_REASON_TIMEOUT:     return "timeout";
        case STOP_REASON_OVERCURRENT: return "overcurrent";
        case STOP_REASON_ENDSTOP:     return "endstop";
        default:                      return "none";
    }
}

bool motor_take_state_changed(motor_t *m) {
    bool v = m->state_changed;
    m->state_changed = false;
    return v;
}

bool motor_take_alarm(motor_t *m) {
    bool v = m->alarm_pending;
    m->alarm_pending = false;
    return v;
}
