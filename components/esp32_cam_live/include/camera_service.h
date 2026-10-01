#pragma once

#include <stdbool.h>

#include "esp_camera.h"
#include "esp_err.h"

typedef struct {
    bool auto_exposure;
    int exposure;
    int jpeg_quality;
    framesize_t frame_size;
} camera_settings_t;

esp_err_t camera_service_init(void);

camera_fb_t *camera_service_capture(void);
void camera_service_release(camera_fb_t *frame);

void camera_service_get_settings(camera_settings_t *settings);
esp_err_t camera_service_set_auto_exposure(bool enabled);
esp_err_t camera_service_set_exposure(int exposure);
esp_err_t camera_service_set_quality(int jpeg_quality);
esp_err_t camera_service_set_frame_size(framesize_t frame_size);

const char *camera_service_frame_size_name(framesize_t frame_size);
bool camera_service_parse_frame_size(const char *name, framesize_t *frame_size);
