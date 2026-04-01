#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "config.h"
#include "display_module.h"

static SPIClass s_tftSpi(FSPI);
static Adafruit_ST7789 s_tft(&s_tftSpi, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static bool s_displayOk = false;

static void drawTitle() {
    s_tft.fillRect(0, 0, s_tft.width(), 16, ST77XX_BLUE);
    s_tft.setTextColor(ST77XX_WHITE);
    s_tft.setTextSize(1);
    s_tft.setCursor(4, 4);
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

    s_tft.fillRect(0, 16, s_tft.width(), s_tft.height() - 16, ST77XX_BLACK);
    drawTitle();

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
