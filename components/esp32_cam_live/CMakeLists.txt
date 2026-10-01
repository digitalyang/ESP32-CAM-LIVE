idf_component_register(
    SRCS
        "src/camera_service.c"
        "src/frame_pipeline.c"
        "src/jpeg_rtp.c"
        "src/rtsp_server.c"
        "src/stream_stats.c"
        "src/web_server.c"
        "src/wifi_ap.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES
        espressif__esp32-camera
        esp_event
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        freertos
)
