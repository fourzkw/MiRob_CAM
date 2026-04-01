#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include <esp_camera.h>

bool camera_init();
bool camera_ok();

// Inform camera module about current device mode (1=stream, 2=photo-only).
// Used to select resolution/quality profiles.
void camera_set_device_mode(uint8_t mode);

// Capture one JPEG frame from the camera.
camera_fb_t* camera_capture_frame();

// Return the frame buffer acquired by camera_capture_frame().
void camera_return_frame(camera_fb_t* fb);

// Switch sensor settings for low-latency preview.
void camera_set_stream_profile();

// Switch sensor settings for higher-quality photo capture.
void camera_set_photo_profile();

// Whether current sensor profile is photo profile.
bool camera_is_photo_profile_active();

// Restore idle profile according to device mode.
// Mode 1 -> stream profile, Mode 2 -> photo profile.
void camera_set_idle_profile_for_mode();

#endif // CAMERA_MODULE_H
