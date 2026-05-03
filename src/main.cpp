#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceSD.h>
#include "AudioOutputM5Speaker.h"
#include "embedded_sounds.h"

// SD card SPI pins for M5 Cardputer ADV
#define SD_SCK  40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS   12

#define SOUNDS_DIR "/sounds"
#define MP3_DIR    "/mp3"

#define SCREEN_W 240
#define SCREEN_H 135
#define IMG_H    110   // top area for image
#define STATUS_Y 110   // bottom strip for status text

enum AppMode { SOUNDBOARD, MP3_PLAYER };
static AppMode mode = SOUNDBOARD;
static bool sdReady = false;

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

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool isEscKey(const Keyboard_Class::KeysState &st) {
    for (char c : st.word) {
        if (c == '`' || c == 27) return true;
    }
    return false;
}

static void drawStatusBar(const String &text, uint16_t color) {
    M5Cardputer.Display.fillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, TFT_BLACK);
    M5Cardputer.Display.setTextColor(color, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.drawString(text, 4, STATUS_Y + 6);
}

// Read a file from SD into a heap buffer. Caller must free(). Returns nullptr on failure.
static uint8_t* readFileToBuffer(const char* path, size_t &outLen) {
    File f = SD.open(path, FILE_READ);
    if (!f) return nullptr;
    outLen = f.size();
    uint8_t* buf = (uint8_t*)malloc(outLen);
    if (!buf) { f.close(); return nullptr; }
    f.read(buf, outLen);
    f.close();
    return buf;
}

// Try JPEG then PNG. Returns true if image was displayed.
static bool showImageForKey(char key) {
    char path[48];
    size_t len = 0;
    uint8_t* buf;

    // Try JPG
    snprintf(path, sizeof(path), "%s/%c.jpg", SOUNDS_DIR, key);
    if (sdReady && SD.exists(path)) {
        buf = readFileToBuffer(path, len);
        if (buf) {
            M5Cardputer.Display.drawJpg(buf, len, 0, 0, SCREEN_W, IMG_H);
            free(buf);
            return true;
        }
    }
    // Try PNG
    snprintf(path, sizeof(path), "%s/%c.png", SOUNDS_DIR, key);
    if (sdReady && SD.exists(path)) {
        buf = readFileToBuffer(path, len);
        if (buf) {
            M5Cardputer.Display.drawPng(buf, len, 0, 0, SCREEN_W, IMG_H);
            free(buf);
            return true;
        }
    }
    return false;
}

// ── Soundboard ────────────────────────────────────────────────────────────────

static char currentKey = 0;

static void soundboardDrawIdle() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.drawCenterString("SOUNDBOARD", SCREEN_W / 2, 36);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(0x7BEF, TFT_BLACK);
    M5Cardputer.Display.drawCenterString("Press a key to play a sound", SCREEN_W / 2, 74);
    M5Cardputer.Display.drawCenterString("TAB = MP3 Player  |  ` = Stop", SCREEN_W / 2, 90);
    if (!sdReady) {
        M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5Cardputer.Display.drawCenterString("! SD card not found", SCREEN_W / 2, 110);
    }
}

// Unique background color per key using a simple hash
static uint16_t keyColor(char key) {
    static const uint16_t palette[] = {
        0xF800, 0xFA60, 0xFFE0, 0x87E0, 0x07E0, 0x07EF,
        0x001F, 0x401F, 0x780F, 0xF81F, 0xFC0F, 0xFDA0,
    };
    int idx = ((key >= 'a' && key <= 'z') ? key - 'a' : (key - '0') + 26) % 12;
    return palette[idx];
}

static void soundboardPlayKey(char key) {
    currentKey = key;
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    bool hasImg = showImageForKey(key);
    if (!hasImg) {
        // Colorful fallback: background + large key letter
        uint16_t bg = keyColor(key);
        M5Cardputer.Display.fillRect(0, 0, SCREEN_W, IMG_H, bg);
        M5Cardputer.Display.setTextSize(8);
        M5Cardputer.Display.setTextColor(TFT_WHITE, bg);
        char label[4] = { (char)toupper(key), 0 };
        M5Cardputer.Display.drawCenterString(label, SCREEN_W / 2, IMG_H / 2 - 30);
        M5Cardputer.Display.setTextSize(1);
    }

    char audioPath[48];
    snprintf(audioPath, sizeof(audioPath), "%s/%c.mp3", SOUNDS_DIR, key);
    bool hasAudio = sdReady && SD.exists(audioPath) && startMp3(audioPath);

    // Fallback to built-in synthesized sound
    if (!hasAudio) {
        BuiltinSound bs = getBuiltinSound(key);
        if (bs.data && bs.len > 0) {
            stopAudio();
            // Copy to RAM — PROGMEM can't be DMA-read directly
            int16_t* buf = (int16_t*)malloc(bs.len * 2);
            if (buf) {
                memcpy_P(buf, bs.data, bs.len * 2);
                M5.Speaker.stop(0);
                M5.Speaker.playRaw(buf, bs.len, BUILTIN_SAMPLE_RATE, false, 1, 0, true);
                free(buf);
                hasAudio = true;
            }
        }
    }

    String status = String("[ ") + (char)toupper(key) + " ]";
    if (!hasAudio) status += "  (no audio)";
    status += "  ` = stop  TAB = player";
    drawStatusBar(status, TFT_YELLOW);
}

static void soundboardHandleKeys(const Keyboard_Class::KeysState &st) {
    if (isEscKey(st)) {
        stopAudio();
        currentKey = 0;
        soundboardDrawIdle();
        return;
    }
    for (char c : st.word) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            soundboardPlayKey(c);
            return;
        }
    }
}

// ── MP3 Player ────────────────────────────────────────────────────────────────

struct DirEntry {
    String name;
    bool   isDir;
};

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
            String low = name;
            low.toLowerCase();
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

    // Header
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.drawCenterString("MP3 PLAYER", SCREEN_W / 2, 2);

    // Current path (strip leading /mp3 for brevity)
    String pathDisp = playerPath;
    if (pathDisp.startsWith(String(MP3_DIR))) pathDisp = pathDisp.substring(strlen(MP3_DIR));
    if (pathDisp.length() == 0) pathDisp = "/";
    if (pathDisp.length() > 32) pathDisp = pathDisp.substring(pathDisp.length() - 32);
    d.setTextColor(0x7BEF, TFT_BLACK);
    d.drawString(pathDisp, 2, 14);

    if (!sdReady) {
        d.setTextColor(TFT_RED, TFT_BLACK);
        d.drawCenterString("SD card not found!", SCREEN_W / 2, 70);
        return;
    }
    if (playerEntries.empty()) {
        d.setTextColor(TFT_YELLOW, TFT_BLACK);
        d.drawCenterString("Empty folder", SCREEN_W / 2, 60);
        d.setTextColor(0x7BEF, TFT_BLACK);
        d.drawCenterString("h=back  TAB=soundboard", SCREEN_W / 2, 80);
        return;
    }

    // Visible file list: 5 rows of 16px each, starting at y=27
    const int ROW_H   = 16;
    const int VISIBLE = 5;
    const int LIST_Y  = 27;

    int start = playerSelIdx - VISIBLE / 2;
    if (start < 0) start = 0;
    if (start + VISIBLE > (int)playerEntries.size())
        start = (int)playerEntries.size() - VISIBLE;
    if (start < 0) start = 0;

    for (int i = 0; i < VISIBLE && start + i < (int)playerEntries.size(); i++) {
        int   idx  = start + i;
        auto &e    = playerEntries[idx];
        bool  sel  = (idx == playerSelIdx);

        String fullPath = playerPath + "/" + e.name;
        bool isPlaying = (!e.isDir && fullPath == playerFile && playerState == PLAYER_PLAYING);
        bool isPaused  = (!e.isDir && fullPath == playerFile && playerState == PLAYER_PAUSED);

        uint16_t color = sel ? TFT_YELLOW : TFT_WHITE;
        if (isPlaying) color = TFT_GREEN;
        if (isPaused)  color = 0xFD20; // orange

        d.setTextColor(color, TFT_BLACK);

        String prefix;
        if (isPlaying)     prefix = "\x10 "; // ► character
        else if (isPaused) prefix = "~ ";
        else if (sel)      prefix = "> ";
        else               prefix = "  ";

        String name = e.name;
        if (!e.isDir && name.length() > 4) {
            String low = name; low.toLowerCase();
            if (low.endsWith(".mp3")) name = name.substring(0, name.length() - 4);
        }
        if (e.isDir) name = "[" + name + "]";
        if (name.length() > 27) name = name.substring(0, 27);

        d.drawString(prefix + name, 2, LIST_Y + i * ROW_H);
    }

    // Scroll indicator
    if ((int)playerEntries.size() > VISIBLE) {
        int barH = SCREEN_H - LIST_Y - (SCREEN_H - STATUS_Y) - 2;
        int thumbH = max(4, barH * VISIBLE / (int)playerEntries.size());
        int thumbY = LIST_Y + barH * playerSelIdx / (int)playerEntries.size();
        d.fillRect(SCREEN_W - 3, LIST_Y, 3, barH, 0x2104);
        d.fillRect(SCREEN_W - 3, thumbY, 3, thumbH, TFT_WHITE);
    }

    // Status bar
    String status;
    if (playerState == PLAYER_PLAYING) status = "ENTER=pause  `=stop  h=back  l=fwd";
    else if (playerState == PLAYER_PAUSED)  status = "ENTER=resume  `=stop  h=back  l=fwd";
    else                                    status = "ENTER=play  h=back  l=enter  TAB=board";
    d.setTextColor(0x7BEF, TFT_BLACK);
    d.drawString(status, 2, STATUS_Y + 5);
}

static void playerGoForward() {
    if (playerEntries.empty()) return;
    auto &e = playerEntries[playerSelIdx];
    if (!e.isDir) return;
    playerPath    = playerPath + "/" + e.name;
    playerSelIdx  = 0;
    playerLoadDir(playerPath);
    playerDrawUI();
}

static void playerGoBack() {
    if (playerPath == MP3_DIR) return;
    int slash = playerPath.lastIndexOf('/');
    playerPath   = (slash > 0) ? playerPath.substring(0, slash) : MP3_DIR;
    playerSelIdx = 0;
    playerLoadDir(playerPath);
    playerDrawUI();
}

static void playerTogglePlay() {
    if (playerEntries.empty()) return;
    auto &e = playerEntries[playerSelIdx];

    if (e.isDir) {
        playerGoForward();
        return;
    }

    String filePath = playerPath + "/" + e.name;

    if (playerState == PLAYER_PLAYING && playerFile == filePath) {
        // Pause: halt audio decoder, remember file
        stopAudio();
        playerState = PLAYER_PAUSED;
    } else if (playerState == PLAYER_PAUSED && playerFile == filePath) {
        // Resume (restart from beginning — MP3 has no seek)
        if (startMp3(playerFile.c_str())) playerState = PLAYER_PLAYING;
        else                              playerState = PLAYER_STOPPED;
    } else {
        // Play new or restart stopped
        playerFile = filePath;
        if (startMp3(playerFile.c_str())) playerState = PLAYER_PLAYING;
        else                              playerState = PLAYER_STOPPED;
    }
    playerDrawUI();
}

static void playerAutoAdvance() {
    // Find next playable file in current directory
    int next = playerSelIdx + 1;
    while (next < (int)playerEntries.size() && playerEntries[next].isDir) next++;
    if (next < (int)playerEntries.size()) {
        playerSelIdx = next;
        playerFile   = playerPath + "/" + playerEntries[next].name;
        if (!startMp3(playerFile.c_str())) playerState = PLAYER_STOPPED;
        // state stays PLAYER_PLAYING if startMp3 returned true
    } else {
        playerState = PLAYER_STOPPED;
    }
    playerDrawUI();
}

static void playerHandleKeys(const Keyboard_Class::KeysState &st) {
    if (isEscKey(st)) {
        stopAudio();
        playerState = PLAYER_STOPPED;
        playerDrawUI();
        return;
    }
    if (st.enter) {
        playerTogglePlay();
        return;
    }
    bool changed = false;
    for (char c : st.word) {
        if (c == 'j' || c == ',') {
            if (playerSelIdx < (int)playerEntries.size() - 1) { playerSelIdx++; changed = true; }
        } else if (c == 'k' || c == ';') {
            if (playerSelIdx > 0) { playerSelIdx--; changed = true; }
        } else if (c == 'h') {
            playerGoBack();
            return;
        } else if (c == 'l') {
            playerGoForward();
            return;
        }
    }
    if (changed) playerDrawUI();
}

// ── Mode Switching ────────────────────────────────────────────────────────────

static void enterSoundboard() {
    stopAudio();
    mode         = SOUNDBOARD;
    currentKey   = 0;
    playerState  = PLAYER_STOPPED;
    soundboardDrawIdle();
}

static void enterMp3Player() {
    stopAudio();
    mode         = MP3_PLAYER;
    currentKey   = 0;
    playerState  = PLAYER_STOPPED;
    playerPath   = MP3_DIR;
    playerSelIdx = 0;
    playerLoadDir(playerPath);
    playerDrawUI();
}

// ── Arduino Entry Points ──────────────────────────────────────────────────────

static void drawBootScreen() {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);

    // Gradient-like colored bars
    d.fillRect(0,  0,  SCREEN_W, 27, 0xF800); // red
    d.fillRect(0, 27,  SCREEN_W, 27, 0xFFE0); // yellow
    d.fillRect(0, 54,  SCREEN_W, 27, 0x07E0); // green
    d.fillRect(0, 81,  SCREEN_W, 27, 0x001F); // blue
    d.fillRect(0, 108, SCREEN_W, 27, 0xF81F); // magenta

    // Title overlay
    d.setTextSize(2);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.fillRect(30, 48, 180, 38, TFT_BLACK);
    d.drawCenterString("MEME BOARD", SCREEN_W / 2, 52);
    d.setTextSize(1);
    d.setTextColor(0xBDF7, TFT_BLACK);
    d.drawCenterString("v1.0  |  TAB = MP3 Player", SCREEN_W / 2, 76);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(200);
    M5Cardputer.Display.setTextSize(1);

    // Boot screen first so user sees something immediately
    drawBootScreen();

    M5.Speaker.begin();
    M5.Speaker.setVolume(200);

    // Startup jingle: three ascending tones
    M5.Speaker.tone(523, 120);  // C5
    delay(130);
    M5.Speaker.tone(659, 120);  // E5
    delay(130);
    M5.Speaker.tone(784, 200);  // G5
    delay(220);
    M5.Speaker.stop();

    spk = new AudioOutputM5Speaker(&M5.Speaker, 0);
    spk->SetGain(1.0f);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    sdReady = SD.begin(SD_CS, SPI, 25000000);

    delay(800); // let user see the boot screen
    soundboardDrawIdle();
}

void loop() {
    M5Cardputer.update();

    // Service non-blocking audio
    if (gen && gen->isRunning()) {
        if (!gen->loop()) {
            spk->flush();
            gen->stop();
            if (mode == MP3_PLAYER && playerState == PLAYER_PLAYING) {
                playerAutoAdvance();
            } else {
                playerState = PLAYER_STOPPED;
            }
        }
    }

    // Keyboard input
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

        if (st.tab) {
            if (mode == SOUNDBOARD) enterMp3Player();
            else                    enterSoundboard();
            return;
        }

        if (mode == SOUNDBOARD) soundboardHandleKeys(st);
        else                    playerHandleKeys(st);
    }
}
