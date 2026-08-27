#include "mqtt.h"
#include "config.h"

#include <string.h>
#include "mqtt_client.h"
#include "esp_log.h"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_cmd_cb_t s_cmd_cb = NULL;

static const char *state_topics[2] = { TOPIC_STATE_M1, TOPIC_STATE_M2 };
static const char *alarm_topics[2] = { TOPIC_ALARM_M1, TOPIC_ALARM_M2 };

// ─── Command topic parsing ────────────────────────────────────────────────────

static void handle_data(esp_mqtt_event_handle_t event) {
    // Safely copy the topic and payload; MQTT event buffers are not null-terminated.
    char topic[64]  = {};
    char payload[32] = {};
    int tlen = event->topic_len   < (int)sizeof(topic)   - 1 ? event->topic_len   : (int)sizeof(topic)   - 1;
    int plen = event->data_len    < (int)sizeof(payload)  - 1 ? event->data_len    : (int)sizeof(payload)  - 1;
    memcpy(topic,   event->topic, tlen);
    memcpy(payload, event->data,  plen);

    ESP_LOGI(TAG, "← %s : \"%s\"", topic, payload);

    if (!s_cmd_cb) return;

    if (strcmp(topic, TOPIC_CMD_M1)  == 0) s_cmd_cb(0, payload);
    else if (strcmp(topic, TOPIC_CMD_M2)  == 0) s_cmd_cb(1, payload);
    else if (strcmp(topic, TOPIC_CMD_ALL) == 0) s_cmd_cb(2, payload);
}

// ─── Event handler ────────────────────────────────────────────────────────────

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected to broker");
        // Publish online status and replace the retained LWT value.
        esp_mqtt_client_publish(s_client, TOPIC_LWT, LWT_ONLINE, 0, 1, 1);
        // Subscribe to command topics.
        esp_mqtt_client_subscribe(s_client, TOPIC_CMD_M1,  1);
        esp_mqtt_client_subscribe(s_client, TOPIC_CMD_M2,  1);
        esp_mqtt_client_subscribe(s_client, TOPIC_CMD_ALL, 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected — the client will reconnect automatically");
        break;

    case MQTT_EVENT_DATA:
        handle_data(event);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t mqtt_init(mqtt_cmd_cb_t cb) {
    s_cmd_cb = cb;

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri      = MQTT_BROKER_URI,
        .credentials.client_id   = MQTT_CLIENT_ID,
        .session.keepalive        = MQTT_KEEPALIVE_S,
        // The broker publishes the LWT after an unexpected disconnect.
        .session.last_will = {
            .topic  = TOPIC_LWT,
            .msg    = LWT_OFFLINE,
            .qos    = 1,
            .retain = 1,
        },
    };

    // Configure authentication only when credentials are provided.
    if (strlen(MQTT_USERNAME) > 0) {
        cfg.credentials.username              = MQTT_USERNAME;
        cfg.credentials.authentication.password = MQTT_PASSWORD;
    }

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_FAIL;

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    return esp_mqtt_client_start(s_client);
}

void mqtt_publish_state(int motor_idx, const char *json) {
    if (!s_client || motor_idx < 0 || motor_idx > 1) return;
    esp_mqtt_client_publish(s_client, state_topics[motor_idx], json, 0, 1, 1); // retain
}

void mqtt_publish_alarm(int motor_idx, const char *json) {
    if (!s_client || motor_idx < 0 || motor_idx > 1) return;
    esp_mqtt_client_publish(s_client, alarm_topics[motor_idx], json, 0, 1, 0);
}

void mqtt_publish_diag(const char *json) {
    if (!s_client) return;
    esp_mqtt_client_publish(s_client, TOPIC_DIAG, json, 0, 0, 0);
}
