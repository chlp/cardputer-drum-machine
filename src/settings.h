#pragma once

#include <stdint.h>

extern uint8_t  masterVolume;
extern uint32_t volumeDisplayUntilMs;
extern bool     prevKeyPlus;
extern bool     prevKeyMinus;

void saveSettings();
void loadSettings();
void applyVolume(int delta);
void drawVolumeOverlay();
