#ifndef NETWORK_MODULE_H
#define NETWORK_MODULE_H

#include <Arduino.h>

// Station WiFi for MQTT preview (mode4): connect to router/AP.
bool network_sta_connect();
void network_sta_disconnect();
bool network_sta_connected();
String network_sta_local_ip();

// Backward compatibility (no-op / redirect).
void network_setup();

#endif // NETWORK_MODULE_H
