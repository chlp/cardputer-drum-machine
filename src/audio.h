#pragma once

#include <AudioGeneratorMP3.h>
#include <AudioFileSourceSD.h>
#include "AudioOutputM5Speaker.h"

extern AudioOutputM5Speaker *spk;
extern AudioGeneratorMP3    *gen;
extern AudioFileSourceSD    *src;

void stopAudio();
bool startMp3(const char *path);
