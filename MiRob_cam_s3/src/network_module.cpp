#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "network_module.h"

static bool s_staConnected = false;

bool network_sta_connect() {
    if (s_staConnected && WiFi.status() == WL_CONNECTED) {
        return true;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(MQTT_WIFI_SSID, MQTT_WIFI_PASSWORD);

    const uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startMs > MQTT_WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("Network: WiFi connect timeout");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            s_staConnected = false;
            return false;
        }
        delay(200);
    }

    s_staConnected = true;
    Serial.printf("Network: WiFi connected IP=%s\n", WiFi.localIP().toString().c_str());
    return true;
}

void network_sta_disconnect() {
    if (!s_staConnected) {
        return;
    }
    WiFi.disconnect(true);
    delay(20);
    WiFi.mode(WIFI_OFF);
    s_staConnected = false;
    Serial.println("Network: WiFi stopped");
}

bool network_sta_connected() {
    return s_staConnected && (WiFi.status() == WL_CONNECTED);
}

String network_sta_local_ip() {
    if (!network_sta_connected()) {
        return String("0.0.0.0");
    }
    return WiFi.localIP().toString();
}

void network_setup() {
    (void)network_sta_connect();
}
