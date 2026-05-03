#include "ui_utils.h"
#include <SD.h>

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
    uint8_t *buf = (uint8_t *)malloc(outLen);
    if (!buf) { f.close(); return nullptr; }
    f.read(buf, outLen);
    f.close();
    return buf;
}
