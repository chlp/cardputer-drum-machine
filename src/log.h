#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Lightweight Serial logger.  Output format: `[<millis>][TAG] message`.
// The leading `[` + `][` pattern matches the regex used by tools/sd_xfer.py to
// drop ESP-IDF style log lines, so the SD serial transfer protocol is never
// confused by these messages.
//
// Usage:
//   logLine("AUDIO", "play %s size=%u", path, size);
//   logHeap("AUDIO");                 // free / min / largest contiguous / psram
//   logTaskStack("AUDIO", taskHandle);
//   logBoot();                        // reset reason + IDF/Arduino version + heap

void logLine(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void logBoot();
void logHeap(const char *tag);
void logTaskStack(const char *tag, TaskHandle_t handle);
