#include "mqtt_app.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_crt_bundle.h"
#include <mqtt_client.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "MQTT_App";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static mqtt_message_cb_t s_message_cb = NULL;
static bool s_is_connected = false;
static mqtt_connected_cb_t s_connected_cb = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to broker");
        s_is_connected = true;
        if (s_connected_cb) {
            s_connected_cb();
        }
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected from broker");
        s_is_connected = false;
        break;
        
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT published, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT message received - Topic: %.*s", 
                event->topic_len, event->topic);
        if (s_message_cb) {
            // Create null-terminated strings
            char topic[128] = {0};
            char data[256] = {0};
            
            int topic_len = (event->topic_len < sizeof(topic) - 1) ? 
                           event->topic_len : sizeof(topic) - 1;
            int data_len = (event->data_len < sizeof(data) - 1) ? 
                          event->data_len : sizeof(data) - 1;
            
            memcpy(topic, event->topic, topic_len);
            memcpy(data, event->data, data_len);
            
            s_message_cb(topic, data, data_len);
        }
        break;
        
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error event");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "TCP transport error");
        }
        break;
        
    default:
        ESP_LOGD(TAG, "MQTT event id: %d", event->event_id);
        break;
    }
}

esp_err_t mqtt_app_init(const char *broker_host, int broker_port, bool use_tls,
                        const char *username, const char *password,
                        const char *client_id, mqtt_message_cb_t message_cb)
{
    s_message_cb = message_cb;
    
    // Build broker URI - use MQTT TLS (mqtts) for port 8883
    char broker_uri[256];
    snprintf(broker_uri, sizeof(broker_uri), "%s://%s:%d",
             use_tls ? "mqtts" : "mqtt", broker_host, broker_port);
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.client_id = client_id,
    };
    
    // Configure TLS settings if enabled
    if (use_tls) {
        mqtt_cfg.broker.verification.use_global_ca_store = false;
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        mqtt_cfg.broker.verification.skip_cert_common_name_check = false;
        ESP_LOGI(TAG, "TLS enabled with certificate bundle");
    }
    
    // Add authentication if provided
    if (username != NULL && password != NULL) {
        mqtt_cfg.credentials.username = username;
        mqtt_cfg.credentials.authentication.password = password;
        ESP_LOGI(TAG, "MQTT authentication enabled - Username: %s", username);
    }
    
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }
    
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, 
                                   mqtt_event_handler, NULL);
    
    ESP_LOGI(TAG, "MQTT app initialized - Broker: %s, Client ID: %s", 
            broker_uri, client_id);
    
    return ESP_OK;
}

void mqtt_app_set_connected_cb(mqtt_connected_cb_t cb)
{
    s_connected_cb = cb;
}

esp_err_t mqtt_app_start(void)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_FAIL;
    }
    
    esp_err_t ret = esp_mqtt_client_start(s_mqtt_client);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MQTT app started");
    } else {
        ESP_LOGE(TAG, "Failed to start MQTT app");
    }
    
    return ret;
}

esp_err_t mqtt_app_stop(void)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_FAIL;
    }
    
    esp_err_t ret = esp_mqtt_client_stop(s_mqtt_client);
    s_is_connected = false;
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MQTT app stopped");
    }
    
    return ret;
}

esp_err_t mqtt_app_subscribe(const char *topic, int qos)
{
    if (s_mqtt_client == NULL || !s_is_connected) {
        ESP_LOGE(TAG, "MQTT app not ready");
        return ESP_FAIL;
    }
    
    int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "Subscribed to topic: %s, msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
    return ESP_FAIL;
}

esp_err_t mqtt_app_publish(const char *topic, const char *data, int qos, int retain)
{
    if (s_mqtt_client == NULL || !s_is_connected) {
        ESP_LOGE(TAG, "MQTT app not ready");
        return ESP_FAIL;
    }
    
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, data, 
                                        strlen(data), qos, retain);
    if (msg_id >= 0) {
        ESP_LOGD(TAG, "Published to topic: %s, msg_id=%d", topic, msg_id);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to publish to topic: %s", topic);
    return ESP_FAIL;
}

bool mqtt_app_is_connected(void)
{
    return s_is_connected;
}
