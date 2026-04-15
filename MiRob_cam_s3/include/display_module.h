#ifndef DISPLAY_MODULE_H
#define DISPLAY_MODULE_H

#include <Arduino.h>
#include <stddef.h>
#include <esp_camera.h>

bool display_init();
bool display_ok();

void display_show_boot(const char* line1, const char* line2);
void display_show_capture(bool ok, const String& detail);

// Live view from RGB565 camera frame, aspect-fit into the TFT content area.
void display_draw_rgb565_live(const camera_fb_t* fb);

// Live view: JPEG (decode) or RGB565, aspect-fit into the TFT content area.
void display_draw_camera_live(const camera_fb_t* fb);

// When true, live preview draws a small corner hint (TF missing / init fail).
void display_set_tf_unavailable(bool unavailable);

// Call after display_draw_camera_live to draw corner status (e.g. TF error).
void display_draw_live_status_overlays();

// Short mode line on live view / review (updated when app mode changes).
void display_set_mode_overlay_text(const char* text);
void display_draw_mode_overlay();

// Queue JPEG for TFT task: decodes once, shows aspect-fit review, holds live preview for holdMs.
void display_queue_post_capture_jpeg(uint8_t* jpegBuf, size_t jpegLen, uint32_t holdMs);
bool display_try_show_pending_capture_review();
bool display_capture_review_hold_active();
uint32_t display_capture_review_remaining_ms();

// Solid-color JPEG test path for TFT validation.
void display_set_jpeg_solid_test(bool enabled);
bool display_jpeg_solid_test_enabled();

// Called from the TFT refresh task. Returns true when this cycle is consumed.
bool display_jpeg_solid_test_tick();

#endif // DISPLAY_MODULE_H
