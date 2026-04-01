#include <Arduino.h>
#include <esp_camera.h>

#include "config.h"
#include "camera_module.h"

// AI-Thinker pin map (compatible with OV3660 wiring)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

static bool s_camOk = false;
static uint8_t s_deviceMode = 1; // 1=stream, 2=photo-only

enum CameraProfile : uint8_t {
    CAMERA_PROFILE_STREAM = 0,
    CAMERA_PROFILE_PHOTO = 1,
};

// Preview profile: reduce payload for smoother refresh.
static const framesize_t STREAM_FRAME_SIZE = FRAMESIZE_QVGA;   // 320x240
static const int STREAM_JPEG_QUALITY = 35;                     // higher value => smaller file

// Photo profile for different device modes:
// Mode 1 (preview + photo): keep moderate resolution.
static const framesize_t PHOTO_FRAME_SIZE_MODE1 = FRAMESIZE_VGA;   // 640x480
// Mode 2 (photo-only): use higher UXGA resolution.
static const framesize_t PHOTO_FRAME_SIZE_MODE2 = FRAMESIZE_UXGA;  // 1600x1200
// Initialize camera buffers with the largest frame size we will use at runtime.
static const framesize_t CAMERA_INIT_FRAME_SIZE = PHOTO_FRAME_SIZE_MODE2;

static CameraProfile s_profile = CAMERA_PROFILE_PHOTO;

static framesize_t get_photo_frame_size_for_mode() {
    if (s_deviceMode == 2) {
        return PHOTO_FRAME_SIZE_MODE2;
    }
    return PHOTO_FRAME_SIZE_MODE1;
}

static bool apply_profile(CameraProfile profile) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        return false;
    }

    int frameRc = 0;
    int qualityRc = 0;
    if (profile == CAMERA_PROFILE_STREAM) {
        frameRc = s->set_framesize(s, STREAM_FRAME_SIZE);
        qualityRc = s->set_quality(s, STREAM_JPEG_QUALITY);
    } else {
        frameRc = s->set_framesize(s, get_photo_frame_size_for_mode());
        qualityRc = s->set_quality(s, PHOTO_QUALITY);
    }

    if (frameRc != 0 || qualityRc != 0) {
        Serial.printf(
            "Camera profile apply failed: profile=%u frameRc=%d qualityRc=%d\n",
            (unsigned)profile,
            frameRc,
            qualityRc
        );
        return false;
    }

    s_profile = profile;
    return true;
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
    config.grab_mode = CAMERA_GRAB_LATEST;

#if defined(BOARD_HAS_PSRAM) || defined(CAMERA_MODEL_AI_THINKER)
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
#else
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
#endif

    config.frame_size = CAMERA_INIT_FRAME_SIZE;
    config.jpeg_quality = PHOTO_QUALITY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        s_camOk = false;
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_hmirror(s, 1);
        s->set_vflip(s, 0);
    }

    s_camOk = true;
    camera_set_idle_profile_for_mode();
    return true;
}

bool camera_ok() {
    return s_camOk;
}

void camera_set_device_mode(uint8_t mode) {
    // Normalize to 1 or 2, default to 1 if invalid
    if (mode != 1 && mode != 2) {
        mode = 1;
    }
    s_deviceMode = mode;

    // 如果当前已经处于拍照 profile，下次切模式后重新应用一次拍照 profile 生效。
    if (s_camOk && s_profile == CAMERA_PROFILE_PHOTO) {
        apply_profile(CAMERA_PROFILE_PHOTO);
    }
}

camera_fb_t* camera_capture_frame() {
    if (!s_camOk) {
        return nullptr;
    }
    return esp_camera_fb_get();
}

void camera_return_frame(camera_fb_t* fb) {
    if (!fb) {
        return;
    }
    esp_camera_fb_return(fb);
}

void camera_set_stream_profile() {
    if (!s_camOk || s_profile == CAMERA_PROFILE_STREAM) {
        return;
    }
    apply_profile(CAMERA_PROFILE_STREAM);
}

void camera_set_photo_profile() {
    if (!s_camOk || s_profile == CAMERA_PROFILE_PHOTO) {
        return;
    }
    apply_profile(CAMERA_PROFILE_PHOTO);
}

bool camera_is_photo_profile_active() {
    return s_profile == CAMERA_PROFILE_PHOTO;
}

void camera_set_idle_profile_for_mode() {
    if (!s_camOk) {
        return;
    }

    if (s_deviceMode == 2) {
        camera_set_photo_profile();
    } else {
        camera_set_stream_profile();
    }
}
