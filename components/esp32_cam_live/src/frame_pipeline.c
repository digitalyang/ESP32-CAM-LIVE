#include "frame_pipeline.h"

#include "camera_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define CAPTURE_TASK_STACK_SIZE 4096
#define CAPTURE_TASK_PRIORITY   6
#define CAPTURE_TASK_CORE       1

static const char *TAG = "frame_pipeline";
static QueueHandle_t latest_frame_queue;
static TaskHandle_t capture_task_handle;
static portMUX_TYPE drop_count_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t drop_count;

static void record_dropped_frame(void)
{
    portENTER_CRITICAL(&drop_count_lock);
    drop_count++;
    portEXIT_CRITICAL(&drop_count_lock);
}

static void capture_task(void *argument)
{
    (void)argument;
    while (true) {
        camera_fb_t *new_frame = camera_service_capture();
        if (new_frame == NULL) {
            ESP_LOGW(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (xQueueSend(latest_frame_queue, &new_frame, 0) == pdTRUE) {
            continue;
        }

        /* The consumer is behind: discard the queued frame, never the newest one. */
        camera_fb_t *stale_frame = NULL;
        if (xQueueReceive(latest_frame_queue, &stale_frame, 0) == pdTRUE) {
            camera_service_release(stale_frame);
            record_dropped_frame();
        }

        if (xQueueSend(latest_frame_queue, &new_frame, 0) != pdTRUE) {
            camera_service_release(new_frame);
            record_dropped_frame();
        }
    }
}

esp_err_t frame_pipeline_start(void)
{
    if (latest_frame_queue != NULL) {
        return ESP_OK;
    }

    latest_frame_queue = xQueueCreate(1, sizeof(camera_fb_t *));
    if (latest_frame_queue == NULL) {
        ESP_LOGE(TAG, "Unable to create latest-frame queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreatePinnedToCore(
        capture_task,
        "camera_capture",
        CAPTURE_TASK_STACK_SIZE,
        NULL,
        CAPTURE_TASK_PRIORITY,
        &capture_task_handle,
        CAPTURE_TASK_CORE);
    if (created != pdPASS) {
        vQueueDelete(latest_frame_queue);
        latest_frame_queue = NULL;
        ESP_LOGE(TAG, "Unable to create camera capture task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Capture task started on core %d with latest-frame queue",
             CAPTURE_TASK_CORE);
    return ESP_OK;
}

camera_fb_t *frame_pipeline_take_latest(uint32_t timeout_ms)
{
    if (latest_frame_queue == NULL) {
        return NULL;
    }

    camera_fb_t *frame = NULL;
    if (xQueueReceive(latest_frame_queue, &frame,
                      pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return NULL;
    }
    return frame;
}

uint32_t frame_pipeline_dropped_frames(void)
{
    uint32_t value;
    portENTER_CRITICAL(&drop_count_lock);
    value = drop_count;
    portEXIT_CRITICAL(&drop_count_lock);
    return value;
}
