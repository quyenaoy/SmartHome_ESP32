#include "wifi_manager.h"
#include "wifi_portal.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WiFi_Manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5
#define RECONNECT_PERIOD_MS (30 * 1000)  // 30s kết nối lại 1 lần
#define PORTAL_CONNECT_MAX_RETRY 3  // Số lần thử kết nối khi nhận config từ điện thoại

// Provisioning AP settings
#define PROV_AP_SSID       "ESP32-Setup"
#define PROV_AP_PASSWORD   ""          // Open network for simplicity
#define PROV_AP_CHANNEL    1
#define PROV_AP_MAX_CONN   4

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static int s_portal_retry_count = 0;  // Đếm số lần thử kết nối từ portal (max 3 lần)
static bool s_is_connected = false;
static bool s_has_credentials = false;
static bool s_portal_running = false;
static bool s_is_connecting_from_portal = false;  // Đang kết nối từ portal hay không
static TaskHandle_t s_reconnect_task = NULL;

static wifi_connected_cb_t s_connected_cb = NULL;
static wifi_disconnected_cb_t s_disconnected_cb = NULL;

static char s_sta_ssid[32] = {0};
static char s_sta_password[64] = {0};
static char s_room_id[32] = {0};

// Forward declarations
static esp_err_t start_provisioning_ap(void);
static esp_err_t apply_sta_config_and_connect(const char *ssid, const char *password, bool keep_portal_active);
static void credentials_from_portal(const char *ssid, const char *password, const char *room_id);
static void start_reconnect_task_if_needed(void);
static void stop_reconnect_task(void);

// ===== NVS helpers =====
// Đọc thông tin WiFi và roomId từ bộ nhớ flash (NVS)
// Trả về ESP_OK nếu đọc thành công, ESP_FAIL nếu chưa có thông tin lưu
static esp_err_t load_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi credentials stored (nvs_open err=%s)", esp_err_to_name(err));
        return err;
    }

    size_t ssid_len = sizeof(s_sta_ssid);
    size_t pass_len = sizeof(s_sta_password);
    err = nvs_get_str(handle, "ssid", s_sta_ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SSID not found in NVS");
        nvs_close(handle);
        return err;
    }
    err = nvs_get_str(handle, "pwd", s_sta_password, &pass_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Password not found in NVS");
        nvs_close(handle);
        return err;
    }

    // Load roomId (optional, không fail nếu không có)
    size_t room_len = sizeof(s_room_id);
    nvs_get_str(handle, "roomId", s_room_id, &room_len);

    nvs_close(handle);
    s_has_credentials = true;
    ESP_LOGI(TAG, "Loaded WiFi credentials from NVS (SSID: %s, Room: %s)", 
             s_sta_ssid, s_room_id);
    return ESP_OK;
}

// Lưu thông tin WiFi và roomId vào bộ nhớ flash (NVS)
// Thông tin này sẽ được giữ lại ngay cả khi ESP khởi động lại
static esp_err_t save_credentials(const char *ssid, const char *password, const char *room_id)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(nvs_set_str(handle, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(handle, "pwd", password));
    ESP_ERROR_CHECK(nvs_set_str(handle, "roomId", room_id));
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved config to NVS (SSID: %s, Room: %s)", ssid, room_id);
    }
    return err;
}

// ===== WiFi helpers =====
// Task FreeRTOS: thử kết nối lại WiFi mỗi 3 phút khi đang ở chế độ portal
// Nếu đã có thông tin WiFi lưu trước, sẽ tự động thử kết nối
static void reconnect_task(void *arg)
{
    while (s_portal_running) {
        if (!s_is_connected && s_has_credentials) {
            ESP_LOGI(TAG, "Periodic reconnect attempt to stored WiFi: %s", s_sta_ssid);
            apply_sta_config_and_connect(s_sta_ssid, s_sta_password, true);
        }
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_PERIOD_MS));
    }
    s_reconnect_task = NULL;
    vTaskDelete(NULL);
}

// Khởi động task reconnect nếu đã có thông tin WiFi lưu và chưa có task đang chạy
static void start_reconnect_task_if_needed(void)
{
    if (s_reconnect_task || !s_has_credentials) {
        return;
    }
    xTaskCreate(reconnect_task, "wifi_reconnect", 4096, NULL, 4, &s_reconnect_task);
}

// Dừng task reconnect nếu đang chạy
static void stop_reconnect_task(void)
{
    if (s_reconnect_task) {
        vTaskDelete(s_reconnect_task);
        s_reconnect_task = NULL;
    }
}

// Cấu hình và kết nối WiFi theo chế độ STA
// Nếu keep_portal_active = true: chạy ở chế độ APSTA (vừa là AP vừa là STA)
// Nếu keep_portal_active = false: chỉ chạy STA, tắt portal
static esp_err_t apply_sta_config_and_connect(const char *ssid, const char *password, bool keep_portal_active)
{
    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Configuring STA for SSID: %s (keep_portal=%d)", ssid, keep_portal_active ? 1 : 0);

    if (!keep_portal_active) {
        wifi_portal_stop();
        s_portal_running = false;
        stop_reconnect_task();
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(500));  // Đợi WiFi stop xong
    }

    wifi_mode_t target_mode = keep_portal_active ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    esp_err_t err = esp_wifi_set_mode(target_mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGW(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return err;
    }

    esp_wifi_connect();
    return ESP_OK;
}

// Callback khi nhận được thông tin WiFi từ điện thoại qua portal
// Lưu thông tin vào NVS, sau đó thử kết nối WiFi (giữ portal để báo trạng thái)
static void credentials_from_portal(const char *ssid, const char *password, const char *room_id)
{
    ESP_LOGI(TAG, "Received config from portal: %s (Room: %s)", ssid, room_id);
    save_credentials(ssid, password, room_id);

    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    strncpy(s_sta_password, password, sizeof(s_sta_password) - 1);
    strncpy(s_room_id, room_id, sizeof(s_room_id) - 1);
    s_has_credentials = true;

    // Reset retry count và set flag: đang kết nối từ portal
    s_portal_retry_count = 0;
    s_is_connecting_from_portal = true;
    
    // Giữ portal active để có thể báo trạng thái về cho điện thoại
    apply_sta_config_and_connect(s_sta_ssid, s_sta_password, true);
}

// Khởi động AP để provisioning (cho phép điện thoại kết nối vào ESP và cấu hình WiFi)
// ESP sẽ tạo WiFi với tên "ESP32-Setup", không mật khẩu
// Điện thoại kết nối vào WiFi này, mở 192.168.4.1 và gửi JSON cấu hình
static esp_err_t start_provisioning_ap(void)
{
    if (s_portal_running) {
        ESP_LOGI(TAG, "Provisioning AP already running");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Starting provisioning AP '%s' (open network)", PROV_AP_SSID);
    s_portal_running = true;
    s_is_connected = false;

    wifi_config_t ap_config = { 0 };
    strncpy((char *)ap_config.ap.ssid, PROV_AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = (uint8_t)strlen(PROV_AP_SSID);
    ap_config.ap.channel = PROV_AP_CHANNEL;
    ap_config.ap.max_connection = PROV_AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    esp_wifi_stop();
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP+STA mode: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return err;
    }

    wifi_portal_start(credentials_from_portal);
    start_reconnect_task_if_needed();
    ESP_LOGI(TAG, "Connect to WiFi '%s' and open http://192.168.4.1 to configure.", PROV_AP_SSID);
    return ESP_OK;
}

// ===== Event handler =====
// Xử lý các event từ WiFi: kết nối, ngắt kết nối, nhận IP
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // WiFi STA đã start, bắt đầu kết nối
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Mất kết nối WiFi
        s_is_connected = false;
        if (s_disconnected_cb) {
            s_disconnected_cb();
        }

        // Nếu đang kết nối từ portal, chỉ thử tối đa PORTAL_CONNECT_MAX_RETRY lần
        if (s_is_connecting_from_portal) {
            s_portal_retry_count++;
            ESP_LOGI(TAG, "Portal connection retry %d/%d", s_portal_retry_count, PORTAL_CONNECT_MAX_RETRY);
            
            if (s_portal_retry_count < PORTAL_CONNECT_MAX_RETRY) {
                // Còn lượt thử, tiếp tục kết nối
                esp_wifi_connect();
            } else {
                // Đã thử đủ 3 lần, báo thất bại về portal
                ESP_LOGE(TAG, "Failed to connect after %d retries from portal", PORTAL_CONNECT_MAX_RETRY);
                wifi_portal_report_status(false, "Không thể kết nối WiFi sau 3 lần thử. Kiểm tra SSID/Password");
                s_is_connecting_from_portal = false;
                s_portal_retry_count = 0;
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        } else {
            // Kết nối tự động (không từ portal), thử MAX_RETRY lần
            if (s_retry_num < MAX_RETRY) {
                s_retry_num++;
                ESP_LOGI(TAG, "Retry connecting to WiFi (%d/%d)...", s_retry_num, MAX_RETRY);
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGE(TAG, "Failed to connect after %d retries, starting provisioning AP", MAX_RETRY);
                start_provisioning_ap();
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Đã nhận được IP, kết nối thành công!
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_connected = true;
        
        // Nếu đang kết nối từ portal, báo thành công
        if (s_is_connecting_from_portal) {
            ESP_LOGI(TAG, "Successfully connected from portal!");
            wifi_portal_report_status(true, "Kết nối WiFi thành công!");
            s_is_connecting_from_portal = false;
            s_portal_retry_count = 0;
            
            // Tắt portal và chuyển sang chế độ STA
            if (s_portal_running) {
                ESP_LOGI(TAG, "Shutting down portal and switching to STA mode");
                wifi_portal_stop();
                s_portal_running = false;
                stop_reconnect_task();
                esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
                if (mode_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to switch to STA mode: %s", esp_err_to_name(mode_err));
                }
            }
        } else {
            // Kết nối tự động thành công
            if (s_portal_running) {
                ESP_LOGI(TAG, "Connected while portal active, shutting portal and switching to STA");
                wifi_portal_stop();
                s_portal_running = false;
                stop_reconnect_task();
                esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
                if (mode_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to switch to STA mode: %s", esp_err_to_name(mode_err));
                }
            }
        }
        
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_connected_cb) {
            s_connected_cb();
        }
    }
}

// ===== Public API =====
esp_err_t wifi_manager_init(wifi_connected_cb_t connected_cb,
                            wifi_disconnected_cb_t disconnected_cb)
{
    s_connected_cb = connected_cb;
    s_disconnected_cb = disconnected_cb;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    load_credentials();

    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (s_has_credentials) {
        ESP_LOGI(TAG, "Starting WiFi STA with stored credentials...");
        apply_sta_config_and_connect(s_sta_ssid, s_sta_password, false);

        ESP_LOGI(TAG, "Waiting for WiFi connection or failure...");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                pdMS_TO_TICKS(15000));  // 15s timeout

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to WiFi successfully");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Connection failed or timed out, starting provisioning AP");
    }

    // No credentials or failed to connect -> start AP portal
    start_provisioning_ap();
    // Return OK so caller does not treat as fatal; portal is running
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    wifi_portal_stop();
    stop_reconnect_task();
    s_portal_running = false;
    s_is_connected = false;
    esp_wifi_stop();
    ESP_LOGI(TAG, "WiFi stopped");
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}

const char* wifi_manager_get_room_id(void)
{
    return s_room_id;
}
