#include "ui_utils.h"
#include <SD.h>
#include "esp_heap_caps.h"

// Maximum image size we will load into RAM.  Larger files are skipped and the
// caller falls back to the colour-tile.  240×135 RGB565 = ~63 KB; 200 KB is a
// generous ceiling that still leaves heap headroom for the MP3 decoder.
static constexpr size_t MAX_IMAGE_BUF = 200 * 1024;

bool isEscKey(const Keyboard_Class::KeysState &st) {
    for (char c : st.word) if (c == '`' || c == 27) return true;
    return false;
}

void clearStatusBar() {
    M5Cardputer.Display.fillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, TFT_BLACK);
}

uint8_t *readFileToBuffer(const char *path, size_t &outLen) {
    File f = SD.open(path, FILE_READ);
    if (!f) return nullptr;
    outLen = f.size();
    // Refuse oversized files — protects the heap and ensures the buffer lands
    // in DMA-capable internal RAM (required by the SPI SD driver).
    if (outLen == 0 || outLen > MAX_IMAGE_BUF) { f.close(); return nullptr; }
    // MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA guarantees the buffer is in
    // internal DRAM so SD SPI-DMA writes work correctly even when PSRAM is
    // enabled.  plain free() is safe for heap_caps_malloc pointers on ESP32.
    uint8_t *buf = (uint8_t *)heap_caps_malloc(outLen, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf) { f.close(); return nullptr; }
    f.read(buf, outLen);
    f.close();
    return buf;
}
