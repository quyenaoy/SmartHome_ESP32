#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback khi nhận được thông tin WiFi từ điện thoại
typedef void (*wifi_portal_credentials_cb_t)(const char *ssid, const char *password, const char *room_id);

// Callback để báo trạng thái kết nối WiFi về cho điện thoại
typedef void (*wifi_portal_status_cb_t)(bool success, const char *message);

/**
 * @brief Khởi động HTTP portal để nhận cấu hình từ điện thoại
 *
 * Portal chạy ở 192.168.4.1, endpoint: POST /configure
 * Điện thoại kết nối vào WiFi của ESP32, gửi JSON chứa SSID/password/IDs
 * ESP sẽ thử kết nối WiFi đó 3 lần và báo lại trạng thái
 */
esp_err_t wifi_portal_start(wifi_portal_credentials_cb_t credentials_cb);

/**
 * @brief Dừng HTTP portal
 */
void wifi_portal_stop(void);

/**
 * @brief Đăng ký callback để nhận trạng thái kết nối WiFi
 */
void wifi_portal_set_status_callback(wifi_portal_status_cb_t status_cb);

/**
 * @brief Báo trạng thái kết nối WiFi (gọi từ wifi_manager)
 */
void wifi_portal_report_status(bool success, const char *message);

#ifdef __cplusplus
}
#endif
