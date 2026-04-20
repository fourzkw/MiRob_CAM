#include "http_server_module.h"

// HTTP preview removed; MQTT preview is used in mode4. Stubs remain for API compatibility.

bool http_server_start() {
    return false;
}

void http_server_stop() {}

bool http_server_running() {
    return false;
}

void http_server_init() {}

void http_server_handle_client() {}

void http_server_set_mode(uint8_t mode) {
    (void)mode;
}

uint8_t http_server_get_mode() {
    return 1;
}

void http_server_set_mode_change_handler(void (*handler)(uint8_t mode)) {
    (void)handler;
}
