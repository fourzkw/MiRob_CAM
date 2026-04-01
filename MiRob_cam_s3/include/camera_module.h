#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include <esp_camera.h>

bool camera_init();
bool camera_ok();

// Apply fixed-manual capture profile (high resolution, save-to-SD tuning).
bool camera_apply_photo_profile();

// Apply auto-capture profile (high resolution, full auto exposure/gain/AWB).
bool camera_apply_auto_photo_profile();

// Apply recording profile (video mode with independent frame size/quality).
bool camera_apply_record_profile();

// Apply preview profile (low resolution, web stream tuning).
bool camera_apply_preview_profile();

// Capture one JPEG frame from the camera.
camera_fb_t* camera_capture_frame();

// Return the frame buffer acquired by camera_capture_frame().
void camera_return_frame(camera_fb_t* fb);

#endif // CAMERA_MODULE_H
