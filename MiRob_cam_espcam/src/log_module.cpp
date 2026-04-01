/**
 * 简单内存日志实现：保存最近 N 行文本日志。
 */
#include <Arduino.h>
#include "log_module.h"

// 可根据需要调整日志行数上限
static const size_t LOG_MAX_LINES = 80;

static String s_lines[LOG_MAX_LINES];
static size_t s_count = 0;

void log_init() {
    for (size_t i = 0; i < LOG_MAX_LINES; ++i) {
        s_lines[i] = "";
    }
    s_count = 0;
}

void log_append(const String& line) {
    // 带毫秒时间戳前缀
    String withTs = String(millis()) + "ms  " + line;
    size_t idx = s_count % LOG_MAX_LINES;
    s_lines[idx] = withTs;
    ++s_count;

    // 同步输出到串口，便于调试
    Serial.println(withTs);
}

String log_get_all() {
    String out;
    if (s_count == 0) {
        return out;
    }

    size_t total = (s_count < LOG_MAX_LINES) ? s_count : LOG_MAX_LINES;
    size_t start = (s_count > LOG_MAX_LINES) ? (s_count - LOG_MAX_LINES) : 0;

    for (size_t i = 0; i < total; ++i) {
        size_t idx = (start + i) % LOG_MAX_LINES;
        out += s_lines[idx];
        if (i + 1 < total) out += "\n";
    }
    return out;
}

