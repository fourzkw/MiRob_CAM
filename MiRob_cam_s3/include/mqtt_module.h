#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include <Arduino.h>

bool mqtt_client_init();
bool mqtt_client_connect();
void mqtt_client_disconnect();
bool mqtt_client_connected();
void mqtt_client_loop();
bool mqtt_client_publish_jpeg(const uint8_t* jpeg, size_t len);

#endif // MQTT_MODULE_H
