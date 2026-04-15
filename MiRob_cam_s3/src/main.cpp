#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "config.h"
#include "camera_module.h"
#include "storage_module.h"
#include "log_module.h"
#include "display_module.h"
#include "network_module.h"
#include "http_server_module.h"

enum AppMode : uint8_t {
    APP_MODE_CAPTURE_NO_LIGHT = 1,
    APP_MODE_CAPTURE_FILL_LIGHT = 2,
    APP_MODE_CAPTURE_AUTO = 3,
    APP_MODE_PREVIEW = 4,
    APP_MODE_RECORD = 5
};

enum InputEventType : uint8_t {
    INPUT_EVENT_CAPTURE = 1,
    INPUT_EVENT_TOGGLE_MODE = 2
};

enum CaptureSource : uint8_t {
    CAPTURE_SOURCE_BUTTON = 1,
    CAPTURE_SOURCE_SERIAL = 2
};

enum CaptureRequestType : uint8_t {
    CAPTURE_REQUEST_PHOTO = 1,
    CAPTURE_REQUEST_VIDEO = 2
};

enum NetworkCommandType : uint8_t {
    NETWORK_CMD_START_PREVIEW = 1,
    NETWORK_CMD_STOP_PREVIEW = 2
};

struct CaptureRequest {
    CaptureSource source;
    CaptureRequestType type;
    bool useFillLight;
    bool useAutoCamera;
};

struct NetworkCommand {
    NetworkCommandType type;
};

struct NetworkResult {
    NetworkCommandType type;
    bool ok;
    char ip[20];
};

static QueueHandle_t s_inputEventQueue = nullptr;
static QueueHandle_t s_captureRequestQueue = nullptr;
static QueueHandle_t s_networkCommandQueue = nullptr;
static QueueHandle_t s_networkResultQueue = nullptr;
static volatile AppMode s_currentMode = APP_MODE_CAPTURE_NO_LIGHT;
static volatile bool s_recording = false;
static Adafruit_NeoPixel s_modeLed(
    MODE_WS2812_LED_COUNT,
    PIN_MODE_WS2812,
    NEO_GRB + NEO_KHZ800
);
static bool s_modeLedReady = false;

static uint8_t fillLightOnLevel() {
    return (FILL_LIGHT_ACTIVE == HIGH) ? HIGH : LOW;
}

static uint8_t fillLightOffLevel() {
    return (FILL_LIGHT_ACTIVE == HIGH) ? LOW : HIGH;
}

static void setFillLight(bool on) {
    if (PIN_FILL_LIGHT < 0) {
        return;
    }
    digitalWrite(PIN_FILL_LIGHT, on ? fillLightOnLevel() : fillLightOffLevel());
}

static bool modeIsPreview(AppMode mode) {
    return mode == APP_MODE_PREVIEW;
}

static bool modeIsRecord(AppMode mode) {
    return mode == APP_MODE_RECORD;
}

static bool modeIsPhotoCapture(AppMode mode) {
    return mode == APP_MODE_CAPTURE_NO_LIGHT ||
           mode == APP_MODE_CAPTURE_FILL_LIGHT ||
           mode == APP_MODE_CAPTURE_AUTO;
}

static bool modeIsCapture(AppMode mode) {
    return modeIsPhotoCapture(mode) || modeIsRecord(mode);
}

static bool modeUsesFillLight(AppMode mode) {
    if (mode == APP_MODE_CAPTURE_FILL_LIGHT) {
        return true;
    }
    if (mode == APP_MODE_RECORD) {
        return (VIDEO_USE_FILL_LIGHT != 0);
    }
    return false;
}

static bool modeUsesAutoCamera(AppMode mode) {
    return mode == APP_MODE_CAPTURE_AUTO;
}

static uint32_t modeLedColor(AppMode mode) {
    if (mode == APP_MODE_CAPTURE_NO_LIGHT) {
        return s_modeLed.Color(0, 0, 255); // blue
    }
    if (mode == APP_MODE_CAPTURE_FILL_LIGHT) {
        return s_modeLed.Color(255, 140, 0); // orange
    }
    if (mode == APP_MODE_CAPTURE_AUTO) {
        return s_modeLed.Color(0, 255, 255); // cyan
    }
    if (mode == APP_MODE_RECORD) {
        return s_modeLed.Color(255, 0, 0); // red
    }
    return s_modeLed.Color(0, 255, 0); // green
}

static uint32_t recordingSignalColor() {
    return s_modeLed.Color(
        RECORD_SIGNAL_COLOR_R,
        RECORD_SIGNAL_COLOR_G,
        RECORD_SIGNAL_COLOR_B
    );
}

static void setModeLedRaw(uint32_t color) {
    if (!s_modeLedReady || MODE_WS2812_LED_COUNT <= 0) {
        return;
    }
    s_modeLed.setPixelColor(0, color);
    s_modeLed.show();
}

static void setModeLed(AppMode mode) {
    setModeLedRaw(modeLedColor(mode));
}

static const char* modeName(AppMode mode) {
    if (mode == APP_MODE_CAPTURE_NO_LIGHT) {
        return "Mode1-Capture(NoLight)";
    }
    if (mode == APP_MODE_CAPTURE_FILL_LIGHT) {
        return "Mode2-Capture(FillLight)";
    }
    if (mode == APP_MODE_CAPTURE_AUTO) {
        return "Mode3-Capture(Auto)";
    }
    if (mode == APP_MODE_RECORD) {
        return "Mode5-Record";
    }
    return "Mode4-Preview";
}

static const char* modeTftLabel(AppMode mode) {
    if (mode == APP_MODE_CAPTURE_NO_LIGHT) {
        return "Mode1 Photo";
    }
    if (mode == APP_MODE_CAPTURE_FILL_LIGHT) {
        return "Mode2 Photo+Light";
    }
    if (mode == APP_MODE_CAPTURE_AUTO) {
        return "Mode3 Auto";
    }
    if (mode == APP_MODE_RECORD) {
        return "Mode5 Record";
    }
    return "Mode4 Preview";
}

static AppMode currentMode() {
    return s_currentMode;
}

static void setMode(AppMode mode) {
    s_currentMode = mode;
    setModeLed(mode);
    display_set_mode_overlay_text(modeTftLabel(mode));
}

static void printHelp() {
    Serial.println("Serial commands:");
    Serial.println("  snap/cap/capture -> photo(mode1/2/3) or video(mode5)");
    Serial.println("  rec/record       -> record video (mode5)");
    Serial.println("  mode1            -> capture without fill light");
    Serial.println("  mode2            -> capture with fill light");
    Serial.println("  mode3            -> auto capture (auto exposure/gain)");
    Serial.println("  mode4            -> web preview mode");
    Serial.println("  mode5            -> record video mode");
    Serial.println("  tfttest          -> TFT solid JPEG test (RGB888->JPEG->decode)");
    Serial.println("  tfttest off      -> exit TFT JPEG test, resume camera live");
    Serial.println("  mode             -> print current mode");
    Serial.println("  help             -> print this help");
    Serial.println("Button IO3:");
    Serial.println("  short press      -> switch mode (1->2->3->4->5->1)");
    Serial.println("Button IO21:");
    Serial.println("  short press      -> photo(mode1/2/3) or video(mode5)");
    Serial.println("Mode LED WS2812 IO20:");
    Serial.println("  mode1=blue mode2=orange mode3=cyan mode4=green mode5=red");
}

static const char* captureSourceTag(CaptureSource source) {
    return (source == CAPTURE_SOURCE_SERIAL) ? "[SERIAL]" : "[BTN]";
}

static void capturePhotoWithLog(const char* sourceTag, bool useAutoCamera) {
    String path;
    uint8_t* jpegDup = nullptr;
    size_t jpegDupLen = 0;
    if (storage_capture_and_save(path, useAutoCamera, &jpegDup, &jpegDupLen)) {
        Serial.println(String(sourceTag) + " capture ok: " + path);
        log_append(String(sourceTag) + " capture ok: " + path);
        if (jpegDup && jpegDupLen > 0) {
            display_queue_post_capture_jpeg(jpegDup, jpegDupLen, TFT_CAPTURE_REVIEW_HOLD_MS);
        } else {
            display_show_capture(true, path);
        }
    } else {
        Serial.println(String(sourceTag) + " capture failed");
        log_append(String(sourceTag) + " capture failed");
        display_show_capture(false, String(sourceTag) + " capture failed");
    }
}

static void captureVideoWithLog(const char* sourceTag) {
    String path;
    uint32_t frames = 0;
    uint32_t durationMs = 0;
    if (storage_record_video(path, frames, durationMs)) {
        String detail = path + " " + String(frames) + "f/" + String(durationMs) + "ms";
        Serial.println(String(sourceTag) + " record ok: " + detail);
        log_append(String(sourceTag) + " record ok: " + detail);
        display_show_capture(true, detail);
    } else {
        Serial.println(String(sourceTag) + " record failed");
        log_append(String(sourceTag) + " record failed");
        display_show_capture(false, String(sourceTag) + " record failed");
    }
}

static bool requestCaptureOrRecord(CaptureSource source) {
    AppMode mode = currentMode();
    if (modeIsPreview(mode)) {
        Serial.println("Action ignored: switch to mode1/mode2/mode3/mode5 first");
        return false;
    }

    if (s_recording) {
        Serial.println("Record in progress, request ignored");
        return false;
    }

    if (!s_captureRequestQueue) {
        Serial.println("Capture queue unavailable");
        return false;
    }

    CaptureRequest req = {};
    req.source = source;
    req.type = modeIsRecord(mode) ? CAPTURE_REQUEST_VIDEO : CAPTURE_REQUEST_PHOTO;
    req.useFillLight = modeUsesFillLight(mode);
    req.useAutoCamera = modeUsesAutoCamera(mode);
    if (xQueueSend(s_captureRequestQueue, &req, 0) != pdTRUE) {
        Serial.println("Capture queue full, request dropped");
        return false;
    }
    return true;
}

static bool sendNetworkCommandAndWait(NetworkCommandType type, NetworkResult& outResult, uint32_t timeoutMs) {
    if (!s_networkCommandQueue || !s_networkResultQueue) {
        return false;
    }

    NetworkCommand cmd = {};
    cmd.type = type;
    if (xQueueSend(s_networkCommandQueue, &cmd, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }

    NetworkResult result = {};
    if (xQueueReceive(s_networkResultQueue, &result, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }

    if (result.type != type) {
        return false;
    }

    outResult = result;
    return true;
}

static bool applyCameraProfileForMode(AppMode mode) {
    if (mode == APP_MODE_PREVIEW) {
        return camera_apply_preview_profile();
    }
    if (modeIsRecord(mode)) {
        camera_set_tft_live_ae(CAMERA_TFT_LIVE_AE_STREAM_AUTO);
    } else if (modeUsesAutoCamera(mode)) {
        camera_set_tft_live_ae(CAMERA_TFT_LIVE_AE_PHOTO_AUTO);
    } else {
        camera_set_tft_live_ae(CAMERA_TFT_LIVE_AE_PHOTO_MANUAL);
    }
    return camera_apply_tft_live_profile();
}

static bool switchToCaptureMode(AppMode targetMode, const char* switchingLine) {
    if (!modeIsCapture(targetMode)) {
        return false;
    }
    const AppMode fromMode = currentMode();
    if (fromMode == targetMode) {
        return true;
    }

    display_show_boot("Switching...", switchingLine);

    if (fromMode == APP_MODE_PREVIEW) {
        NetworkResult stopResult = {};
        if (!sendNetworkCommandAndWait(NETWORK_CMD_STOP_PREVIEW, stopResult, 2000)) {
            Serial.println("Capture mode switch warning: network stop timeout");
            log_append("Capture mode switch warning: network stop timeout");
        }
    }

    camera_set_tft_live_ae(
        modeUsesAutoCamera(targetMode) ? CAMERA_TFT_LIVE_AE_PHOTO_AUTO : CAMERA_TFT_LIVE_AE_PHOTO_MANUAL
    );
    const bool needCameraProfileUpdate =
        (fromMode == APP_MODE_PREVIEW) ||
        (fromMode == APP_MODE_RECORD) ||
        (modeUsesAutoCamera(fromMode) != modeUsesAutoCamera(targetMode));
    if (needCameraProfileUpdate && !camera_apply_tft_live_profile()) {
        Serial.println("Mode switch failed: camera TFT live profile");
        log_append("Mode switch failed: camera TFT live profile");
        display_show_boot("Capture switch FAIL", "Camera profile");
        return false;
    }

    setFillLight(false);
    setMode(targetMode);

    String modeText = String(modeName(targetMode));
    Serial.println(String("Switched -> ") + modeText);
    log_append(String("Switched -> ") + modeText);
    if (targetMode == APP_MODE_CAPTURE_NO_LIGHT) {
        display_show_boot("Mode1: Capture", "No fill | IO21=snap");
    } else if (targetMode == APP_MODE_CAPTURE_FILL_LIGHT) {
        display_show_boot("Mode2: Capture", "Fill on snap | IO21=snap");
    } else {
        display_show_boot("Mode3: Capture", "Pure auto | IO21=snap");
    }
    return true;
}

static bool switchToCaptureNoLightMode() {
    return switchToCaptureMode(APP_MODE_CAPTURE_NO_LIGHT, "Mode1 Capture");
}

static bool switchToCaptureFillLightMode() {
    return switchToCaptureMode(APP_MODE_CAPTURE_FILL_LIGHT, "Mode2 Capture");
}

static bool switchToCaptureAutoMode() {
    return switchToCaptureMode(APP_MODE_CAPTURE_AUTO, "Mode3 Auto Capture");
}

static bool switchToRecordMode() {
    const AppMode fromMode = currentMode();
    if (fromMode == APP_MODE_RECORD) {
        return true;
    }

    display_show_boot("Switching...", "Mode5 Record");

    if (fromMode == APP_MODE_PREVIEW) {
        NetworkResult stopResult = {};
        if (!sendNetworkCommandAndWait(NETWORK_CMD_STOP_PREVIEW, stopResult, 2000)) {
            Serial.println("Record mode switch warning: network stop timeout");
            log_append("Record mode switch warning: network stop timeout");
        }
    }

    camera_set_tft_live_ae(CAMERA_TFT_LIVE_AE_STREAM_AUTO);
    if (!camera_apply_tft_live_profile()) {
        Serial.println("Mode switch failed: camera TFT live profile");
        log_append("Mode switch failed: camera TFT live profile");
        display_show_boot("Mode5 switch FAIL", "Camera profile");
        return false;
    }

    setFillLight(false);
    setMode(APP_MODE_RECORD);
    Serial.println("Switched -> Mode5 Record");
    log_append("Switched -> Mode5 Record");
    display_show_boot("Mode5: Record", "IO21=start clip");
    return true;
}

static bool switchToPreviewMode() {
    const AppMode fromMode = currentMode();
    if (fromMode == APP_MODE_PREVIEW) {
        return true;
    }

    display_show_boot("Switching...", "Mode4 Preview");

    if (!camera_apply_preview_profile()) {
        Serial.println("Mode switch failed: camera preview profile");
        log_append("Mode switch failed: camera preview profile");
        display_show_boot("Mode4 switch FAIL", "Camera profile");
        return false;
    }

    NetworkResult startResult = {};
    if (!sendNetworkCommandAndWait(NETWORK_CMD_START_PREVIEW, startResult, 3000) || !startResult.ok) {
        Serial.println("Mode switch failed: network start");
        log_append("Mode switch failed: network start");
        (void)applyCameraProfileForMode(fromMode);
        display_show_boot("Mode4 switch FAIL", "Network start");
        return false;
    }

    setFillLight(false);
    setMode(APP_MODE_PREVIEW);
    String previewUrl = String("http://") + String(startResult.ip) + "/";
    Serial.println("Switched -> Mode4 Preview");
    Serial.println(String("Preview URL: ") + previewUrl);
    log_append("Switched -> Mode4 Preview");
    log_append(String("Preview URL: ") + previewUrl);
    display_show_boot("Mode4: Web Preview", previewUrl.c_str());
    return true;
}

static void toggleMode() {
    AppMode mode = currentMode();
    if (mode == APP_MODE_CAPTURE_NO_LIGHT) {
        (void)switchToCaptureFillLightMode();
    } else if (mode == APP_MODE_CAPTURE_FILL_LIGHT) {
        (void)switchToCaptureAutoMode();
    } else if (mode == APP_MODE_CAPTURE_AUTO) {
        (void)switchToPreviewMode();
    } else if (mode == APP_MODE_PREVIEW) {
        (void)switchToRecordMode();
    } else {
        (void)switchToCaptureNoLightMode();
    }
}

static void handleSerialCommands() {
    static String line;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            String cmd = line;
            line = "";
            cmd.trim();
            if (cmd.length() == 0) {
                continue;
            }

            String lower = cmd;
            lower.toLowerCase();

            if (lower == "help" || lower == "h" || lower == "?") {
                printHelp();
                continue;
            }

            if (lower == "mode") {
                Serial.printf("Current mode: %s\n", modeName(currentMode()));
                continue;
            }

            if (lower == "mode1") {
                (void)switchToCaptureNoLightMode();
                continue;
            }

            if (lower == "mode2") {
                (void)switchToCaptureFillLightMode();
                continue;
            }

            if (lower == "mode3") {
                (void)switchToCaptureAutoMode();
                continue;
            }

            if (lower == "mode4") {
                (void)switchToPreviewMode();
                continue;
            }

            if (lower == "mode5") {
                (void)switchToRecordMode();
                continue;
            }

            if (lower == "tfttest off" || lower == "tfttest_off") {
                display_set_jpeg_solid_test(false);
                Serial.println("TFT JPEG solid test: OFF");
                continue;
            }
            if (lower == "tfttest" || lower == "tfttest on" || lower == "tfttest_on") {
                display_set_jpeg_solid_test(true);
                Serial.println("TFT JPEG solid test: ON (red/green/blue/white/black cycle)");
                continue;
            }

            if (lower == "rec" || lower == "record") {
                if (currentMode() != APP_MODE_RECORD && !switchToRecordMode()) {
                    continue;
                }
                (void)requestCaptureOrRecord(CAPTURE_SOURCE_SERIAL);
                continue;
            }

            if (lower == "snap" || lower == "cap" || lower == "capture") {
                (void)requestCaptureOrRecord(CAPTURE_SOURCE_SERIAL);
                continue;
            }

            Serial.println("Unknown command. Type: help");
        } else {
            if (line.length() < 64) {
                line += c;
            }
        }
    }
}

static void handleInputEvent(InputEventType eventType) {
    if (eventType == INPUT_EVENT_CAPTURE) {
        (void)requestCaptureOrRecord(CAPTURE_SOURCE_BUTTON);
        return;
    }

    if (eventType == INPUT_EVENT_TOGGLE_MODE) {
        toggleMode();
    }
}

static bool readButtonPressed(int pin, bool activeLow) {
    return activeLow ? (digitalRead(pin) == LOW) : (digitalRead(pin) == HIGH);
}

static void processButtonState(
    bool rawPressed,
    bool& lastRawPressed,
    bool& stablePressed,
    uint32_t& lastRawChangeMs,
    InputEventType eventType,
    bool triggerOnPress
) {
    const uint32_t nowMs = millis();
    if (rawPressed != lastRawPressed) {
        lastRawPressed = rawPressed;
        lastRawChangeMs = nowMs;
    }

    if ((nowMs - lastRawChangeMs) >= CAPTURE_BUTTON_DEBOUNCE_MS &&
        stablePressed != lastRawPressed) {
        stablePressed = lastRawPressed;
        const bool shouldEmit = triggerOnPress ? stablePressed : !stablePressed;
        if (shouldEmit && s_inputEventQueue) {
            (void)xQueueSend(s_inputEventQueue, &eventType, 0);
        }
    }
}

static void buttonTask(void* arg) {
    (void)arg;

    bool modeLastRawPressed = false;
    bool modeStablePressed = false;
    uint32_t modeLastRawChangeMs = 0;

    bool captureLastRawPressed = false;
    bool captureStablePressed = false;
    uint32_t captureLastRawChangeMs = 0;

    for (;;) {
        processButtonState(
            readButtonPressed(PIN_MODE_BUTTON, MODE_BUTTON_ACTIVE_LOW != 0),
            modeLastRawPressed,
            modeStablePressed,
            modeLastRawChangeMs,
            INPUT_EVENT_TOGGLE_MODE,
            false
        );

        processButtonState(
            readButtonPressed(PIN_CAPTURE_BUTTON, CAPTURE_BUTTON_ACTIVE_LOW != 0),
            captureLastRawPressed,
            captureStablePressed,
            captureLastRawChangeMs,
            INPUT_EVENT_CAPTURE,
            true
        );

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void recordingSignalTask(void* arg) {
    (void)arg;

    const uint32_t blinkMs = (RECORD_SIGNAL_BLINK_MS > 0) ? RECORD_SIGNAL_BLINK_MS : 180;
    bool blinkOn = false;
    bool wasRecording = false;

    for (;;) {
        if (s_recording) {
            wasRecording = true;
            blinkOn = !blinkOn;
            setModeLedRaw(blinkOn ? recordingSignalColor() : 0);
            vTaskDelay(pdMS_TO_TICKS(blinkMs));
            continue;
        }

        if (wasRecording) {
            setModeLed(currentMode());
            wasRecording = false;
            blinkOn = false;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

static void captureTask(void* arg) {
    (void)arg;

    for (;;) {
        CaptureRequest req = {};
        if (!s_captureRequestQueue || xQueueReceive(s_captureRequestQueue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (req.type == CAPTURE_REQUEST_VIDEO) {
            s_recording = true;
            if (req.useFillLight) {
                setFillLight(true);
            }
            if (req.useFillLight && VIDEO_FILL_LIGHT_WARMUP_MS > 0) {
                vTaskDelay(pdMS_TO_TICKS(VIDEO_FILL_LIGHT_WARMUP_MS));
            }
            captureVideoWithLog(captureSourceTag(req.source));
            if (req.useFillLight) {
                setFillLight(false);
            }
            s_recording = false;
            setModeLed(currentMode());
            continue;
        }

        if (req.useFillLight) {
            setFillLight(true);
        }
        if (req.useFillLight && FILL_LIGHT_WARMUP_MS > 0) {
            vTaskDelay(pdMS_TO_TICKS(FILL_LIGHT_WARMUP_MS));
        }
        capturePhotoWithLog(captureSourceTag(req.source), req.useAutoCamera);
        if (req.useFillLight) {
            setFillLight(false);
        }
    }
}

static void networkTask(void* arg) {
    (void)arg;

    for (;;) {
        NetworkCommand cmd = {};
        if (!s_networkCommandQueue || xQueueReceive(s_networkCommandQueue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        NetworkResult result = {};
        result.type = cmd.type;
        result.ok = false;
        String ip = "0.0.0.0";

        if (cmd.type == NETWORK_CMD_START_PREVIEW) {
            bool apOk = network_start_preview_ap();
            bool httpOk = false;
            if (apOk) {
                httpOk = http_server_start();
            }
            if (!(apOk && httpOk)) {
                http_server_stop();
                network_stop_preview_ap();
            }
            result.ok = apOk && httpOk;
            ip = network_preview_ap_ip();
            if (result.ok) {
                Serial.println(String("Network online: http://") + ip + "/");
            } else {
                Serial.println("Network start failed");
            }
        } else if (cmd.type == NETWORK_CMD_STOP_PREVIEW) {
            http_server_stop();
            network_stop_preview_ap();
            result.ok = true;
            ip = "0.0.0.0";
            Serial.println("Network offline");
        }

        ip.toCharArray(result.ip, sizeof(result.ip));
        if (s_networkResultQueue) {
            (void)xQueueSend(s_networkResultQueue, &result, 0);
        }
    }
}

static void controlTask(void* arg) {
    (void)arg;

    for (;;) {
        storage_poll();
        display_set_tf_unavailable(!storage_ok());
        handleSerialCommands();

        InputEventType ev = INPUT_EVENT_CAPTURE;
        while (s_inputEventQueue && xQueueReceive(s_inputEventQueue, &ev, 0) == pdTRUE) {
            handleInputEvent(ev);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void keepCameraLiveDuringReviewHold() {
    static uint32_t s_nextKeepaliveMs = 0;

    const uint32_t intervalMs =
        (TFT_CAPTURE_REVIEW_KEEPALIVE_MS > 0) ? static_cast<uint32_t>(TFT_CAPTURE_REVIEW_KEEPALIVE_MS) : 0U;
    const uint32_t remainingMs = display_capture_review_remaining_ms();
    if (intervalMs == 0 || remainingMs == 0 || !camera_ok() || currentMode() == APP_MODE_PREVIEW || s_recording) {
        s_nextKeepaliveMs = 0;
        return;
    }

    const uint32_t now = millis();
    if (s_nextKeepaliveMs != 0 && (int32_t)(now - s_nextKeepaliveMs) < 0) {
        return;
    }

    camera_fb_t* fb = camera_capture_frame();
    if (fb) {
        camera_return_frame(fb);
    }
    s_nextKeepaliveMs = now + intervalMs;
}

static void tftPreviewTask(void* arg) {
    (void)arg;

    for (;;) {
        if (!display_ok()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (display_jpeg_solid_test_tick()) {
            vTaskDelay(pdMS_TO_TICKS(TFT_LIVE_REFRESH_MIN_MS));
            continue;
        }
        if (display_try_show_pending_capture_review()) {
            vTaskDelay(pdMS_TO_TICKS(TFT_LIVE_REFRESH_MIN_MS));
            continue;
        }
        if (display_capture_review_hold_active()) {
            keepCameraLiveDuringReviewHold();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!camera_ok()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (currentMode() == APP_MODE_PREVIEW) {
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }
        if (s_recording) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        camera_fb_t* fb = camera_capture_frame();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        if (fb->format == PIXFORMAT_JPEG || fb->format == PIXFORMAT_RGB565) {
            display_draw_camera_live(fb);
            display_draw_live_status_overlays();
            display_draw_mode_overlay();
        }
        camera_return_frame(fb);
        vTaskDelay(pdMS_TO_TICKS(TFT_LIVE_REFRESH_MIN_MS));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\nMiRob CAM %s + TF\n", CAMERA_MODEL_NAME);

    display_init();
#if (TFT_JPEG_SOLID_TEST_AT_BOOT != 0)
    display_set_jpeg_solid_test(true);
#endif
    display_show_boot("Booting...", "Init camera");

    log_init();
    log_append(String("Booting MiRob CAM ") + CAMERA_MODEL_NAME + " + TF");

    pinMode(PIN_MODE_BUTTON, MODE_BUTTON_USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT_PULLDOWN);
    pinMode(PIN_CAPTURE_BUTTON, CAPTURE_BUTTON_USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT_PULLDOWN);
    if (PIN_FILL_LIGHT >= 0) {
        pinMode(PIN_FILL_LIGHT, OUTPUT);
        setFillLight(false);
    }
    if (PIN_MODE_WS2812 >= 0 && MODE_WS2812_LED_COUNT > 0) {
        s_modeLed.begin();
        s_modeLed.clear();
        s_modeLed.setBrightness(MODE_WS2812_BRIGHTNESS);
        s_modeLed.show();
        s_modeLedReady = true;
    }

    Serial.println("Init: camera_init start");
    if (!camera_init()) {
        log_append("Camera init fail");
        Serial.println("Init: camera_init FAILED");
        display_show_boot("Camera init FAIL", "Init storage");
    } else {
        display_show_boot("Camera init OK", "Init storage");
        camera_set_tft_live_ae(CAMERA_TFT_LIVE_AE_PHOTO_MANUAL);
        if (!camera_apply_tft_live_profile()) {
            Serial.println("Init: TFT live camera profile failed");
            log_append("TFT live camera profile failed");
        }
    }
    Serial.println("Init: camera_init done");

    Serial.println("Init: storage_init start");
    if (!storage_init()) {
        log_append("SD init fail");
        Serial.println("Init: storage_init FAILED");
        display_set_tf_unavailable(true);
    } else {
        display_set_tf_unavailable(false);
        display_show_boot("Mode1: Capture", "No fill | IO21=snap");
    }
    Serial.println("Init: storage_init done");

    setMode(APP_MODE_CAPTURE_NO_LIGHT);
    printHelp();

    s_inputEventQueue = xQueueCreate(12, sizeof(InputEventType));
    s_captureRequestQueue = xQueueCreate(4, sizeof(CaptureRequest));
    s_networkCommandQueue = xQueueCreate(4, sizeof(NetworkCommand));
    s_networkResultQueue = xQueueCreate(4, sizeof(NetworkResult));
    if (!s_inputEventQueue || !s_captureRequestQueue || !s_networkCommandQueue || !s_networkResultQueue) {
        Serial.println("Init failed: queue create");
        log_append("Init failed: queue create");
        return;
    }

    xTaskCreatePinnedToCore(buttonTask, "button_task", 3072, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(controlTask, "control_task", 6144, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(recordingSignalTask, "record_led_task", 3072, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(captureTask, "capture_task", 8192, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(tftPreviewTask, "tft_preview", 8192, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(networkTask, "network_task", 6144, nullptr, 1, nullptr, 0);
}

void loop() {
    // Logic runs in FreeRTOS tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
