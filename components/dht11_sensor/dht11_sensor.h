#ifndef DHT11_SENSOR_H
#define DHT11_SENSOR_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DHT11 sensor reading callback
 */
typedef void (*dht11_reading_cb_t)(float temperature, float humidity);

/**
 * @brief Initialize DHT11 sensor
 * 
 * @param gpio_pin GPIO pin connected to DHT11 data pin
 * @param reading_cb Callback for sensor readings
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dht11_sensor_init(gpio_num_t gpio_pin, dht11_reading_cb_t reading_cb);

/**
 * @brief Read temperature and humidity from DHT11
 * 
 * @param temperature Pointer to store temperature (°C)
 * @param humidity Pointer to store humidity (%)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dht11_sensor_read(float *temperature, float *humidity);

/**
 * @brief Start periodic reading task
 * 
 * @param interval_ms Reading interval in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dht11_sensor_start_periodic(uint32_t interval_ms);

/**
 * @brief Stop periodic reading task
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dht11_sensor_stop_periodic(void);

#ifdef __cplusplus
}
#endif

#endif // DHT11_SENSOR_H
