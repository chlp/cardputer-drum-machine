#include "audio.h"
#include "log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <SD.h>

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
TaskHandle_t          audioTaskHandle     = nullptr;

static SemaphoreHandle_t s_mutex = nullptr;

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
                        logLine("AUDIO", "stream ended (natural)");
                        logHeap("AUDIO");
                        logTaskStack("AUDIO", audioTaskHandle);
                    }
                    xSemaphoreGive(s_mutex);
                    // Yield 1 tick (~1 ms) so the IDLE0 task gets CPU time
                    // and the IDF task watchdog stays fed.  Without this,
                    // a long (~5 s) MP3 decode session can starve IDLE0 →
                    // ESP_RST_TASK_WDT reboot.  We have ~17 ms of buffered
                    // audio per chunk so 1 ms here is harmless.
                    vTaskDelay(1);
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
    // libmad uses ~10-12 KB of stack for complex stereo frames; some MP3s
    // (variable bit-rate, joint-stereo) push that higher.  20 KB gives a
    // safety margin so the high-water mark stays well clear of overflow
    // even on the longest tracks.  Watch the AUDIO task_stack_min_free
    // log line — if it ever drops below ~2 KB, bump this further.
    xTaskCreatePinnedToCore(audioTaskFn, "audio", 20480, nullptr, 2, &audioTaskHandle, 0);
    logLine("AUDIO", "task started stack=20480 prio=2 core=0");
}

void stopAudio() {
    // Tell the output to skip the next blocking playRaw() call so the audio
    // task releases the mutex quickly rather than waiting a full DMA period.
    if (spk) spk->requestAbort();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool wasPlaying = (gen != nullptr);
    if (gen) { gen->stop(); gen = nullptr; }
    // src->close() is safe even if the file was already closed by gen->stop().
    if (src) { src->close(); src = nullptr; }
    if (spk) spk->stop(); // also resets _abortRequested
    audioEndedNaturally = false;
    xSemaphoreGive(s_mutex);
    if (wasPlaying) {
        logLine("AUDIO", "stop");
        logHeap("AUDIO");
        logTaskStack("AUDIO", audioTaskHandle);
    }
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

    // Step 2: peek file size for diagnostics — open() inside AudioFileSourceSD
    // doesn't expose size before .begin() so we look it up via the SD API.
    uint32_t fileSize = 0;
    {
        File probe = SD.open(path, FILE_READ);
        if (probe) {
            fileSize = (uint32_t)probe.size();
            probe.close();
        }
    }

    // Step 3: open the file.  The audio task is idle so there is no concurrent
    // SPI access — the caller must guarantee they have already stopped audio
    // before loading any other SD data (images etc.) for the same reason.
    if (!s_srcObj.open(path)) {
        logLine("AUDIO", "open FAILED path=%s size=%u", path, (unsigned)fileSize);
        logHeap("AUDIO");
        return false;
    }

    // Step 4: hand the static decoder + source to the audio task.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    src = &s_srcObj;
    gen = &s_genObj;
    bool ok = gen->begin(src, spk);
    if (!ok) {
        gen->stop();
        gen = nullptr;
        src->close();
        src = nullptr;
    }
    xSemaphoreGive(s_mutex);

    if (!ok) {
        logLine("AUDIO", "begin FAILED path=%s size=%u", path, (unsigned)fileSize);
        logHeap("AUDIO");
        return false;
    }

    logLine("AUDIO", "play path=%s size=%u", path, (unsigned)fileSize);
    logHeap("AUDIO");
    logTaskStack("AUDIO", audioTaskHandle);
    return true;
}
