#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include <esp_camera.h>

// TFT 实时预览时传感器 AE/AGC/AWB 与哪类场景对齐（在 camera_apply_tft_live_profile 时生效）。
typedef enum {
    CAMERA_TFT_LIVE_AE_PHOTO_MANUAL = 0, // 与 mode1/mode2 拍照（手动曝光/增益）一致
    CAMERA_TFT_LIVE_AE_PHOTO_AUTO = 1,   // 与 mode3 拍照（全自动）一致
    CAMERA_TFT_LIVE_AE_STREAM_AUTO = 2   // 与网页预览/录像待机相同（全自动，高帧收敛）
} camera_tft_live_ae_t;

bool camera_init();
bool camera_ok();

// 设置下一次 TFT live profile 应用的 AE 风格（默认 PHOTO_MANUAL，与开机 mode1 一致）。
void camera_set_tft_live_ae(camera_tft_live_ae_t ae);

// Apply fixed-manual capture profile (high resolution, save-to-SD tuning).
bool camera_apply_photo_profile();

// Apply auto-capture profile (high resolution, full auto exposure/gain/AWB).
bool camera_apply_auto_photo_profile();

// Apply recording profile (video mode with independent frame size/quality).
bool camera_apply_record_profile();

// Apply preview profile (low resolution, web stream tuning).
bool camera_apply_preview_profile();

// JPEG QVGA for SPI TFT live view；AE/AGC/AWB 由 camera_set_tft_live_ae 决定。
bool camera_apply_tft_live_profile();

// Hold camera mutex, switch to JPEG still profile; pair with camera_end_exclusive_capture().
bool camera_begin_exclusive_still_capture(bool useAuto);

// Hold camera mutex, switch to JPEG record profile; pair with camera_end_exclusive_capture().
bool camera_begin_exclusive_video_capture();

// Restore RGB565 TFT live profile and release mutex from begin_exclusive_*.
void camera_end_exclusive_capture();

// Capture one frame (format depends on active profile).
camera_fb_t* camera_capture_frame();

// Return the frame buffer acquired by camera_capture_frame().
void camera_return_frame(camera_fb_t* fb);

#endif // CAMERA_MODULE_H
