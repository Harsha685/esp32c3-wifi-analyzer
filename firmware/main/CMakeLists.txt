idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash esp_event esp_timer esp_system esp_driver_usb_serial_jtag
             spsc_ring dot11 wifi_stats telemetry)
