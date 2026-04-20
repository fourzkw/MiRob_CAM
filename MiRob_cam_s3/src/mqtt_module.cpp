#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "config.h"
#include "mqtt_module.h"

static WiFiClient s_wifiTcp;
static PubSubClient s_mqtt(s_wifiTcp);

bool mqtt_client_init() {
    s_mqtt.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
#if defined(MQTT_MAX_PACKET_SIZE) && (MQTT_MAX_PACKET_SIZE > 256)
    {
        uint32_t sz = static_cast<uint32_t>(MQTT_MAX_PACKET_SIZE);
        if (sz > 65535U) {
            sz = 65535U;
        }
        (void)s_mqtt.setBufferSize(static_cast<uint16_t>(sz));
    }
#endif
    return true;
}

bool mqtt_client_connect() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (s_mqtt.connected()) {
        return true;
    }

    const char* user =
        (MQTT_USER[0] != '\0') ? MQTT_USER : static_cast<const char*>(nullptr);
    const char* pass =
        (MQTT_PASSWORD[0] != '\0') ? MQTT_PASSWORD : static_cast<const char*>(nullptr);

    const bool ok = s_mqtt.connect(MQTT_CLIENT_ID, user, pass);
    if (!ok) {
        Serial.printf("MQTT: connect failed state=%d\n", s_mqtt.state());
    } else {
        Serial.println("MQTT: connected");
    }
    return ok;
}

void mqtt_client_disconnect() {
    if (s_mqtt.connected()) {
        s_mqtt.disconnect();
        Serial.println("MQTT: disconnected");
    }
}

bool mqtt_client_connected() {
    return s_mqtt.connected();
}

void mqtt_client_loop() {
    if (s_mqtt.connected()) {
        s_mqtt.loop();
    }
}

bool mqtt_client_publish_jpeg(const uint8_t* jpeg, size_t len) {
    if (!jpeg || len == 0) {
        return false;
    }
    if (!s_mqtt.connected()) {
        return false;
    }

    const size_t kMaxPayload = MQTT_MAX_PUBLISH_BYTES;
    if (len > kMaxPayload) {
        Serial.printf("MQTT: JPEG too large (%u > %u)\n", (unsigned)len, (unsigned)kMaxPayload);
        return false;
    }

    const bool ok = s_mqtt.publish(MQTT_IMAGE_TOPIC, jpeg, len, false);
    if (!ok) {
        Serial.println("MQTT: publish failed");
    }
    return ok;
}
