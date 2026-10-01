#pragma once

#include <stdint.h>

typedef struct {
    uint32_t fps_x10;
    uint32_t kbps;
} stream_stats_t;

void stream_stats_record_frame(uint32_t frame_bytes, uint32_t header_bytes,
                               int64_t timestamp_us);
void stream_stats_get(stream_stats_t *stats);
void stream_stats_reset(void);
