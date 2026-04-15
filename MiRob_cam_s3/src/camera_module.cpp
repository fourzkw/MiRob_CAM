#include <Arduino.h>
#include <esp_camera.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "camera_module.h"

// Pin map from provided schematic (camera + TF + mode LED board)
// Data lines: D0..D7 -> Y2..Y9 (esp_camera naming convention in this project).
// Camera control: VSYNC/HREF/PCLK/SCCB and RESET.
#define PWDN_GPIO_NUM     45  // pwdn-io45
#define RESET_GPIO_NUM    46  // reset-io46
#define XCLK_GPIO_NUM     9   // mclk-io9

// SCCB (I2C-like) lines
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

// Camera data lines
#define Y2_GPIO_NUM       15  // D0
#define Y3_GPIO_NUM       16  // D1
#define Y4_GPIO_NUM       17  // D2
#define Y5_GPIO_NUM       38  // D3
#define Y6_GPIO_NUM       39  // D4
#define Y7_GPIO_NUM       40  // D5
#define Y8_GPIO_NUM       41  // D6
#define Y9_GPIO_NUM       42  // D7

#define VSYNC_GPIO_NUM    8
#define HREF_GPIO_NUM     1   // href-io1
#define PCLK_GPIO_NUM     2   // pclk-io2

static bool s_camOk = false;
static SemaphoreHandle_t s_camMux = nullptr;
static camera_tft_live_ae_t s_tftLiveAe = CAMERA_TFT_LIVE_AE_PHOTO_MANUAL;

static const uint8_t AWB_WARMUP_FRAMES = 2;
static const uint16_t AWB_WARMUP_DELAY_MS = 40;

enum CameraRuntimeProfile : uint8_t {
    CAMERA_PROFILE_PHOTO_MANUAL = 1,
    CAMERA_PROFILE_PHOTO_AUTO = 2,
    CAMERA_PROFILE_PREVIEW = 3,
    CAMERA_PROFILE_RECORD = 4,
    CAMERA_PROFILE_TFT_LIVE = 5
};

// Caller must hold s_camMux (or call only from camera_init before mux exists — not used).
static void warmupFramesDirect(uint8_t count, uint16_t delayMs) {
    for (uint8_t i = 0; i < count; ++i) {
        camera_fb_t* warmupFb = esp_camera_fb_get();
        if (warmupFb) {
            esp_camera_fb_return(warmupFb);
        }
        if (delayMs > 0) {
            delay(delayMs);
        }
    }
}

static const char* profileName(CameraRuntimeProfile profile) {
    if (profile == CAMERA_PROFILE_PHOTO_MANUAL) {
        return "photo-manual";
    }
    if (profile == CAMERA_PROFILE_PHOTO_AUTO) {
        return "photo-auto";
    }
    if (profile == CAMERA_PROFILE_RECORD) {
        return "record";
    }
    if (profile == CAMERA_PROFILE_TFT_LIVE) {
        return "tft-live";
    }
    return "preview";
}

void camera_set_tft_live_ae(camera_tft_live_ae_t ae) {
    s_tftLiveAe = ae;
}

#if defined(LOCK_AE_AWB) && (LOCK_AE_AWB)
static void applyAePhotoManual(sensor_t* s) {
    s->set_exposure_ctrl(s, 0);
    s->set_aec_value(s, CAM_AEC_VALUE);
    s->set_gain_ctrl(s, 0);
    s->set_agc_gain(s, CAM_AGC_GAIN);
    s->set_whitebal(s, 1);
    warmupFramesDirect(AWB_WARMUP_FRAMES, AWB_WARMUP_DELAY_MS);
}

static void applyAePhotoAuto(sensor_t* s) {
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_whitebal(s, 1);
    warmupFramesDirect(2, 30);
}

static void applyAeStreamAuto(sensor_t* s) {
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_whitebal(s, 1);
    warmupFramesDirect(1, 20);
}
#endif

// Requires s_camMux held (except camera_init first apply — see below).
static bool applyProfileInternal(
    pixformat_t pixformat,
    framesize_t frameSize,
    uint8_t jpegQuality,
    CameraRuntimeProfile profile
) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("Camera: sensor unavailable");
        return false;
    }

    bool ok = true;
    if (s->set_pixformat(s, pixformat) != 0) {
        Serial.printf("Camera: set_pixformat failed (%d)\n", (int)pixformat);
        ok = false;
    }
    if (s->set_framesize(s, frameSize) != 0) {
        Serial.printf("Camera: set_framesize failed (%d)\n", (int)frameSize);
        ok = false;
    }
    if (pixformat == PIXFORMAT_JPEG) {
        if (s->set_quality(s, jpegQuality) != 0) {
            Serial.printf("Camera: set_quality failed (%d)\n", (int)jpegQuality);
            ok = false;
        }
    }

#if defined(LOCK_AE_AWB) && (LOCK_AE_AWB)
    if (profile == CAMERA_PROFILE_PHOTO_MANUAL) {
        applyAePhotoManual(s);
    } else if (profile == CAMERA_PROFILE_PHOTO_AUTO) {
        applyAePhotoAuto(s);
    } else if (profile == CAMERA_PROFILE_RECORD) {
        applyAeStreamAuto(s);
    } else if (profile == CAMERA_PROFILE_PREVIEW) {
        applyAeStreamAuto(s);
    } else if (profile == CAMERA_PROFILE_TFT_LIVE) {
        if (s_tftLiveAe == CAMERA_TFT_LIVE_AE_PHOTO_MANUAL) {
            applyAePhotoManual(s);
        } else if (s_tftLiveAe == CAMERA_TFT_LIVE_AE_PHOTO_AUTO) {
            applyAePhotoAuto(s);
        } else {
            applyAeStreamAuto(s);
        }
    } else {
        applyAeStreamAuto(s);
    }
#endif

    Serial.printf(
        "Camera profile applied: mode=%s pixfmt=%d frame_size=%d quality=%d\n",
        profileName(profile),
        (int)pixformat,
        (int)frameSize,
        (int)jpegQuality
    );
    return ok;
}

// One-time init path: mutex not yet created; do not take s_camMux.
static bool applyProfileInternalInitOnly(
    pixformat_t pixformat,
    framesize_t frameSize,
    uint8_t jpegQuality,
    CameraRuntimeProfile profile
) {
    return applyProfileInternal(pixformat, frameSize, jpegQuality, profile);
}

bool camera_apply_photo_profile() {
    if (!s_camOk || !s_camMux) {
        return false;
    }
    xSemaphoreTakeRecursive(s_camMux, portMAX_DELAY);
    bool ok = applyProfileInternal(PIXFORMAT_JPEG, CAM_PHOTO_FRAME_SIZE, CAM_PHOTO_QUALITY, CAMERA_PROFILE_PHOTO_MANUAL);
    xSemaphoreGiveRecursive(s_camMux);
    return ok;
}

bool camera_apply_auto_photo_profile() {
    if (!s_camOk || !s_camMux) {
        return false;
    }
    xSemaphoreTakeRecursive(s_camMux, portMAX_DELAY);
    bool ok = applyProfileInternal(PIXFORMAT_JPEG, CAM_PHOTO_FRAME_SIZE, CAM_PHOTO_QUALITY, CAMERA_PROFILE_PHOTO_AUTO);
    xSemaphoreGiveRecursive(s_camMux);
    return ok;
}

bool camera_apply_record_profile() {
    if (!s_camOk || !s_camMux) {
        return false;
    }
    xSemaphoreTakeRecursive(s_camMux, portMAX_DELAY);
    bool ok = applyProfileInternal(PIXFORMAT_JPEG, CAM_RECORD_FRAME_SIZE, CAM_RECORD_QUALITY, CAMERA_PROFILE_RECORD);
    xSemaphoreGiveRecursive(s_camMux);
    return ok;
}

bool camera_apply_preview_profile() {
    if (!s_camOk || !s_camMux) {
        return false;
    }
    xSemaphoreTakeRecursive(s_camMux, portMAX_DELAY);
    bool ok = applyProfileInternal(PIXFORMAT_JPEG, CAM_PREVIEW_FRAME_SIZE, CAM_PREVIEW_QUALITY, CAMERA_PROFILE_PREVIEW);
    xSemaphoreGiveRecursive(s_camMux);
    return ok;
}

bool camera_apply_tft_live_profile() {
    if (!s_camOk || !s_camMux) {
        return false;
    }
    xSemaphoreTakeRecursive(s_camMux, portMAX_DELAY);
    // JPEG QVGA: OV3660 等对 RGB565 支持不稳定时仍可预览；TFT 侧用 jpg2rgb565 解码显示。
    bool ok = applyProfileInternal(
        PIXFORMAT_JPEG,
        CAM_TFT_LIVE_FRAME_SIZE,
        CAM_PREVIEW_QUALITY,
        CAMERA_PROFILE_TFT_LIVE
    );
    xSemaphoreGiveRecursive(s_camMux);
    return ok;
}

bool camera_begin_exclusive_still_capture(bool useAuto) {
    if (!s_camOk || !s_camMux) {
        return false;
    }
    xSemaphoreTakeRecursive(s_camMux, portMAX_DELAY);
    const bool ok = useAuto
        ? applyProfileInternal(PIXFORMAT_JPEG, CAM_PHOTO_FRAME_SIZE, CAM_PHOTO_QUALITY, CAMERA_PROFILE_PHOTO_AUTO)
        : applyProfileInternal(PIXFORMAT_JPEG, CAM_PHOTO_FRAME_SIZE, CAM_PHOTO_QUALITY, CAMERA_PROFILE_PHOTO_MANUAL);
    if (!ok) {
        xSemaphoreGiveRecursive(s_camMux);
    }
    return ok;
}

bool camera_begin_exclusive_video_capture() {
    if (!s_camOk || !s_camMux) {
        return false;
    }
    xSemaphoreTakeRecursive(s_camMux, portMAX_DELAY);
    const bool ok = applyProfileInternal(
        PIXFORMAT_JPEG,
        CAM_RECORD_FRAME_SIZE,
        CAM_RECORD_QUALITY,
        CAMERA_PROFILE_RECORD
    );
    if (!ok) {
        xSemaphoreGiveRecursive(s_camMux);
    }
    return ok;
}

void camera_end_exclusive_capture() {
    if (!s_camMux) {
        return;
    }
    (void)applyProfileInternal(
        PIXFORMAT_JPEG,
        CAM_TFT_LIVE_FRAME_SIZE,
        CAM_PREVIEW_QUALITY,
        CAMERA_PROFILE_TFT_LIVE
    );
    xSemaphoreGiveRecursive(s_camMux);
}

bool camera_init() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = CAM_XCLK_FREQ;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    // Prefer PSRAM buffer when available for stable frame allocation.
#if defined(BOARD_HAS_PSRAM) || defined(CAMERA_MODEL_AI_THINKER)
    if (psramFound()) {
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        Serial.println("Camera: Using PSRAM frame buffer");
    } else {
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        Serial.println("Camera: PSRAM not found at runtime, using DRAM buffer");
    }
#else
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
#endif

    // Boot with photo profile defaults.
    config.frame_size = CAM_PHOTO_FRAME_SIZE;
    config.jpeg_quality = CAM_PHOTO_QUALITY;

    Serial.printf(
        "Camera init profile: model=%s frame_size=%d jpeg_quality=%d\n",
        CAMERA_MODEL_NAME,
        (int)CAM_PHOTO_FRAME_SIZE,
        (int)CAM_PHOTO_QUALITY
    );

    Serial.println("Camera: esp_camera_init start...");
    delay(10); // yield for watchdog
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        s_camOk = false;
        return false;
    }
    Serial.println("Camera: esp_camera_init ok");

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        Serial.printf("Camera sensor PID=0x%04x\n", (unsigned)s->id.PID);
        s->set_hmirror(s, 1);
        s->set_vflip(s, 0);
    }

    s_camMux = xSemaphoreCreateRecursiveMutex();
    if (!s_camMux) {
        Serial.println("Camera: mutex create failed");
        s_camOk = false;
        return false;
    }

    s_camOk = true;
    if (!applyProfileInternalInitOnly(
            PIXFORMAT_JPEG,
            CAM_PHOTO_FRAME_SIZE,
            CAM_PHOTO_QUALITY,
            CAMERA_PROFILE_PHOTO_MANUAL
        )) {
        Serial.println("Camera: failed to apply initial photo profile");
        s_camOk = false;
        return false;
    }
    return true;
}

bool camera_ok() {
    return s_camOk;
}

camera_fb_t* camera_capture_frame() {
    if (!s_camOk || !s_camMux) {
        return nullptr;
    }
    if (xSemaphoreTakeRecursive(s_camMux, portMAX_DELAY) != pdTRUE) {
        return nullptr;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        xSemaphoreGiveRecursive(s_camMux);
    }
    return fb;
}

void camera_return_frame(camera_fb_t* fb) {
    if (!fb) {
        return;
    }
    esp_camera_fb_return(fb);
    if (s_camMux) {
        xSemaphoreGiveRecursive(s_camMux);
    }
}
