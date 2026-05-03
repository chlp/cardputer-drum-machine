#pragma once

#include <stdint.h>

// Physical keyboard rows, bottom → top = low → high notes:
//   z x c v b n m         →  C3 … F#3   (7 semitones)
//   a s d f g h j k l     →  G3 … D#4   (9 semitones)
//   q w e r t y u i o p   →  E4 … C#5   (10 semitones)
//   1 2 3 4 5 6 7 8 9 0   →  D5 … B5    (10 semitones)
// Total: 36 notes, chromatic C3–B5.

extern const char     NOTE_KEY[36];
extern const uint32_t NOTE_FREQ[36];
extern const char    *NOTE_NAME[36];

// reverse lookup: ASCII → note index (−1 = not a note key)
extern int8_t keyNoteIdx[256];

// Polyphony state. Speaker channel 0 is used by AudioOutputM5Speaker (MP3).
// Note channels: 1–7 (NOTE_CH_BASE … NOTE_CH_BASE + NOTE_CH_CNT − 1).
extern bool   noteActive[256];
extern int8_t noteKeyChannel[256];
extern char   channelNoteKey[8];

void initNotes();
void noteOn(char key);
void noteOff(char key);
void stopAllNotes();
