#include "camera_service.h"

#include <string.h>

#include "camera_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "camera_service";
static portMUX_TYPE settings_lock = portMUX_INITIALIZER_UNLOCKED;

static camera_settings_t current_settings = {
    .auto_exposure = false,
    .exposure = 100,
    .jpeg_quality = 20,
    .frame_size = FRAMESIZE_QVGA,
};

static sensor_t *get_sensor(void)
{
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL) {
        ESP_LOGE(TAG, "Camera sensor is unavailable");
    }
    return sensor;
}

esp_err_t camera_service_init(void)
{
    const camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        /* Reserve buffers for the largest selectable mode. */
        .frame_size = FRAMESIZE_VGA,
        .jpeg_quality = 20,
        .fb_count = 3,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t error = esp_camera_init(&config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(error));
        return error;
    }

    sensor_t *sensor = get_sensor();
    if (sensor == NULL) {
        esp_camera_deinit();
        return ESP_FAIL;
    }

    int result = sensor->set_framesize(sensor, current_settings.frame_size);
    result |= sensor->set_exposure_ctrl(sensor, 0);
    result |= sensor->set_aec2(sensor, 0);
    result |= sensor->set_aec_value(sensor, current_settings.exposure);
    if (result != 0) {
        ESP_LOGE(TAG, "Unable to apply initial camera settings");
        esp_camera_deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OV2640 initialized at QVGA with manual exposure");
    return ESP_OK;
}

camera_fb_t *camera_service_capture(void)
{
    return esp_camera_fb_get();
}

void camera_service_release(camera_fb_t *frame)
{
    if (frame != NULL) {
        esp_camera_fb_return(frame);
    }
}

void camera_service_get_settings(camera_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    portENTER_CRITICAL(&settings_lock);
    *settings = current_settings;
    portEXIT_CRITICAL(&settings_lock);
}

esp_err_t camera_service_set_auto_exposure(bool enabled)
{
    sensor_t *sensor = get_sensor();
    if (sensor == NULL) {
        return ESP_FAIL;
    }

    camera_settings_t settings;
    camera_service_get_settings(&settings);
    int result = sensor->set_exposure_ctrl(sensor, enabled ? 1 : 0);
    result |= sensor->set_aec2(sensor, enabled ? 1 : 0);
    if (!enabled) {
        result |= sensor->set_aec_value(sensor, settings.exposure);
    }
    if (result != 0) {
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&settings_lock);
    current_settings.auto_exposure = enabled;
    portEXIT_CRITICAL(&settings_lock);
    return ESP_OK;
}

esp_err_t camera_service_set_exposure(int exposure)
{
    if (exposure < 0 || exposure > 1200) {
        return ESP_ERR_INVALID_ARG;
    }

    camera_settings_t settings;
    camera_service_get_settings(&settings);
    if (!settings.auto_exposure) {
        sensor_t *sensor = get_sensor();
        if (sensor == NULL || sensor->set_aec_value(sensor, exposure) != 0) {
            return ESP_FAIL;
        }
    }

    portENTER_CRITICAL(&settings_lock);
    current_settings.exposure = exposure;
    portEXIT_CRITICAL(&settings_lock);
    return ESP_OK;
}

esp_err_t camera_service_set_quality(int jpeg_quality)
{
    if (jpeg_quality != 12 && jpeg_quality != 20 && jpeg_quality != 30) {
        return ESP_ERR_INVALID_ARG;
    }
    sensor_t *sensor = get_sensor();
    if (sensor == NULL || sensor->set_quality(sensor, jpeg_quality) != 0) {
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&settings_lock);
    current_settings.jpeg_quality = jpeg_quality;
    portEXIT_CRITICAL(&settings_lock);
    return ESP_OK;
}

esp_err_t camera_service_set_frame_size(framesize_t frame_size)
{
    if (frame_size != FRAMESIZE_QVGA &&
        frame_size != FRAMESIZE_CIF &&
        frame_size != FRAMESIZE_VGA) {
        return ESP_ERR_INVALID_ARG;
    }
    sensor_t *sensor = get_sensor();
    if (sensor == NULL || sensor->set_framesize(sensor, frame_size) != 0) {
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&settings_lock);
    current_settings.frame_size = frame_size;
    portEXIT_CRITICAL(&settings_lock);
    return ESP_OK;
}

const char *camera_service_frame_size_name(framesize_t frame_size)
{
    if (frame_size == FRAMESIZE_VGA) {
        return "vga";
    }
    if (frame_size == FRAMESIZE_CIF) {
        return "cif";
    }
    return "qvga";
}

bool camera_service_parse_frame_size(const char *name, framesize_t *frame_size)
{
    if (name == NULL || frame_size == NULL) {
        return false;
    }
    if (strcmp(name, "qvga") == 0) {
        *frame_size = FRAMESIZE_QVGA;
    } else if (strcmp(name, "cif") == 0) {
        *frame_size = FRAMESIZE_CIF;
    } else if (strcmp(name, "vga") == 0) {
        *frame_size = FRAMESIZE_VGA;
    } else {
        return false;
    }
    return true;
}
