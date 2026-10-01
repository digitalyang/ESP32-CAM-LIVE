#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "camera_service.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frame_pipeline.h"
#include "lwip/sockets.h"
#include "stream_stats.h"
#include "web_ui.h"

#define STREAM_BOUNDARY "frame"

static const char *TAG = "web_server";

static esp_err_t index_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, WEB_INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t snapshot_handler(httpd_req_t *request)
{
    camera_fb_t *frame = camera_service_capture();
    if (frame == NULL) {
        ESP_LOGE(TAG, "Snapshot capture failed");
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Camera capture failed");
        return ESP_FAIL;
    }

    esp_err_t result;
    if (frame->format != PIXFORMAT_JPEG) {
        result = httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                     "Unexpected camera frame format");
    } else {
        httpd_resp_set_type(request, "image/jpeg");
        httpd_resp_set_hdr(request, "Content-Disposition",
                           "inline; filename=capture.jpg");
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        result = httpd_resp_send(request, (const char *)frame->buf, frame->len);
    }
    camera_service_release(frame);
    return result;
}

static esp_err_t stats_handler(httpd_req_t *request)
{
    stream_stats_t stats;
    stream_stats_get(&stats);

    char json[48];
    int length = snprintf(json, sizeof(json), "{\"fps\":%u.%u,\"kbps\":%u}",
                          (unsigned)(stats.fps_x10 / 10),
                          (unsigned)(stats.fps_x10 % 10),
                          (unsigned)stats.kbps);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json, length);
}

static esp_err_t apply_control_query(httpd_req_t *request)
{
    char query[96];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
        return ESP_OK;
    }

    char value[16];
    if (httpd_query_key_value(query, "auto", value, sizeof(value)) == ESP_OK) {
        if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        ESP_RETURN_ON_ERROR(camera_service_set_auto_exposure(strcmp(value, "1") == 0),
                            TAG, "Unable to update auto exposure");
    }

    if (httpd_query_key_value(query, "exposure", value, sizeof(value)) == ESP_OK) {
        int exposure = atoi(value);
        if (exposure != 100 && exposure != 300 && exposure != 600) {
            return ESP_ERR_INVALID_ARG;
        }
        ESP_RETURN_ON_ERROR(camera_service_set_exposure(exposure), TAG,
                            "Unable to update exposure");
    }

    if (httpd_query_key_value(query, "quality", value, sizeof(value)) == ESP_OK) {
        ESP_RETURN_ON_ERROR(camera_service_set_quality(atoi(value)), TAG,
                            "Unable to update JPEG quality");
    }

    if (httpd_query_key_value(query, "resolution", value, sizeof(value)) == ESP_OK) {
        framesize_t frame_size;
        if (!camera_service_parse_frame_size(value, &frame_size)) {
            return ESP_ERR_INVALID_ARG;
        }
        ESP_RETURN_ON_ERROR(camera_service_set_frame_size(frame_size), TAG,
                            "Unable to update frame size");
    }
    return ESP_OK;
}

static esp_err_t control_handler(httpd_req_t *request)
{
    esp_err_t result = apply_control_query(request);
    if (result != ESP_OK) {
        httpd_resp_send_err(request,
                            result == ESP_ERR_INVALID_ARG ? HTTPD_400_BAD_REQUEST :
                                                           HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Unable to update camera setting");
        return result;
    }

    camera_settings_t settings;
    camera_service_get_settings(&settings);
    char json[112];
    int length = snprintf(
        json, sizeof(json),
        "{\"auto\":%s,\"exposure\":%d,\"quality\":%d,"
        "\"resolution\":\"%s\",\"fpsLimit\":%d}",
        settings.auto_exposure ? "true" : "false",
        settings.exposure,
        settings.jpeg_quality,
        camera_service_frame_size_name(settings.frame_size),
        APP_MAX_STREAM_FPS);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json, length);
}

static esp_err_t stream_handler(httpd_req_t *request)
{
    static const char *stream_type =
        "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY;
    static const char *part_header =
        "\r\n--" STREAM_BOUNDARY "\r\n"
        "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

    esp_err_t result = httpd_resp_set_type(request, stream_type);
    if (result != ESP_OK) {
        return result;
    }
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");

    int no_delay = 1;
    int socket_fd = httpd_req_to_sockfd(request);
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY,
                   &no_delay, sizeof(no_delay)) < 0) {
        ESP_LOGW(TAG, "Unable to enable TCP_NODELAY");
    }
    const struct timeval send_timeout = {
        .tv_sec = 0,
        .tv_usec = APP_SEND_TIMEOUT_MS * 1000,
    };
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO,
                   &send_timeout, sizeof(send_timeout)) < 0) {
        ESP_LOGW(TAG, "Unable to set the send timeout");
    }

    int64_t last_frame_start_us = 0;
    while (true) {
        int64_t now_us = esp_timer_get_time();
        if (last_frame_start_us != 0) {
            int64_t wait_us = APP_STREAM_INTERVAL_US -
                              (now_us - last_frame_start_us);
            if (wait_us > 0) {
                TickType_t ticks = pdMS_TO_TICKS(wait_us / 1000);
                if (ticks > 0) {
                    vTaskDelay(ticks);
                }
                while (esp_timer_get_time() - last_frame_start_us <
                       APP_STREAM_INTERVAL_US) {
                    taskYIELD();
                }
            }
        }
        last_frame_start_us = esp_timer_get_time();

        camera_fb_t *frame = frame_pipeline_take_latest(1000);
        if (frame == NULL || frame->format != PIXFORMAT_JPEG) {
            camera_service_release(frame);
            ESP_LOGE(TAG, "Stream capture failed");
            result = ESP_FAIL;
            break;
        }

        char header[96];
        int header_length = snprintf(header, sizeof(header), part_header,
                                     (unsigned)frame->len);
        if (header_length < 0 || header_length >= (int)sizeof(header)) {
            camera_service_release(frame);
            result = ESP_FAIL;
            break;
        }

        result = httpd_resp_send_chunk(request, header, header_length);
        uint32_t frame_bytes = (uint32_t)frame->len;
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(request,
                                           (const char *)frame->buf,
                                           frame->len);
        }
        camera_service_release(frame);
        if (result != ESP_OK) {
            ESP_LOGI(TAG, "Stream client disconnected");
            break;
        }

        stream_stats_record_frame(frame_bytes, (uint32_t)header_length,
                                  esp_timer_get_time());
    }

    stream_stats_reset();
    return result;
}

esp_err_t web_server_start(void)
{
    httpd_config_t page_config = HTTPD_DEFAULT_CONFIG();
    page_config.server_port = APP_PAGE_SERVER_PORT;
    page_config.max_uri_handlers = 4;
    page_config.max_open_sockets = 3;
    page_config.lru_purge_enable = true;
    page_config.stack_size = 6144;

    httpd_handle_t page_server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&page_server, &page_config), TAG,
                        "Unable to start page server");

    const httpd_uri_t page_routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_handler },
        { .uri = "/snapshot", .method = HTTP_GET, .handler = snapshot_handler },
        { .uri = "/stats", .method = HTTP_GET, .handler = stats_handler },
        { .uri = "/control", .method = HTTP_GET, .handler = control_handler },
    };
    for (size_t i = 0; i < sizeof(page_routes) / sizeof(page_routes[0]); ++i) {
        esp_err_t error = httpd_register_uri_handler(page_server, &page_routes[i]);
        if (error != ESP_OK) {
            httpd_stop(page_server);
            return error;
        }
    }

    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = APP_STREAM_SERVER_PORT;
    stream_config.ctrl_port = page_config.ctrl_port + 1;
    stream_config.max_uri_handlers = 1;
    stream_config.max_open_sockets = 1;
    stream_config.lru_purge_enable = true;
    stream_config.stack_size = 8192;
    stream_config.core_id = 1;

    httpd_handle_t stream_server = NULL;
    esp_err_t error = httpd_start(&stream_server, &stream_config);
    if (error != ESP_OK) {
        httpd_stop(page_server);
        return error;
    }

    const httpd_uri_t stream_route = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
    };
    error = httpd_register_uri_handler(stream_server, &stream_route);
    if (error != ESP_OK) {
        httpd_stop(stream_server);
        httpd_stop(page_server);
        return error;
    }

    ESP_LOGI(TAG, "HTTP page server started on port %d", APP_PAGE_SERVER_PORT);
    ESP_LOGI(TAG, "MJPEG stream server started on port %d",
             APP_STREAM_SERVER_PORT);
    return ESP_OK;
}
