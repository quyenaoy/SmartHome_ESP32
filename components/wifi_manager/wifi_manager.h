#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi connection status callback
 */
typedef void (*wifi_connected_cb_t)(void);
typedef void (*wifi_disconnected_cb_t)(void);

/**
 * @brief Initialize WiFi Manager
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @param connected_cb Callback when connected
 * @param disconnected_cb Callback when disconnected
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_init(const char *ssid, const char *password, 
                            wifi_connected_cb_t connected_cb,
                            wifi_disconnected_cb_t disconnected_cb);

/**
 * @brief Start WiFi connection
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Stop WiFi connection
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if connected
 */
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
