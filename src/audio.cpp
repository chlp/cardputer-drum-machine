#include "audio.h"

AudioOutputM5Speaker *spk = nullptr;
AudioGeneratorMP3    *gen = nullptr;
AudioFileSourceSD    *src = nullptr;

void stopAudio() {
    if (gen) { gen->stop(); delete gen; gen = nullptr; }
    if (src) { delete src; src = nullptr; }
    if (spk) spk->stop();
}

bool startMp3(const char *path) {
    stopAudio();
    src = new AudioFileSourceSD(path);
    if (!src->isOpen()) { delete src; src = nullptr; return false; }
    gen = new AudioGeneratorMP3();
    gen->begin(src, spk);
    return true;
}
