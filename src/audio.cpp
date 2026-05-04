#include "audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

AudioOutputM5Speaker *spk = nullptr;
AudioGeneratorMP3    *gen = nullptr;
AudioFileSourceSD    *src = nullptr;
volatile bool         audioEndedNaturally = false;

static SemaphoreHandle_t s_mutex     = nullptr;
static TaskHandle_t      s_audioTask = nullptr;

// Runs on core 0.  Calls gen->loop() (which blocks in playRaw) without ever
// touching the main-loop core, so keyboard / UI remain fully responsive.
static void audioTaskFn(void *) {
    for (;;) {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5))) {
            if (gen && gen->isRunning()) {
                if (!gen->loop()) {
                    spk->flush();
                    gen->stop();
                    audioEndedNaturally = true;
                }
                xSemaphoreGive(s_mutex);
                // No yield: keep pumping the decoder as fast as possible to
                // maintain the speaker double-buffer while audio is active.
            } else {
                xSemaphoreGive(s_mutex);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }
}

void audioTaskInit() {
    s_mutex = xSemaphoreCreateMutex();
    // Priority 2 > Arduino loop priority (1), pinned to core 0.
    xTaskCreatePinnedToCore(audioTaskFn, "audio", 8192, nullptr, 2, &s_audioTask, 0);
}

void stopAudio() {
    // Tell the output to skip the next blocking playRaw() call so the audio task
    // releases the mutex within microseconds rather than waiting a full DMA period.
    if (spk) spk->requestAbort();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (gen) { gen->stop(); delete gen; gen = nullptr; }
    if (src) { delete src; src = nullptr; }
    if (spk) spk->stop(); // also resets _abortRequested
    audioEndedNaturally = false;
    xSemaphoreGive(s_mutex);
}

bool startMp3(const char *path) {
    if (spk) spk->requestAbort(); // unblock any in-flight playRaw() before locking
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (gen) { gen->stop(); delete gen; gen = nullptr; }
    if (src) { delete src; src = nullptr; }
    if (spk) spk->stop(); // resets _abortRequested
    audioEndedNaturally = false;

    src = new AudioFileSourceSD(path);
    if (!src->isOpen()) {
        delete src; src = nullptr;
        xSemaphoreGive(s_mutex);
        return false;
    }
    gen = new AudioGeneratorMP3();
    gen->begin(src, spk);
    xSemaphoreGive(s_mutex);
    return true;
}
