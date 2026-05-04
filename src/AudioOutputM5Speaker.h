#pragma once
#include <AudioOutput.h>
#include <M5Unified.h>

// Bridges ESP8266Audio to M5Unified Speaker using triple-buffered stereo output.
//
// Why three buffers?  M5Unified Speaker holds a 2-slot queue per channel
// (current + next wav_info_t).  At any instant the speaker is reading from
// one buffer (current) and the other (next) is queued.  If we used only two
// buffers — like the obvious double-buffer pattern — the next `playRaw` call
// would have to overwrite the buffer that the speaker is still draining via
// DMA, which corrupts the samples and is heard as a constant hiss / static
// underneath the music.  Speaker_Class.hpp itself says: "If you want to use
// the data generated at runtime, you can either have three buffers and use
// them in sequence, or have two buffers and use them alternately, then split
// them in half and call playRaw twice."  We take the three-buffer route
// (same approach AdvanceOS uses on the same hardware).
//
// Why stereo?  Mixing L+R down to mono before sending it to the speaker
// works, but it forces the speaker to up-convert mono → stereo internally
// and the extra averaging in `ConsumeSample` is wasted work.  Sending the
// stereo pair untouched lets the speaker driver play it directly.
//
// Abort handling: stopAudio() / startMp3() must be able to interrupt a
// pending `playRaw` quickly so the audio task releases the mutex.  When the
// abort flag is set, `flushBuffer` skips the blocking call and simply drops
// the half-filled buffer.  See audio.cpp for the threading details.
class AudioOutputM5Speaker : public AudioOutput {
public:
    // 1536 int16_t slots = 768 stereo frames ≈ 17.4 ms @ 44.1 kHz per buffer.
    // Three buffers in flight ≈ 52 ms of head-room before a decoder stall
    // turns into an audible underrun.
    static constexpr size_t BUF_SLOTS = 1536;
    static constexpr size_t NUM_BUFS  = 3;

    AudioOutputM5Speaker(m5::Speaker_Class* spk, uint8_t ch = 0)
        : _spk(spk), _ch(ch), _idx(0), _pos(0), _abortRequested(false) {}

    bool begin() override { return true; }

    // Signal that a stop is imminent — flushBuffer() will skip playRaw() so
    // the audio task does not block waiting for a free DMA slot.
    void requestAbort() { _abortRequested = true; }
    bool isAbortRequested() const { return _abortRequested; }

    bool ConsumeSample(int16_t sample[2]) override {
        if (_abortRequested) return true; // drain decoder without outputting audio

        if (_pos + 1 < BUF_SLOTS) {
            _bufs[_idx][_pos++] = sample[0];
            _bufs[_idx][_pos++] = sample[1];
            return true;
        }

        // Buffer full: hand it to the speaker.  Returning false tells the
        // generator to stop feeding for this iteration; it will retry on
        // the next loop() call once we have rotated to a fresh buffer.
        flushBuffer();
        return false;
    }

    bool stop() override {
        _abortRequested = false; // reset for next playback session
        _pos = 0;
        _spk->stop(_ch);
        return true;
    }

    // Call after the last gen->loop() returns false to push remaining samples.
    void flush() override { flushBuffer(); }

private:
    void flushBuffer() {
        if (_pos == 0 || _abortRequested) { _pos = 0; return; }
        // stop_current_sound = false → enqueue, do not interrupt the
        // currently playing buffer.  The call blocks (yielding the task)
        // until the speaker frees a queue slot.
        _spk->playRaw(_bufs[_idx], _pos, hertz, true /*stereo*/, 1, _ch, false);
        _idx = (_idx + 1) % NUM_BUFS;
        _pos = 0;
    }

    m5::Speaker_Class* _spk;
    uint8_t  _ch;
    int16_t  _bufs[NUM_BUFS][BUF_SLOTS];
    uint8_t  _idx;
    size_t   _pos;
    volatile bool _abortRequested;
};
