#include <Arduino.h>
#include <SD_MMC.h>
#include <FS.h>
#include <time.h>

#include "config.h"
#include "camera_module.h"
#include "storage_module.h"

static bool s_sdOk = false;
static uint32_t s_lastCaptureMs = 0;
// After switching sensor profile, drop stale buffered frames first.
static const uint8_t PHOTO_PROFILE_WARMUP_FRAMES = 2;
// Camera can occasionally return null frame right after a profile switch.
static const uint8_t CAPTURE_RETRY_COUNT = 3;
static const uint16_t CAPTURE_RETRY_DELAY_MS = 60;

static camera_fb_t* captureFrameWithRetry() {
    for (uint8_t i = 0; i <= CAPTURE_RETRY_COUNT; ++i) {
        camera_fb_t* fb = camera_capture_frame();
        if (fb) {
            return fb;
        }
        if (i < CAPTURE_RETRY_COUNT) {
            delay(CAPTURE_RETRY_DELAY_MS);
        }
    }
    return nullptr;
}

static camera_fb_t* capturePhotoFrameAfterProfileSwitch() {
    for (uint8_t i = 0; i < PHOTO_PROFILE_WARMUP_FRAMES; ++i) {
        camera_fb_t* stale = captureFrameWithRetry();
        if (!stale) {
            return nullptr;
        }
        camera_return_frame(stale);
        delay(30);
    }
    return captureFrameWithRetry();
}
static uint32_t s_photoSeq = 0;  // 连续照片编号

static void initPhotoSeqFromDir() {
    if (!s_sdOk) {
        return;
    }

    File root = SD_MMC.open(PHOTO_DIR);
    if (!root || !root.isDirectory()) {
        return;
    }

    uint32_t maxSeq = 0;
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String fullName = String(f.name());
            if (fullName.endsWith(PHOTO_EXT)) {
                int slash = fullName.lastIndexOf('/');
                String baseName = (slash >= 0) ? fullName.substring(slash + 1) : fullName;

                // 期望格式：PHOTO_PREFIX + "%06u" + PHOTO_EXT，例如 img_000123.jpg
                if (baseName.startsWith(PHOTO_PREFIX)) {
                    int dot = baseName.lastIndexOf('.');
                    if (dot > 0) {
                        int prefixLen = strlen(PHOTO_PREFIX);
                        if (dot > prefixLen) {
                            String numStr = baseName.substring(prefixLen, dot);
                            uint32_t v = (uint32_t)numStr.toInt();
                            if (v > maxSeq) {
                                maxSeq = v;
                            }
                        }
                    }
                }
            }
        }
        f = root.openNextFile();
    }

    root.close();
    s_photoSeq = maxSeq;
}

static void makePhotoPath(uint32_t seq, char* buf, size_t len) {
    // 连续编号形式：PHOTO_DIR "/" PHOTO_PREFIX "%06lu" PHOTO_EXT
    snprintf(buf, len, PHOTO_DIR "/" PHOTO_PREFIX "%06lu" PHOTO_EXT, (unsigned long)seq);
}

bool storage_init() {
#if SD_MMC_1BIT
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD_MMC 1-bit init failed");
        s_sdOk = false;
        return false;
    }
#else
    if (!SD_MMC.begin("/sdcard")) {
        Serial.println("SD_MMC init failed");
        s_sdOk = false;
        return false;
    }
#endif

    if (!SD_MMC.exists(PHOTO_DIR)) {
        if (!SD_MMC.mkdir(PHOTO_DIR)) {
            Serial.println("Create " PHOTO_DIR " failed");
            s_sdOk = false;
            return false;
        }
    }

    s_sdOk = true;
    // 初始化连续编号：从现有文件中找到最大的编号
    initPhotoSeqFromDir();
    return true;
}

bool storage_ok() {
    return s_sdOk;
}

bool storage_capture_ready(uint32_t* waitMs) {
    uint32_t remain = 0;
    if (s_lastCaptureMs != 0) {
        uint32_t elapsed = millis() - s_lastCaptureMs;
        if (elapsed < CAPTURE_MIN_INTERVAL_MS) {
            remain = CAPTURE_MIN_INTERVAL_MS - elapsed;
        }
    }

    if (waitMs) {
        *waitMs = remain;
    }
    return remain == 0;
}

bool storage_capture_and_save(String& outPath) {
    if (!camera_ok()) {
        Serial.println("Capture skipped: camera not ready");
        return false;
    }
    if (!s_sdOk) {
        Serial.println("Capture skipped: SD card not ready");
        return false;
    }

    uint32_t waitMs = 0;
    if (!storage_capture_ready(&waitMs)) {
        Serial.printf("Capture throttled, wait %lu ms\n", (unsigned long)waitMs);
        return false;
    }

    bool switchedToPhotoProfile = !camera_is_photo_profile_active();
    camera_set_photo_profile();
    if (!camera_is_photo_profile_active()) {
        Serial.println("Capture failed: photo profile switch failed");
        camera_set_idle_profile_for_mode();
        return false;
    }

    camera_fb_t* fb = switchedToPhotoProfile
        ? capturePhotoFrameAfterProfileSwitch()
        : captureFrameWithRetry();
    if (!fb) {
        Serial.println("Capture failed: camera frame unavailable");
        camera_set_idle_profile_for_mode();
        return false;
    }

    // 预分配下一个编号，仅在保存成功后才更新全局 s_photoSeq
    uint32_t nextSeq = s_photoSeq + 1;
    char path[64];
    makePhotoPath(nextSeq, path, sizeof(path));

    File f = SD_MMC.open(path, "w");
    bool ok = false;
    if (f) {
        size_t written = f.write(fb->buf, fb->len);
        f.close();
        if (written == fb->len) {
            Serial.printf(
                "Saved: %s (%ux%u, %u bytes)\n",
                path,
                (unsigned)fb->width,
                (unsigned)fb->height,
                (unsigned)fb->len
            );
            outPath = String(path);
            ok = true;
            s_lastCaptureMs = millis();
            s_photoSeq = nextSeq;
        } else {
            Serial.printf(
                "Write failed: %s (%u/%u bytes)\n",
                path,
                (unsigned)written,
                (unsigned)fb->len
            );
        }
    } else {
        Serial.println("Write failed: " + String(path));
    }

    camera_return_frame(fb);
    camera_set_idle_profile_for_mode();
    return ok;
}

File storage_open_photo_dir() {
    if (!s_sdOk) {
        return File();
    }
    return SD_MMC.open(PHOTO_DIR);
}

bool storage_file_exists(const String& path) {
    if (!s_sdOk) {
        return false;
    }
    return SD_MMC.exists(path);
}

File storage_open_file(const String& path) {
    if (!s_sdOk) {
        return File();
    }
    return SD_MMC.open(path);
}
