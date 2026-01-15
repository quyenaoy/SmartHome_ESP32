#ifndef MQTT_APP_H
#define MQTT_APP_H

#include <stdbool.h>
#include "esp_err.h"
#include <mqtt_client.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT message callback
 */
typedef void (*mqtt_message_cb_t)(const char *topic, const char *data, int data_len);

/**
 * @brief MQTT connected callback (called on MQTT_EVENT_CONNECTED)
 */
typedef void (*mqtt_connected_cb_t)(void);
/**
 * @brief Initialize MQTT application
 * 
 * @param broker_host MQTT broker host (e.g., "5b49...hivemq.cloud")
 * @param broker_port MQTT broker port (e.g., 8883 for TLS, 1883 for TCP)
 * @param use_tls Use TLS/SSL encryption (1 = mqtts, 0 = mqtt)
 * @param username MQTT username (NULL if not required)
 * @param password MQTT password (NULL if not required)
 * @param client_id Client ID
 * @param message_cb Callback for received messages
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_app_init(const char *broker_host, int broker_port, bool use_tls,
                        const char *username, const char *password,
                        const char *client_id, mqtt_message_cb_t message_cb);

/**
 * @brief Register a callback called when MQTT connects
 */
void mqtt_app_set_connected_cb(mqtt_connected_cb_t cb);

/**
 * @brief Start MQTT application
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_app_start(void);

/**
 * @brief Stop MQTT application
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_app_stop(void);

/**
 * @brief Subscribe to a topic
 * 
 * @param topic Topic to subscribe
 * @param qos QoS level (0, 1, or 2)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_app_subscribe(const char *topic, int qos);

/**
 * @brief Publish a message
 * 
 * @param topic Topic to publish
 * @param data Data to publish
 * @param qos QoS level (0, 1, or 2)
 * @param retain Retain flag
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_app_publish(const char *topic, const char *data, int qos, int retain);

/**
 * @brief Subscribe to device command topic based on roomId
 * Topic: {roomId}/device
 * 
 * @param room_id Room identifier
 * @param qos QoS level (0, 1, or 2)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_app_subscribe_device_topic(const char *room_id, int qos);

/**
 * @brief Publish device data (LED states) to topic based on roomId
 * Topic: {roomId}/device
 * 
 * @param room_id Room identifier
 * @param data JSON data to publish (e.g., LED states)
 * @param qos QoS level (0, 1, or 2)
 * @param retain Retain flag
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_app_publish_device_topic(const char *room_id, const char *data, int qos, int retain);

/**
 * @brief Publish status to topic based on roomId
 * Topic: {roomId}/status
 * 
 * @param room_id Room identifier
 * @param data JSON data to publish
 * @param qos QoS level (0, 1, or 2)
 * @param retain Retain flag
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_app_publish_status_topic(const char *room_id, const char *data, int qos, int retain);

/**
 * @brief Check if MQTT is connected
 * 
 * @return true if connected
 */
bool mqtt_app_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_APP_H
