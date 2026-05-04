#include "soundboard.h"
#include "config.h"
#include "audio.h"
#include "notes.h"
#include "ui_utils.h"
#include <SD.h>
#include <vector>
#include <algorithm>

extern bool sdReady;

std::vector<String> boardPaths;
int                 soundboardBoardIdx = 0;
String              soundboardDir;
bool                boardSplashActive  = false;
bool                sdSoundActive      = false;
char                sbCurKey           = 'z';
bool                sbPrevComma        = false;
bool                sbPrevSlash        = false;

static std::vector<char> prevSbKeys;
static bool  prevNoteActive[256]  = {};
static bool  pianoNeedsFullRedraw = true;

// Browse-mode debounce state.
// sbLastNavMs  — millis() of the last navigation / key-change event.
//                drawSoundboardBrowse() shows a fast colour tile when the caller
//                is too close to this timestamp (< BROWSE_RAPID_NAV_MS).
// sbAfterMs    — millis() threshold after which the pending action fires.
// sbImagePending — load full JPEG after , / navigation settles.
// sbPlayPending  — auto-play + show image after letter-key debounce fires.
static uint32_t sbLastNavMs   = 0;
static uint32_t sbAfterMs     = 0;
static bool     sbImagePending = false;
static bool     sbPlayPending  = false;

// ── Helpers ───────────────────────────────────────────────────────────────────

static String sdEntryBaseName(const String &entryName) {
    int p = entryName.lastIndexOf('/');
    return (p >= 0) ? entryName.substring(p + 1) : entryName;
}

static bool tryDrawImageFromDir(const String &dir, char key) {
    if (!sdReady || dir.length() == 0) return false;
    char path[64];
    size_t len = 0;
    uint8_t *buf;
    snprintf(path, sizeof(path), "%s/%c.jpg", dir.c_str(), key);
    if (SD.exists(path)) {
        buf = readFileToBuffer(path, len);
        if (buf) { M5Cardputer.Display.drawJpg(buf, len, 0, 0, SCREEN_W, IMG_H); free(buf); return true; }
    }
    snprintf(path, sizeof(path), "%s/%c.png", dir.c_str(), key);
    if (SD.exists(path)) {
        buf = readFileToBuffer(path, len);
        if (buf) { M5Cardputer.Display.drawPng(buf, len, 0, 0, SCREEN_W, IMG_H); free(buf); return true; }
    }
    return false;
}

static bool showImageForKey(char key) {
    if (soundboardDir.length() > 0) {
        if (tryDrawImageFromDir(soundboardDir, key)) return true;
        if (soundboardDir != String(BOARDS_MEME)) {
            if (tryDrawImageFromDir(String(BOARDS_MEME), key)) return true;
        }
    } else {
        if (tryDrawImageFromDir(String(BOARDS_MEME), key)) return true;
    }
    return false;
}

// ── Public API ────────────────────────────────────────────────────────────────

void scanSoundboardDirs() {
    boardPaths.clear();
    if (!sdReady) return;
    File root = SD.open(BOARDS_DIR);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    std::vector<String> names;
    while (true) {
        File f = root.openNextFile();
        if (!f) break;
        if (f.isDirectory()) names.push_back(sdEntryBaseName(String(f.name())));
        f.close();
    }
    root.close();
    std::sort(names.begin(), names.end());
    // Ensure "meme" is always first
    for (size_t i = 0; i < names.size(); i++) {
        String low = names[i]; low.toLowerCase();
        if (low == "meme") {
            String m = names[i];
            names.erase(names.begin() + (int)i);
            names.insert(names.begin(), m);
            break;
        }
    }
    for (auto &n : names) {
        if (n.length() == 0) continue;
        boardPaths.push_back(String(BOARDS_DIR) + "/" + n);
    }
}

bool resolveMemeMp3ForKey(char key, char *path, size_t pathCap) {
    if (!sdReady) return false;
    if (soundboardDir.length() > 0) {
        snprintf(path, pathCap, "%s/%c.mp3", soundboardDir.c_str(), key);
        if (SD.exists(path)) return true;
        if (soundboardDir != String(BOARDS_MEME)) {
            snprintf(path, pathCap, "%s/%c.mp3", BOARDS_MEME, key);
            if (SD.exists(path)) return true;
        }
    } else {
        snprintf(path, pathCap, "%s/%c.mp3", BOARDS_MEME, key);
        if (SD.exists(path)) return true;
    }
    return false;
}

bool useSoundboardBrowseUI() {
    return sdReady && !boardPaths.empty() && soundboardDir.length() > 0;
}

void sbInitBrowseSelection() {
    char path[64];
    sbCurKey = NOTE_KEY[0];
    for (int i = 0; i < 36; i++) {
        if (resolveMemeMp3ForKey(NOTE_KEY[i], path, sizeof(path))) {
            sbCurKey = NOTE_KEY[i];
            return;
        }
    }
}

void sbResetInputState() {
    prevSbKeys.clear();
    sbPrevComma = sbPrevSlash = false;
    sbPlayPending = sbImagePending = false;
    sbLastNavMs = 0;
}

static void sbStepPlayable(int dir) {
    char path[64];
    int start = 0;
    for (int i = 0; i < 36; i++) {
        if (NOTE_KEY[i] == sbCurKey) { start = i; break; }
    }
    for (int step = 1; step <= 36; step++) {
        int i = (start + dir * step + 144) % 36;
        if (resolveMemeMp3ForKey(NOTE_KEY[i], path, sizeof(path))) {
            sbCurKey = NOTE_KEY[i];
            return;
        }
    }
    sbCurKey = NOTE_KEY[(start + dir + 36) % 36];
}

// Fast colour tile — no SD access, always instant.
static void drawColorTileForKey(char key) {
    auto &d = M5Cardputer.Display;
    static const uint16_t pal[] = {
        0xF800,0xFA60,0xFFE0,0x87E0,0x07E0,0x07EF,
        0x001F,0x401F,0x780F,0xF81F,0xFC0F,0xFDA0
    };
    int pidx = ((key >= 'a') ? key - 'a' : key - '0' + 26) % 12;
    uint16_t bg = pal[pidx];
    d.fillRect(0, 0, SCREEN_W, IMG_H, bg);
    d.setTextSize(8);
    d.setTextColor(TFT_WHITE, bg);
    char lbl[2] = {(char)toupper(key), 0};
    d.drawCenterString(lbl, SCREEN_W / 2, IMG_H / 2 - 30);
    d.setTextSize(1);
}

// Image or a colour tile for the given key slot (fills rows 0..IMG_H-1).
static void drawMemeKeyPreviewGraphic(char key) {
    if (showImageForKey(key)) return;
    drawColorTileForKey(key);
}

// rapidNav=true  → colour tile only (no SD); used while navigating quickly.
// rapidNav=false → full image from SD (may fall back to colour tile).
static void drawSoundboardBrowse(char key, bool rapidNav = false) {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    if (rapidNav) {
        drawColorTileForKey(key);
    } else {
        drawMemeKeyPreviewGraphic(key);
    }
    // Small key badge in the bottom-right corner of the image
    const int SZ = 22;
    const int bx = SCREEN_W - SZ;
    const int by = IMG_H - SZ;
    d.fillRect(bx, by, SZ, SZ, TFT_BLACK);
    d.drawRect(bx, by, SZ, SZ, TFT_CYAN);
    d.setTextSize(2);
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    char lbl[2] = {(char)toupper(key), 0};
    d.drawCenterString(lbl, bx + SZ / 2, by + 3);
    d.setTextSize(1);
    clearStatusBar();
}

static void playSoundboardBrowseSelection() {
    char path[64];
    char c = sbCurKey;
    if (resolveMemeMp3ForKey(c, path, sizeof(path))) {
        stopAllNotes();
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        drawMemeKeyPreviewGraphic(c);
        startMp3(path);
        sdSoundActive = true;
        clearStatusBar();
    } else {
        if (sdSoundActive) { stopAudio(); sdSoundActive = false; }
        int ni = keyNoteIdx[(uint8_t)c];
        if (ni >= 0)
            M5.Speaker.tone(NOTE_FREQ[ni], 220, NOTE_CH_BASE, true);
        drawSoundboardBrowse(c);
    }
}

// ── Piano display (36-note strip) ────────────────────────────────────────────

void drawPiano() {
    auto &d = M5Cardputer.Display;

    const int WH = 78;
    const int BH = 47;
    const int TW = SCREEN_W;
    const int NW = 21; // white keys in 3 octaves

    static const int8_t wIdx[12] = { 0,-1,1,-1,2, 3,-1,4,-1,5,-1,6 };
    static const int8_t bLeft[12]= {-1, 0,-1, 1,-1,-1, 3,-1, 4,-1, 5,-1};

    auto drawWhiteKey = [&](int i) {
        int oct = i / 12, sem = i % 12;
        int wn = oct * 7 + wIdx[sem];
        int x0 = (wn * TW) / NW;
        int x1 = ((wn + 1) * TW) / NW;
        uint16_t col = noteActive[(uint8_t)NOTE_KEY[i]] ? TFT_YELLOW : TFT_WHITE;
        d.fillRect(x0, 0, x1 - x0 - 1, WH, col);
        d.drawFastVLine(x0, 0, WH, TFT_BLACK);
    };

    auto drawBlackKey = [&](int i) {
        int oct = i / 12, sem = i % 12;
        int leftWn = oct * 7 + bLeft[sem];
        int cx = ((leftWn + 1) * TW) / NW;
        int bw = max(4, (TW / NW) * 6 / 10);
        int bx = cx - bw / 2;
        uint16_t col = noteActive[(uint8_t)NOTE_KEY[i]] ? TFT_YELLOW : 0x18C6;
        d.fillRect(bx, 0, bw, BH, col);
    };

    if (pianoNeedsFullRedraw) {
        d.fillRect(0, 0, TW, WH + 1, TFT_BLACK);
        for (int i = 0; i < 36; i++) { if (wIdx[i % 12] >= 0) drawWhiteKey(i); }
        d.drawFastVLine(TW - 1, 0, WH, TFT_BLACK);
        d.drawFastHLine(0, WH, TW, TFT_BLACK);
        for (int i = 0; i < 36; i++) { if (bLeft[i % 12] >= 0) drawBlackKey(i); }
        pianoNeedsFullRedraw = false;
    } else {
        bool anyWhiteChanged = false;
        for (int i = 0; i < 36; i++) {
            uint8_t k = (uint8_t)NOTE_KEY[i];
            if (noteActive[k] == prevNoteActive[k]) continue;
            if (wIdx[i % 12] >= 0) { drawWhiteKey(i); anyWhiteChanged = true; }
        }
        if (anyWhiteChanged) {
            for (int i = 0; i < 36; i++) { if (bLeft[i % 12] >= 0) drawBlackKey(i); }
        } else {
            for (int i = 0; i < 36; i++) {
                uint8_t k = (uint8_t)NOTE_KEY[i];
                if (noteActive[k] != prevNoteActive[k] && bLeft[i % 12] >= 0) drawBlackKey(i);
            }
        }
    }

    for (int i = 0; i < 36; i++) prevNoteActive[(uint8_t)NOTE_KEY[i]] = noteActive[(uint8_t)NOTE_KEY[i]];

    String names = "";
    int cnt = 0;
    for (int i = 0; i < 36; i++) {
        if (noteActive[(uint8_t)NOTE_KEY[i]]) {
            if (cnt) names += " ";
            names += NOTE_NAME[i];
            cnt++;
        }
    }
    d.setTextSize(cnt > 3 ? 1 : 2);
    d.setTextColor(cnt ? TFT_YELLOW : 0x7BEF, TFT_BLACK);
    d.fillRect(0, WH + 2, TW, IMG_H - WH - 2, TFT_BLACK);
    d.drawCenterString(cnt ? names : "~ play keys ~", TW / 2, WH + 6);

    clearStatusBar();
}

void drawBoardSplash() {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    String name;
    if (soundboardDir.length() == 0) name = "PIANO";
    else name = sdEntryBaseName(soundboardDir);
    name.toUpperCase();
    if (name.length() > BOARD_TITLE_MAX) name = name.substring(0, BOARD_TITLE_MAX);
    String title = name + " BOARD";
    d.setTextSize(2);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.drawCenterString(title, SCREEN_W / 2, SCREEN_H / 2 - 16);
    d.setTextSize(1);
    d.setTextColor(0x7BEF, TFT_BLACK);
    d.drawCenterString("TAB = next board   + - = vol", SCREEN_W / 2, SCREEN_H / 2 + 6);
}

void soundboardRefresh() {
    if (useSoundboardBrowseUI()) drawSoundboardBrowse(sbCurKey);
    else { pianoNeedsFullRedraw = true; drawPiano(); }
}

void soundboardDrawIdle() {
    stopAllNotes();
    sdSoundActive      = false;
    sbPlayPending      = false;
    sbImagePending     = false;
    soundboardRefresh();
}

// Called every loop() iteration while in SOUNDBOARD mode.
// Fires deferred image loads and auto-play after debounce timers expire.
void soundboardLoop() {
    if (!useSoundboardBrowseUI()) return;
    uint32_t now = millis();
    if (sbPlayPending && now >= sbAfterMs) {
        sbPlayPending  = false;
        sbImagePending = false;
        sbLastNavMs    = 0; // force full-image path
        playSoundboardBrowseSelection();
    } else if (sbImagePending && now >= sbAfterMs) {
        sbImagePending = false;
        sbLastNavMs    = 0; // force full-image path
        drawSoundboardBrowse(sbCurKey, /*rapidNav=*/false);
    }
}

void soundboardHandleKeyChange(const Keyboard_Class::KeysState &st) {
    if (isEscKey(st)) {
        stopAudio();
        stopAllNotes();
        sdSoundActive      = false;
        sbPlayPending      = false;
        sbImagePending     = false;
        prevSbKeys.clear();
        sbPrevComma = sbPrevSlash = false;
        soundboardRefresh();
        return;
    }

    bool comma = false, slash = false;
    for (char ch : st.word) {
        if (ch == ',') comma = true;
        if (ch == '/') slash = true;
    }

    const bool browse = useSoundboardBrowseUI();

    if (browse) {
        if (comma && !sbPrevComma) {
            if (sdSoundActive) { stopAudio(); sdSoundActive = false; }
            sbPlayPending = false;
            sbStepPlayable(-1);
            sbLastNavMs    = millis();
            sbImagePending = true;
            sbAfterMs      = sbLastNavMs + BROWSE_NAV_SETTLE_MS;
            drawSoundboardBrowse(sbCurKey, /*rapidNav=*/true);
        }
        if (slash && !sbPrevSlash) {
            if (sdSoundActive) { stopAudio(); sdSoundActive = false; }
            sbPlayPending = false;
            sbStepPlayable(1);
            sbLastNavMs    = millis();
            sbImagePending = true;
            sbAfterMs      = sbLastNavMs + BROWSE_NAV_SETTLE_MS;
            drawSoundboardBrowse(sbCurKey, /*rapidNav=*/true);
        }
        sbPrevComma = comma;
        sbPrevSlash = slash;

        if (M5Cardputer.Keyboard.isPressed() && st.enter) {
            // ENTER always plays immediately and cancels any pending debounce.
            sbPlayPending  = false;
            sbImagePending = false;
            sbLastNavMs    = 0; // ensure full-image path in playSoundboardBrowseSelection
            playSoundboardBrowseSelection();
            std::vector<char> currEnter;
            for (char c : st.word) {
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) currEnter.push_back(c);
            }
            prevSbKeys = currEnter;
            return;
        }
    } else {
        sbPrevComma = comma;
        sbPrevSlash = slash;
    }

    std::vector<char> curr;
    for (char c : st.word) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            curr.push_back(c);
    }

    if (browse) {
        for (char c : curr) {
            bool wasHeld = false;
            for (char pc : prevSbKeys) if (pc == c) { wasHeld = true; break; }
            if (wasHeld) continue;
            // New key pressed: update selection and schedule play after debounce.
            // Don't start audio yet — rapid key presses just reschedule the timer,
            // so only the last key in a burst actually triggers an SD open.
            sbCurKey       = c;
            sbLastNavMs    = millis();
            sbImagePending = false;
            sbPlayPending  = true;
            sbAfterMs      = sbLastNavMs + BROWSE_PLAY_DEBOUNCE_MS;
            drawSoundboardBrowse(sbCurKey, /*rapidNav=*/true);
        }
        prevSbKeys = curr;
        return;
    }

    // Piano-only: hold keys for polyphony; play meme MP3 if file exists
    bool pianoUpdated = false;
    for (char c : prevSbKeys) {
        bool stillHeld = false;
        for (char nc : curr) if (nc == c) { stillHeld = true; break; }
        if (!stillHeld && noteActive[(uint8_t)c]) { noteOff(c); pianoUpdated = true; }
    }
    for (char c : curr) {
        bool wasHeld = false;
        for (char pc : prevSbKeys) if (pc == c) { wasHeld = true; break; }
        if (wasHeld) continue;

        char audioPath[64];
        // In pure PIANO mode (no active board) always play tones, ignore meme/
        bool haveMeme = (soundboardDir.length() > 0)
                        && resolveMemeMp3ForKey(c, audioPath, sizeof(audioPath));

        if (haveMeme) {
            stopAllNotes();
            M5Cardputer.Display.fillScreen(TFT_BLACK);
            drawMemeKeyPreviewGraphic(c);
            startMp3(audioPath);
            sdSoundActive = true;
            clearStatusBar();
        } else {
            if (sdSoundActive) { stopAudio(); sdSoundActive = false; }
            noteOn(c);
            pianoUpdated = true;
        }
    }

    if (pianoUpdated && !sdSoundActive) drawPiano();

    prevSbKeys = curr;
}
