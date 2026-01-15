# Smart Home IoT Project

Một hệ thống nhà thông minh toàn diện được xây dựng trên nền tảng ESP32, sử dụng FreeRTOS và các thành phần IoT hiện đại.

## Giới thiệu

Dự án Smart Home cung cấp một giải pháp IoT hoàn chỉnh để điều khiển các thiết bị trong nhà thông qua mạng Wi-Fi. Hệ thống hỗ trợ các cảm biến như DHT11 (nhiệt độ-độ ẩm), cảm biến âm thanh và tích hợp MQTT để kết nối với các máy chủ IoT.

## Các thành phần chính

- **WiFi Manager**: Quản lý kết nối Wi-Fi và MQTT
- **LED Controller**: Điều khiển các đèn LED thông minh
- **DHT11 Sensor**: Cảm biến nhiệt độ và độ ẩm
- **Sound Sensor**: Cảm biến âm thanh để phát hiện hoạt động
- **Button Handler**: Xử lý các nút bấm cho điều khiển thủ công
- **MQTT App**: Ứng dụng MQTT để giao tiếp với cloud

## Cấu trúc dự án

```
smart_home/
├── main/                    Chương trình chính
├── components/              Các thành phần tái sử dụng
│   ├── wifi_manager/       Quản lý Wi-Fi
│   ├── mqtt_app/           Ứng dụng MQTT
│   ├── led_controller/      Điều khiển LED
│   ├── dht11_sensor/       Cảm biến DHT11
│   ├── sound_sensor/       Cảm biến âm thanh
│   └── button_handler/     Xử lý nút bấm
├── CMakeLists.txt          Tệp build
└── README.md               Tài liệu này
```

## Công nghệ sử dụng

- **Microcontroller**: ESP32
- **RTOS**: FreeRTOS
- **Build System**: CMake
- **Framework**: ESP-IDF
- **Protocol**: MQTT, Wi-Fi

## Khởi động nhanh

1. Cài đặt [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
2. Clone và vào thư mục dự án
3. Chạy: `idf.py build`
4. Nạp firmware: `idf.py -p COM_PORT flash`
5. Monitor: `idf.py -p COM_PORT monitor`

## Tính năng

- Kết nối Wi-Fi tự động
- Điều khiển từ xa qua MQTT
- Giám sát nhiệt độ và độ ẩm
- Phát hiện âm thanh
- Điều khiển LED thông minh
- Xử lý sự kiện nút bấm
