#pragma once

#include "esp_err.h"

// Blocking Wi-Fi initialization; waits for an IP address or retry exhaustion.
esp_err_t wifi_init_sta(void);
