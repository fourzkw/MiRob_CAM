#ifndef STORAGE_MODULE_H
#define STORAGE_MODULE_H

#include <Arduino.h>
#include <FS.h>

bool storage_init();
bool storage_ok();

// Check whether capture is allowed now.
// If blocked by minimum interval, waitMs returns remaining ms.
bool storage_capture_ready(uint32_t* waitMs);

// Capture one photo and save it to TF card.
// outPath returns the saved path when successful.
bool storage_capture_and_save(String& outPath);

File storage_open_photo_dir();
bool storage_file_exists(const String& path);
File storage_open_file(const String& path);

#endif // STORAGE_MODULE_H
