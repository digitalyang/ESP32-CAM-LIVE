#include "rtsp_server.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "camera_service.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "frame_pipeline.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_rtp.h"
#include "lwip/sockets.h"
#include "stream_stats.h"

#define RTSP_TASK_STACK_SIZE 8192
#define RTSP_TASK_PRIORITY   7
#define RTSP_TASK_CORE       1
#define RTSP_SESSION_ID      13572468U
#define RTP_PAYLOAD_JPEG     26
#define RTP_CLOCK_STEP       (90000U / APP_MAX_STREAM_FPS)

typedef struct {
    int control_fd;
    struct sockaddr_in peer;
    uint16_t client_rtp_port;
    bool setup;
    bool playing;
    bool close_requested;
    char request_buffer[2048];
    size_t request_length;
} rtsp_session_t;

static const char *TAG = "rtsp_server";
static TaskHandle_t rtsp_task_handle;

static void write_u16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void write_u32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static bool send_all(int socket_fd, const char *data, size_t length)
{
    while (length > 0) {
        int sent = send(socket_fd, data, length, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        length -= (size_t)sent;
    }
    return true;
}

static int request_cseq(const char *request)
{
    const char *header = strstr(request, "CSeq:");
    return header == NULL ? 0 : atoi(header + 5);
}

static bool send_simple_response(rtsp_session_t *session, int cseq,
                                 const char *status, const char *extra_headers)
{
    char response[768];
    int length = snprintf(response, sizeof(response),
                          "RTSP/1.0 %s\r\nCSeq: %d\r\n%s\r\n",
                          status, cseq,
                          extra_headers == NULL ? "" : extra_headers);
    return length > 0 && length < (int)sizeof(response) &&
           send_all(session->control_fd, response, (size_t)length);
}

static void handle_options(rtsp_session_t *session, int cseq)
{
    send_simple_response(session, cseq, "200 OK",
                         "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, "
                         "TEARDOWN, GET_PARAMETER\r\n");
}

static void handle_describe(rtsp_session_t *session, int cseq)
{
    char sdp[384];
    int sdp_length = snprintf(
        sdp, sizeof(sdp),
        "v=0\r\n"
        "o=- %u 1 IN IP4 192.168.4.1\r\n"
        "s=ESP32-CAM Live\r\n"
        "c=IN IP4 192.168.4.1\r\n"
        "t=0 0\r\n"
        "a=control:*\r\n"
        "m=video 0 RTP/AVP 26\r\n"
        "a=rtpmap:26 JPEG/90000\r\n"
        "a=framerate:%d\r\n"
        "a=control:track1\r\n",
        RTSP_SESSION_ID, APP_MAX_STREAM_FPS);

    char response[768];
    int length = snprintf(
        response, sizeof(response),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Content-Base: rtsp://192.168.4.1:%d/mjpeg/1/\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n\r\n%s",
        cseq, APP_RTSP_PORT, sdp_length, sdp);
    if (length > 0 && length < (int)sizeof(response)) {
        send_all(session->control_fd, response, (size_t)length);
    }
}

static bool parse_client_ports(const char *request, uint16_t *rtp_port)
{
    const char *transport = strstr(request, "Transport:");
    if (transport == NULL || strstr(transport, "RTP/AVP/TCP") != NULL) {
        return false;
    }
    const char *ports = strstr(transport, "client_port=");
    if (ports == NULL) {
        return false;
    }
    long value = strtol(ports + strlen("client_port="), NULL, 10);
    if (value <= 0 || value > 65535) {
        return false;
    }
    *rtp_port = (uint16_t)value;
    return true;
}

static void handle_setup(rtsp_session_t *session, const char *request, int cseq,
                         uint32_t ssrc)
{
    uint16_t client_port;
    if (!parse_client_ports(request, &client_port)) {
        send_simple_response(session, cseq, "461 Unsupported Transport", NULL);
        return;
    }

    session->client_rtp_port = client_port;
    session->setup = true;
    char headers[384];
    snprintf(headers, sizeof(headers),
             "Transport: RTP/AVP;unicast;destination=%s;source=192.168.4.1;"
             "client_port=%u-%u;server_port=%d-%d;ssrc=%08lX\r\n"
             "Session: %u\r\n",
             inet_ntoa(session->peer.sin_addr),
             client_port, (uint16_t)(client_port + 1),
             APP_RTP_PORT, APP_RTP_PORT + 1,
             (unsigned long)ssrc, RTSP_SESSION_ID);
    send_simple_response(session, cseq, "200 OK", headers);
}

static void handle_play(rtsp_session_t *session, int cseq,
                        uint16_t sequence, uint32_t timestamp)
{
    if (!session->setup) {
        send_simple_response(session, cseq, "455 Method Not Valid in This State", NULL);
        return;
    }
    session->playing = true;
    char headers[320];
    snprintf(headers, sizeof(headers),
             "Session: %u\r\n"
             "Range: npt=0.000-\r\n"
             "RTP-Info: url=rtsp://192.168.4.1:%d/mjpeg/1/track1;"
             "seq=%u;rtptime=%lu\r\n",
             RTSP_SESSION_ID, APP_RTSP_PORT, sequence,
             (unsigned long)timestamp);
    send_simple_response(session, cseq, "200 OK", headers);
    ESP_LOGI(TAG, "RTSP PLAY to %s:%u", inet_ntoa(session->peer.sin_addr),
             session->client_rtp_port);
}

static void handle_request(rtsp_session_t *session, const char *request,
                           uint32_t ssrc, uint16_t sequence, uint32_t timestamp)
{
    int cseq = request_cseq(request);
    if (strncmp(request, "OPTIONS ", 8) == 0) {
        handle_options(session, cseq);
    } else if (strncmp(request, "DESCRIBE ", 9) == 0) {
        handle_describe(session, cseq);
    } else if (strncmp(request, "SETUP ", 6) == 0) {
        handle_setup(session, request, cseq, ssrc);
    } else if (strncmp(request, "PLAY ", 5) == 0) {
        handle_play(session, cseq, sequence, timestamp);
    } else if (strncmp(request, "PAUSE ", 6) == 0) {
        session->playing = false;
        send_simple_response(session, cseq, "200 OK", "Session: 13572468\r\n");
    } else if (strncmp(request, "TEARDOWN ", 9) == 0) {
        send_simple_response(session, cseq, "200 OK", "Session: 13572468\r\n");
        session->close_requested = true;
    } else if (strncmp(request, "GET_PARAMETER ", 14) == 0) {
        send_simple_response(session, cseq, "200 OK", "Session: 13572468\r\n");
    } else {
        send_simple_response(session, cseq, "501 Not Implemented", NULL);
    }
}

static bool receive_control_requests(rtsp_session_t *session, uint32_t ssrc,
                                     uint16_t sequence, uint32_t timestamp)
{
    size_t available = sizeof(session->request_buffer) - session->request_length - 1;
    if (available == 0) {
        return false;
    }

    int received = recv(session->control_fd,
                        session->request_buffer + session->request_length,
                        available, MSG_DONTWAIT);
    if (received == 0) {
        return false;
    }
    if (received < 0) {
        return errno == EWOULDBLOCK || errno == EAGAIN;
    }
    session->request_length += (size_t)received;
    session->request_buffer[session->request_length] = '\0';

    char *request_end;
    while ((request_end = strstr(session->request_buffer, "\r\n\r\n")) != NULL) {
        size_t request_size = (size_t)(request_end - session->request_buffer) + 4;
        char saved = session->request_buffer[request_size];
        session->request_buffer[request_size] = '\0';
        handle_request(session, session->request_buffer, ssrc, sequence, timestamp);
        session->request_buffer[request_size] = saved;

        size_t remaining = session->request_length - request_size;
        memmove(session->request_buffer,
                session->request_buffer + request_size, remaining);
        session->request_length = remaining;
        session->request_buffer[remaining] = '\0';
    }
    return true;
}

static bool send_rtp_jpeg(int rtp_fd, const rtsp_session_t *session,
                          const camera_fb_t *frame, uint32_t ssrc,
                          uint16_t *sequence, uint32_t timestamp)
{
    jpeg_rtp_info_t jpeg;
    if (!jpeg_rtp_parse(frame->buf, frame->len, &jpeg)) {
        ESP_LOGW(TAG, "Unable to parse JPEG for RTP");
        return false;
    }

    const size_t regular_header = 12 + 8;
    const size_t first_header = regular_header + 4 + 128;
    struct sockaddr_in destination = session->peer;
    destination.sin_port = htons(session->client_rtp_port);
    size_t offset = 0;
    unsigned queued_packets = 0;
    while (offset < jpeg.scan_size) {
        bool first = offset == 0;
        size_t header_size = first ? first_header : regular_header;
        size_t payload_capacity = APP_RTP_PACKET_SIZE - header_size;
        size_t payload_size = jpeg.scan_size - offset;
        if (payload_size > payload_capacity) {
            payload_size = payload_capacity;
        }
        bool last = offset + payload_size == jpeg.scan_size;

        uint8_t packet[APP_RTP_PACKET_SIZE];
        memset(packet, 0, header_size);
        packet[0] = 0x80;
        packet[1] = RTP_PAYLOAD_JPEG | (last ? 0x80 : 0x00);
        write_u16(packet + 2, (*sequence)++);
        write_u32(packet + 4, timestamp);
        write_u32(packet + 8, ssrc);
        packet[13] = (uint8_t)(offset >> 16);
        packet[14] = (uint8_t)(offset >> 8);
        packet[15] = (uint8_t)offset;
        packet[16] = jpeg.rtp_type;
        /* A dynamic quant-table identifier must be identical in every
         * fragment of the frame. The table header itself is present only
         * when fragment offset is zero, as required by RFC 2435. */
        packet[17] = 128;
        packet[18] = (uint8_t)(frame->width / 8);
        packet[19] = (uint8_t)(frame->height / 8);
        if (first) {
            packet[22] = 0;
            packet[23] = 128;
            memcpy(packet + 24, jpeg.quant_luma, 64);
            memcpy(packet + 88, jpeg.quant_chroma, 64);
        }
        memcpy(packet + header_size, jpeg.scan + offset, payload_size);

        size_t packet_size = header_size + payload_size;
        /* Never let one congested UDP packet consume several 30 FPS frame
         * periods. The socket still carries the configured 100 ms timeout,
         * while streaming follows the stricter latest-frame/drop policy. */
        int sent = -1;
        for (int attempt = 0; attempt < APP_RTP_SEND_ATTEMPTS; ++attempt) {
            sent = sendto(rtp_fd, packet, packet_size, MSG_DONTWAIT,
                          (const struct sockaddr *)&destination,
                          sizeof(destination));
            if (sent == (int)packet_size) {
                break;
            }
            if (attempt + 1 < APP_RTP_SEND_ATTEMPTS) {
                esp_rom_delay_us(APP_RTP_RETRY_DELAY_US);
            }
        }
        if (sent != (int)packet_size) {
            return false;
        }
        offset += payload_size;
        queued_packets++;
        if (!last && queued_packets % APP_RTP_PACKETS_PER_YIELD == 0) {
            /* Large, high-motion JPEGs contain many more RTP fragments than
             * static frames. Drain the Wi-Fi queue in small batches so a
             * single frame cannot create a packet burst that drops later
             * fragments. At the 1 kHz RTOS tick this costs only 1 ms per
             * four packets and leaves the 33 ms frame budget intact. */
            vTaskDelay(1);
        }
    }
    return true;
}

static int create_tcp_listener(void)
{
    int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_fd < 0) {
        return -1;
    }
    int reuse = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(APP_RTSP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(socket_fd, 1) < 0) {
        close(socket_fd);
        return -1;
    }
    fcntl(socket_fd, F_SETFL, O_NONBLOCK);
    return socket_fd;
}

static int create_rtp_socket(void)
{
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        return -1;
    }
    const struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = APP_SEND_TIMEOUT_MS * 1000,
    };
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(APP_RTP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

static void reset_session(rtsp_session_t *session)
{
    if (session->control_fd >= 0) {
        close(session->control_fd);
    }
    memset(session, 0, sizeof(*session));
    session->control_fd = -1;
    stream_stats_reset();
}

static void rtsp_task(void *argument)
{
    (void)argument;
    int listener = create_tcp_listener();
    int rtp_fd = create_rtp_socket();
    if (listener < 0 || rtp_fd < 0) {
        ESP_LOGE(TAG, "Unable to open RTSP/RTP sockets");
        if (listener >= 0) close(listener);
        if (rtp_fd >= 0) close(rtp_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "RTSP ready at rtsp://192.168.4.1:%d/mjpeg/1",
             APP_RTSP_PORT);
    rtsp_session_t session = { .control_fd = -1 };
    uint32_t ssrc = esp_random();
    uint16_t sequence = (uint16_t)esp_random();
    uint32_t timestamp = esp_random();
    int64_t next_frame_us = 0;

    while (true) {
        if (session.control_fd < 0) {
            socklen_t peer_size = sizeof(session.peer);
            int client = accept(listener, (struct sockaddr *)&session.peer, &peer_size);
            if (client >= 0) {
                session.control_fd = client;
                fcntl(client, F_SETFL, O_NONBLOCK);
                int no_delay = 1;
                setsockopt(client, IPPROTO_TCP, TCP_NODELAY,
                           &no_delay, sizeof(no_delay));
                ESP_LOGI(TAG, "RTSP client connected: %s",
                         inet_ntoa(session.peer.sin_addr));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        }

        if (!receive_control_requests(&session, ssrc, sequence, timestamp) ||
            session.close_requested) {
            ESP_LOGI(TAG, "RTSP client disconnected");
            reset_session(&session);
            next_frame_us = 0;
            continue;
        }

        int64_t now_us = esp_timer_get_time();
        if (session.playing && (next_frame_us == 0 || now_us >= next_frame_us)) {
            if (next_frame_us == 0) {
                next_frame_us = now_us;
            }
            do {
                next_frame_us += APP_STREAM_INTERVAL_US;
            } while (next_frame_us <= now_us);
            camera_fb_t *frame = frame_pipeline_take_latest(5);
            if (frame != NULL) {
                timestamp += RTP_CLOCK_STEP;
                bool sent = frame->format == PIXFORMAT_JPEG &&
                            send_rtp_jpeg(rtp_fd, &session, frame, ssrc,
                                          &sequence, timestamp);
                uint32_t frame_bytes = (uint32_t)frame->len;
                camera_service_release(frame);
                if (sent) {
                    stream_stats_record_frame(frame_bytes, 0,
                                              esp_timer_get_time());
                }
            }
        } else {
            vTaskDelay(1);
        }
    }
}

esp_err_t rtsp_server_start(void)
{
    if (rtsp_task_handle != NULL) {
        return ESP_OK;
    }
    BaseType_t created = xTaskCreatePinnedToCore(
        rtsp_task,
        "rtsp_server",
        RTSP_TASK_STACK_SIZE,
        NULL,
        RTSP_TASK_PRIORITY,
        &rtsp_task_handle,
        RTSP_TASK_CORE);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
