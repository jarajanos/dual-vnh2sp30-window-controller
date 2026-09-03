#include "settings.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "SETTINGS";
static const char *NAMESPACE = "motor_cfg";

#define SETTINGS_VERSION 1

typedef struct {
    uint32_t version;
    motor_config_t config;
} stored_motor_config_t;

esp_err_t settings_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs reinitialization");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Could not erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t settings_load_motor(int motor_idx, const motor_config_t *defaults,
                              motor_config_t *config) {
    if (!defaults || !config || motor_idx < 0 || motor_idx > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = *defaults;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    stored_motor_config_t stored = {};
    size_t size = sizeof(stored);
    const char *key = motor_idx == 0 ? "motor1" : "motor2";
    err = nvs_get_blob(nvs, key, &stored, &size);
    nvs_close(nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGW(TAG, "%s settings have a different size; using defaults", key);
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    if (size != sizeof(stored) || stored.version != SETTINGS_VERSION ||
        !motor_config_validate(&stored.config, NULL, NULL)) {
        ESP_LOGW(TAG, "%s settings are incompatible or invalid; using defaults", key);
        return ESP_OK;
    }

    *config = stored.config;
    ESP_LOGI(TAG, "Loaded %s settings from NVS", key);
    return ESP_OK;
}

esp_err_t settings_save_all(const motor_config_t *motor1,
                            const motor_config_t *motor2) {
    if (!motor1 || !motor2 ||
        !motor_config_validate(motor1, NULL, NULL) ||
        !motor_config_validate(motor2, NULL, NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    stored_motor_config_t stored1 = {
        .version = SETTINGS_VERSION,
        .config = *motor1,
    };
    stored_motor_config_t stored2 = {
        .version = SETTINGS_VERSION,
        .config = *motor2,
    };

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(nvs, "motor1", &stored1, sizeof(stored1));
    if (err == ESP_OK) err = nvs_set_blob(nvs, "motor2", &stored2, sizeof(stored2));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err == ESP_OK) ESP_LOGI(TAG, "Motor settings saved to NVS");
    return err;
}
