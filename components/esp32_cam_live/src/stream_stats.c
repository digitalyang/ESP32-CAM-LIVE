#include "stream_stats.h"

#include "freertos/FreeRTOS.h"

static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t fps_x10;
static uint32_t kbps;
static uint32_t frame_count;
static uint64_t byte_count;
static int64_t stats_start_us;

void stream_stats_record_frame(uint32_t frame_bytes, uint32_t header_bytes,
                               int64_t timestamp_us)
{
    portENTER_CRITICAL(&stats_lock);
    if (stats_start_us == 0) {
        stats_start_us = timestamp_us;
    }
    frame_count++;
    byte_count += frame_bytes + header_bytes;

    int64_t elapsed_us = timestamp_us - stats_start_us;
    if (elapsed_us >= 1000000) {
        fps_x10 = (uint32_t)(((uint64_t)frame_count * 10000000ULL) /
                             (uint64_t)elapsed_us);
        kbps = (uint32_t)((byte_count * 8000ULL) / (uint64_t)elapsed_us);
        frame_count = 0;
        byte_count = 0;
        stats_start_us = timestamp_us;
    }
    portEXIT_CRITICAL(&stats_lock);
}

void stream_stats_get(stream_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    portENTER_CRITICAL(&stats_lock);
    stats->fps_x10 = fps_x10;
    stats->kbps = kbps;
    portEXIT_CRITICAL(&stats_lock);
}

void stream_stats_reset(void)
{
    portENTER_CRITICAL(&stats_lock);
    fps_x10 = 0;
    kbps = 0;
    frame_count = 0;
    byte_count = 0;
    stats_start_us = 0;
    portEXIT_CRITICAL(&stats_lock);
}
