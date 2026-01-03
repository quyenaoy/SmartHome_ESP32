idf_component_register(SRCS "smart_home_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES wifi_manager mqtt_app led_controller 
                                dht11_sensor button_handler nvs_flash)
