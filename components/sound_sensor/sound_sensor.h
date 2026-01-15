#ifndef SOUND_SENSOR_H
#define SOUND_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sound trigger callback
 * Called when sound level exceeds threshold
 */
typedef void (*sound_trigger_cb_t)(int sound_level);

/**
 * @brief Initialize sound sensor with ADC
 * 
 * @param adc_gpio ADC GPIO pin (e.g., GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36)
 * @param threshold Sound level threshold (0-4095, 12-bit ADC)
 * @param trigger_cb Callback when sound exceeds threshold
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sound_sensor_init(gpio_num_t adc_gpio, int threshold, sound_trigger_cb_t trigger_cb);

/**
 * @brief Start sound monitoring task
 * 
 * @param sampling_interval_ms Sampling interval in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sound_sensor_start(uint32_t sampling_interval_ms);

/**
 * @brief Stop sound monitoring task
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sound_sensor_stop(void);

/**
 * @brief Update sound threshold dynamically
 * 
 * @param threshold New threshold value (0-4095)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sound_sensor_set_threshold(int threshold);

/**
 * @brief Get current sound level reading
 * 
 * @return int Current ADC value (0-4095)
 */
int sound_sensor_get_level(void);

#ifdef __cplusplus
}
#endif

#endif // SOUND_SENSOR_H
