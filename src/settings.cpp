#include "settings.h"
#include "config.h"
#include <M5Cardputer.h>
#include <SD.h>

extern bool sdReady;
extern int  soundboardBoardIdx;

uint8_t  masterVolume         = 200;
uint32_t volumeDisplayUntilMs = 0;
bool     prevKeyPlus          = false;
bool     prevKeyMinus         = false;

void drawVolumeOverlay() {
    auto &d = M5Cardputer.Display;
    d.fillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, TFT_BLACK);
    uint8_t level = masterVolume / VOL_STEP;
    char buf[20];
    snprintf(buf, sizeof(buf), "VOL  %2d / 15", (int)level);
    d.setTextSize(1);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.drawCenterString(buf, SCREEN_W / 2, STATUS_Y + 2);
    const int bx = 10, by = STATUS_Y + 13, bw = SCREEN_W - 20, bh = 8;
    int filled = bw * masterVolume / 255;
    d.fillRect(bx, by, bw, bh, 0x2104);
    d.fillRect(bx, by, filled, bh, TFT_YELLOW);
}

void saveSettings() {
    if (!sdReady) return;
    File f = SD.open(SETTINGS_PATH, FILE_WRITE);
    if (!f) return;
    f.printf("volume=%d\n", (int)masterVolume);
    f.printf("panel=%d\n",  soundboardBoardIdx);
    f.close();
}

void loadSettings() {
    if (!sdReady) return;
    File f = SD.open(SETTINGS_PATH, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        int    val = line.substring(eq + 1).toInt();
        if      (key == "volume") masterVolume       = (uint8_t)constrain(val, 0, 255);
        else if (key == "panel")  soundboardBoardIdx = constrain(val, 0, 999);
    }
    f.close();
}

void applyVolume(int delta) {
    int v = (int)masterVolume + delta;
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    masterVolume = (uint8_t)v;
    M5.Speaker.setVolume(masterVolume);
    volumeDisplayUntilMs = millis() + VOL_DISPLAY_MS;
    drawVolumeOverlay();
    saveSettings();
}
