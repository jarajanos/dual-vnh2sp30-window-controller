idf_component_register(
    SRCS
        "main.c"
        "motor.c"
        "switch.c"
        "wifi.c"
        "mqtt.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_adc
        esp_driver_ledc
        esp_driver_gpio
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        mqtt
)
