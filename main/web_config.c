#include "web_config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "settings.h"
#include "config.h"

#ifndef WEB_CONFIG_PORT
#define WEB_CONFIG_PORT 80
#endif

static const char *TAG = "WEB_CONFIG";
static motor_t *s_motors[2];

static const char PAGE[] =
    "<!doctype html><html lang=cs><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Nastaveni motoru</title><style>"
    ":root{font-family:system-ui,sans-serif;color-scheme:light dark}"
    "body{max-width:900px;margin:2rem auto;padding:0 1rem}"
    "main{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));gap:1rem}"
    "fieldset{border:1px solid #8888;border-radius:10px;padding:1rem}"
    "label{display:grid;grid-template-columns:1fr 8rem;gap:.8rem;align-items:center;margin:.55rem 0}"
    "input{width:100%;box-sizing:border-box;padding:.4rem}button{padding:.7rem 1.2rem;margin-top:1rem}"
    "#msg{min-height:1.5rem}.ok{color:#198754}.err{color:#dc3545}small{opacity:.75}"
    "</style></head><body><h1>Nastaveni motoru</h1>"
    "<p><small>Hodnoty se ukladaji do NVS. U beziciho motoru se projevi az pri dalsim pohybu.</small></p>"
    "<form id=f><main id=motors></main><button type=submit>Ulozit konfiguraci</button></form><p id=msg></p>"
    "<script>"
    "const form=document.getElementById('f'),motors=document.getElementById('motors'),msg=document.getElementById('msg');"
    "const defs=[['pwm_duty','PWM duty','1..255'],['timeout_ms','Timeout [ms]','100..3600000'],"
    "['overcurrent_min_run_ms','Overcurrent: prodleva [ms]','0..timeout'],"
    "['overcurrent_min_raw','Overcurrent: minimum ADC','0..4095'],"
    "['overcurrent_percent','Overcurrent: procent prumeru','101..1000'],"
    "['overcurrent_count','Overcurrent: pocet mereni','1..255'],"
    "['endstop_min_run_ms','Endstop: prodleva [ms]','0..timeout'],"
    "['endstop_min_raw','Endstop: minimum ADC','0..4095'],"
    "['endstop_drop_percent','Endstop: procent prumeru','1..99'],"
    "['endstop_count','Endstop: pocet mereni','1..255']];"
    "async function load(){try{let c=await(await fetch('/api/config')).json();"
    "for(let n=1;n<=2;n++){let s=document.createElement('fieldset');s.innerHTML='<legend>Motor '+n+'</legend>';"
    "for(let d of defs)s.innerHTML+=`<label><span>${d[1]}<br><small>${d[2]}</small></span>"
    "<input required type=number name=m${n}_${d[0]} value=${c['motor'+n][d[0]]}></label>`;"
    "motors.appendChild(s)}}catch(e){show('Konfiguraci se nepodarilo nacist: '+e,true)}}"
    "function show(t,e){msg.textContent=t;msg.className=e?'err':'ok'}"
    "form.onsubmit=async e=>{e.preventDefault();show('Ukladam...',false);try{let r=await fetch('/api/config',"
    "{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});"
    "let j=await r.json();if(!r.ok)throw Error(j.error||r.statusText);show('Ulozeno. Nove hodnoty se pouziji pri dalsim pohybu.',false)}"
    "catch(e){show('Chyba: '+e.message,true)}};load();</script></body></html>";

static esp_err_t send_json_error(httpd_req_t *req, const char *status,
                                 const char *error) {
    char response[192];
    snprintf(response, sizeof(response), "{\"ok\":false,\"error\":\"%s\"}", error);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t page_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_get(httpd_req_t *req) {
    motor_config_t c[2];
    motor_get_config(s_motors[0], &c[0]);
    motor_get_config(s_motors[1], &c[1]);

    char json[1024];
#define VALUES(i) \
    (unsigned long)c[i].pwm_duty, (unsigned long)c[i].timeout_ms, \
    (unsigned long)c[i].overcurrent_min_run_ms, (unsigned long)c[i].overcurrent_min_raw, \
    (unsigned long)c[i].overcurrent_percent, (unsigned long)c[i].overcurrent_count, \
    (unsigned long)c[i].endstop_min_run_ms, (unsigned long)c[i].endstop_min_raw, \
    (unsigned long)c[i].endstop_drop_percent, (unsigned long)c[i].endstop_count
    const char *motor_format =
        "{\"pwm_duty\":%lu,\"timeout_ms\":%lu,"
        "\"overcurrent_min_run_ms\":%lu,\"overcurrent_min_raw\":%lu,"
        "\"overcurrent_percent\":%lu,\"overcurrent_count\":%lu,"
        "\"endstop_min_run_ms\":%lu,\"endstop_min_raw\":%lu,"
        "\"endstop_drop_percent\":%lu,\"endstop_count\":%lu}";
    char m1[480], m2[480];
    snprintf(m1, sizeof(m1), motor_format, VALUES(0));
    snprintf(m2, sizeof(m2), motor_format, VALUES(1));
#undef VALUES
    snprintf(json, sizeof(json), "{\"motor1\":%s,\"motor2\":%s}", m1, m2);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static bool parse_u32(const char *body, const char *key, uint32_t *value) {
    char text[16];
    if (httpd_query_key_value(body, key, text, sizeof(text)) != ESP_OK || !text[0]) {
        return false;
    }
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || *end != '\0' || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_motor(const char *body, int idx, motor_config_t *c,
                        char *bad_field, size_t bad_field_size) {
    const char *names[] = {
        "pwm_duty", "timeout_ms", "overcurrent_min_run_ms", "overcurrent_min_raw",
        "overcurrent_percent", "overcurrent_count", "endstop_min_run_ms",
        "endstop_min_raw", "endstop_drop_percent", "endstop_count"
    };
    uint32_t *values[] = {
        &c->pwm_duty, &c->timeout_ms, &c->overcurrent_min_run_ms, &c->overcurrent_min_raw,
        &c->overcurrent_percent, &c->overcurrent_count, &c->endstop_min_run_ms,
        &c->endstop_min_raw, &c->endstop_drop_percent, &c->endstop_count
    };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char key[48];
        snprintf(key, sizeof(key), "m%d_%s", idx + 1, names[i]);
        if (!parse_u32(body, key, values[i])) {
            snprintf(bad_field, bad_field_size, "%s is missing or not an integer", key);
            return false;
        }
    }

    const char *field, *reason;
    if (!motor_config_validate(c, &field, &reason)) {
        snprintf(bad_field, bad_field_size, "m%d_%s %s", idx + 1, field, reason);
        return false;
    }
    return true;
}

static esp_err_t config_post(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len >= 1024) {
        return send_json_error(req, "400 Bad Request", "request body is empty or too large");
    }

    char body[1024];
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        received += n;
    }
    body[received] = '\0';

    motor_config_t c[2] = {};
    char error[128];
    if (!parse_motor(body, 0, &c[0], error, sizeof(error)) ||
        !parse_motor(body, 1, &c[1], error, sizeof(error))) {
        return send_json_error(req, "400 Bad Request", error);
    }

    esp_err_t err = settings_save_all(&c[0], &c[1]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not save settings: %s", esp_err_to_name(err));
        return send_json_error(req, "500 Internal Server Error", "could not write settings to NVS");
    }

    motor_set_config(s_motors[0], &c[0]);
    motor_set_config(s_motors[1], &c[1]);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t web_config_start(motor_t *motor1, motor_t *motor2) {
    if (!motor1 || !motor2) return ESP_ERR_INVALID_ARG;
    s_motors[0] = motor1;
    s_motors[1] = motor2;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_CONFIG_PORT;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;

    const httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = page_get };
    const httpd_uri_t api_get = { .uri = "/api/config", .method = HTTP_GET, .handler = config_get };
    const httpd_uri_t api_post = { .uri = "/api/config", .method = HTTP_POST, .handler = config_post };

    if ((err = httpd_register_uri_handler(server, &page)) != ESP_OK ||
        (err = httpd_register_uri_handler(server, &api_get)) != ESP_OK ||
        (err = httpd_register_uri_handler(server, &api_post)) != ESP_OK) {
        httpd_stop(server);
        return err;
    }

    ESP_LOGI(TAG, "Configuration UI started on port %d", WEB_CONFIG_PORT);
    return ESP_OK;
}
