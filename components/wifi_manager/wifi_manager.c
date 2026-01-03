#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static const char *TAG = "WiFi_Manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5

// Provisioning AP settings
#define PROV_AP_SSID       "ESP32-Setup"
#define PROV_AP_PASSWORD   ""          // Open network for simplicity
#define PROV_AP_CHANNEL    1
#define PROV_AP_MAX_CONN   4

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_connected = false;
static bool s_has_credentials = false;
static bool s_portal_running = false;

static wifi_connected_cb_t s_connected_cb = NULL;
static wifi_disconnected_cb_t s_disconnected_cb = NULL;

static char s_sta_ssid[32] = {0};
static char s_sta_password[64] = {0};

static httpd_handle_t s_http_server = NULL;

// Forward declarations
static esp_err_t start_provisioning_ap(void);
static esp_err_t apply_sta_config_and_connect(const char *ssid, const char *password);

// ===== URL decode helper =====
static int url_decode(const char *src, char *dst, size_t dst_size)
{
    size_t si = 0, di = 0;
    while (src[si] && di + 1 < dst_size) {
        if (src[si] == '%' && isxdigit((int)src[si + 1]) && isxdigit((int)src[si + 2])) {
            char hex[3] = {src[si + 1], src[si + 2], '\0'};
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 3;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
            si++;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
    return (int)di;
}

// ===== NVS helpers =====
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

    nvs_close(handle);
    s_has_credentials = true;
    ESP_LOGI(TAG, "Loaded WiFi credentials from NVS (SSID: %s)", s_sta_ssid);
    return ESP_OK;
}

static esp_err_t save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(nvs_set_str(handle, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(handle, "pwd", password));
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved WiFi credentials to NVS (SSID: %s)", ssid);
    }
    return err;
}

// ===== HTTP Handlers =====
static const char *FORM_HTML =
    "<!DOCTYPE html>"
    "<html><head><meta charset='utf-8'><title>ESP32 WiFi Setup</title></head>"
    "<body><h2>ESP32 WiFi Setup</h2>"
    "<form method='POST' action='/save'>"
    "SSID:<br><input name='ssid' maxlength='31'><br><br>"
    "Password:<br><input name='password' type='password' maxlength='63'><br><br>"
    "<button type='submit'>Save & Connect</button>"
    "</form>"
    "</body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, FORM_HTML, strlen(FORM_HTML));
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int total = req->content_len;
    if (total >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, buf, total);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    // Parse form: ssid=...&password=...
    char ssid_enc[64] = {0};
    char pass_enc[64] = {0};
    char *ssid_pos = strstr(buf, "ssid=");
    if (ssid_pos) {
        ssid_pos += 5;
        char *amp = strchr(ssid_pos, '&');
        size_t len = amp ? (size_t)(amp - ssid_pos) : strlen(ssid_pos);
        len = (len >= sizeof(ssid_enc)) ? sizeof(ssid_enc) - 1 : len;
        strncpy(ssid_enc, ssid_pos, len);
        ssid_enc[len] = '\0';
    }
    char *pass_pos = strstr(buf, "password=");
    if (pass_pos) {
        pass_pos += 9;
        size_t len = strlen(pass_pos);
        len = (len >= sizeof(pass_enc)) ? sizeof(pass_enc) - 1 : len;
        strncpy(pass_enc, pass_pos, len);
        pass_enc[len] = '\0';
    }

    char ssid_dec[32] = {0};
    char pass_dec[64] = {0};
    url_decode(ssid_enc, ssid_dec, sizeof(ssid_dec));
    url_decode(pass_enc, pass_dec, sizeof(pass_dec));

    if (strlen(ssid_dec) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    save_credentials(ssid_dec, pass_dec);

    const char resp[] = "Saved. Connecting...";
    httpd_resp_send(req, resp, sizeof(resp) - 1);

    // Apply new credentials and connect
    strncpy(s_sta_ssid, ssid_dec, sizeof(s_sta_ssid) - 1);
    strncpy(s_sta_password, pass_dec, sizeof(s_sta_password) - 1);
    s_has_credentials = true;

    // Stop portal and connect STA
    apply_sta_config_and_connect(s_sta_ssid, s_sta_password);
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    if (s_http_server) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = root_get_handler,
            .user_ctx = NULL
        };
        httpd_uri_t save = {
            .uri      = "/save",
            .method   = HTTP_POST,
            .handler  = save_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_http_server, &root);
        httpd_register_uri_handler(s_http_server, &save);
        ESP_LOGI(TAG, "HTTP provisioning server started at http://192.168.4.1/");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}

static void stop_http_server(void)
{
    if (s_http_server) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

// ===== WiFi helpers =====
static esp_err_t apply_sta_config_and_connect(const char *ssid, const char *password)
{
    stop_http_server();
    s_portal_running = false;

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Switching to STA mode with SSID: %s", ssid);
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
    return ESP_OK;
}

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
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    start_http_server();
    ESP_LOGI(TAG, "Connect to WiFi '%s' and open http://192.168.4.1 to configure.", PROV_AP_SSID);
    return ESP_OK;
}

// ===== Event handler =====
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        if (s_disconnected_cb) {
            s_disconnected_cb();
        }

        if (s_retry_num < MAX_RETRY) {
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi (%d/%d)...", s_retry_num, MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect after %d retries, starting provisioning AP", MAX_RETRY);
            start_provisioning_ap();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_connected = true;
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
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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
        apply_sta_config_and_connect(s_sta_ssid, s_sta_password);

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
    stop_http_server();
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
