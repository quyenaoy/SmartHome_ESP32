#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LEDS 3

/**
 * @brief LED state change callback
 */
typedef void (*led_state_change_cb_t)(int led_id, bool state);

/**
 * @brief Initialize LED controller
 * 
 * @param gpio_pins Array of GPIO pins for LEDs
 * @param num_leds Number of LEDs (max 3)
 * @param state_change_cb Callback when LED state changes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_controller_init(const gpio_num_t *gpio_pins, int num_leds,
                              led_state_change_cb_t state_change_cb);

/**
 * @brief Turn LED on
 * 
 * @param led_id LED index (0, 1, or 2)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_controller_turn_on(int led_id);

/**
 * @brief Turn LED off
 * 
 * @param led_id LED index (0, 1, or 2)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_controller_turn_off(int led_id);

/**
 * @brief Toggle LED state
 * 
 * @param led_id LED index (0, 1, or 2)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_controller_toggle(int led_id);

/**
 * @brief Set LED state
 * 
 * @param led_id LED index (0, 1, or 2)
 * @param state true for ON, false for OFF
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_controller_set_state(int led_id, bool state);

/**
 * @brief Get LED state
 * 
 * @param led_id LED index (0, 1, or 2)
 * @return true if LED is ON, false if OFF
 */
bool led_controller_get_state(int led_id);

/**
 * @brief Turn all LEDs off
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_controller_all_off(void);

/**
 * @brief Turn all LEDs on
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_controller_all_on(void);

#ifdef __cplusplus
}
#endif

#endif // LED_CONTROLLER_H
