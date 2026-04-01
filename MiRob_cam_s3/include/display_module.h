#ifndef DISPLAY_MODULE_H
#define DISPLAY_MODULE_H

#include <Arduino.h>

bool display_init();
bool display_ok();

void display_show_boot(const char* line1, const char* line2);
void display_show_capture(bool ok, const String& detail);

#endif // DISPLAY_MODULE_H
