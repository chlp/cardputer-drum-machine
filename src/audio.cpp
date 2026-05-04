#include "audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Pre-allocate the MP3 decoder's internal buffers as a static array so they
// are never new/delete'd.  This eliminates heap fragmentation and the OOM
// panics (abort() → reboot) that occur when switching between sounds while the
// previous decoder still holds its allocations.
static uint8_t           s_mp3Buf[AudioGeneratorMP3::preAllocSize()];
static AudioGeneratorMP3 s_genObj(s_mp3Buf, sizeof(s_mp3Buf));
static AudioFileSourceSD s_srcObj;

AudioOutputM5Speaker *spk = nullptr;
// gen / src are non-null only while a stream is active.
// They point to the static objects above — never heap-allocated.
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
                        // gen->stop() closes the SD file internally.
                        gen->stop();
                        gen = nullptr;
                        src = nullptr;
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
    // libmad uses ~10-12 KB of stack for complex stereo frames; 16 KB gives
    // enough headroom to avoid stack overflow → heap corruption.
    xTaskCreatePinnedToCore(audioTaskFn, "audio", 16384, nullptr, 2, &s_audioTask, 0);
}

void stopAudio() {
    // Tell the output to skip the next blocking playRaw() call so the audio
    // task releases the mutex quickly rather than waiting a full DMA period.
    if (spk) spk->requestAbort();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (gen) { gen->stop(); gen = nullptr; }
    // src->close() is safe even if the file was already closed by gen->stop().
    if (src) { src->close(); src = nullptr; }
    if (spk) spk->stop(); // also resets _abortRequested
    audioEndedNaturally = false;
    xSemaphoreGive(s_mutex);
}

bool startMp3(const char *path) {
    // Step 1: abort current playback.  After this block the audio task is
    // idle (gen == nullptr) and will not touch the SD bus.
    if (spk) spk->requestAbort();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (gen) { gen->stop(); gen = nullptr; }
    if (src) { src->close(); src = nullptr; }
    if (spk) spk->stop(); // resets _abortRequested
    audioEndedNaturally = false;
    xSemaphoreGive(s_mutex); // release before touching SD

    // Step 2: open the file.  The audio task is idle so there is no concurrent
    // SPI access — the caller must guarantee they have already stopped audio
    // before loading any other SD data (images etc.) for the same reason.
    if (!s_srcObj.open(path)) return false;

    // Step 3: hand the static decoder + source to the audio task.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    src = &s_srcObj;
    gen = &s_genObj;
    gen->begin(src, spk);
    xSemaphoreGive(s_mutex);
    return true;
}
