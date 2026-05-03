#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include "esp_log.h"
#include "config.h"
#include "audio.h"
#include "notes.h"
#include "settings.h"
#include "soundboard.h"
#include "mp3player.h"
#include "ui_utils.h"
#include "sd_serial_xfer.h"

bool    sdReady = false;
AppMode mode    = SOUNDBOARD;

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
        // boardPaths.size() is used as sentinel for PIANO mode
        if (soundboardBoardIdx > (int)boardPaths.size()) soundboardBoardIdx = (int)boardPaths.size();
        soundboardDir = (soundboardBoardIdx == (int)boardPaths.size())
                        ? "" : boardPaths[soundboardBoardIdx];
    }
    sbResetInputState();
    sbInitBrowseSelection();
    boardSplashActive = true;
    drawBoardSplash();
    saveSettings();
}

static void enterMp3Player() {
    stopAudio();
    stopAllNotes();
    boardSplashActive = false;
    playerHintUntilMs = millis() + PLAYER_HINT_MS;
    mode          = MP3_PLAYER;
    sdSoundActive = false;
    playerState   = PLAYER_STOPPED;
    playerPath    = MP3_DIR;
    playerSelIdx  = 0;
    playerLoadDir(playerPath);
    playerDrawUI();
}

// ── Boot Screen ───────────────────────────────────────────────────────────────

static void drawBootScreen() {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.fillRect(0,   0, SCREEN_W, 27, 0xF800);
    d.fillRect(0,  27, SCREEN_W, 27, 0xFFE0);
    d.fillRect(0,  54, SCREEN_W, 27, 0x07E0);
    d.fillRect(0,  81, SCREEN_W, 27, 0x001F);
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
    esp_log_level_set("*", ESP_LOG_NONE);
    Serial.setRxBufferSize(16384);
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(200);
    M5Cardputer.Display.setTextSize(1);

    drawBootScreen();

    M5.Speaker.begin();

    spk = new AudioOutputM5Speaker(&M5.Speaker, 0);
    spk->SetGain(1.0f);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    sdReady = SD.begin(SD_CS, SPI, 10000000);
    scanSoundboardDirs();

    loadSettings();
    M5.Speaker.setVolume(masterVolume);

    if (!boardPaths.empty()) {
        if (soundboardBoardIdx >= (int)boardPaths.size()) soundboardBoardIdx = 0;
        soundboardDir = boardPaths[soundboardBoardIdx];
    } else {
        soundboardBoardIdx = 0;
        soundboardDir      = "";
    }
    initNotes();

    delay(800);
    if (!boardPaths.empty()) sbInitBrowseSelection();
    boardSplashActive = true;
    drawBoardSplash();

    sdSerialXferSetup();
}

void loop() {
    M5Cardputer.update();
    sdSerialXferLoop();

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
                    soundboardRefresh();
            }
        }
    }

    if (M5Cardputer.Keyboard.isChange()) {
        Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

        // Volume: + / - (global, fires on fresh press)
        bool keyPlus = false, keyMinus = false;
        for (char c : st.word) {
            if (c == '+' || c == '=') keyPlus  = true;
            if (c == '-')             keyMinus = true;
        }
        if (keyPlus  && !prevKeyPlus)  applyVolume( VOL_STEP);
        if (keyMinus && !prevKeyMinus) applyVolume(-VOL_STEP);
        prevKeyPlus  = keyPlus;
        prevKeyMinus = keyMinus;

        // TAB: cycles boards[0..N-1] → PIANO → MP3 Player → boards[0] → …
        if (M5Cardputer.Keyboard.isPressed() && st.tab) {
            if (mode == MP3_PLAYER) {
                enterSoundboard(0);
            } else {
                int next = soundboardBoardIdx + 1;
                if (next > (int)boardPaths.size()) enterMp3Player();
                else enterSoundboard(next);
            }
            return;
        }

        if (mode == SOUNDBOARD && boardSplashActive) {
            if (M5Cardputer.Keyboard.isPressed()) {
                boardSplashActive = false;
                soundboardRefresh();
            }
            return;
        }

        if (mode == SOUNDBOARD) {
            soundboardHandleKeyChange(st);
        } else if (M5Cardputer.Keyboard.isPressed()) {
            playerHandleKeys(st);
        }
    }
}
