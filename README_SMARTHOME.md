# Smart Home Light Switch - ESP32

Dự án hệ thống smart home với ESP32 điều khiển 3 đèn LED, 3 nút bấm (cơ chế 2 chiều) và cảm biến DHT11.

## 🎯 Tính năng

- ✅ Điều khiển 3 đèn LED qua MQTT hoặc nút bấm vật lý
- ✅ Cơ chế nút bấm 2 chiều (toggle LED mỗi lần nhấn)
- ✅ Đọc nhiệt độ và độ ẩm từ DHT11
- ✅ Kết nối WiFi tự động
- ✅ Giao tiếp qua MQTT broker
- ✅ Báo cáo trạng thái định kỳ

## 📁 Cấu trúc thư mục

```
smart_home/
├── main/
│   ├── smart_home_main.c      # Main application
│   ├── smart_home_config.h    # Configuration file
│   └── CMakeLists.txt
├── components/
│   ├── wifi_manager/          # WiFi connection manager
│   ├── mqtt_client/           # MQTT communication
│   ├── led_controller/        # LED control
│   ├── dht11_sensor/          # DHT11 temperature/humidity
│   └── button_handler/        # Button press handling
├── CMakeLists.txt
└── README.md
```

## 🔌 Kết nối phần cứng

### Mặc định (có thể thay đổi trong smart_home_config.h):

| Thiết bị | GPIO Pin | Ghi chú |
|----------|----------|---------|
| LED 1    | GPIO 25  | Đèn phòng 1 |
| LED 2    | GPIO 26  | Đèn phòng 2 |
| LED 3    | GPIO 27  | Đèn phòng 3 |
| Button 1 | GPIO 32  | Nút bấm LED 1 |
| Button 2 | GPIO 33  | Nút bấm LED 2 |
| Button 3 | GPIO 13  | Nút bấm LED 3 |
| DHT11    | GPIO 4   | Cảm biến nhiệt độ/độ ẩm |

### Sơ đồ đấu nối:

**LED:**
- Anode (+) → GPIO (qua điện trở 220Ω)
- Cathode (-) → GND

**Button:**
- Một đầu → GPIO
- Đầu kia → GND
- (Pull-up resistor được kích hoạt nội bộ)

**DHT11:**
- VCC → 3.3V
- Data → GPIO 4
- GND → GND

## ⚙️ Cấu hình

Chỉnh sửa file `main/smart_home_config.h`:

```c
// WiFi
#define WIFI_SSID           "TenWiFiCuaBan"
#define WIFI_PASSWORD       "MatKhauWiFi"

// MQTT Broker
#define MQTT_BROKER_URL     "mqtt://broker.hivemq.com"
#define MQTT_CLIENT_ID      "esp32_room_01"

// GPIO Pins (nếu muốn thay đổi)
#define LED1_GPIO           GPIO_NUM_25
#define BUTTON1_GPIO        GPIO_NUM_32
// ...
```

## 🚀 Cách sử dụng

### 1. Cài đặt ESP-IDF

Đảm bảo đã cài ESP-IDF và thiết lập biến môi trường.

### 2. Cấu hình WiFi và MQTT

Sửa file `main/smart_home_config.h` với thông tin WiFi và MQTT broker của bạn.

### 3. Build và Flash

```bash
# Build project
idf.py build

# Flash to ESP32
idf.py -p COM3 flash

# Monitor serial output
idf.py -p COM3 monitor
```

(Thay `COM3` bằng port của bạn)

### 4. Sử dụng

**Điều khiển bằng nút bấm:**
- Nhấn Button 1/2/3 để bật/tắt LED tương ứng

**Điều khiển qua MQTT:**

Gửi lệnh đến các topic:
- `home/room1/led1` → ON/OFF/TOGGLE
- `home/room1/led2` → ON/OFF/TOGGLE
- `home/room1/led3` → ON/OFF/TOGGLE

Nhận dữ liệu từ các topic:
- `home/room1/temperature` → Nhiệt độ (°C)
- `home/room1/humidity` → Độ ẩm (%)
- `home/room1/status` → Trạng thái hệ thống (JSON)

## 📊 MQTT Topics

| Topic | Loại | Mô tả |
|-------|------|-------|
| `home/room1/led1` | Sub/Pub | Điều khiển/trạng thái LED 1 |
| `home/room1/led2` | Sub/Pub | Điều khiển/trạng thái LED 2 |
| `home/room1/led3` | Sub/Pub | Điều khiển/trạng thái LED 3 |
| `home/room1/temperature` | Pub | Nhiệt độ từ DHT11 |
| `home/room1/humidity` | Pub | Độ ẩm từ DHT11 |
| `home/room1/status` | Pub | Trạng thái hệ thống |

## 🔧 Tùy chỉnh

### Thay đổi số lượng LED

1. Sửa `#define NUM_LEDS` trong `smart_home_config.h`
2. Thêm GPIO pins tương ứng
3. Thêm MQTT topics nếu cần

### Thay đổi tần suất đọc DHT11

Sửa `#define DHT11_READ_INTERVAL_MS` (mặc định: 30000ms = 30 giây)

### Sử dụng broker MQTT khác

Thay đổi `MQTT_BROKER_URL` trong config. Ví dụ:
- `mqtt://broker.hivemq.com` (public)
- `mqtt://192.168.1.100:1883` (local)
- `mqtts://your-broker.com` (SSL)

## 🐛 Troubleshooting

**WiFi không kết nối:**
- Kiểm tra SSID và password trong config
- Đảm bảo ESP32 trong phạm vi WiFi

**MQTT không kết nối:**
- Kiểm tra broker URL
- Thử broker public: `mqtt://broker.hivemq.com`

**DHT11 đọc lỗi:**
- Kiểm tra kết nối Data pin
- DHT11 cần delay ít nhất 2 giây giữa các lần đọc

**Button không hoạt động:**
- Kiểm tra kết nối GND
- GPIO 34-39 chỉ có input, không có pull-up nội bộ

## 🧪 Test MQTT với MQTT Explorer

1. Tải MQTT Explorer: http://mqtt-explorer.com/
2. Kết nối tới broker (ví dụ: broker.hivemq.com)
3. Subscribe tất cả topics `home/room1/#`
4. Publish lệnh: `ON`, `OFF`, `TOGGLE` tới các topic LED

## 📝 License

MIT License - Tự do sử dụng cho mục đích học tập và thương mại.

## 👤 Tác giả

Smart Home Project - HUST IoT Course
