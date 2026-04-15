#ifndef STORAGE_MODULE_H
#define STORAGE_MODULE_H

#include <Arduino.h>
#include <FS.h>

bool storage_init();
bool storage_ok();
void storage_poll();

// Check whether capture is allowed now.
// If blocked by minimum interval, waitMs returns remaining ms.
bool storage_capture_ready(uint32_t* waitMs);

// Capture one photo and save it to TF card.
// outPath returns the saved path when successful.
// useAutoCamera selects auto exposure profile (mode3); otherwise manual photo profile.
// If outJpegDup/outJpegDupLen are non-null and save succeeds, a heap copy of the JPEG is returned (caller must free()).
bool storage_capture_and_save(
    String& outPath,
    bool useAutoCamera = false,
    uint8_t** outJpegDup = nullptr,
    size_t* outJpegDupLen = nullptr
);

// Record one MJPEG-in-AVI clip to TF card using VIDEO_* config parameters.
// outPath returns saved file path; outFrames/outDurationMs report actual clip stats.
bool storage_record_video(String& outPath, uint32_t& outFrames, uint32_t& outDurationMs);

File storage_open_photo_dir();
bool storage_file_exists(const String& path);
File storage_open_file(const String& path);

#endif // STORAGE_MODULE_H
