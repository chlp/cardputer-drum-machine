#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceSD.h>
#include "AudioOutputM5Speaker.h"
#include "sd_serial_xfer.h"

// SD card SPI pins for M5 Cardputer ADV
#define SD_SCK  40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS   12

#define BOARDS_DIR "/boards"
#define BOARDS_MEME "/boards/meme"  // стандартные мемы; fallback для кастом-досок
#define MP3_DIR     "/mp3"

#define BOARD_TITLE_MAX 10
#define BOARD_SPLASH_MS 900
#define PLAYER_HINT_MS  2200

#define SETTINGS_PATH   "/settings.cfg"
#define VOL_STEP        16    // ~16 steps from 0 to 255
#define VOL_DISPLAY_MS  1500

#define SCREEN_W 240
#define SCREEN_H 135
#define IMG_H    110
#define STATUS_Y 110

enum AppMode { SOUNDBOARD, MP3_PLAYER };
static AppMode mode     = SOUNDBOARD;
static bool    sdReady  = false;

// Подпапки /boards (ровно 1 уровень) — отдельные soundboard-панели; `meme` = стандарт, первый в TAB.
static std::vector<String> boardPaths;
static int                 soundboardBoardIdx = 0;
static String              soundboardDir;       // активная панель: "/boards/<name>" или пусто
static uint32_t            boardSplashUntilMs = 0;
static uint32_t            playerHintUntilMs  = 0;

static uint8_t  masterVolume         = 200;
static uint32_t volumeDisplayUntilMs = 0;
static bool     prevKeyPlus          = false;
static bool     prevKeyMinus         = false;

// Browse UI when SD has /boards/*: , / = prev/next MP3 slot, letter/digit = pick, ENTER = play.
static char     sbCurKey      = 'z';
static bool     sbPrevComma   = false;
static bool     sbPrevSlash   = false;

static void drawPiano();

// ── Audio ─────────────────────────────────────────────────────────────────────

static AudioOutputM5Speaker *spk = nullptr;
static AudioGeneratorMP3    *gen = nullptr;
static AudioFileSourceSD    *src = nullptr;

static void stopAudio() {
    if (gen) { gen->stop(); delete gen; gen = nullptr; }
    if (src) { delete src; src = nullptr; }
    if (spk) spk->stop();
}

static bool startMp3(const char *path) {
    stopAudio();
    src = new AudioFileSourceSD(path);
    if (!src->isOpen()) { delete src; src = nullptr; return false; }
    gen = new AudioGeneratorMP3();
    gen->begin(src, spk);
    return true;
}

// ── Note table ────────────────────────────────────────────────────────────────
//
// Physical keyboard rows, bottom → top = low → high notes:
//   z x c v b n m         →  C3 … F#3   (7 semitones)
//   a s d f g h j k l     →  G3 … D#4   (9 semitones)
//   q w e r t y u i o p   →  E4 … C#5   (10 semitones)
//   1 2 3 4 5 6 7 8 9 0   →  D5 … B5    (10 semitones)
//
// Total: 36 notes, chromatic C3–B5.

static const char NOTE_KEY[36] = {
    'z','x','c','v','b','n','m',
    'a','s','d','f','g','h','j','k','l',
    'q','w','e','r','t','y','u','i','o','p',
    '1','2','3','4','5','6','7','8','9','0'
};

static const uint32_t NOTE_FREQ[36] = {
    131,139,147,156,165,175,185,
    196,208,220,233,247,262,277,294,311,
    330,349,370,392,415,440,466,494,523,554,
    587,622,659,698,740,784,831,880,932,988
};

static const char *NOTE_NAME[36] = {
    "C3","C#3","D3","D#3","E3","F3","F#3",
    "G3","G#3","A3","A#3","B3","C4","C#4","D4","D#4",
    "E4","F4","F#4","G4","G#4","A4","A#4","B4","C5","C#5",
    "D5","D#5","E5","F5","F#5","G5","G#5","A5","A#5","B5"
};

// reverse lookup: ASCII → note index (−1 = not a note key)
static int8_t keyNoteIdx[256];

// ── Polyphony state ───────────────────────────────────────────────────────────
// Speaker channel 0 is used by AudioOutputM5Speaker (MP3).
// Note channels: 1–7 (7 voices).

#define NOTE_CH_BASE 1
#define NOTE_CH_CNT  7

static bool  noteActive[256]          = {};   // key → tone playing
static int8_t noteKeyChannel[256];            // key → speaker channel (−1 = none)
static char  channelNoteKey[8]        = {};   // speaker channel → key (0 = free)
static bool  sdSoundActive            = false;

static void noteOn(char key) {
    uint8_t k = (uint8_t)key;
    int idx = keyNoteIdx[k];
    if (idx < 0 || noteActive[k]) return;

    // Find free note channel
    int ch = -1;
    for (int i = NOTE_CH_BASE; i < NOTE_CH_BASE + NOTE_CH_CNT; i++) {
        if (!channelNoteKey[i]) { ch = i; break; }
    }
    if (ch < 0) {
        // Steal lowest channel
        ch = NOTE_CH_BASE;
        char stolen = channelNoteKey[ch];
        M5.Speaker.stop(ch);
        noteActive[(uint8_t)stolen] = false;
        noteKeyChannel[(uint8_t)stolen] = -1;
    }

    channelNoteKey[ch] = key;
    noteKeyChannel[k]  = ch;
    noteActive[k]      = true;
    M5.Speaker.tone(NOTE_FREQ[idx], 0, ch, false); // indefinite, non-interrupting
}

static void noteOff(char key) {
    uint8_t k = (uint8_t)key;
    int ch = noteKeyChannel[k];
    if (ch < 0) return;
    M5.Speaker.stop(ch);
    channelNoteKey[ch] = 0;
    noteKeyChannel[k]  = -1;
    noteActive[k]      = false;
}

static void stopAllNotes() {
    for (int i = NOTE_CH_BASE; i < NOTE_CH_BASE + NOTE_CH_CNT; i++) {
        if (channelNoteKey[i]) {
            M5.Speaker.stop(i);
            noteActive[(uint8_t)channelNoteKey[i]]     = false;
            noteKeyChannel[(uint8_t)channelNoteKey[i]] = -1;
            channelNoteKey[i] = 0;
        }
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool isEscKey(const Keyboard_Class::KeysState &st) {
    for (char c : st.word) if (c == '`' || c == 27) return true;
    return false;
}

static void clearStatusBar() {
    M5Cardputer.Display.fillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, TFT_BLACK);
}

// ── Volume & Settings ─────────────────────────────────────────────────────────

static void drawVolumeOverlay() {
    auto &d = M5Cardputer.Display;
    d.fillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, TFT_BLACK);
    uint8_t level = masterVolume / VOL_STEP; // 0..15
    char buf[20];
    snprintf(buf, sizeof(buf), "VOL  %2d / 15", (int)level);
    d.setTextSize(1);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.drawCenterString(buf, SCREEN_W / 2, STATUS_Y + 2);
    const int bx = 10, by = STATUS_Y + 13, bw = SCREEN_W - 20, bh = 8;
    int filled = bw * masterVolume / 255;
    d.fillRect(bx, by, bw, bh, 0x2104);
    d.fillRect(bx, by, filled, bh, TFT_YELLOW);
}

static void saveSettings() {
    if (!sdReady) return;
    File f = SD.open(SETTINGS_PATH, FILE_WRITE);
    if (!f) return;
    f.printf("volume=%d\n", (int)masterVolume);
    f.printf("panel=%d\n",  soundboardBoardIdx);
    f.close();
}

static void loadSettings() {
    if (!sdReady) return;
    File f = SD.open(SETTINGS_PATH, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        int    val = line.substring(eq + 1).toInt();
        if      (key == "volume") masterVolume       = (uint8_t)constrain(val, 0, 255);
        else if (key == "panel")  soundboardBoardIdx = constrain(val, 0, 999);
    }
    f.close();
}

static void applyVolume(int delta) {
    int v = (int)masterVolume + delta;
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    masterVolume = (uint8_t)v;
    M5.Speaker.setVolume(masterVolume);
    volumeDisplayUntilMs = millis() + VOL_DISPLAY_MS;
    drawVolumeOverlay();
    saveSettings();
}

static uint8_t* readFileToBuffer(const char *path, size_t &outLen) {
    File f = SD.open(path, FILE_READ);
    if (!f) return nullptr;
    outLen = f.size();
    uint8_t *buf = (uint8_t *)malloc(outLen);
    if (!buf) { f.close(); return nullptr; }
    f.read(buf, outLen);
    f.close();
    return buf;
}

static String sdEntryBaseName(const String &entryName) {
    int p = entryName.lastIndexOf('/');
    return (p >= 0) ? entryName.substring(p + 1) : entryName;
}

static void scanSoundboardDirs() {
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

// true + path: воспроизвести MP3 с SD (активная доска, иначе /boards/meme)
static bool resolveMemeMp3ForKey(char key, char *path, size_t pathCap) {
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

static bool useSoundboardBrowseUI() {
    return sdReady && !boardPaths.empty();
}

static void sbInitBrowseSelection() {
    char path[64];
    sbCurKey = NOTE_KEY[0];
    for (int i = 0; i < 36; i++) {
        if (resolveMemeMp3ForKey(NOTE_KEY[i], path, sizeof(path))) {
            sbCurKey = NOTE_KEY[i];
            return;
        }
    }
}

static void sbStepPlayable(int dir) {
    char path[64];
    int start = 0;
    for (int i = 0; i < 36; i++) {
        if (NOTE_KEY[i] == sbCurKey) {
            start = i;
            break;
        }
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

// Image or color tile for key (fills 0..IMG_H).
static void drawMemeKeyPreviewGraphic(char key) {
    auto &d = M5Cardputer.Display;
    if (showImageForKey(key)) return;
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

static void drawSoundboardBrowse(char key) {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    drawMemeKeyPreviewGraphic(key);
    d.fillRect(0, IMG_H - 18, SCREEN_W, 18, TFT_BLACK);
    d.setTextSize(2);
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    char line[12];
    snprintf(line, sizeof(line), "[%c] OK=play", (char)toupper(key));
    d.drawCenterString(line, SCREEN_W / 2, IMG_H - 16);
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
        if (sdSoundActive) {
            stopAudio();
            sdSoundActive = false;
        }
        int ni = keyNoteIdx[(uint8_t)c];
        if (ni >= 0)
            M5.Speaker.tone(NOTE_FREQ[ni], 220, NOTE_CH_BASE, true);
        drawSoundboardBrowse(c);
    }
}

static void soundboardRefresh() {
    if (useSoundboardBrowseUI()) drawSoundboardBrowse(sbCurKey);
    else drawPiano();
}

static void drawBoardSplash() {
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
    d.drawCenterString(title, SCREEN_W / 2, SCREEN_H / 2 - 26);
    d.setTextSize(1);
    d.setTextColor(0x7BEF, TFT_BLACK);
    d.drawCenterString("` = clear/stop   TAB = next panel", SCREEN_W / 2, SCREEN_H / 2 + 4);
    d.drawCenterString("or MP3 player", SCREEN_W / 2, SCREEN_H / 2 + 16);
    d.drawCenterString(", / = prev/next  a-z 0-9 = pick", SCREEN_W / 2, SCREEN_H / 2 + 28);
    d.drawCenterString("OK = play", SCREEN_W / 2, SCREEN_H / 2 + 40);
}

// ── Piano display (36-note strip) ────────────────────────────────────────────
//
// Draws a 3-octave piano keyboard (C3–B5) in the IMG_H area.
// Active (held) keys are highlighted yellow.

static void drawPiano() {
    auto &d = M5Cardputer.Display;

    // White key height / black key height
    const int WH = 78;
    const int BH = 47;
    const int TW = SCREEN_W; // 240
    const int NW = 21;       // white keys in 3 octaves

    // Per-semitone-in-octave: white key index within octave (−1 = black key)
    static const int8_t wIdx[12] = { 0,-1,1,-1,2, 3,-1,4,-1,5,-1,6 };
    // Per-semitone-in-octave: index of white key to the LEFT of this black key (−1 = white key)
    static const int8_t bLeft[12]= {-1, 0,-1, 1,-1,-1, 3,-1, 4,-1, 5,-1};

    // Clear area
    d.fillRect(0, 0, TW, IMG_H, TFT_BLACK);

    // Draw white keys
    for (int i = 0; i < 36; i++) {
        int oct = i / 12, sem = i % 12;
        if (wIdx[sem] < 0) continue;
        int wn = oct * 7 + wIdx[sem]; // global white key #
        int x0 = (wn * TW) / NW;
        int x1 = ((wn + 1) * TW) / NW;
        char key = NOTE_KEY[i];
        uint16_t col = noteActive[(uint8_t)key] ? TFT_YELLOW : TFT_WHITE;
        d.fillRect(x0, 0, x1 - x0 - 1, WH, col);
        d.drawFastVLine(x0, 0, WH, TFT_BLACK);
    }
    d.drawFastVLine(TW - 1, 0, WH, TFT_BLACK);
    d.drawFastHLine(0, WH, TW, TFT_BLACK);

    // Draw black keys on top
    for (int i = 0; i < 36; i++) {
        int oct = i / 12, sem = i % 12;
        if (bLeft[sem] < 0) continue;
        int leftWn = oct * 7 + bLeft[sem];
        int cx = ((leftWn + 1) * TW) / NW; // right edge of left white key
        int bw = max(4, (TW / NW) * 6 / 10);
        int bx = cx - bw / 2;
        char key = NOTE_KEY[i];
        uint16_t col = noteActive[(uint8_t)key] ? TFT_YELLOW : 0x18C6; // dark gray
        d.fillRect(bx, 0, bw, BH, col);
    }

    // Note names of active keys
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

// ── Soundboard ────────────────────────────────────────────────────────────────

// Keys pressed in previous frame (for press/release detection)
static std::vector<char> prevSbKeys;

static void soundboardDrawIdle() {
    stopAllNotes();
    sdSoundActive = false;
    soundboardRefresh();
}

static void soundboardHandleKeyChange(const Keyboard_Class::KeysState &st) {
    if (isEscKey(st)) {
        stopAudio();
        stopAllNotes();
        sdSoundActive = false;
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
            if (sdSoundActive) {
                stopAudio();
                sdSoundActive = false;
            }
            sbStepPlayable(-1);
            drawSoundboardBrowse(sbCurKey);
        }
        if (slash && !sbPrevSlash) {
            if (sdSoundActive) {
                stopAudio();
                sdSoundActive = false;
            }
            sbStepPlayable(1);
            drawSoundboardBrowse(sbCurKey);
        }
        sbPrevComma = comma;
        sbPrevSlash = slash;

        if (M5Cardputer.Keyboard.isPressed() && st.enter) {
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
            for (char pc : prevSbKeys) if (pc == c) {
                wasHeld = true;
                break;
            }
            if (wasHeld) continue;
            sbCurKey = c;
            if (sdSoundActive) {
                stopAudio();
                sdSoundActive = false;
            }
            drawSoundboardBrowse(sbCurKey);
        }
        prevSbKeys = curr;
        return;
    }

    // Piano-only (no /boards panels): hold keys for polyphony; instant meme if file exists
    for (char c : prevSbKeys) {
        bool stillHeld = false;
        for (char nc : curr) if (nc == c) {
            stillHeld = true;
            break;
        }
        if (!stillHeld && noteActive[(uint8_t)c]) noteOff(c);
    }

    bool pianoUpdated = false;
    for (char c : curr) {
        bool wasHeld = false;
        for (char pc : prevSbKeys) if (pc == c) {
            wasHeld = true;
            break;
        }
        if (wasHeld) continue;

        char audioPath[64];
        bool haveMeme = resolveMemeMp3ForKey(c, audioPath, sizeof(audioPath));

        if (haveMeme) {
            stopAllNotes();
            M5Cardputer.Display.fillScreen(TFT_BLACK);
            drawMemeKeyPreviewGraphic(c);
            startMp3(audioPath);
            sdSoundActive = true;
            clearStatusBar();
        } else {
            if (sdSoundActive) {
                stopAudio();
                sdSoundActive = false;
            }
            noteOn(c);
            pianoUpdated = true;
        }
    }

    if (pianoUpdated && !sdSoundActive) drawPiano();

    prevSbKeys = curr;
}

// ── MP3 Player ────────────────────────────────────────────────────────────────

struct DirEntry { String name; bool isDir; };

enum PlayerState { PLAYER_STOPPED, PLAYER_PLAYING, PLAYER_PAUSED };

static String      playerPath  = MP3_DIR;
static std::vector<DirEntry> playerEntries;
static int         playerSelIdx = 0;
static String      playerFile   = "";
static PlayerState playerState  = PLAYER_STOPPED;

static void playerLoadDir(const String &path) {
    playerEntries.clear();
    if (!sdReady) return;
    File dir = SD.open(path.c_str());
    if (!dir || !dir.isDirectory()) return;
    std::vector<String> dirs, files;
    while (true) {
        File f = dir.openNextFile();
        if (!f) break;
        String name = f.name();
        if (f.isDirectory()) {
            dirs.push_back(name);
        } else {
            String low = name; low.toLowerCase();
            if (low.endsWith(".mp3")) files.push_back(name);
        }
        f.close();
    }
    dir.close();
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    for (auto &d : dirs)  playerEntries.push_back({d, true});
    for (auto &f : files) playerEntries.push_back({f, false});
}

static void playerDrawUI() {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.drawCenterString("MP3 PLAYER", SCREEN_W / 2, 2);

    String pathDisp = playerPath;
    if (pathDisp.startsWith(String(MP3_DIR))) pathDisp = pathDisp.substring(strlen(MP3_DIR));
    if (pathDisp.length() == 0) pathDisp = "/";
    if (pathDisp.length() > 32) pathDisp = pathDisp.substring(pathDisp.length() - 32);
    d.setTextColor(0x7BEF, TFT_BLACK);
    d.drawString(pathDisp, 2, 14);

    if (!sdReady) {
        d.setTextColor(TFT_RED, TFT_BLACK); d.drawCenterString("SD card not found!", SCREEN_W / 2, 70); return;
    }
    if (playerEntries.empty()) {
        d.setTextColor(TFT_YELLOW, TFT_BLACK);
        d.drawCenterString("Empty folder", SCREEN_W / 2, 52);
        d.setTextColor(0x7BEF, TFT_BLACK);
        d.drawCenterString("h = up   TAB = soundboard", SCREEN_W / 2, 70);
        clearStatusBar();
        return;
    }

    const int ROW_H   = 16;
    const int VISIBLE = 5;
    const int LIST_Y  = 27;

    int start = playerSelIdx - VISIBLE / 2;
    if (start < 0) start = 0;
    if (start + VISIBLE > (int)playerEntries.size()) start = (int)playerEntries.size() - VISIBLE;
    if (start < 0) start = 0;

    for (int i = 0; i < VISIBLE && start + i < (int)playerEntries.size(); i++) {
        int idx = start + i;
        auto &e = playerEntries[idx];
        bool sel = (idx == playerSelIdx);
        String fullPath = playerPath + "/" + e.name;
        bool isPlaying = (!e.isDir && fullPath == playerFile && playerState == PLAYER_PLAYING);
        bool isPaused  = (!e.isDir && fullPath == playerFile && playerState == PLAYER_PAUSED);

        uint16_t color = sel ? TFT_YELLOW : TFT_WHITE;
        if (isPlaying) color = TFT_GREEN;
        if (isPaused)  color = 0xFD20;
        d.setTextColor(color, TFT_BLACK);

        String prefix = isPlaying ? "\x10 " : (isPaused ? "~ " : (sel ? "> " : "  "));
        String name = e.name;
        if (!e.isDir && name.length() > 4) {
            String low = name; low.toLowerCase();
            if (low.endsWith(".mp3")) name = name.substring(0, name.length() - 4);
        }
        if (e.isDir) name = "[" + name + "]";
        if (name.length() > 27) name = name.substring(0, 27);
        d.drawString(prefix + name, 2, LIST_Y + i * ROW_H);
    }

    if ((int)playerEntries.size() > VISIBLE) {
        int barH = STATUS_Y - LIST_Y - 2;
        int thumbH = max(4, barH * VISIBLE / (int)playerEntries.size());
        int thumbY = LIST_Y + barH * playerSelIdx / (int)playerEntries.size();
        d.fillRect(SCREEN_W - 3, LIST_Y, 3, barH, 0x2104);
        d.fillRect(SCREEN_W - 3, thumbY, 3, thumbH, TFT_WHITE);
    }

    if (playerHintUntilMs && millis() < playerHintUntilMs) {
        const char *l1 = playerState == PLAYER_PLAYING ? "j/k ,; nav  ENTER pause  h up  l in"
                       : playerState == PLAYER_PAUSED  ? "j/k ,; nav  ENTER resume  h up  l in"
                       :                                   "j/k ,; nav  ENTER play  h up  l in";
        d.setTextColor(0x7BEF, TFT_BLACK);
        d.drawString(l1, 2, STATUS_Y + 3);
        d.drawString("TAB soundboard   ` stop", 2, STATUS_Y + 13);
    } else {
        clearStatusBar();
    }
}

static void playerGoForward() {
    if (playerEntries.empty()) return;
    auto &e = playerEntries[playerSelIdx];
    if (!e.isDir) return;
    playerPath = playerPath + "/" + e.name;
    playerSelIdx = 0;
    playerLoadDir(playerPath);
    playerDrawUI();
}

static void playerGoBack() {
    if (playerPath == MP3_DIR) return;
    int slash = playerPath.lastIndexOf('/');
    playerPath = (slash > 0) ? playerPath.substring(0, slash) : MP3_DIR;
    playerSelIdx = 0;
    playerLoadDir(playerPath);
    playerDrawUI();
}

static void playerTogglePlay() {
    if (playerEntries.empty()) return;
    auto &e = playerEntries[playerSelIdx];
    if (e.isDir) { playerGoForward(); return; }
    String filePath = playerPath + "/" + e.name;
    if (playerState == PLAYER_PLAYING && playerFile == filePath) {
        stopAudio(); playerState = PLAYER_PAUSED;
    } else if (playerState == PLAYER_PAUSED && playerFile == filePath) {
        playerState = startMp3(playerFile.c_str()) ? PLAYER_PLAYING : PLAYER_STOPPED;
    } else {
        playerFile = filePath;
        playerState = startMp3(playerFile.c_str()) ? PLAYER_PLAYING : PLAYER_STOPPED;
    }
    playerDrawUI();
}

static void playerAutoAdvance() {
    int next = playerSelIdx + 1;
    while (next < (int)playerEntries.size() && playerEntries[next].isDir) next++;
    if (next < (int)playerEntries.size()) {
        playerSelIdx = next;
        playerFile   = playerPath + "/" + playerEntries[next].name;
        if (!startMp3(playerFile.c_str())) playerState = PLAYER_STOPPED;
    } else {
        playerState = PLAYER_STOPPED;
    }
    playerDrawUI();
}

static void playerHandleKeys(const Keyboard_Class::KeysState &st) {
    if (isEscKey(st)) { stopAudio(); playerState = PLAYER_STOPPED; playerDrawUI(); return; }
    if (st.enter) { playerTogglePlay(); return; }
    bool changed = false;
    for (char c : st.word) {
        if      (c == 'j' || c == ',') { if (playerSelIdx < (int)playerEntries.size()-1) { playerSelIdx++; changed = true; } }
        else if (c == 'k' || c == ';') { if (playerSelIdx > 0) { playerSelIdx--; changed = true; } }
        else if (c == 'h') { playerGoBack(); return; }
        else if (c == 'l') { playerGoForward(); return; }
    }
    if (changed) playerDrawUI();
}

// ── Mode Switching ────────────────────────────────────────────────────────────

static void enterSoundboard(int boardIdx) {
    stopAudio();
    stopAllNotes();
    playerHintUntilMs = 0;
    mode = SOUNDBOARD;
    sdSoundActive = false;
    if (boardPaths.empty()) {
        soundboardBoardIdx = 0;
        soundboardDir = "";
    } else {
        soundboardBoardIdx = boardIdx;
        if (soundboardBoardIdx < 0) soundboardBoardIdx = 0;
        if (soundboardBoardIdx >= (int)boardPaths.size()) soundboardBoardIdx = (int)boardPaths.size() - 1;
        soundboardDir = boardPaths[soundboardBoardIdx];
    }
    prevSbKeys.clear();
    sbPrevComma = sbPrevSlash = false;
    sbInitBrowseSelection();
    boardSplashUntilMs = millis() + BOARD_SPLASH_MS;
    drawBoardSplash();
    saveSettings();
}

static void enterMp3Player() {
    stopAudio();
    stopAllNotes();
    boardSplashUntilMs = 0;
    playerHintUntilMs = millis() + PLAYER_HINT_MS;
    mode         = MP3_PLAYER;
    sdSoundActive = false;
    playerState  = PLAYER_STOPPED;
    playerPath   = MP3_DIR;
    playerSelIdx = 0;
    playerLoadDir(playerPath);
    playerDrawUI();
}

// ── Boot screen ───────────────────────────────────────────────────────────────

static void drawBootScreen() {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.fillRect(0,  0,  SCREEN_W, 27, 0xF800);
    d.fillRect(0, 27,  SCREEN_W, 27, 0xFFE0);
    d.fillRect(0, 54,  SCREEN_W, 27, 0x07E0);
    d.fillRect(0, 81,  SCREEN_W, 27, 0x001F);
    d.fillRect(0, 108, SCREEN_W, 27, 0xF81F);
    d.setTextSize(2);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.fillRect(30, 48, 180, 38, TFT_BLACK);
    d.drawCenterString("MEME BOARD", SCREEN_W / 2, 52);
    d.setTextSize(1);
    d.setTextColor(0xBDF7, TFT_BLACK);
    d.drawCenterString("v2.0  |  TAB = boards / MP3", SCREEN_W / 2, 76);
}

// ── Arduino Entry Points ──────────────────────────────────────────────────────

void setup() {
    Serial.setRxBufferSize(16384);
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(200);
    M5Cardputer.Display.setTextSize(1);

    drawBootScreen();

    M5.Speaker.begin();

    spk = new AudioOutputM5Speaker(&M5.Speaker, 0);
    spk->SetGain(1.0f);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    sdReady = SD.begin(SD_CS, SPI, 25000000);
    scanSoundboardDirs();

    // Restore saved settings (volume + last panel)
    loadSettings();
    M5.Speaker.setVolume(masterVolume);

    if (!boardPaths.empty()) {
        if (soundboardBoardIdx >= (int)boardPaths.size()) soundboardBoardIdx = 0;
        soundboardDir = boardPaths[soundboardBoardIdx];
    } else {
        soundboardBoardIdx = 0;
        soundboardDir      = "";
    }
    boardSplashUntilMs = 0;

    // Init note lookup table
    memset(keyNoteIdx, -1, sizeof(keyNoteIdx));
    memset(noteKeyChannel, -1, sizeof(noteKeyChannel));
    memset(channelNoteKey, 0, sizeof(channelNoteKey));
    for (int i = 0; i < 36; i++) keyNoteIdx[(uint8_t)NOTE_KEY[i]] = i;

    delay(800);
    if (!boardPaths.empty()) sbInitBrowseSelection();
    soundboardRefresh();

    sdSerialXferSetup();
}

void loop() {
    M5Cardputer.update();
    sdSerialXferLoop();

    if (mode == SOUNDBOARD && boardSplashUntilMs && millis() >= boardSplashUntilMs) {
        boardSplashUntilMs = 0;
        soundboardRefresh();
    }
    if (mode == MP3_PLAYER && playerHintUntilMs && millis() >= playerHintUntilMs) {
        playerHintUntilMs = 0;
        playerDrawUI();
    }
    if (volumeDisplayUntilMs && millis() >= volumeDisplayUntilMs) {
        volumeDisplayUntilMs = 0;
        if (mode == MP3_PLAYER) playerDrawUI();
        else clearStatusBar();
    }

    // Service MP3 stream
    if (gen && gen->isRunning()) {
        if (!gen->loop()) {
            spk->flush();
            gen->stop();
            if (mode == MP3_PLAYER && playerState == PLAYER_PLAYING) {
                playerAutoAdvance();
            } else {
                sdSoundActive = false;
                playerState = PLAYER_STOPPED;
                if (mode == SOUNDBOARD && useSoundboardBrowseUI())
                    drawSoundboardBrowse(sbCurKey);
            }
        }
    }

    // Keyboard
    if (M5Cardputer.Keyboard.isChange()) {
        Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

        // + / - volume (global, both modes, fires on fresh press)
        bool keyPlus = false, keyMinus = false;
        for (char c : st.word) {
            if (c == '+') keyPlus  = true;
            if (c == '-') keyMinus = true;
        }
        if (keyPlus  && !prevKeyPlus)  applyVolume( VOL_STEP);
        if (keyMinus && !prevKeyMinus) applyVolume(-VOL_STEP);
        prevKeyPlus  = keyPlus;
        prevKeyMinus = keyMinus;

        // TAB only on press: MP3 → доски по кругу (/boards/*, `meme` первый) → MP3
        if (M5Cardputer.Keyboard.isPressed() && st.tab) {
            if (mode == MP3_PLAYER) {
                enterSoundboard(0);
            } else {
                if (boardPaths.empty()) enterMp3Player();
                else {
                    int next = soundboardBoardIdx + 1;
                    if (next >= (int)boardPaths.size()) enterMp3Player();
                    else enterSoundboard(next);
                }
            }
            return;
        }

        if (mode == SOUNDBOARD && boardSplashUntilMs && millis() < boardSplashUntilMs) {
            return;
        }

        if (mode == SOUNDBOARD) {
            // Handle both press and release for polyphony
            soundboardHandleKeyChange(st);
        } else if (M5Cardputer.Keyboard.isPressed()) {
            playerHandleKeys(st);
        }
    }
}
