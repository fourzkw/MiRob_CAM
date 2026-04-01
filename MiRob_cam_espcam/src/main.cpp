#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "camera_module.h"
#include "storage_module.h"
#include "network_module.h"
#include "http_server_module.h"
#include "log_module.h"

static uint32_t lastCaptureButtonMs = 0;
static uint32_t lastModeButtonMs = 0;
static bool s_modeButtonPrevPressed = false;
static bool s_captureButtonPrevPressed = false;
static const uint32_t DEBOUNCE_MS = 200;

static Preferences s_prefs;
static const char* PREF_NS = "mirob_cam";
static const char* PREF_KEY_MODE = "mode";

static uint8_t s_mode = DEFAULT_DEVICE_MODE;

static uint8_t normalizeMode(uint8_t mode) {
    if (mode == DEVICE_MODE_PHOTO_ONLY) {
        return DEVICE_MODE_PHOTO_ONLY;
    }
    return DEVICE_MODE_STREAM;
}

static void updateModeLed() {
#ifdef PIN_MODE_LED
    pinMode(PIN_MODE_LED, OUTPUT);
    if (s_mode == DEVICE_MODE_PHOTO_ONLY) {
        // active LOW LED: LOW = ON (photo-only mode)
        digitalWrite(PIN_MODE_LED, LOW);
    } else {
        // stream mode: turn LED off
        digitalWrite(PIN_MODE_LED, HIGH);
    }
#endif
}

static void applyMode(uint8_t mode, bool persist) {
    s_mode = normalizeMode(mode);
    http_server_set_mode(s_mode);
    camera_set_device_mode(s_mode);
    updateModeLed();

    if (persist) {
        s_prefs.putUChar(PREF_KEY_MODE, s_mode);
    }

    Serial.printf("[MODE] now=%u  (1=stream, 2=photo-only)\n", (unsigned)s_mode);
}

static void onWebModeChange(uint8_t mode) {
    applyMode(mode, true);
    log_append(String("[MODE] changed from web -> ") + String((unsigned)mode));
}

static void printHelp() {
    Serial.println("Serial commands:");
    Serial.println("  mode 1    -> stream mode");
    Serial.println("  mode 2    -> photo-only mode");
    Serial.println("  mode?     -> print current mode");
    Serial.println("  help      -> print this help");
    Serial.println("Buttons:");
    Serial.println("  GPIO13    -> capture photo");
    Serial.println("  GPIO12    -> toggle mode 1/2");
}

static void handleSerialCommands() {
    static String line;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            String cmd = line;
            line = "";
            cmd.trim();
            if (cmd.length() == 0) {
                continue;
            }

            String lower = cmd;
            lower.toLowerCase();

            if (lower == "help" || lower == "h" || lower == "?") {
                printHelp();
                continue;
            }

            if (lower == "mode?" || lower == "mode" || lower == "get mode") {
                Serial.printf("[MODE] %u\n", (unsigned)s_mode);
                continue;
            }

            if (lower.startsWith("mode")) {
                int idx = lower.indexOf(' ');
                if (idx < 0) {
                    idx = lower.indexOf('=');
                }
                if (idx < 0) {
                    idx = lower.indexOf(':');
                }
                if (idx < 0) {
                    Serial.println("Bad command. Try: mode 1 or mode 2");
                    continue;
                }

                String v = lower.substring(idx + 1);
                v.trim();
                int m = v.toInt();
                if (m != 1 && m != 2) {
                    Serial.println("Mode must be 1 or 2");
                    continue;
                }

                applyMode((uint8_t)m, true);
                continue;
            }

            Serial.println("Unknown command. Type: help");
        } else {
            if (line.length() < 64) {
                line += c;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\nMiRob CAM OV3660 + TF");

    log_init();
    log_append("Booting MiRob CAM OV3660 + TF");

    pinMode(PIN_BUTTON, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    pinMode(PIN_MODE_BUTTON, MODE_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);

    s_prefs.begin(PREF_NS, false);
    uint8_t saved = s_prefs.getUChar(PREF_KEY_MODE, (uint8_t)DEFAULT_DEVICE_MODE);
    s_mode = normalizeMode(saved);
    camera_set_device_mode(s_mode);
    updateModeLed();

    if (!camera_init()) {
        log_append("Camera init fail");
    }

    if (!storage_init()) {
        log_append("SD init fail");
    }

    network_setup();
    http_server_set_mode(s_mode);
    http_server_set_mode_change_handler(onWebModeChange);
    http_server_init();

    {
        char buf[64];
        snprintf(buf, sizeof(buf), "[MODE] boot=%u  (1=stream, 2=photo-only)", (unsigned)s_mode);
        log_append(String(buf));
    }

    printHelp();
}

void loop() {
    handleSerialCommands();
    http_server_handle_client();

#if MODE_BUTTON_ACTIVE_LOW
    bool modePressed = (digitalRead(PIN_MODE_BUTTON) == LOW);
#else
    bool modePressed = (digitalRead(PIN_MODE_BUTTON) == HIGH);
#endif
    if (modePressed && !s_modeButtonPrevPressed && (millis() - lastModeButtonMs >= DEBOUNCE_MS)) {
        lastModeButtonMs = millis();
        uint8_t nextMode = (s_mode == DEVICE_MODE_STREAM) ? DEVICE_MODE_PHOTO_ONLY : DEVICE_MODE_STREAM;
        applyMode(nextMode, true);
        log_append(String("[MODE] toggled by GPIO12 -> ") + String((unsigned)nextMode));
    }
    s_modeButtonPrevPressed = modePressed;

#if BUTTON_ACTIVE_LOW
    bool capturePressed = (digitalRead(PIN_BUTTON) == LOW);
#else
    bool capturePressed = (digitalRead(PIN_BUTTON) == HIGH);
#endif
    if (capturePressed && !s_captureButtonPrevPressed && (millis() - lastCaptureButtonMs >= DEBOUNCE_MS)) {
        lastCaptureButtonMs = millis();
        Serial.println("[BTN] capture pressed");
        String path;
        if (storage_capture_and_save(path)) {
            Serial.println("[BTN] capture ok: " + path);
            log_append("[BTN] capture ok: " + path);
        } else {
            Serial.println("[BTN] capture failed");
            log_append("[BTN] capture failed");
        }
    }
    s_captureButtonPrevPressed = capturePressed;
}
