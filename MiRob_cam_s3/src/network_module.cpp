#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "network_module.h"

static bool s_previewApRunning = false;

bool network_start_preview_ap() {
    if (s_previewApRunning) {
        return true;
    }

    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);

    bool ok = WiFi.softAP(
        PREVIEW_AP_SSID,
        PREVIEW_AP_PASSWORD,
        PREVIEW_AP_CHANNEL,
        0,
        PREVIEW_AP_MAX_CLIENTS
    );
    if (!ok) {
        Serial.println("Network: failed to start preview SoftAP");
        return false;
    }

    s_previewApRunning = true;
    Serial.printf("Network: SoftAP started SSID=%s IP=%s\n", PREVIEW_AP_SSID, WiFi.softAPIP().toString().c_str());
    return true;
}

void network_stop_preview_ap() {
    if (!s_previewApRunning) {
        return;
    }

    WiFi.softAPdisconnect(true);
    delay(20);
    WiFi.mode(WIFI_OFF);
    s_previewApRunning = false;
    Serial.println("Network: SoftAP stopped");
}

bool network_is_preview_ap_running() {
    return s_previewApRunning;
}

String network_preview_ap_ip() {
    if (!s_previewApRunning) {
        return String("0.0.0.0");
    }
    return WiFi.softAPIP().toString();
}

void network_setup() {
    (void)network_start_preview_ap();
}
