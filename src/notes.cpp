#include "notes.h"
#include "config.h"
#include <M5Cardputer.h>
#include <string.h>

const char NOTE_KEY[36] = {
    'z','x','c','v','b','n','m',
    'a','s','d','f','g','h','j','k','l',
    'q','w','e','r','t','y','u','i','o','p',
    '1','2','3','4','5','6','7','8','9','0'
};

const uint32_t NOTE_FREQ[36] = {
    131,139,147,156,165,175,185,
    196,208,220,233,247,262,277,294,311,
    330,349,370,392,415,440,466,494,523,554,
    587,622,659,698,740,784,831,880,932,988
};

const char *NOTE_NAME[36] = {
    "C3","C#3","D3","D#3","E3","F3","F#3",
    "G3","G#3","A3","A#3","B3","C4","C#4","D4","D#4",
    "E4","F4","F#4","G4","G#4","A4","A#4","B4","C5","C#5",
    "D5","D#5","E5","F5","F#5","G5","G#5","A5","A#5","B5"
};

int8_t keyNoteIdx[256];
bool   noteActive[256]     = {};
int8_t noteKeyChannel[256];
char   channelNoteKey[8]   = {};

void initNotes() {
    memset(keyNoteIdx,     -1, sizeof(keyNoteIdx));
    memset(noteKeyChannel, -1, sizeof(noteKeyChannel));
    memset(channelNoteKey,  0, sizeof(channelNoteKey));
    for (int i = 0; i < 36; i++) keyNoteIdx[(uint8_t)NOTE_KEY[i]] = i;
}

void noteOn(char key) {
    uint8_t k = (uint8_t)key;
    int idx = keyNoteIdx[k];
    if (idx < 0 || noteActive[k]) return;

    int ch = -1;
    for (int i = NOTE_CH_BASE; i < NOTE_CH_BASE + NOTE_CH_CNT; i++) {
        if (!channelNoteKey[i]) { ch = i; break; }
    }
    if (ch < 0) {
        // Steal the lowest channel
        ch = NOTE_CH_BASE;
        char stolen = channelNoteKey[ch];
        M5.Speaker.stop(ch);
        noteActive[(uint8_t)stolen]     = false;
        noteKeyChannel[(uint8_t)stolen] = -1;
    }

    channelNoteKey[ch] = key;
    noteKeyChannel[k]  = ch;
    noteActive[k]      = true;
    M5.Speaker.tone(NOTE_FREQ[idx], 0, ch, false); // indefinite, non-interrupting
}

void noteOff(char key) {
    uint8_t k = (uint8_t)key;
    int ch = noteKeyChannel[k];
    if (ch < 0) return;
    M5.Speaker.stop(ch);
    channelNoteKey[ch] = 0;
    noteKeyChannel[k]  = -1;
    noteActive[k]      = false;
}

void stopAllNotes() {
    for (int i = NOTE_CH_BASE; i < NOTE_CH_BASE + NOTE_CH_CNT; i++) {
        if (channelNoteKey[i]) {
            M5.Speaker.stop(i);
            noteActive[(uint8_t)channelNoteKey[i]]     = false;
            noteKeyChannel[(uint8_t)channelNoteKey[i]] = -1;
            channelNoteKey[i] = 0;
        }
    }
}
