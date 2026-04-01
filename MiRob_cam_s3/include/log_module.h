#ifndef LOG_MODULE_H
#define LOG_MODULE_H

#include <Arduino.h>

// Initialize in-memory log buffer.
void log_init();

// Append one line to log (auto-prefixed with timestamp in milliseconds).
void log_append(const String& line);

// Get all log lines joined by '\n'.
String log_get_all();

#endif // LOG_MODULE_H
