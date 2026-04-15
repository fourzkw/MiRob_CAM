#include <Arduino.h>
#include <SD_MMC.h>
#include <FS.h>
#include <time.h>

#include <esp_heap_caps.h>

#include "config.h"
#include "camera_module.h"
#include "storage_module.h"

static bool s_sdOk = false;
static uint32_t s_lastCaptureMs = 0;
static uint32_t s_lastReinitTryMs = 0;
// Camera can occasionally return null frame on transient capture failures.
static const uint8_t CAPTURE_RETRY_COUNT = 3;
static const uint16_t CAPTURE_RETRY_DELAY_MS = 60;

// TF/SD pins from schematic
#define TF_CLK_GPIO_NUM   6
#define TF_CMD_GPIO_NUM   11
#define TF_D0_GPIO_NUM    7
#define TF_D1_GPIO_NUM    12
#define TF_D2_GPIO_NUM    13
#define TF_D3_GPIO_NUM    14

static bool jpeg_frame_is_valid(const camera_fb_t* fb) {
    // Minimal JPEG check: SOI (FF D8) and EOI (FF D9).
    // Some drivers may pad bytes after EOI, so don't require EOI to be
    // exactly the last two bytes.
    if (!fb || !fb->buf || fb->len < 4) {
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(fb->buf);
    if (p[0] != 0xFF || p[1] != 0xD8) {
        return false;
    }

    // Search for EOI in the last part of the buffer.
    // If the JPEG was truncated, EOI likely won't be present.
    const size_t kSearchTail = 32; // bytes
    size_t start = (fb->len > kSearchTail) ? (fb->len - kSearchTail) : 0;
    for (size_t i = start; i + 1 < fb->len; ++i) {
        if (p[i] == 0xFF && p[i + 1] == 0xD9) {
            return true;
        }
    }
    return false;
}

static void log_invalid_frame(const char* stage, const camera_fb_t* fb) {
    if (!fb) {
        Serial.printf("Capture invalid frame at %s: fb=null\n", stage);
        return;
    }

    Serial.printf(
        "Capture invalid frame at %s: %ux%u len=%u format=%u\n",
        stage,
        (unsigned)fb->width,
        (unsigned)fb->height,
        (unsigned)fb->len,
        (unsigned)fb->format
    );
}

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

static void discardFramesBeforeCapture() {
    for (uint8_t i = 0; i < CAPTURE_DISCARD_FRAMES_BEFORE_SAVE; ++i) {
        camera_fb_t* warmupFb = captureFrameWithRetry();
        if (warmupFb) {
            camera_return_frame(warmupFb);
        } else {
            Serial.println("Capture warmup: frame discard skipped (fb=null)");
            return;
        }

        if (i + 1 < CAPTURE_DISCARD_FRAMES_BEFORE_SAVE) {
            delay(CAPTURE_DISCARD_FRAME_DELAY_MS);
        }
    }
}

static uint32_t s_photoSeq = 0;  // 连续照片编号

static void discardFramesBeforeVideo() {
    for (uint8_t i = 0; i < VIDEO_DISCARD_FRAMES_BEFORE_SAVE; ++i) {
        camera_fb_t* warmupFb = captureFrameWithRetry();
        if (warmupFb) {
            camera_return_frame(warmupFb);
        } else {
            Serial.println("Video warmup: frame discard skipped (fb=null)");
            return;
        }

        if (i + 1 < VIDEO_DISCARD_FRAMES_BEFORE_SAVE) {
            delay(VIDEO_DISCARD_FRAME_DELAY_MS);
        }
    }
}

static uint32_t s_videoSeq = 0;

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

                // Expected format: PHOTO_PREFIX + "%06u" + PHOTO_EXT, e.g. img_000123.jpg
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

static void initVideoSeqFromDir() {
    if (!s_sdOk) {
        return;
    }

    File root = SD_MMC.open(VIDEO_DIR);
    if (!root || !root.isDirectory()) {
        return;
    }

    uint32_t maxSeq = 0;
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String fullName = String(f.name());
            if (fullName.endsWith(VIDEO_EXT)) {
                int slash = fullName.lastIndexOf('/');
                String baseName = (slash >= 0) ? fullName.substring(slash + 1) : fullName;
                if (baseName.startsWith(VIDEO_PREFIX)) {
                    int dot = baseName.lastIndexOf('.');
                    if (dot > 0) {
                        int prefixLen = strlen(VIDEO_PREFIX);
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
    s_videoSeq = maxSeq;
}

static void makePhotoPath(uint32_t seq, char* buf, size_t len) {
    // 连续编号形式：PHOTO_DIR "/" PHOTO_PREFIX "%06lu" PHOTO_EXT
    snprintf(buf, len, PHOTO_DIR "/" PHOTO_PREFIX "%06lu" PHOTO_EXT, (unsigned long)seq);
}

static void makeVideoPath(uint32_t seq, char* buf, size_t len) {
    snprintf(buf, len, VIDEO_DIR "/" VIDEO_PREFIX "%06lu" VIDEO_EXT, (unsigned long)seq);
}

static void markSdOffline(const char* reason) {
    if (s_sdOk) {
        Serial.printf("SD offline: %s\n", reason);
    }
    s_sdOk = false;
}

struct AviHeaderPatchPoints {
    uint32_t riffSizePos;
    uint32_t avihMaxBytesPerSecPos;
    uint32_t avihTotalFramesPos;
    uint32_t avihSuggestedBufferPos;
    uint32_t strhLengthPos;
    uint32_t strhSuggestedBufferPos;
    uint32_t moviListSizePos;
    uint32_t moviListStartPos;
};

static bool fileWriteBytes(File& f, const void* data, size_t len) {
    return f.write(reinterpret_cast<const uint8_t*>(data), len) == len;
}

static bool fileWriteFourCC(File& f, const char (&fcc)[5]) {
    return fileWriteBytes(f, fcc, 4);
}

static bool fileWriteU16LE(File& f, uint16_t value) {
    uint8_t b[2];
    b[0] = static_cast<uint8_t>(value & 0xFF);
    b[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    return fileWriteBytes(f, b, sizeof(b));
}

static bool fileWriteU32LE(File& f, uint32_t value) {
    uint8_t b[4];
    b[0] = static_cast<uint8_t>(value & 0xFF);
    b[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    b[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    b[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    return fileWriteBytes(f, b, sizeof(b));
}

static bool filePatchU32LE(File& f, uint32_t pos, uint32_t value) {
    const uint32_t curPos = static_cast<uint32_t>(f.position());
    if (!f.seek(pos)) {
        return false;
    }
    if (!fileWriteU32LE(f, value)) {
        return false;
    }
    return f.seek(curPos);
}

static bool fileWriteAviHeader(
    File& f,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    AviHeaderPatchPoints& patch
) {
    if (fps == 0 || width == 0 || height == 0) {
        return false;
    }

    patch = {};
    const uint32_t microSecPerFrame = 1000000UL / fps;
    const uint32_t strlListSize = 116;
    const uint32_t hdrlListSize = 192;
    const uint32_t bitmapImageSize = width * height * 3UL;

    if (!fileWriteFourCC(f, "RIFF")) {
        return false;
    }
    patch.riffSizePos = static_cast<uint32_t>(f.position());
    if (!fileWriteU32LE(f, 0)) {
        return false;
    }
    if (!fileWriteFourCC(f, "AVI ")) {
        return false;
    }

    if (!fileWriteFourCC(f, "LIST")) {
        return false;
    }
    if (!fileWriteU32LE(f, hdrlListSize)) {
        return false;
    }
    if (!fileWriteFourCC(f, "hdrl")) {
        return false;
    }

    if (!fileWriteFourCC(f, "avih")) {
        return false;
    }
    if (!fileWriteU32LE(f, 56)) {
        return false;
    }
    if (!fileWriteU32LE(f, microSecPerFrame)) {
        return false;
    }
    patch.avihMaxBytesPerSecPos = static_cast<uint32_t>(f.position());
    if (!fileWriteU32LE(f, 0)) {
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // dwPaddingGranularity
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // dwFlags
        return false;
    }
    patch.avihTotalFramesPos = static_cast<uint32_t>(f.position());
    if (!fileWriteU32LE(f, 0)) {
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // dwInitialFrames
        return false;
    }
    if (!fileWriteU32LE(f, 1)) { // dwStreams
        return false;
    }
    patch.avihSuggestedBufferPos = static_cast<uint32_t>(f.position());
    if (!fileWriteU32LE(f, 0)) {
        return false;
    }
    if (!fileWriteU32LE(f, width)) {
        return false;
    }
    if (!fileWriteU32LE(f, height)) {
        return false;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (!fileWriteU32LE(f, 0)) {
            return false;
        }
    }

    if (!fileWriteFourCC(f, "LIST")) {
        return false;
    }
    if (!fileWriteU32LE(f, strlListSize)) {
        return false;
    }
    if (!fileWriteFourCC(f, "strl")) {
        return false;
    }

    if (!fileWriteFourCC(f, "strh")) {
        return false;
    }
    if (!fileWriteU32LE(f, 56)) {
        return false;
    }
    if (!fileWriteFourCC(f, "vids")) {
        return false;
    }
    if (!fileWriteFourCC(f, "MJPG")) {
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // dwFlags
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // wPriority + wLanguage
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // dwInitialFrames
        return false;
    }
    if (!fileWriteU32LE(f, 1)) { // dwScale
        return false;
    }
    if (!fileWriteU32LE(f, fps)) { // dwRate
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // dwStart
        return false;
    }
    patch.strhLengthPos = static_cast<uint32_t>(f.position());
    if (!fileWriteU32LE(f, 0)) { // dwLength
        return false;
    }
    patch.strhSuggestedBufferPos = static_cast<uint32_t>(f.position());
    if (!fileWriteU32LE(f, 0)) { // dwSuggestedBufferSize
        return false;
    }
    if (!fileWriteU32LE(f, 0xFFFFFFFFUL)) { // dwQuality (-1)
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // dwSampleSize
        return false;
    }
    // AVIStreamHeader::rcFrame uses 16-bit fields (SHORT), total 8 bytes.
    if (!fileWriteU16LE(f, 0)) { // rcFrame.left
        return false;
    }
    if (!fileWriteU16LE(f, 0)) { // rcFrame.top
        return false;
    }
    if (!fileWriteU16LE(f, static_cast<uint16_t>(width))) { // rcFrame.right
        return false;
    }
    if (!fileWriteU16LE(f, static_cast<uint16_t>(height))) { // rcFrame.bottom
        return false;
    }

    if (!fileWriteFourCC(f, "strf")) {
        return false;
    }
    if (!fileWriteU32LE(f, 40)) {
        return false;
    }
    if (!fileWriteU32LE(f, 40)) { // biSize
        return false;
    }
    if (!fileWriteU32LE(f, width)) { // biWidth
        return false;
    }
    if (!fileWriteU32LE(f, height)) { // biHeight
        return false;
    }
    if (!fileWriteU16LE(f, 1)) { // biPlanes
        return false;
    }
    if (!fileWriteU16LE(f, 24)) { // biBitCount
        return false;
    }
    if (!fileWriteFourCC(f, "MJPG")) { // biCompression
        return false;
    }
    if (!fileWriteU32LE(f, bitmapImageSize)) { // biSizeImage
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // biXPelsPerMeter
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // biYPelsPerMeter
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // biClrUsed
        return false;
    }
    if (!fileWriteU32LE(f, 0)) { // biClrImportant
        return false;
    }

    patch.moviListStartPos = static_cast<uint32_t>(f.position());
    if (!fileWriteFourCC(f, "LIST")) {
        return false;
    }
    patch.moviListSizePos = static_cast<uint32_t>(f.position());
    if (!fileWriteU32LE(f, 0)) {
        return false;
    }
    if (!fileWriteFourCC(f, "movi")) {
        return false;
    }

    return true;
}

static bool fileWriteAviFrame(File& f, const uint8_t* jpegBuf, size_t jpegLen, uint32_t& maxFrameBytes) {
    if (!jpegBuf || jpegLen == 0 || jpegLen > 0xFFFFFFFFULL) {
        return false;
    }

    if (!fileWriteFourCC(f, "00dc")) {
        return false;
    }
    if (!fileWriteU32LE(f, static_cast<uint32_t>(jpegLen))) {
        return false;
    }
    if (!fileWriteBytes(f, jpegBuf, jpegLen)) {
        return false;
    }

    if ((jpegLen & 1U) != 0U) {
        const uint8_t pad = 0;
        if (!fileWriteBytes(f, &pad, 1)) {
            return false;
        }
    }

    if (jpegLen > maxFrameBytes) {
        maxFrameBytes = static_cast<uint32_t>(jpegLen);
    }
    return true;
}

static bool fileFinalizeAvi(
    File& f,
    const AviHeaderPatchPoints& patch,
    uint32_t frameCount,
    uint32_t maxFrameBytes,
    uint32_t fps
) {
    const uint32_t fileSize = static_cast<uint32_t>(f.position());
    if (fileSize < 8 || fileSize < (patch.moviListStartPos + 8)) {
        return false;
    }

    const uint32_t riffSize = fileSize - 8;
    const uint32_t moviListSize = fileSize - (patch.moviListStartPos + 8);
    const uint32_t maxBytesPerSec = maxFrameBytes * fps;

    if (!filePatchU32LE(f, patch.riffSizePos, riffSize)) {
        return false;
    }
    if (!filePatchU32LE(f, patch.avihMaxBytesPerSecPos, maxBytesPerSec)) {
        return false;
    }
    if (!filePatchU32LE(f, patch.avihTotalFramesPos, frameCount)) {
        return false;
    }
    if (!filePatchU32LE(f, patch.avihSuggestedBufferPos, maxFrameBytes)) {
        return false;
    }
    if (!filePatchU32LE(f, patch.strhLengthPos, frameCount)) {
        return false;
    }
    if (!filePatchU32LE(f, patch.strhSuggestedBufferPos, maxFrameBytes)) {
        return false;
    }
    if (!filePatchU32LE(f, patch.moviListSizePos, moviListSize)) {
        return false;
    }

    return true;
}

bool storage_init() {
    SD_MMC.end();

    // Must call setPins() before begin() when using non-default pins.
#if SD_MMC_1BIT
    if (!SD_MMC.setPins(TF_CLK_GPIO_NUM, TF_CMD_GPIO_NUM, TF_D0_GPIO_NUM)) {
        Serial.println("SD_MMC.setPins(1bit) failed");
        s_sdOk = false;
        return false;
    }
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD_MMC 1-bit init failed");
        s_sdOk = false;
        return false;
    }
#else
    if (!SD_MMC.setPins(TF_CLK_GPIO_NUM, TF_CMD_GPIO_NUM, TF_D0_GPIO_NUM, TF_D1_GPIO_NUM, TF_D2_GPIO_NUM, TF_D3_GPIO_NUM)) {
        Serial.println("SD_MMC.setPins(4bit) failed");
        s_sdOk = false;
        return false;
    }
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

    if (!SD_MMC.exists(VIDEO_DIR)) {
        if (!SD_MMC.mkdir(VIDEO_DIR)) {
            Serial.println("Create " VIDEO_DIR " failed");
            s_sdOk = false;
            return false;
        }
    }

    s_sdOk = true;
    // Initialize continuous numbering: scan existing files to find max id.
    initPhotoSeqFromDir();
    initVideoSeqFromDir();
    return true;
}

bool storage_ok() {
    return s_sdOk;
}

void storage_poll() {
    if (s_sdOk) {
        return;
    }

    uint32_t nowMs = millis();
    if (s_lastReinitTryMs != 0 && (nowMs - s_lastReinitTryMs) < SD_REINIT_INTERVAL_MS) {
        return;
    }

    s_lastReinitTryMs = nowMs;
    Serial.println("SD reinit attempt...");
    if (storage_init()) {
        Serial.println("SD reinit success");
    } else {
        Serial.println("SD reinit failed");
    }
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

bool storage_capture_and_save(String& outPath, bool useAutoCamera, uint8_t** outJpegDup, size_t* outJpegDupLen) {
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

    if (!camera_begin_exclusive_still_capture(useAutoCamera)) {
        Serial.println("Capture skipped: camera exclusive still profile failed");
        return false;
    }
    struct ScopedCameraExclusiveEnd {
        ~ScopedCameraExclusiveEnd() {
            camera_end_exclusive_capture();
        }
    } scopedCameraExclusive;

    // Discard stale frame(s) before the final save capture.
    // This helps align the saved frame with the current scene/lighting state.
    discardFramesBeforeCapture();

    camera_fb_t* fb = captureFrameWithRetry();

    // Validate JPEG frame; if the first attempt is truncated/stale,
    // retry once to avoid saving a broken/green frame.
    if (!fb || !jpeg_frame_is_valid(fb)) {
        log_invalid_frame("post-switch-first-check", fb);
        if (fb) {
            camera_return_frame(fb);
            fb = nullptr;
        }
        // If the first attempt is truncated/stale, don't redo full warmup:
        // just try one more capture to keep latency bounded.
        delay(40);
        fb = captureFrameWithRetry();
    }

    if (!fb || !jpeg_frame_is_valid(fb)) {
        log_invalid_frame("final-check", fb);
        Serial.println("Capture failed: invalid JPEG frame");
        if (fb) {
            camera_return_frame(fb);
        }
        return false;
    }

    // Pre-allocate next sequence id, update global only after save succeeds.
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
            markSdOffline("write size mismatch");
        }
    } else {
        Serial.println(String("Write failed: ") + String(path));
        markSdOffline("open for write failed");
    }

    if (outJpegDup) {
        *outJpegDup = nullptr;
    }
    if (outJpegDupLen) {
        *outJpegDupLen = 0;
    }
    if (ok && outJpegDup && fb && fb->buf && fb->len > 0) {
        uint8_t* dup = static_cast<uint8_t*>(heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!dup) {
            dup = static_cast<uint8_t*>(malloc(fb->len));
        }
        if (dup) {
            memcpy(dup, fb->buf, fb->len);
            *outJpegDup = dup;
            if (outJpegDupLen) {
                *outJpegDupLen = fb->len;
            }
        }
    }

    camera_return_frame(fb);
    return ok;
}

bool storage_record_video(String& outPath, uint32_t& outFrames, uint32_t& outDurationMs) {
    outFrames = 0;
    outDurationMs = 0;

    if (!camera_ok()) {
        Serial.println("Record skipped: camera not ready");
        return false;
    }
    if (!s_sdOk) {
        Serial.println("Record skipped: SD card not ready");
        return false;
    }
    if (VIDEO_FPS == 0 || VIDEO_DURATION_MS == 0) {
        Serial.println("Record skipped: invalid VIDEO_FPS/VIDEO_DURATION_MS");
        return false;
    }

    if (!camera_begin_exclusive_video_capture()) {
        Serial.println("Record skipped: camera exclusive video profile failed");
        return false;
    }
    struct ScopedCameraExclusiveEnd {
        ~ScopedCameraExclusiveEnd() {
            camera_end_exclusive_capture();
        }
    } scopedCameraExclusive;

    discardFramesBeforeVideo();

    // Pre-allocate next sequence id, update global only after save succeeds.
    uint32_t nextSeq = s_videoSeq + 1;
    char path[64];
    makeVideoPath(nextSeq, path, sizeof(path));

    const uint32_t frameIntervalMs = 1000UL / VIDEO_FPS;
    const uint32_t safeFrameIntervalMs = (frameIntervalMs == 0) ? 1 : frameIntervalMs;
    const uint32_t maxFrames = (VIDEO_DURATION_MS + safeFrameIntervalMs - 1) / safeFrameIntervalMs;

    const uint32_t startMs = millis();
    uint32_t nextFrameMs = startMs;
    bool ok = true;
    bool fileCreated = false;
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;
    uint32_t maxFrameBytes = 0;
    AviHeaderPatchPoints aviPatch = {};
    File f;

    while ((millis() - startMs) < VIDEO_DURATION_MS && outFrames < maxFrames) {
        const uint32_t nowMs = millis();
        if ((int32_t)(nextFrameMs - nowMs) > 0) {
            delay(nextFrameMs - nowMs);
        }

        camera_fb_t* fb = captureFrameWithRetry();
        if (!fb || !jpeg_frame_is_valid(fb)) {
            log_invalid_frame("video-frame", fb);
            if (fb) {
                camera_return_frame(fb);
            }
            nextFrameMs += safeFrameIntervalMs;
            continue;
        }

        if (!fileCreated) {
            frameWidth = static_cast<uint32_t>(fb->width);
            frameHeight = static_cast<uint32_t>(fb->height);
            f = SD_MMC.open(path, "w");
            if (!f) {
                Serial.println(String("Record open failed: ") + String(path));
                markSdOffline("open video for write failed");
                camera_return_frame(fb);
                ok = false;
                break;
            }
            fileCreated = true;

            if (!fileWriteAviHeader(f, frameWidth, frameHeight, VIDEO_FPS, aviPatch)) {
                Serial.println("Video write failed: AVI header");
                markSdOffline("write AVI header failed");
                camera_return_frame(fb);
                ok = false;
                break;
            }
        }

        const bool frameOk = fileWriteAviFrame(f, fb->buf, fb->len, maxFrameBytes);
        camera_return_frame(fb);

        if (!frameOk) {
            Serial.println("Video write failed: AVI frame");
            markSdOffline("write AVI frame failed");
            ok = false;
            break;
        }

        ++outFrames;
        nextFrameMs += safeFrameIntervalMs;
    }

    outDurationMs = millis() - startMs;

    if (ok && outFrames > 0 && fileCreated) {
        if (!fileFinalizeAvi(f, aviPatch, outFrames, maxFrameBytes, VIDEO_FPS)) {
            Serial.println("Video write failed: AVI finalize");
            markSdOffline("finalize AVI failed");
            ok = false;
        }
    }

    if (f) {
        f.close();
    }

    if (!ok || outFrames == 0) {
        if (fileCreated) {
            (void)SD_MMC.remove(path);
        }
        if (!ok) {
            Serial.println("Record failed: file write error");
        } else {
            Serial.println("Record failed: no valid frame captured");
        }
        return false;
    }

    outPath = String(path);
    s_videoSeq = nextSeq;
    Serial.printf(
        "Recorded: %s (%lu frames, %lu ms, target %u fps, %lux%lu MJPEG/AVI)\n",
        path,
        (unsigned long)outFrames,
        (unsigned long)outDurationMs,
        (unsigned)VIDEO_FPS,
        (unsigned long)frameWidth,
        (unsigned long)frameHeight
    );
    return true;
}

File storage_open_photo_dir() {
    if (!s_sdOk) {
        return File();
    }
    File dir = SD_MMC.open(PHOTO_DIR);
    if (!dir) {
        markSdOffline("open photo dir failed");
    }
    return dir;
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
    File f = SD_MMC.open(path);
    if (!f) {
        markSdOffline("open file failed");
    }
    return f;
}
