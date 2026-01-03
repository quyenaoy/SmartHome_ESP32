#include "button_handler.h"
#include "esp_log.h"
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "Button_Handler";

typedef struct {
    gpio_num_t gpio_pin;
    uint32_t press_count;
    int64_t last_press_time;
} button_state_t;

static button_state_t s_buttons[MAX_BUTTONS];
static int s_num_buttons = 0;
static button_press_cb_t s_press_cb = NULL;
static uint32_t s_debounce_ms = 50;
static QueueHandle_t s_button_evt_queue = NULL;

static void IRAM_ATTR button_isr_handler(void *arg)
{
    int button_id = (int)arg;
    xQueueSendFromISR(s_button_evt_queue, &button_id, NULL);
}

static void button_task(void *arg)
{
    int button_id;
    
    while (1) {
        if (xQueueReceive(s_button_evt_queue, &button_id, portMAX_DELAY)) {
            // Check if button_id is valid
            if (button_id < 0 || button_id >= s_num_buttons) {
                continue;
            }

            // Get current time
            int64_t current_time = esp_timer_get_time() / 1000;  // Convert to ms
            
            // Debounce check
            if ((current_time - s_buttons[button_id].last_press_time) < s_debounce_ms) {
                continue;  // Too soon, ignore
            }

            // Update last press time
            s_buttons[button_id].last_press_time = current_time;

            // Verify button is still pressed (filter out noise)
            vTaskDelay(pdMS_TO_TICKS(20));  // Small delay
            if (gpio_get_level(s_buttons[button_id].gpio_pin) == 0) {
                // Button confirmed pressed (active low)
                s_buttons[button_id].press_count++;
                
                ESP_LOGI(TAG, "Button %d pressed (count: %lu, GPIO: %d)", 
                        button_id, 
                        s_buttons[button_id].press_count,
                        s_buttons[button_id].gpio_pin);

                // Call callback
                if (s_press_cb) {
                    s_press_cb(button_id);
                }
            }
        }
    }
}

esp_err_t button_handler_init(const gpio_num_t *gpio_pins, int num_buttons,
                              button_press_cb_t press_cb, uint32_t debounce_ms)
{
    if (num_buttons > MAX_BUTTONS || num_buttons <= 0) {
        ESP_LOGE(TAG, "Invalid number of buttons: %d (max: %d)", num_buttons, MAX_BUTTONS);
        return ESP_ERR_INVALID_ARG;
    }

    s_num_buttons = num_buttons;
    s_press_cb = press_cb;
    s_debounce_ms = debounce_ms;

    // Create event queue
    s_button_evt_queue = xQueueCreate(10, sizeof(int));
    if (s_button_evt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_FAIL;
    }

    // Configure GPIO pins
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Use internal pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // Trigger on falling edge (button press)
    };

    for (int i = 0; i < num_buttons; i++) {
        s_buttons[i].gpio_pin = gpio_pins[i];
        s_buttons[i].press_count = 0;
        s_buttons[i].last_press_time = 0;

        io_conf.pin_bit_mask = (1ULL << gpio_pins[i]);
        gpio_config(&io_conf);

        // Install ISR service and add handler
        gpio_install_isr_service(0);
        gpio_isr_handler_add(gpio_pins[i], button_isr_handler, (void *)i);

        ESP_LOGI(TAG, "Button %d initialized on GPIO %d", i, gpio_pins[i]);
    }

    // Create button handling task
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);

    ESP_LOGI(TAG, "Button handler initialized with %d buttons (debounce: %lu ms)", 
            num_buttons, debounce_ms);
    
    return ESP_OK;
}

uint32_t button_handler_get_press_count(int button_id)
{
    if (button_id < 0 || button_id >= s_num_buttons) {
        ESP_LOGE(TAG, "Invalid button ID: %d", button_id);
        return 0;
    }

    return s_buttons[button_id].press_count;
}

esp_err_t button_handler_reset_count(int button_id)
{
    if (button_id < 0 || button_id >= s_num_buttons) {
        ESP_LOGE(TAG, "Invalid button ID: %d", button_id);
        return ESP_ERR_INVALID_ARG;
    }

    s_buttons[button_id].press_count = 0;
    ESP_LOGI(TAG, "Button %d press count reset", button_id);
    
    return ESP_OK;
}
