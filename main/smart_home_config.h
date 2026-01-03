#ifndef SMART_HOME_CONFIG_H
#define SMART_HOME_CONFIG_H

#include "driver/gpio.h"

// ===== WiFi Provisioning =====
// WiFi credentials will be provided via provisioning portal (SoftAP + HTTP) and stored in NVS.
// AP SSID (for provisioning) is configured inside wifi_manager (default: "ESP32-Setup").

// ===== MQTT Configuration =====
#define MQTT_BROKER_HOST    "5b495904868d4c9e9e07c33dde4f6274.s1.eu.hivemq.cloud"
#define MQTT_BROKER_PORT    8883
#define MQTT_USE_TLS        1                    // 1 = mqtts (TLS/SSL), 0 = mqtt (không mã hóa)
#define MQTT_USERNAME       "quyen"         // Username cho cluster
#define MQTT_PASSWORD       "Quyen1234"   // Password cho cluster
#define MQTT_CLIENT_ID      "esp32_room_01"
#define MQTT_QOS            1

// MQTT Topics
#define MQTT_TOPIC_DEVICE       "home/room1/device"    // LED states (JSON)
#define MQTT_TOPIC_STATUS       "home/room1/status"    // DHT11 sensor data (JSON)

// MQTT Commands
#define MQTT_CMD_ON         "ON"
#define MQTT_CMD_OFF        "OFF"
#define MQTT_CMD_TOGGLE     "TOGGLE"

// ===== GPIO Pin Configuration =====
// LED pins
#define LED1_GPIO           GPIO_NUM_25
#define LED2_GPIO           GPIO_NUM_26
#define LED3_GPIO           GPIO_NUM_27

// Button pins (cho cơ chế đèn 2 chiều)
#define BUTTON1_GPIO        GPIO_NUM_32
#define BUTTON2_GPIO        GPIO_NUM_33
#define BUTTON3_GPIO        GPIO_NUM_13

// DHT11 sensor pin
#define DHT11_GPIO          GPIO_NUM_4

// ===== Timing Configuration =====
#define BUTTON_DEBOUNCE_MS      50      // Thời gian debounce cho nút bấm
#define DHT11_READ_INTERVAL_MS  300000  // Đọc DHT11 mỗi 5 phút (300 giây)
#define MQTT_PUBLISH_INTERVAL_MS 5000   // Gửi trạng thái mỗi 5 giây

// ===== System Configuration =====
#define NUM_LEDS            3
#define NUM_BUTTONS         3

#endif // SMART_HOME_CONFIG_H
