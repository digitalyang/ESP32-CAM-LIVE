#include "jpeg_rtp.h"

#include <string.h>

static uint16_t read_be16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static bool parse_quantization_tables(const uint8_t *data, size_t size,
                                      jpeg_rtp_info_t *info,
                                      bool *have_luma, bool *have_chroma)
{
    size_t offset = 0;
    while (offset < size) {
        uint8_t table_info = data[offset++];
        uint8_t precision = table_info >> 4;
        uint8_t table_id = table_info & 0x0f;
        size_t table_size = precision == 0 ? 64 : 128;
        if (offset + table_size > size || precision != 0) {
            return false;
        }
        if (table_id == 0) {
            memcpy(info->quant_luma, data + offset, 64);
            *have_luma = true;
        } else if (table_id == 1) {
            memcpy(info->quant_chroma, data + offset, 64);
            *have_chroma = true;
        }
        offset += table_size;
    }
    return true;
}

bool jpeg_rtp_parse(const uint8_t *jpeg, size_t jpeg_size,
                    jpeg_rtp_info_t *info)
{
    if (jpeg == NULL || info == NULL || jpeg_size < 4 ||
        jpeg[0] != 0xff || jpeg[1] != 0xd8) {
        return false;
    }

    memset(info, 0, sizeof(*info));
    bool have_luma = false;
    bool have_chroma = false;
    bool have_supported_sampling = false;
    size_t offset = 2;
    while (offset + 4 <= jpeg_size) {
        if (jpeg[offset] != 0xff) {
            offset++;
            continue;
        }
        while (offset < jpeg_size && jpeg[offset] == 0xff) {
            offset++;
        }
        if (offset >= jpeg_size) {
            return false;
        }

        uint8_t marker = jpeg[offset++];
        if (marker == 0xd9) {
            break;
        }
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
            continue;
        }
        if (offset + 2 > jpeg_size) {
            return false;
        }

        uint16_t segment_length = read_be16(jpeg + offset);
        if (segment_length < 2 || offset + segment_length > jpeg_size) {
            return false;
        }
        const uint8_t *segment = jpeg + offset + 2;
        size_t segment_size = segment_length - 2;

        if (marker == 0xdb &&
            !parse_quantization_tables(segment, segment_size, info,
                                       &have_luma, &have_chroma)) {
            return false;
        }
        if (marker == 0xc0 && segment_size >= 9) {
            uint8_t component_count = segment[5];
            if (component_count >= 1 && segment_size >= 6 + component_count * 3) {
                uint8_t y_sampling = segment[7];
                if (y_sampling == 0x21) {
                    info->rtp_type = 0; /* 4:2:2 */
                    have_supported_sampling = true;
                } else if (y_sampling == 0x22) {
                    info->rtp_type = 1; /* 4:2:0 */
                    have_supported_sampling = true;
                }
            }
        }
        if (marker == 0xda) {
            size_t scan_start = offset + segment_length;
            size_t scan_end = jpeg_size;
            if (scan_end >= 2 && jpeg[scan_end - 2] == 0xff &&
                jpeg[scan_end - 1] == 0xd9) {
                scan_end -= 2;
            }
            if (!have_luma || !have_chroma || !have_supported_sampling ||
                scan_start >= scan_end) {
                return false;
            }
            info->scan = jpeg + scan_start;
            info->scan_size = scan_end - scan_start;
            return true;
        }
        offset += segment_length;
    }
    return false;
}
