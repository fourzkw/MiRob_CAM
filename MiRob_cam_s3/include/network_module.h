#ifndef NETWORK_MODULE_H
#define NETWORK_MODULE_H

#include <Arduino.h>

// Start preview SoftAP (mode4).
bool network_start_preview_ap();

// Stop preview SoftAP (leave mode4).
void network_stop_preview_ap();

bool network_is_preview_ap_running();
String network_preview_ap_ip();

// Backward compatibility API.
void network_setup();

#endif // NETWORK_MODULE_H
