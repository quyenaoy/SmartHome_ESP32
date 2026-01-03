#include "dht11_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

static const char *TAG = "DHT11_Sensor";

static gpio_num_t s_gpio_pin;
static dht11_reading_cb_t s_reading_cb = NULL;
static TaskHandle_t s_periodic_task_handle = NULL;
static uint32_t s_interval_ms = 2000;

// DHT11 timing constants (microseconds)
#define DHT11_START_SIGNAL_LOW  18000
#define DHT11_START_SIGNAL_HIGH 30
#define DHT11_WAIT_RESPONSE     40
#define DHT11_TIMEOUT           85

/**
 * @brief Wait for GPIO pin to reach expected level
 */
static esp_err_t wait_for_level(gpio_num_t pin, int expected_level, int timeout_us)
{
    int elapsed = 0;
    while (gpio_get_level(pin) != expected_level) {
        if (elapsed > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
        ets_delay_us(1);
        elapsed++;
    }
    return ESP_OK;
}

/**
 * @brief Read one bit from DHT11
 */
static int read_bit(gpio_num_t pin)
{
    // Wait for low signal
    if (wait_for_level(pin, 0, DHT11_TIMEOUT) != ESP_OK) {
        return -1;
    }
    
    // Wait for high signal
    if (wait_for_level(pin, 1, DHT11_TIMEOUT) != ESP_OK) {
        return -1;
    }
    
    // Measure high signal duration
    ets_delay_us(30);
    
    if (gpio_get_level(pin) == 1) {
        // Still high = bit 1
        wait_for_level(pin, 0, DHT11_TIMEOUT);
        return 1;
    } else {
        // Already low = bit 0
        return 0;
    }
}

esp_err_t dht11_sensor_init(gpio_num_t gpio_pin, dht11_reading_cb_t reading_cb)
{
    s_gpio_pin = gpio_pin;
    s_reading_cb = reading_cb;

    // Configure GPIO as open-drain with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(gpio_pin, 1);  // Idle state

    ESP_LOGI(TAG, "DHT11 sensor initialized on GPIO %d", gpio_pin);
    return ESP_OK;
}

esp_err_t dht11_sensor_read(float *temperature, float *humidity)
{
    uint8_t data[5] = {0};

    // Send start signal
    gpio_set_level(s_gpio_pin, 0);
    ets_delay_us(DHT11_START_SIGNAL_LOW);
    gpio_set_level(s_gpio_pin, 1);
    ets_delay_us(DHT11_START_SIGNAL_HIGH);

    // Wait for DHT11 response
    if (wait_for_level(s_gpio_pin, 0, DHT11_TIMEOUT) != ESP_OK) {
        ESP_LOGE(TAG, "No response from DHT11");
        return ESP_ERR_TIMEOUT;
    }

    if (wait_for_level(s_gpio_pin, 1, DHT11_TIMEOUT) != ESP_OK) {
        ESP_LOGE(TAG, "DHT11 response timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (wait_for_level(s_gpio_pin, 0, DHT11_TIMEOUT) != ESP_OK) {
        ESP_LOGE(TAG, "DHT11 ready timeout");
        return ESP_ERR_TIMEOUT;
    }

    // Read 40 bits of data
    for (int i = 0; i < 40; i++) {
        int bit = read_bit(s_gpio_pin);
        if (bit < 0) {
            ESP_LOGE(TAG, "Failed to read bit %d", i);
            return ESP_FAIL;
        }
        data[i / 8] |= (bit << (7 - (i % 8)));
    }

    // Verify checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        ESP_LOGE(TAG, "Checksum error: calculated=%d, received=%d", checksum, data[4]);
        return ESP_FAIL;
    }

    // Parse data
    *humidity = (float)data[0] + (float)data[1] * 0.1f;
    *temperature = (float)data[2] + (float)data[3] * 0.1f;

    ESP_LOGI(TAG, "Temperature: %.1f°C, Humidity: %.1f%%", *temperature, *humidity);

    return ESP_OK;
}

static void dht11_periodic_task(void *arg)
{
    float temperature, humidity;

    while (1) {
        esp_err_t ret = dht11_sensor_read(&temperature, &humidity);
        
        if (ret == ESP_OK && s_reading_cb) {
            s_reading_cb(temperature, humidity);
        }

        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
    }
}

esp_err_t dht11_sensor_start_periodic(uint32_t interval_ms)
{
    if (s_periodic_task_handle != NULL) {
        ESP_LOGW(TAG, "Periodic task already running");
        return ESP_OK;
    }

    s_interval_ms = interval_ms;

    BaseType_t ret = xTaskCreate(dht11_periodic_task, "dht11_task", 
                                 2048, NULL, 5, &s_periodic_task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create periodic task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started periodic reading every %lu ms", interval_ms);
    return ESP_OK;
}

esp_err_t dht11_sensor_stop_periodic(void)
{
    if (s_periodic_task_handle == NULL) {
        ESP_LOGW(TAG, "Periodic task not running");
        return ESP_OK;
    }

    vTaskDelete(s_periodic_task_handle);
    s_periodic_task_handle = NULL;

    ESP_LOGI(TAG, "Stopped periodic reading");
    return ESP_OK;
}
