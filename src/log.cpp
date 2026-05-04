#include "log.h"

#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <stdarg.h>
#include <stdio.h>

void logLine(const char *tag, const char *fmt, ...) {
    char   buf[192];
    int    n = snprintf(buf, sizeof(buf), "[%lu][%s] ", (unsigned long)millis(), tag);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    (void)m; // truncation is fine — snprintf won't overflow
    Serial.println(buf);
}

static const char *resetReasonStr(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

void logBoot() {
    esp_reset_reason_t r = esp_reset_reason();
    logLine("BOOT", "reset=%s code=%d", resetReasonStr(r), (int)r);
    logLine("BOOT", "idf=%s arduino=%d.%d.%d",
            esp_get_idf_version(),
            ESP_ARDUINO_VERSION_MAJOR,
            ESP_ARDUINO_VERSION_MINOR,
            ESP_ARDUINO_VERSION_PATCH);
    logHeap("BOOT");
}

void logHeap(const char *tag) {
    size_t free_int    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t min_int     = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free_ps     = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    logLine(tag, "heap free=%u min=%u largest=%u psram_free=%u",
            (unsigned)free_int,
            (unsigned)min_int,
            (unsigned)largest_int,
            (unsigned)free_ps);
}

void logTaskStack(const char *tag, TaskHandle_t handle) {
    if (handle == nullptr) return;
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(handle);
    // hwm is reported in StackType_t units (4 bytes on Xtensa).
    logLine(tag, "task_stack_min_free=%u bytes",
            (unsigned)(hwm * sizeof(StackType_t)));
}
