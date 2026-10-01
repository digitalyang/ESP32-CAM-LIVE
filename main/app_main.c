#include "camera_service.h"
#include "esp_err.h"
#include "frame_pipeline.h"
#include "nvs_flash.h"
#include "rtsp_server.h"
#include "web_server.h"
#include "wifi_ap.h"

void app_main(void)
{
    esp_err_t nvs_error = nvs_flash_init();
    if (nvs_error == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_error);

    ESP_ERROR_CHECK(camera_service_init());
    ESP_ERROR_CHECK(frame_pipeline_start());
    ESP_ERROR_CHECK(wifi_ap_start());
    ESP_ERROR_CHECK(rtsp_server_start());
    ESP_ERROR_CHECK(web_server_start());
}
