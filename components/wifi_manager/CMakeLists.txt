idf_component_register(
    SRCS "wifi_manager.c" "wifi_portal.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event esp_http_server nvs_flash
)
