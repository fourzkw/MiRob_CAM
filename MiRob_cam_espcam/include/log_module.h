/**
 * 简单日志模块：在内存中保存最近若干条日志，
 * 既输出到串口，也可通过 HTTP 接口在网页查看。
 */
#ifndef LOG_MODULE_H
#define LOG_MODULE_H

#include <Arduino.h>

// 初始化日志系统（清空缓冲）
void log_init();

// 追加一条日志（会自动加上毫秒时间前缀）
void log_append(const String& line);

// 获取全部日志，以多行文本形式返回
String log_get_all();

#endif // LOG_MODULE_H

