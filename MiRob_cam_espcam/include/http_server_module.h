#ifndef HTTP_SERVER_MODULE_H
#define HTTP_SERVER_MODULE_H

#include <Arduino.h>

enum DeviceMode : uint8_t {
    DEVICE_MODE_STREAM = 1,
    DEVICE_MODE_PHOTO_ONLY = 2,
};

void http_server_init();
void http_server_handle_client();

void http_server_set_mode(uint8_t mode);
uint8_t http_server_get_mode();

// Called when web UI requests mode changes.
// Register from main.cpp to persist mode in Preferences.
void http_server_set_mode_change_handler(void (*handler)(uint8_t mode));

#endif // HTTP_SERVER_MODULE_H
