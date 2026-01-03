#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BUTTONS 3

/**
 * @brief Button press callback
 */
typedef void (*button_press_cb_t)(int button_id);

/**
 * @brief Initialize button handler
 * 
 * @param gpio_pins Array of GPIO pins for buttons
 * @param num_buttons Number of buttons (max 3)
 * @param press_cb Callback when button is pressed
 * @param debounce_ms Debounce time in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t button_handler_init(const gpio_num_t *gpio_pins, int num_buttons,
                              button_press_cb_t press_cb, uint32_t debounce_ms);

/**
 * @brief Get button press count
 * 
 * @param button_id Button index (0, 1, or 2)
 * @return uint32_t Number of times button has been pressed
 */
uint32_t button_handler_get_press_count(int button_id);

/**
 * @brief Reset button press count
 * 
 * @param button_id Button index (0, 1, or 2)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t button_handler_reset_count(int button_id);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_HANDLER_H
