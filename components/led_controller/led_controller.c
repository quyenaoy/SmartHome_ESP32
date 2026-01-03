#include "led_controller.h"
#include "esp_log.h"

static const char *TAG = "LED_Controller";

static gpio_num_t s_led_pins[MAX_LEDS];
static bool s_led_states[MAX_LEDS] = {false};
static int s_num_leds = 0;
static led_state_change_cb_t s_state_change_cb = NULL;

esp_err_t led_controller_init(const gpio_num_t *gpio_pins, int num_leds,
                              led_state_change_cb_t state_change_cb)
{
    if (num_leds > MAX_LEDS || num_leds <= 0) {
        ESP_LOGE(TAG, "Invalid number of LEDs: %d (max: %d)", num_leds, MAX_LEDS);
        return ESP_ERR_INVALID_ARG;
    }

    s_num_leds = num_leds;
    s_state_change_cb = state_change_cb;

    // Configure GPIO pins
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    for (int i = 0; i < num_leds; i++) {
        s_led_pins[i] = gpio_pins[i];
        s_led_states[i] = false;
        
        io_conf.pin_bit_mask = (1ULL << gpio_pins[i]);
        gpio_config(&io_conf);
        gpio_set_level(gpio_pins[i], 0);  // Start with LED off
        
        ESP_LOGI(TAG, "LED %d initialized on GPIO %d", i, gpio_pins[i]);
    }

    ESP_LOGI(TAG, "LED controller initialized with %d LEDs", num_leds);
    return ESP_OK;
}

esp_err_t led_controller_turn_on(int led_id)
{
    return led_controller_set_state(led_id, true);
}

esp_err_t led_controller_turn_off(int led_id)
{
    return led_controller_set_state(led_id, false);
}

esp_err_t led_controller_toggle(int led_id)
{
    if (led_id < 0 || led_id >= s_num_leds) {
        ESP_LOGE(TAG, "Invalid LED ID: %d", led_id);
        return ESP_ERR_INVALID_ARG;
    }

    bool new_state = !s_led_states[led_id];
    return led_controller_set_state(led_id, new_state);
}

esp_err_t led_controller_set_state(int led_id, bool state)
{
    if (led_id < 0 || led_id >= s_num_leds) {
        ESP_LOGE(TAG, "Invalid LED ID: %d", led_id);
        return ESP_ERR_INVALID_ARG;
    }

    s_led_states[led_id] = state;
    gpio_set_level(s_led_pins[led_id], state ? 1 : 0);
    
    ESP_LOGI(TAG, "LED %d turned %s (GPIO %d)", 
            led_id, state ? "ON" : "OFF", s_led_pins[led_id]);

    // Call state change callback
    if (s_state_change_cb) {
        s_state_change_cb(led_id, state);
    }

    return ESP_OK;
}

bool led_controller_get_state(int led_id)
{
    if (led_id < 0 || led_id >= s_num_leds) {
        ESP_LOGE(TAG, "Invalid LED ID: %d", led_id);
        return false;
    }

    return s_led_states[led_id];
}

esp_err_t led_controller_all_off(void)
{
    ESP_LOGI(TAG, "Turning all LEDs off");
    for (int i = 0; i < s_num_leds; i++) {
        led_controller_turn_off(i);
    }
    return ESP_OK;
}

esp_err_t led_controller_all_on(void)
{
    ESP_LOGI(TAG, "Turning all LEDs on");
    for (int i = 0; i < s_num_leds; i++) {
        led_controller_turn_on(i);
    }
    return ESP_OK;
}
