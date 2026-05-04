#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "audio.h"
#include "log.h"
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
    playerHintUntilMs = 0;
    mode          = MP3_PLAYER;
    sdSoundActive = false;
    playerState   = PLAYER_STOPPED;
    playerPath    = MP3_DIR;
    playerSelIdx  = 0;
    playerLoadDir(playerPath);
    playerSplashActive = true;
    drawPlayerSplash();
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
    // Allow IDF warnings/errors through (e.g. the "Task watchdog got triggered"
    // diagnostic from task_wdt.c).  setDebugOutput routes IDF logs to the
    // active Serial (USB CDC), so we actually see those messages.
    Serial.setRxBufferSize(16384);
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5Cardputer.begin(cfg, true);
    Serial.setDebugOutput(true);
    esp_log_level_set("*", ESP_LOG_WARN);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(200);
    M5Cardputer.Display.setTextSize(1);

    drawBootScreen();

    logBoot();

    // Bump the task watchdog timeout to 15 s and re-init with panic enabled.
    // The 5 s default is the same value used by IDLE-task starvation checks;
    // when we kept hitting it during long MP3 decodes we want extra headroom
    // for transient hiccups (SD-card read stalls, libmad recovery loops on
    // partially corrupt frames) before declaring the audio task hung.
    {
        esp_err_t initRc = esp_task_wdt_init(15, true);
        logLine("BOOT", "task_wdt_init(15s, panic) rc=%d", (int)initRc);
    }

    M5.Speaker.begin();

    spk = new AudioOutputM5Speaker(&M5.Speaker, 0);
    spk->SetGain(1.0f);
    audioTaskInit();

    // The audio task lives on core 0 at priority 2 and intentionally hogs
    // CPU while decoding — IDLE0 won't get to run.  Unsubscribe IDLE0 from
    // the IDF task watchdog so it stops triggering false-positive resets
    // (panic_abort in task_wdt_isr) on long MP3 playback.  The audio task
    // itself is subscribed to TWDT inside audioTaskFn, so a real hang
    // (e.g. libmad infinite loop on a corrupt frame) is still detected.
    {
        TaskHandle_t idle0  = xTaskGetIdleTaskHandleForCPU(0);
        esp_err_t    delRc0 = idle0 ? esp_task_wdt_delete(idle0) : ESP_ERR_INVALID_ARG;
        logLine("BOOT", "task_wdt_delete(IDLE0) rc=%d", (int)delRc0);
    }

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    sdReady = SD.begin(SD_CS, SPI, 10000000);
    logLine("BOOT", "sd_ready=%d", (int)sdReady);
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

    if (volumeDisplayUntilMs && millis() >= volumeDisplayUntilMs) {
        volumeDisplayUntilMs = 0;
        if (mode == MP3_PLAYER) playerDrawUI();
        // In browse mode the image fills the whole screen, so the volume
        // overlay sits on top of pixels we want to keep — redraw the image
        // instead of just blanking the status bar (which would leave a hole).
        else if (mode == SOUNDBOARD && !boardSplashActive && useSoundboardBrowseUI())
            soundboardRefresh();
        else clearStatusBar();
    }

    // Audio runs on core 0 (audioTaskFn). When a stream ends naturally the task
    // sets this flag; the main loop handles the state transition here.
    if (audioEndedNaturally) {
        audioEndedNaturally = false;
        if (mode == MP3_PLAYER && playerState == PLAYER_PLAYING) {
            playerAutoAdvance();
        } else {
            sdSoundActive = false;
            playerState = PLAYER_STOPPED;
            if (mode == SOUNDBOARD && useSoundboardBrowseUI())
                soundboardRefresh();
        }
    }

    if (mode == SOUNDBOARD) soundboardLoop();

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
                soundboardRefresh();      // draw base UI first
                soundboardHandleKeyChange(st); // then process the key normally
            }
            return;
        }

        if (mode == MP3_PLAYER && playerSplashActive) {
            if (M5Cardputer.Keyboard.isPressed()) {
                playerSplashActive = false;
                playerDrawUI();
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
