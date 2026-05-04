#pragma once

#include <AudioGeneratorMP3.h>
#include <AudioFileSourceSD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "AudioOutputM5Speaker.h"

extern AudioOutputM5Speaker *spk;
extern AudioGeneratorMP3    *gen;
extern AudioFileSourceSD    *src;

// Set to true by the audio task when a stream ends naturally.
// Cleared by the main loop after it handles auto-advance / state reset.
extern volatile bool audioEndedNaturally;

// Handle of the FreeRTOS audio task — used by log.cpp for stack high-water-mark
// diagnostics.  nullptr until audioTaskInit() has run.
extern TaskHandle_t audioTaskHandle;

// Must be called once in setup(), after spk is initialised.
void audioTaskInit();

void stopAudio();
bool startMp3(const char *path);
