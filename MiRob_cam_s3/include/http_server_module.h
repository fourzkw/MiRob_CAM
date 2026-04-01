#ifndef HTTP_SERVER_MODULE_H
#define HTTP_SERVER_MODULE_H

#include <Arduino.h>

bool http_server_start();
void http_server_stop();
bool http_server_running();

// Compatibility APIs.
void http_server_init();
void http_server_handle_client();
void http_server_set_mode(uint8_t mode);
uint8_t http_server_get_mode();
void http_server_set_mode_change_handler(void (*handler)(uint8_t mode));

#endif // HTTP_SERVER_MODULE_H
