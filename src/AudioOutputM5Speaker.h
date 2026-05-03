#pragma once
#include <AudioOutput.h>
#include <M5Unified.h>

// Bridges ESP8266Audio to M5Unified Speaker using its internal double-buffer.
// playRaw with stop_current=false blocks until the speaker frees the next slot,
// which gives natural backpressure for real-time streaming.
class AudioOutputM5Speaker : public AudioOutput {
public:
    static const size_t BUF_SAMPLES = 512;

    AudioOutputM5Speaker(m5::Speaker_Class* spk, uint8_t ch = 0)
        : _spk(spk), _ch(ch), _pos(0), _flip(0) {}

    bool begin() override { return true; }

    bool ConsumeSample(int16_t sample[2]) override {
        // Mix stereo to mono; Amplify() from AudioOutput applies gainF2P6
        int16_t mono = (int16_t)(((int32_t)sample[0] + sample[1]) >> 1);
        _bufs[_flip][_pos++] = Amplify(mono);
        if (_pos >= BUF_SAMPLES) flushBuffer();
        return true;
    }

    bool stop() override {
        _pos = 0;
        _spk->stop(_ch);
        return true;
    }

    // Call after the last gen->loop() returns false to push remaining samples
    void flush() { flushBuffer(); }

private:
    void flushBuffer() {
        if (_pos == 0) return;
        // Blocks until the speaker's double-buffer has a free slot
        _spk->playRaw(_bufs[_flip], _pos, hertz, false, 1, _ch, false);
        _flip ^= 1; // switch to the other buffer while speaker consumes the current one
        _pos = 0;
    }

    m5::Speaker_Class* _spk;
    uint8_t _ch;
    int16_t _bufs[2][BUF_SAMPLES];
    size_t  _pos;
    uint8_t _flip;
};
