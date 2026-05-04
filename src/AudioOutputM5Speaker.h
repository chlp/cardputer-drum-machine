#pragma once
#include <AudioOutput.h>
#include <M5Unified.h>

// Bridges ESP8266Audio to M5Unified Speaker using its internal double-buffer.
// playRaw with stop_current=false blocks until the speaker frees the next slot,
// which gives natural backpressure for real-time streaming.
//
// Problem: the audio task holds s_mutex while gen->loop() runs, and gen->loop()
// eventually calls flushBuffer() → playRaw() which can block for ~11 ms waiting
// for a DMA slot. If the main loop calls stopAudio() during that time, it blocks
// on xSemaphoreTake(s_mutex, portMAX_DELAY) indefinitely — the UI freezes while
// music keeps playing via DMA.
//
// Fix: requestAbort() sets a flag that makes flushBuffer() skip the blocking
// playRaw() call, so the audio task exits gen->loop() promptly and releases
// the mutex. stopAudio() / startMp3() call requestAbort() before taking the mutex.
class AudioOutputM5Speaker : public AudioOutput {
public:
    // 1024 samples @ 44100 Hz ≈ 23 ms per chunk; doubles latency margin vs 512
    // and reduces the chance of a buffer underrun when the main task briefly
    // stalls the audio task (e.g. during display redraws).
    static const size_t BUF_SAMPLES = 1024;

    AudioOutputM5Speaker(m5::Speaker_Class* spk, uint8_t ch = 0)
        : _spk(spk), _ch(ch), _pos(0), _flip(0), _abortRequested(false) {}

    bool begin() override { return true; }

    // Signal that a stop is imminent — flushBuffer() will skip playRaw() so the
    // audio task doesn't block while holding the mutex.
    void requestAbort() { _abortRequested = true; }
    bool isAbortRequested() const { return _abortRequested; }

    bool ConsumeSample(int16_t sample[2]) override {
        if (_abortRequested) return true; // drain decoder without outputting audio
        int16_t mono = (int16_t)(((int32_t)sample[0] + sample[1]) >> 1);
        _bufs[_flip][_pos++] = Amplify(mono);
        if (_pos >= BUF_SAMPLES) flushBuffer();
        return true;
    }

    bool stop() override {
        _abortRequested = false; // reset for next playback session
        _pos = 0;
        _spk->stop(_ch);
        return true;
    }

    // Call after the last gen->loop() returns false to push remaining samples.
    void flush() { flushBuffer(); }

private:
    void flushBuffer() {
        if (_pos == 0 || _abortRequested) { _pos = 0; return; }
        // Blocks until the speaker's double-buffer has a free slot.
        _spk->playRaw(_bufs[_flip], _pos, hertz, false, 1, _ch, false);
        _flip ^= 1; // switch to the other buffer while speaker consumes the current one
        _pos = 0;
    }

    m5::Speaker_Class* _spk;
    uint8_t  _ch;
    int16_t  _bufs[2][BUF_SAMPLES];
    size_t   _pos;
    uint8_t  _flip;
    volatile bool _abortRequested;
};
