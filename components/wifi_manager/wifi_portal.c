#include "wifi_portal.h"
#include "wifi_portal_page.h"

#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "WiFi_Portal";

static httpd_handle_t s_http_server = NULL;
static wifi_portal_credentials_cb_t s_credentials_cb = NULL;
static wifi_portal_status_cb_t s_status_cb = NULL;  // Callback để báo trạng thái kết nối

// ===== Helpers =====
// Helper: Trích xuất giá trị string từ JSON (simple parser cho các field cơ bản)
static bool json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    
    char *start = strstr(json, search);
    if (!start) return false;
    
    start += strlen(search);
    char *end = strchr(start, '"');
    if (!end) return false;
    
    size_t len = (size_t)(end - start);
    if (len >= out_size) len = out_size - 1;
    
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

// ===== HTTP Handlers =====
// Handler: Trả về trang HTML hướng dẫn API
static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Received GET request to /");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, WIFI_PORTAL_HTML, strlen(WIFI_PORTAL_HTML));
}

// Handler: Nhận cấu hình WiFi từ điện thoại (JSON format)
// POST /configure với body: {"ssid":"...","password":"...","userId":"...","homeId":"...","roomId":"..."}
static esp_err_t save_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Received POST request to /configure");
    ESP_LOGI(TAG, "Content-Length: %d", req->content_len);
    
    char buf[512] = {0};
    int total = req->content_len;
    if (total >= (int)sizeof(buf)) {
        ESP_LOGE(TAG, "Payload too large: %d bytes", total);
        const char *err_resp = "{\"success\":false,\"message\":\"Payload too large\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err_resp, strlen(err_resp));
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, buf, total);
    ESP_LOGI(TAG, "Received %d bytes", received);
    
    if (received <= 0) {
        ESP_LOGE(TAG, "No data received or error");
        const char *err_resp = "{\"success\":false,\"message\":\"No data received\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err_resp, strlen(err_resp));
        return ESP_FAIL;
    }
    buf[received] = '\0';

    ESP_LOGI(TAG, "Raw payload: %s", buf);

    // Parse JSON: {"ssid":"...","password":"...","roomId":"..."}
    char ssid[32] = {0};
    char password[64] = {0};
    char roomId[32] = {0};
    
    if (!json_get_string(buf, "ssid", ssid, sizeof(ssid))) {
        ESP_LOGE(TAG, "Failed to parse 'ssid' from JSON");
        const char *err_resp = "{\"success\":false,\"message\":\"Missing ssid field\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err_resp, strlen(err_resp));
        return ESP_FAIL;
    }
    
    json_get_string(buf, "password", password, sizeof(password));
    
    if (!json_get_string(buf, "roomId", roomId, sizeof(roomId))) {
        ESP_LOGE(TAG, "Failed to parse 'roomId' from JSON");
        const char *err_resp = "{\"success\":false,\"message\":\"Missing roomId field\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err_resp, strlen(err_resp));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "=== Configuration Received ===");
    ESP_LOGI(TAG, "  SSID: %s", ssid);
    ESP_LOGI(TAG, "  Password: %s", password[0] ? "***" : "(empty)");
    ESP_LOGI(TAG, "  Room ID: %s", roomId);
    ESP_LOGI(TAG, "=============================");

    if (!s_credentials_cb) {
        ESP_LOGE(TAG, "No credentials callback registered");
        const char *err_resp = "{\"success\":false,\"message\":\"Internal server error\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, err_resp, strlen(err_resp));
        return ESP_FAIL;
    }

    s_credentials_cb(ssid, password, roomId);

    const char *success_resp = "{\"success\":true,\"message\":\"Configuration saved, connecting to WiFi...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, success_resp, strlen(success_resp));
    
    ESP_LOGI(TAG, "Configuration saved successfully, initiating WiFi connection");
    return ESP_OK;
}

// ===== Public API =====
// Khởi động HTTP server để nhận cấu hình từ điện thoại
esp_err_t wifi_portal_start(wifi_portal_credentials_cb_t credentials_cb)
{
    if (s_http_server) {
        return ESP_OK;
    }
    s_credentials_cb = credentials_cb;

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
            .uri      = "/configure",
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

// Dừng HTTP server
void wifi_portal_stop(void)
{
    if (s_http_server) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

// Đăng ký callback để nhận trạng thái kết nối WiFi (gọi từ wifi_manager)
void wifi_portal_set_status_callback(wifi_portal_status_cb_t status_cb)
{
    s_status_cb = status_cb;
}

// Wifi_manager gọi hàm này để báo trạng thái kết nối về portal
void wifi_portal_report_status(bool success, const char *message)
{
    if (s_status_cb) {
        s_status_cb(success, message);
    }
}
