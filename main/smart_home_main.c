/*
 * Smart Home Light Switch Application
 * ESP32 with 3 LEDs, 3 Buttons (2-way switch), DHT11 sensor
 * WiFi + MQTT communication
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Components
#include "wifi_manager.h"
#include "mqtt_app.h"
#include "led_controller.h"
#include "dht11_sensor.h"
#include "button_handler.h"
#include "sound_sensor.h"
#include "smart_home_config.h"

static const char *TAG = "SmartHome";

// Flag to prevent echo loop when processing MQTT messages
static bool s_processing_mqtt_message = false;

// Flag to prevent multiple LED changes from single sound trigger
static bool s_processing_sound_trigger = false;

// Forward declarations
static void subscribe_to_topics(void);
static void on_mqtt_connected(void);

// ===== Callback Functions =====

/**
 * @brief WiFi connected callback
 */
static void on_wifi_connected(void)
{
    ESP_LOGI(TAG, "=== WiFi Connected Callback Triggered ===");
    
    ESP_LOGI(TAG, "Starting MQTT client...");
    esp_err_t ret = mqtt_app_start();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
    }
    
    // Wait a bit for MQTT connection
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Re-subscribe to topics when reconnected
    ESP_LOGI(TAG, "Subscribing to MQTT topics...");
    subscribe_to_topics();
}

/**
 * @brief WiFi disconnected callback
 */
static void on_wifi_disconnected(void)
{
    ESP_LOGW(TAG, "=== WiFi Disconnected ===");
    mqtt_app_stop();
}

/**
 * @brief LED state change callback
 */
static void on_led_state_changed(int led_id, bool state)
{
    ESP_LOGI(TAG, "LED %d state changed to: %s", led_id, state ? "ON" : "OFF");

    // Only publish if NOT processing MQTT message or sound trigger (avoid echo loop)
    if (!s_processing_mqtt_message && !s_processing_sound_trigger && mqtt_app_is_connected()) {
        const char *room_id = wifi_manager_get_room_id();
        if (room_id && strlen(room_id) > 0) {
            char device_msg[128];
            snprintf(device_msg, sizeof(device_msg),
                    "{\"device1\":%d,\"device2\":%d,\"device3\":%d}",
                    led_controller_get_state(0),
                    led_controller_get_state(1),
                    led_controller_get_state(2));
            mqtt_app_publish_device_topic(room_id, device_msg, MQTT_QOS, 0);
            ESP_LOGI(TAG, "LED state published to %s/device: %s", room_id, device_msg);
        }
    } else if (s_processing_mqtt_message) {
        ESP_LOGD(TAG, "Skipping publish (processing MQTT message - rebound prevention)");
    }
}

/**
 * @brief Button press callback (2-way switch mechanism)
 */
static void on_button_pressed(int button_id)
{
    ESP_LOGI(TAG, "Button %d pressed - Toggling LED %d", button_id, button_id);
    
    // Toggle corresponding LED
    led_controller_toggle(button_id);
}

/**
 * @brief DHT11 reading callback
 */
static void on_dht11_reading(float temperature, float humidity)
{
    ESP_LOGI(TAG, "DHT11 - Temperature: %.1f°C, Humidity: %.1f%%", 
            temperature, humidity);
    
    // Publish sensor data to MQTT as JSON to {roomId}/status
    if (mqtt_app_is_connected()) {
        const char *room_id = wifi_manager_get_room_id();
        if (room_id && strlen(room_id) > 0) {
            char status_msg[128];
            snprintf(status_msg, sizeof(status_msg),
                    "{\"temperature\":%.1f,\"humidity\":%.1f}",
                    temperature, humidity);
            mqtt_app_publish_status_topic(room_id, status_msg, MQTT_QOS, 0);
            ESP_LOGI(TAG, "Sensor data published to %s/status: %s", room_id, status_msg);
        }
    }
}

/**
 * @brief Sound sensor trigger callback
 * Called when sound level exceeds threshold - directly turns on all LEDs
 */
static void on_sound_trigger(int sound_level)
{
    ESP_LOGW(TAG, "Sound trigger activated! Level: %d - Turning on all LEDs", sound_level);
    
    // Set rebound flag to prevent LED state change callbacks from publishing back
    s_processing_sound_trigger = true;
    
    // Directly turn on all 3 LEDs
    led_controller_turn_on(0);  // LED 1
    led_controller_turn_on(1);  // LED 2
    led_controller_turn_on(2);  // LED 3
    
    // Clear rebound flag after LED changes
    s_processing_sound_trigger = false;
    
    // Publish LED state to MQTT
    if (mqtt_app_is_connected()) {
        const char *room_id = wifi_manager_get_room_id();
        if (room_id && strlen(room_id) > 0) {
            char device_msg[128];
            snprintf(device_msg, sizeof(device_msg), "{\"device1\":1,\"device2\":1,\"device3\":1}");
            mqtt_app_publish_device_topic(room_id, device_msg, MQTT_QOS, 0);
            ESP_LOGI(TAG, "LED state published after sound trigger: %s", device_msg);
        }
    }
}

/**
 * @brief Subscribe to all MQTT topics based on roomId
 */
static void subscribe_to_topics(void)
{
    if (mqtt_app_is_connected()) {
        // Lấy roomId để tạo topic: {roomId}/device
        const char *room_id = wifi_manager_get_room_id();
        if (room_id && strlen(room_id) > 0) {
            mqtt_app_subscribe_device_topic(room_id, MQTT_QOS);
            ESP_LOGI(TAG, "Subscribed to device topic with roomId=%s", room_id);
        } else {
            ESP_LOGW(TAG, "No valid room_id, cannot subscribe to device topic");
        }
    } else {
        ESP_LOGW(TAG, "MQTT not connected, skipping subscription");
    }
}

static void on_mqtt_connected(void)
{
    ESP_LOGI(TAG, "MQTT connected callback - subscribing");
    subscribe_to_topics();
}

/**
 * @brief MQTT message received callback
 * Handles JSON messages on device topic: {"device1":0/1, "device2":0/1, "device3":0/1}
 */
static void on_mqtt_message(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "MQTT Message - Topic: %s, Data: %.*s (len=%d)", topic, data_len, data, data_len);
    
    // Build expected device topic: {roomId}/device
    const char *room_id = wifi_manager_get_room_id();
    if (room_id && strlen(room_id) > 0) {
        char expected_device_topic[128] = {0};
        snprintf(expected_device_topic, sizeof(expected_device_topic), "%s/device", room_id);
        
        // Handle device control commands on {roomId}/device (JSON format)
        if (strcmp(topic, expected_device_topic) == 0) {
            // Set flag to prevent echo loop (rebound prevention)
            s_processing_mqtt_message = true;
            ESP_LOGI(TAG, "Processing command from %s (rebound flag set)", expected_device_topic);
            
            // Parse JSON: {"device1":0/1, "device2":0/1, "device3":0/1}
            char json_buf[256] = {0};
            int copy_len = (data_len < (int)sizeof(json_buf) - 1) ? data_len : (int)sizeof(json_buf) - 1;
            memcpy(json_buf, data, copy_len);
            json_buf[copy_len] = '\0';
            
            // Simple JSON parsing for device1, device2, device3
            char *ptr = json_buf;
            for (int led_id = 0; led_id < 3; led_id++) {
                char led_key[16];
                snprintf(led_key, sizeof(led_key), "\"device%d\":", led_id + 1);
                
                char *led_pos = strstr(ptr, led_key);
                if (led_pos) {
                    led_pos += strlen(led_key);
                    // Skip whitespace
                    while (*led_pos == ' ' || *led_pos == '\t') led_pos++;
                    
                    int target_state = (*led_pos == '1') ? 1 : 0;
                    int current_state = led_controller_get_state(led_id);
                    
                    // Only change if different to avoid unnecessary operations
                    if (target_state != current_state) {
                        if (target_state) {
                            ESP_LOGI(TAG, "MQTT: Turning ON LED %d", led_id + 1);
                            led_controller_turn_on(led_id);
                        } else {
                            ESP_LOGI(TAG, "MQTT: Turning OFF LED %d", led_id + 1);
                            led_controller_turn_off(led_id);
                        }
                    }
                }
            }
            
            // Clear flag after processing
            s_processing_mqtt_message = false;
            ESP_LOGI(TAG, "Command processing complete (rebound flag cleared)");
        } else {
            ESP_LOGW(TAG, "Message received on unknown topic: %s (expected: %s)", topic, expected_device_topic);
        }
    } else {
        ESP_LOGE(TAG, "No valid room_id, cannot process MQTT message");
    }
}

/**
 * @brief Device reporting task - publishes LED states every 2 minutes
 */
static void device_report_task(void *arg)
{
    char device_msg[128];
    
    while (1) {
        // Wait 2 minutes (120000 ms)
        vTaskDelay(pdMS_TO_TICKS(120000));
        
        if (mqtt_app_is_connected()) {
            const char *room_id = wifi_manager_get_room_id();
            if (room_id && strlen(room_id) > 0) {
                // Build device message
                snprintf(device_msg, sizeof(device_msg),
                        "{\"device1\":%d,\"device2\":%d,\"device3\":%d}",
                        led_controller_get_state(0),
                        led_controller_get_state(1),
                        led_controller_get_state(2));
                
                mqtt_app_publish_device_topic(room_id, device_msg, MQTT_QOS, 0);
                ESP_LOGI(TAG, "Periodic LED state published to %s/device: %s", room_id, device_msg);
            }
        }
    }
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "        Smart Home Light Switch - ESP32");
    ESP_LOGI(TAG, "==========================================================");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // ===== Initialize Components =====
    
    // 1. LED Controller
    ESP_LOGI(TAG, "Initializing LED Controller...");
    gpio_num_t led_pins[NUM_LEDS] = {LED1_GPIO, LED2_GPIO, LED3_GPIO};
    ESP_ERROR_CHECK(led_controller_init(led_pins, NUM_LEDS, on_led_state_changed));
    
    // 2. Button Handler
    ESP_LOGI(TAG, "Initializing Button Handler...");
    gpio_num_t button_pins[NUM_BUTTONS] = {BUTTON1_GPIO, BUTTON2_GPIO, BUTTON3_GPIO};
    ESP_ERROR_CHECK(button_handler_init(button_pins, NUM_BUTTONS, 
                                       on_button_pressed, BUTTON_DEBOUNCE_MS));
    
    // 3. DHT11 Sensor
    ESP_LOGI(TAG, "Initializing DHT11 Sensor...");
    ESP_ERROR_CHECK(dht11_sensor_init(DHT11_GPIO, on_dht11_reading));
    ESP_ERROR_CHECK(dht11_sensor_start_periodic(DHT11_READ_INTERVAL_MS));
    
    // 4. Sound Sensor (Analog ADC input)
    ESP_LOGI(TAG, "Initializing Sound Sensor...");
    ESP_ERROR_CHECK(sound_sensor_init(SOUND_SENSOR_GPIO, SOUND_THRESHOLD, on_sound_trigger));
    ESP_ERROR_CHECK(sound_sensor_start(SOUND_SAMPLING_INTERVAL_MS));
    
    // 5. MQTT App (must init BEFORE WiFi to avoid callback race condition)
    ESP_LOGI(TAG, "Initializing MQTT App...");
    ESP_ERROR_CHECK(mqtt_app_init(MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_USE_TLS,
                                  MQTT_USERNAME, MQTT_PASSWORD,
                                  MQTT_CLIENT_ID, on_mqtt_message));
    // Register MQTT connected callback to re-subscribe on reconnect
    mqtt_app_set_connected_cb(on_mqtt_connected);
    
    // 6. WiFi Manager (init and start - may trigger on_wifi_connected callback immediately)
    ESP_LOGI(TAG, "Initializing WiFi Manager (provisioning capable)...");
    ESP_ERROR_CHECK(wifi_manager_init(on_wifi_connected, on_wifi_disconnected));
    // wifi_manager_start will either connect with stored creds or start AP portal at 192.168.4.1
    ESP_ERROR_CHECK(wifi_manager_start());
    
    // Note: MQTT will be started automatically when WiFi connects (via on_wifi_connected callback)
    ESP_LOGI(TAG, "MQTT will start automatically when WiFi connection is established");
    
    // Start device reporting task (every 2 minutes)
    xTaskCreate(device_report_task, "device_task", 3072, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "        Smart Home System Started Successfully!");
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "WiFi: provisioning portal if no stored credentials (SSID: ESP32-Setup)");
    ESP_LOGI(TAG, "MQTT Broker: %s:%d (TLS: %s)", MQTT_BROKER_HOST, MQTT_BROKER_PORT, 
             MQTT_USE_TLS ? "Yes" : "No");
    ESP_LOGI(TAG, "LED GPIOs: %d, %d, %d", LED1_GPIO, LED2_GPIO, LED3_GPIO);
    ESP_LOGI(TAG, "Button GPIOs: %d, %d, %d", BUTTON1_GPIO, BUTTON2_GPIO, BUTTON3_GPIO);
    ESP_LOGI(TAG, "DHT11 GPIO: %d", DHT11_GPIO);
    ESP_LOGI(TAG, "Sound Sensor GPIO: %d (ADC, Threshold: %d)", SOUND_SENSOR_GPIO, SOUND_THRESHOLD);
    ESP_LOGI(TAG, "==========================================================");
    
    // Demo: Blink all LEDs once
    ESP_LOGI(TAG, "Running LED test...");
    led_controller_all_on();
    vTaskDelay(pdMS_TO_TICKS(500));
    led_controller_all_off();
    
    ESP_LOGI(TAG, "System ready. Press buttons to control LEDs!");
}
