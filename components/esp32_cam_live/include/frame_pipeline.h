#pragma once

#include <stdint.h>

#include "esp_camera.h"
#include "esp_err.h"

esp_err_t frame_pipeline_start(void);
camera_fb_t *frame_pipeline_take_latest(uint32_t timeout_ms);
uint32_t frame_pipeline_dropped_frames(void);
