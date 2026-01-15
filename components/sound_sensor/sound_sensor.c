#include "sound_sensor.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SoundSensor";

// ADC configuration
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_channel_t s_adc_channel = ADC_CHANNEL_0;
static int s_threshold = 1000;  // Default threshold
static int s_last_reading = 0;

// Task control
static TaskHandle_t s_monitor_task_handle = NULL;
static uint32_t s_sampling_interval_ms = 100;
static sound_trigger_cb_t s_trigger_cb = NULL;

// Triggered state (to avoid spam callback)
static bool s_is_triggered = false;
static uint32_t s_cooldown_ms = 2000;  // Cooldown period after trigger
static uint32_t s_last_trigger_time = 0;

/**
 * @brief Map GPIO to ADC channel (ESP32 specific)
 */
static esp_err_t gpio_to_adc_channel(gpio_num_t gpio, adc_channel_t *channel)
{
    switch (gpio) {
        case GPIO_NUM_36: *channel = ADC_CHANNEL_0; return ESP_OK;  // ADC1_CH0
        case GPIO_NUM_37: *channel = ADC_CHANNEL_1; return ESP_OK;  // ADC1_CH1
        case GPIO_NUM_38: *channel = ADC_CHANNEL_2; return ESP_OK;  // ADC1_CH2
        case GPIO_NUM_39: *channel = ADC_CHANNEL_3; return ESP_OK;  // ADC1_CH3
        case GPIO_NUM_32: *channel = ADC_CHANNEL_4; return ESP_OK;  // ADC1_CH4
        case GPIO_NUM_33: *channel = ADC_CHANNEL_5; return ESP_OK;  // ADC1_CH5
        case GPIO_NUM_34: *channel = ADC_CHANNEL_6; return ESP_OK;  // ADC1_CH6
        case GPIO_NUM_35: *channel = ADC_CHANNEL_7; return ESP_OK;  // ADC1_CH7
        default:
            ESP_LOGE(TAG, "GPIO %d is not a valid ADC pin", gpio);
            return ESP_ERR_INVALID_ARG;
    }
}

/**
 * @brief Sound monitoring task
 */
static void sound_monitor_task(void *arg)
{
    int adc_value = 0;
    uint32_t log_counter = 0;
    uint32_t log_interval_ticks = 5000 / s_sampling_interval_ms;  // Log every 5 seconds
    
    while (1) {
        // Read ADC value
        esp_err_t ret = adc_oneshot_read(s_adc_handle, s_adc_channel, &adc_value);
        if (ret == ESP_OK) {
            s_last_reading = adc_value;
            
            // Log ADC value every 5 seconds
            log_counter++;
            if (log_counter >= log_interval_ticks) {
                ESP_LOGI(TAG, "Sound level from GPIO34: %d (Threshold: %d)", adc_value, s_threshold);
                log_counter = 0;
            }
            
            // Check if sound level exceeds threshold
            if (adc_value > s_threshold) {
                // Check cooldown period to avoid spam
                uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                if (!s_is_triggered || (current_time - s_last_trigger_time) > s_cooldown_ms) {
                    s_is_triggered = true;
                    s_last_trigger_time = current_time;
                    
                    ESP_LOGW(TAG, "Sound threshold exceeded! Level: %d (Threshold: %d)", 
                            adc_value, s_threshold);
                    
                    // Trigger callback
                    if (s_trigger_cb) {
                        s_trigger_cb(adc_value);
                    }
                }
            } else {
                // Reset triggered state when sound drops below threshold
                if (s_is_triggered && adc_value < (s_threshold - 100)) {
                    s_is_triggered = false;
                    ESP_LOGI(TAG, "Sound level returned to normal: %d", adc_value);
                }
            }
        } else {
            ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        }
        
        vTaskDelay(pdMS_TO_TICKS(s_sampling_interval_ms));
    }
}

esp_err_t sound_sensor_init(gpio_num_t adc_gpio, int threshold, sound_trigger_cb_t trigger_cb)
{
    // Map GPIO to ADC channel
    esp_err_t ret = gpio_to_adc_channel(adc_gpio, &s_adc_channel);
    if (ret != ESP_OK) {
        return ret;
    }
    
    s_threshold = threshold;
    s_trigger_cb = trigger_cb;
    
    // Configure ADC oneshot
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    
    ret = adc_oneshot_new_unit(&init_config, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,  // 0-3.3V range
    };
    
    ret = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "Sound sensor initialized on GPIO %d (ADC1_CH%d), threshold=%d", 
            adc_gpio, s_adc_channel, threshold);
    return ESP_OK;
}

esp_err_t sound_sensor_start(uint32_t sampling_interval_ms)
{
    if (s_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "Monitor task already running");
        return ESP_OK;
    }
    
    if (s_adc_handle == NULL) {
        ESP_LOGE(TAG, "Sound sensor not initialized");
        return ESP_FAIL;
    }
    
    s_sampling_interval_ms = sampling_interval_ms;
    
    BaseType_t ret = xTaskCreate(sound_monitor_task, "sound_task", 
                                 3072, NULL, 5, &s_monitor_task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Sound monitoring started (sampling every %lu ms)", sampling_interval_ms);
    return ESP_OK;
}

esp_err_t sound_sensor_stop(void)
{
    if (s_monitor_task_handle == NULL) {
        ESP_LOGW(TAG, "Monitor task not running");
        return ESP_OK;
    }
    
    vTaskDelete(s_monitor_task_handle);
    s_monitor_task_handle = NULL;
    
    ESP_LOGI(TAG, "Sound monitoring stopped");
    return ESP_OK;
}

esp_err_t sound_sensor_set_threshold(int threshold)
{
    if (threshold < 0 || threshold > 4095) {
        ESP_LOGE(TAG, "Invalid threshold: %d (must be 0-4095)", threshold);
        return ESP_ERR_INVALID_ARG;
    }
    
    s_threshold = threshold;
    s_is_triggered = false;  // Reset trigger state
    ESP_LOGI(TAG, "Threshold updated to: %d", threshold);
    return ESP_OK;
}

int sound_sensor_get_level(void)
{
    return s_last_reading;
}
