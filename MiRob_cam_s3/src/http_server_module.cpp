#include <Arduino.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "camera_module.h"
#include "http_server_module.h"
#include "network_module.h"

static httpd_handle_t s_httpServer = nullptr;
static volatile bool s_streamEnabled = false;

static const char* kStreamContentType = "multipart/x-mixed-replace;boundary=frame";
static const char* kStreamBoundary = "\r\n--frame\r\n";
static const char* kStreamPartHeader = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t root_handler(httpd_req_t* req) {
    const String ip = network_preview_ap_ip();
    String html;
    html.reserve(1200);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>MiRob CAM Preview</title>";
    html += "<style>";
    html += "body{margin:0;background:#10151c;color:#e8eef6;font-family:Arial,sans-serif;}";
    html += "header{padding:14px 16px;background:#1d2937;font-weight:700;}";
    html += "main{padding:16px;}p{margin:8px 0;}img{width:100%;max-width:480px;border-radius:10px;border:1px solid #3a4a5e;}";
    html += ".tip{font-size:12px;opacity:0.8;}";
    html += "</style></head><body>";
    html += "<header>MiRob CAM - Realtime Preview</header>";
    html += "<main><p>Mode4 (preview) is active.</p>";
    html += "<p>AP IP: <b>";
    html += ip;
    html += "</b></p>";
    html += "<img src='/stream' alt='preview'>";
    html += "<p class='tip'>IO3: mode1/2/3/4 cycle | IO21: capture in mode1/mode2/mode3</p>";
    html += "</main></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html.c_str(), html.length());
}

static esp_err_t stream_handler(httpd_req_t* req) {
    if (!camera_ok()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not ready");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_set_type(req, kStreamContentType);
    if (res != ESP_OK) {
        return res;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (s_streamEnabled) {
        camera_fb_t* fb = camera_capture_frame();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(CAM_PREVIEW_FRAME_DELAY_MS));
            continue;
        }

        char header[64];
        size_t headerLen = (size_t)snprintf(header, sizeof(header), kStreamPartHeader, (unsigned)fb->len);

        res = httpd_resp_send_chunk(req, kStreamBoundary, strlen(kStreamBoundary));
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, header, headerLen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(fb->buf), fb->len);
        }

        camera_return_frame(fb);
        if (res != ESP_OK) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(CAM_PREVIEW_FRAME_DELAY_MS));
    }

    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

bool http_server_start() {
    if (s_httpServer) {
        s_streamEnabled = true;
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&s_httpServer, &config);
    if (err != ESP_OK || !s_httpServer) {
        Serial.printf("HTTP: start failed err=0x%x\n", (unsigned)err);
        s_httpServer = nullptr;
        s_streamEnabled = false;
        return false;
    }

    httpd_uri_t rootUri = {};
    rootUri.uri = "/";
    rootUri.method = HTTP_GET;
    rootUri.handler = root_handler;
    rootUri.user_ctx = nullptr;
    httpd_register_uri_handler(s_httpServer, &rootUri);

    httpd_uri_t streamUri = {};
    streamUri.uri = "/stream";
    streamUri.method = HTTP_GET;
    streamUri.handler = stream_handler;
    streamUri.user_ctx = nullptr;
    httpd_register_uri_handler(s_httpServer, &streamUri);

    s_streamEnabled = true;
    Serial.println("HTTP: preview server started");
    return true;
}

void http_server_stop() {
    s_streamEnabled = false;
    if (!s_httpServer) {
        return;
    }

    httpd_stop(s_httpServer);
    s_httpServer = nullptr;
    Serial.println("HTTP: preview server stopped");
}

bool http_server_running() {
    return (s_httpServer != nullptr);
}

void http_server_init() {
    (void)http_server_start();
}

void http_server_handle_client() {
    // Not used by esp_http_server (event-driven).
}

void http_server_set_mode(uint8_t mode) {
    if (mode == 4) {
        (void)http_server_start();
    } else {
        http_server_stop();
    }
}

uint8_t http_server_get_mode() {
    return http_server_running() ? 4 : 1;
}

void http_server_set_mode_change_handler(void (*handler)(uint8_t mode)) {
    (void)handler;
}
