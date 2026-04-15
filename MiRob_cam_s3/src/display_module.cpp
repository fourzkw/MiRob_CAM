#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "img_converters.h"
#include "esp_jpg_decode.h"
#include "freertos/portmacro.h"

#include "config.h"
#include "display_module.h"

static SPIClass s_tftSpi(FSPI);
static Adafruit_ST7789 s_tft(&s_tftSpi, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static bool s_displayOk = false;
static bool s_tfUnavailableOverlay = false;
static bool s_jpegSolidTest = false;
static uint32_t s_jpegSolidTestNextMs = 0;
static int8_t s_jpegSolidColorIdx = -1;

static char s_modeOverlay[28] = "";
static bool s_modeOverlayDirty = true;
static uint8_t* s_pendingJpegBuf = nullptr;
static size_t s_pendingJpegLen = 0;
static uint32_t s_pendingHoldMs = 0;
static portMUX_TYPE s_pendingMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_reviewUntilMs = 0;

static constexpr int16_t kModeOverlayBarH = 14;
static constexpr int16_t kContentAreaX = 0;
static constexpr int16_t kContentAreaY = kModeOverlayBarH;
static constexpr int16_t kContentAreaW = TFT_WIDTH;
static constexpr int16_t kContentAreaH = TFT_HEIGHT - kModeOverlayBarH;

struct DisplayRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

static void drawTitle() {
    s_tft.fillRect(0, 0, s_tft.width(), kModeOverlayBarH, ST77XX_BLUE);
    s_tft.setTextColor(ST77XX_WHITE);
    s_tft.setTextSize(1);
    s_tft.setCursor(4, 3);
    s_tft.print("MiRob CAM");
}

static void drawBodyLine(int16_t y, uint16_t color, const char* text, uint8_t textSize) {
    s_tft.setTextColor(color);
    s_tft.setTextSize(textSize);
    s_tft.setCursor(8, y);
    s_tft.print(text);
}

bool display_init() {
    if (TFT_BL_PIN >= 0) {
        pinMode(TFT_BL_PIN, OUTPUT);
        digitalWrite(TFT_BL_PIN, TFT_BL_ACTIVE);
    }

    s_tftSpi.begin(TFT_SCL_PIN, -1, TFT_SDA_PIN, TFT_CS_PIN);
    s_tft.setSPISpeed(TFT_SPI_HZ);
    s_tft.init(TFT_WIDTH, TFT_HEIGHT, TFT_SPI_MODE);
    s_tft.setRotation(TFT_ROTATION);
    s_tft.fillScreen(ST77XX_BLACK);
    s_tft.setTextWrap(true);
    drawTitle();
    drawBodyLine(24, ST77XX_GREEN, "Display ready", 1);
    s_modeOverlayDirty = true;
    s_displayOk = true;
    return true;
}

bool display_ok() {
    return s_displayOk;
}

void display_show_boot(const char* line1, const char* line2) {
    if (!s_displayOk) {
        return;
    }

    s_tft.fillRect(0, kModeOverlayBarH, s_tft.width(), s_tft.height() - kModeOverlayBarH, ST77XX_BLACK);
    drawTitle();
    s_modeOverlayDirty = true;

    if (line1 && line1[0] != '\0') {
        drawBodyLine(24, ST77XX_WHITE, line1, 1);
    }
    if (line2 && line2[0] != '\0') {
        drawBodyLine(36, ST77XX_CYAN, line2, 1);
    }
}

void display_show_capture(bool ok, const String& detail) {
    if (!s_displayOk) {
        return;
    }

    s_tft.fillRect(0, 48, s_tft.width(), s_tft.height() - 48, ST77XX_BLACK);

    s_tft.setTextSize(1);
    s_tft.setCursor(4, 52);
    s_tft.setTextColor(ok ? ST77XX_GREEN : ST77XX_RED);
    s_tft.print(ok ? "Capture OK" : "Capture FAIL");

    s_tft.setTextSize(1);
    s_tft.setCursor(4, 64);
    s_tft.setTextColor(ST77XX_WHITE);
    s_tft.print(detail);
}

static DisplayRect aspectFitRect(int32_t srcW, int32_t srcH) {
    DisplayRect rect = {kContentAreaX, kContentAreaY, 0, 0};
    if (srcW <= 0 || srcH <= 0) {
        return rect;
    }

    int32_t dstW = kContentAreaW;
    int32_t dstH = (srcH * dstW) / srcW;
    if (dstH > kContentAreaH) {
        dstH = kContentAreaH;
        dstW = (srcW * dstH) / srcH;
    }
    if (dstW <= 0) {
        dstW = 1;
    }
    if (dstH <= 0) {
        dstH = 1;
    }

    rect.x = static_cast<int16_t>(kContentAreaX + (kContentAreaW - dstW) / 2);
    rect.y = static_cast<int16_t>(kContentAreaY + (kContentAreaH - dstH) / 2);
    rect.w = static_cast<int16_t>(dstW);
    rect.h = static_cast<int16_t>(dstH);
    return rect;
}

static void clearAspectFitMargins(const DisplayRect& rect) {
    const int16_t contentRight = kContentAreaX + kContentAreaW;
    const int16_t contentBottom = kContentAreaY + kContentAreaH;
    const int16_t rectRight = rect.x + rect.w;
    const int16_t rectBottom = rect.y + rect.h;

    if (rect.w <= 0 || rect.h <= 0) {
        s_tft.fillRect(kContentAreaX, kContentAreaY, kContentAreaW, kContentAreaH, ST77XX_BLACK);
        return;
    }

    if (rect.y > kContentAreaY) {
        s_tft.fillRect(kContentAreaX, kContentAreaY, kContentAreaW, rect.y - kContentAreaY, ST77XX_BLACK);
    }
    if (rectBottom < contentBottom) {
        s_tft.fillRect(kContentAreaX, rectBottom, kContentAreaW, contentBottom - rectBottom, ST77XX_BLACK);
    }
    if (rect.x > kContentAreaX) {
        s_tft.fillRect(kContentAreaX, rect.y, rect.x - kContentAreaX, rect.h, ST77XX_BLACK);
    }
    if (rectRight < contentRight) {
        s_tft.fillRect(rectRight, rect.y, contentRight - rectRight, rect.h, ST77XX_BLACK);
    }
}

static bool blitRgb565AspectFit(const uint8_t* rgb565, uint16_t srcW, uint16_t srcH, bool rgb565BigEndian) {
    if (!s_displayOk || !rgb565 || srcW == 0 || srcH == 0) {
        return false;
    }

    const DisplayRect dst = aspectFitRect(srcW, srcH);
    clearAspectFitMargins(dst);
    if (dst.w <= 0 || dst.h <= 0) {
        return false;
    }

    static uint16_t* s_scaleLineBuf = nullptr;
    if (!s_scaleLineBuf) {
        s_scaleLineBuf = static_cast<uint16_t*>(
            heap_caps_malloc(static_cast<size_t>(TFT_WIDTH) * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
        );
        if (!s_scaleLineBuf) {
            s_scaleLineBuf = static_cast<uint16_t*>(malloc(static_cast<size_t>(TFT_WIDTH) * sizeof(uint16_t)));
        }
    }
    if (!s_scaleLineBuf) {
        return false;
    }

    s_tft.startWrite();
    s_tft.setAddrWindow(dst.x, dst.y, dst.w, dst.h);
    for (int16_t y = 0; y < dst.h; ++y) {
        const uint32_t srcY = (static_cast<uint32_t>(y) * static_cast<uint32_t>(srcH)) / static_cast<uint32_t>(dst.h);
        const uint16_t* srcRow =
            reinterpret_cast<const uint16_t*>(rgb565 + static_cast<size_t>(srcY) * static_cast<size_t>(srcW) * 2U);
        for (int16_t x = 0; x < dst.w; ++x) {
            const uint32_t srcX =
                (static_cast<uint32_t>(x) * static_cast<uint32_t>(srcW)) / static_cast<uint32_t>(dst.w);
            s_scaleLineBuf[x] = srcRow[srcX];
        }
        s_tft.writePixels(s_scaleLineBuf, dst.w, true, rgb565BigEndian);
    }
    s_tft.endWrite();
    return true;
}

static bool jpegReadSofSize(const uint8_t* data, size_t len, uint16_t* outW, uint16_t* outH) {
    for (size_t i = 0; i + 9 < len; ++i) {
        if (data[i] != 0xFF) {
            continue;
        }
        const uint8_t marker = data[i + 1];
        // SOF0 / SOF1 / SOF2 (baseline / extended / progressive)
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            *outH = static_cast<uint16_t>((static_cast<uint16_t>(data[i + 5]) << 8) | data[i + 6]);
            *outW = static_cast<uint16_t>((static_cast<uint16_t>(data[i + 7]) << 8) | data[i + 8]);
            return (*outW > 0 && *outH > 0);
        }
    }
    return false;
}

static uint16_t jpgScaleDivisor(jpg_scale_t scale) {
    if (scale == JPG_SCALE_2X) {
        return 2;
    }
    if (scale == JPG_SCALE_4X) {
        return 4;
    }
    if (scale == JPG_SCALE_8X) {
        return 8;
    }
    return 1;
}

// UXGA @ 1/4 decode => 400x300 RGB565; keep buffer sized for that path.
static bool decodePhotoJpegToTft(const uint8_t* jpg, size_t jpgLen) {
    if (!s_displayOk || !jpg || jpgLen < 10) {
        return false;
    }
    uint16_t iw = 0;
    uint16_t ih = 0;
    if (!jpegReadSofSize(jpg, jpgLen, &iw, &ih)) {
        return false;
    }

    jpg_scale_t scale = JPG_SCALE_4X;
    uint16_t divv = jpgScaleDivisor(scale);
    uint16_t ow = static_cast<uint16_t>(iw / divv);
    uint16_t oh = static_cast<uint16_t>(ih / divv);
    const size_t kMaxRgb565Bytes = 400U * 300U * 2U;
    size_t need = static_cast<size_t>(ow) * static_cast<size_t>(oh) * 2U;

    if (need > kMaxRgb565Bytes) {
        scale = JPG_SCALE_8X;
        divv = jpgScaleDivisor(scale);
        ow = static_cast<uint16_t>(iw / divv);
        oh = static_cast<uint16_t>(ih / divv);
        need = static_cast<size_t>(ow) * static_cast<size_t>(oh) * 2U;
    }
    if (need == 0 || need > kMaxRgb565Bytes) {
        return false;
    }

    static uint8_t* s_reviewRgbBuf = nullptr;
    if (!s_reviewRgbBuf) {
        s_reviewRgbBuf = static_cast<uint8_t*>(heap_caps_malloc(kMaxRgb565Bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_reviewRgbBuf) {
            s_reviewRgbBuf = static_cast<uint8_t*>(malloc(kMaxRgb565Bytes));
        }
    }
    if (!s_reviewRgbBuf) {
        return false;
    }

    if (!jpg2rgb565(jpg, jpgLen, s_reviewRgbBuf, scale)) {
        return false;
    }

    camera_fb_t rgbFb = {};
    rgbFb.width = ow;
    rgbFb.height = oh;
    rgbFb.format = PIXFORMAT_RGB565;
    rgbFb.buf = s_reviewRgbBuf;
    rgbFb.len = need;
    return blitRgb565AspectFit(rgbFb.buf, rgbFb.width, rgbFb.height, false);
}

void display_draw_rgb565_live(const camera_fb_t* fb) {
    if (!fb || fb->format != PIXFORMAT_RGB565) {
        return;
    }
    (void)blitRgb565AspectFit(fb->buf, fb->width, fb->height, (TFT_RGB565_BIG_ENDIAN != 0));
}

void display_draw_camera_live(const camera_fb_t* fb) {
    if (!s_displayOk || !fb) {
        return;
    }
    if (fb->format == PIXFORMAT_RGB565) {
        (void)blitRgb565AspectFit(fb->buf, fb->width, fb->height, (TFT_RGB565_BIG_ENDIAN != 0));
        return;
    }
    if (fb->format != PIXFORMAT_JPEG || !fb->buf || fb->len < 4) {
        return;
    }

    static uint8_t* s_decodeBuf = nullptr;
    const size_t kMaxRgb565Bytes = 320 * 240 * 2;
    const size_t needOut = static_cast<size_t>(fb->width) * static_cast<size_t>(fb->height) * 2U;
    if (needOut == 0 || needOut > kMaxRgb565Bytes) {
        return;
    }
    if (!s_decodeBuf) {
        s_decodeBuf = static_cast<uint8_t*>(heap_caps_malloc(kMaxRgb565Bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_decodeBuf) {
            s_decodeBuf = static_cast<uint8_t*>(malloc(kMaxRgb565Bytes));
        }
    }
    if (!s_decodeBuf) {
        return;
    }

    if (!jpg2rgb565(fb->buf, fb->len, s_decodeBuf, JPG_SCALE_NONE)) {
        return;
    }

    camera_fb_t rgbFb = {};
    rgbFb.width = fb->width;
    rgbFb.height = fb->height;
    rgbFb.format = PIXFORMAT_RGB565;
    rgbFb.buf = s_decodeBuf;
    rgbFb.len = static_cast<size_t>(fb->width) * static_cast<size_t>(fb->height) * 2U;
    // jpg2rgb565 输出字节序与传感器直出 RGB565 常不一致，默认不按 bigEndian 交换
    (void)blitRgb565AspectFit(rgbFb.buf, rgbFb.width, rgbFb.height, false);
}

void display_set_tf_unavailable(bool unavailable) {
    s_tfUnavailableOverlay = unavailable;
}

void display_set_jpeg_solid_test(bool enabled) {
    s_jpegSolidTest = enabled;
    s_jpegSolidTestNextMs = 0;
    s_jpegSolidColorIdx = -1;
}

bool display_jpeg_solid_test_enabled() {
    return s_jpegSolidTest;
}

bool display_jpeg_solid_test_tick() {
    if (!s_displayOk || !s_jpegSolidTest) {
        return false;
    }

    const uint32_t now = millis();
    if (now < s_jpegSolidTestNextMs && s_jpegSolidColorIdx >= 0) {
        return true;
    }
    s_jpegSolidTestNextMs = now + static_cast<uint32_t>(TFT_JPEG_SOLID_TEST_INTERVAL_MS);

    static const uint8_t kPalette[][3] = {
        {255, 0, 0},
        {0, 255, 0},
        {0, 0, 255},
        {255, 255, 255},
        {0, 0, 0},
    };
    s_jpegSolidColorIdx = static_cast<int8_t>((s_jpegSolidColorIdx + 1) % 5);
    const uint8_t* rgb = kPalette[s_jpegSolidColorIdx];

    const uint16_t w = static_cast<uint16_t>(TFT_JPEG_SOLID_TEST_WIDTH);
    const uint16_t h = static_cast<uint16_t>(TFT_JPEG_SOLID_TEST_HEIGHT);
    const size_t rgbBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 3U;

    static uint8_t* s_rgbBuf = nullptr;
    if (!s_rgbBuf) {
        s_rgbBuf = static_cast<uint8_t*>(heap_caps_malloc(rgbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_rgbBuf) {
            s_rgbBuf = static_cast<uint8_t*>(malloc(rgbBytes));
        }
    }
    if (!s_rgbBuf) {
        return true;
    }

    for (size_t i = 0; i < rgbBytes; i += 3U) {
        s_rgbBuf[i] = rgb[0];
        s_rgbBuf[i + 1U] = rgb[1];
        s_rgbBuf[i + 2U] = rgb[2];
    }

    uint8_t* jpg = nullptr;
    size_t jpgLen = 0;
    if (!fmt2jpg(
            s_rgbBuf,
            rgbBytes,
            w,
            h,
            PIXFORMAT_RGB888,
            static_cast<uint8_t>(TFT_JPEG_SOLID_TEST_JPEG_QUALITY),
            &jpg,
            &jpgLen
        ) ||
        !jpg || jpgLen < 4) {
        if (jpg) {
            free(jpg);
        }
        return true;
    }

    camera_fb_t fake = {};
    fake.width = w;
    fake.height = h;
    fake.format = PIXFORMAT_JPEG;
    fake.buf = jpg;
    fake.len = jpgLen;

    display_draw_camera_live(&fake);
    free(jpg);

    s_tft.setTextSize(1);
    s_tft.setTextColor(ST77XX_WHITE);
    s_tft.setCursor(2, TFT_HEIGHT - 10);
    s_tft.fillRect(0, TFT_HEIGHT - 12, 120, 12, ST77XX_BLACK);
    s_tft.print("JPEG solid test");

    return true;
}

void display_draw_live_status_overlays() {
    if (!s_displayOk || !s_tfUnavailableOverlay) {
        return;
    }

    const int16_t barH = 28;
    const int16_t barW = 124;
    const int16_t y0 = TFT_HEIGHT - barH;
    s_tft.fillRect(0, y0, barW, barH, ST77XX_RED);
    s_tft.drawFastHLine(0, y0, barW, ST77XX_YELLOW);
    s_tft.drawFastVLine(0, y0, barH, ST77XX_YELLOW);

    s_tft.setTextSize(1);
    s_tft.setTextColor(ST77XX_WHITE);
    s_tft.setCursor(2, y0 + 4);
    s_tft.print("TF init FAIL");
    s_tft.setCursor(2, y0 + 14);
    s_tft.print("Check card/pins");
}

void display_set_mode_overlay_text(const char* text) {
    if (!text) {
        text = "";
    }
    char nextText[sizeof(s_modeOverlay)] = {};
    strncpy(nextText, text, sizeof(nextText) - 1U);
    nextText[sizeof(nextText) - 1U] = '\0';

    if (strncmp(s_modeOverlay, nextText, sizeof(s_modeOverlay)) == 0) {
        return;
    }

    strncpy(s_modeOverlay, nextText, sizeof(s_modeOverlay) - 1U);
    s_modeOverlay[sizeof(s_modeOverlay) - 1U] = '\0';
    s_modeOverlayDirty = true;
}

void display_draw_mode_overlay() {
    if (!s_displayOk || !s_modeOverlayDirty) {
        return;
    }
    s_tft.fillRect(0, 0, s_tft.width(), kModeOverlayBarH, ST77XX_BLACK);
    s_tft.drawFastHLine(0, kModeOverlayBarH - 1, s_tft.width(), ST77XX_BLUE);

    s_tft.setTextSize(1);
    s_tft.setTextColor(ST77XX_WHITE);
    s_tft.setCursor(2, 3);
    if (s_modeOverlay[0] != '\0') {
        s_tft.print(s_modeOverlay);
    }
    s_modeOverlayDirty = false;
}

void display_queue_post_capture_jpeg(uint8_t* jpegBuf, size_t jpegLen, uint32_t holdMs) {
    if (!jpegBuf || jpegLen == 0) {
        return;
    }
    portENTER_CRITICAL(&s_pendingMux);
    if (s_pendingJpegBuf) {
        free(s_pendingJpegBuf);
        s_pendingJpegBuf = nullptr;
        s_pendingJpegLen = 0;
    }
    s_pendingJpegBuf = jpegBuf;
    s_pendingJpegLen = jpegLen;
    s_pendingHoldMs = (holdMs > 0) ? holdMs : static_cast<uint32_t>(TFT_CAPTURE_REVIEW_HOLD_MS);
    portEXIT_CRITICAL(&s_pendingMux);
}

bool display_try_show_pending_capture_review() {
    if (!s_displayOk) {
        return false;
    }

    uint8_t* jpegBuf = nullptr;
    size_t jpegLen = 0;
    uint32_t holdMs = TFT_CAPTURE_REVIEW_HOLD_MS;

    portENTER_CRITICAL(&s_pendingMux);
    jpegBuf = s_pendingJpegBuf;
    jpegLen = s_pendingJpegLen;
    holdMs = (s_pendingHoldMs > 0) ? s_pendingHoldMs : static_cast<uint32_t>(TFT_CAPTURE_REVIEW_HOLD_MS);
    s_pendingJpegBuf = nullptr;
    s_pendingJpegLen = 0;
    s_pendingHoldMs = 0;
    portEXIT_CRITICAL(&s_pendingMux);

    if (!jpegBuf || jpegLen == 0) {
        return false;
    }

    const uint32_t holdStartMs = millis();
    const bool ok = decodePhotoJpegToTft(jpegBuf, jpegLen);
    free(jpegBuf);
    if (ok) {
        s_reviewUntilMs = holdStartMs + holdMs;
        display_draw_mode_overlay();
    }
    return ok;
}

bool display_capture_review_hold_active() {
    if (!s_displayOk) {
        return false;
    }
    const uint32_t now = millis();
    return now < s_reviewUntilMs;
}

uint32_t display_capture_review_remaining_ms() {
    if (!s_displayOk) {
        return 0;
    }
    const uint32_t now = millis();
    if (now >= s_reviewUntilMs) {
        return 0;
    }
    return s_reviewUntilMs - now;
}
