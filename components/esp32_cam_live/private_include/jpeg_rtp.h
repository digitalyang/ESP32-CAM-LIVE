#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *scan;
    size_t scan_size;
    uint8_t quant_luma[64];
    uint8_t quant_chroma[64];
    uint8_t rtp_type;
} jpeg_rtp_info_t;

bool jpeg_rtp_parse(const uint8_t *jpeg, size_t jpeg_size,
                    jpeg_rtp_info_t *info);
