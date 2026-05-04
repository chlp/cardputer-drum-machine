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
                // If the main task requested an abort, release the mutex
                // immediately and yield so the main task can take it.
                // Continuing to call gen->loop() would stall here because
                // each SD read takes ~5-10 ms; the main task (lower priority)
                // would starve indefinitely and trigger the watchdog.
                if (spk && spk->isAbortRequested()) {
                    xSemaphoreGive(s_mutex);
                    vTaskDelay(pdMS_TO_TICKS(5));
                } else {
                    if (!gen->loop()) {
                        spk->flush();
                        gen->stop();
                        audioEndedNaturally = true;
                    }
                    xSemaphoreGive(s_mutex);
                    // No yield: keep pumping the decoder as fast as possible to
                    // maintain the speaker double-buffer while audio is active.
                }
            } else {
                xSemaphoreGive(s_mutex);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            vTaskDelay(1);
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
    // Step 1: abort current playback and tear down old objects under the mutex.
    if (spk) spk->requestAbort();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (gen) { gen->stop(); delete gen; gen = nullptr; }
    if (src) { delete src; src = nullptr; }
    if (spk) spk->stop(); // resets _abortRequested
    audioEndedNaturally = false;
    xSemaphoreGive(s_mutex); // release before touching SD — open can take 50-200 ms

    // Step 2: open the file outside the mutex so the audio task can idle-loop
    // and the main loop is not blocked any longer than necessary.
    AudioFileSourceSD *newSrc = new AudioFileSourceSD(path);
    if (!newSrc->isOpen()) {
        delete newSrc;
        return false;
    }

    // Step 3: hand the new source + decoder to the audio task.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    src = newSrc;
    gen = new AudioGeneratorMP3();
    gen->begin(src, spk);
    xSemaphoreGive(s_mutex);
    return true;
}
