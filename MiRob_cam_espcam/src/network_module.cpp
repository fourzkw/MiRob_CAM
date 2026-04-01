#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "network_module.h"

void network_setup() {
    bool staEnabled = String(WIFI_STA_SSID).length() > 0;

    if (staEnabled) {
        WiFi.mode(WIFI_AP_STA);

        Serial.print("Connecting STA to ");
        Serial.print(WIFI_STA_SSID);
        Serial.println(" ...");

        WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_STA_TIMEOUT_MS) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("STA connected, IP: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("STA connect failed, continue with AP only");
        }
    } else {
        WiFi.mode(WIFI_AP);
    }

    // Lower latency for HTTP preview streaming.
    WiFi.setSleep(false);

    bool apOk = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);
    if (apOk) {
        Serial.print("AP started. SSID: ");
        Serial.print(WIFI_AP_SSID);
        Serial.print("  IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("SoftAP start failed");
    }

    Serial.println("HTTP server will be available at:");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("  STA: http://");
        Serial.println(WiFi.localIP());
    }
    Serial.print("  AP : http://");
    Serial.println(WiFi.softAPIP());
}

