#include "mp3player.h"
#include "config.h"
#include "audio.h"
#include "ui_utils.h"
#include <SD.h>
#include <vector>
#include <algorithm>

extern bool sdReady;

uint32_t    playerHintUntilMs = 0;
bool        playerSplashActive = false;
String      playerPath        = MP3_DIR;
String      playerFile        = "";
int         playerSelIdx      = 0;
PlayerState playerState       = PLAYER_STOPPED;
std::vector<DirEntry> playerEntries;

void playerLoadDir(const String &path) {
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

void drawPlayerSplash() {
    auto &d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextSize(2);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.drawCenterString("MP3 PLAYER", SCREEN_W / 2, SCREEN_H / 2 - 16);
    d.setTextSize(1);
    d.setTextColor(0x7BEF, TFT_BLACK);
    d.drawCenterString("TAB = next board   + - = vol", SCREEN_W / 2, SCREEN_H / 2 + 6);
}

void playerDrawUI() {
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
        d.setTextColor(TFT_RED, TFT_BLACK);
        d.drawCenterString("SD card not found!", SCREEN_W / 2, 70);
        return;
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
        int barH   = STATUS_Y - LIST_Y - 2;
        int thumbH = max(4, barH * VISIBLE / (int)playerEntries.size());
        int thumbY = LIST_Y + barH * playerSelIdx / (int)playerEntries.size();
        d.fillRect(SCREEN_W - 3, LIST_Y, 3, barH, 0x2104);
        d.fillRect(SCREEN_W - 3, thumbY, 3, thumbH, TFT_WHITE);
    }

    clearStatusBar();
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

void playerGoBack() {
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
        playerFile  = filePath;
        playerState = startMp3(playerFile.c_str()) ? PLAYER_PLAYING : PLAYER_STOPPED;
    }
    playerDrawUI();
}

void playerShowCurrentFile() {
    if (playerFile.isEmpty()) return;
    int lastSlash = playerFile.lastIndexOf('/');
    if (lastSlash < 0) return;
    String dir  = playerFile.substring(0, lastSlash);
    String name = playerFile.substring(lastSlash + 1);
    if (dir != playerPath) {
        playerPath = dir;
        playerLoadDir(playerPath);
    }
    for (int i = 0; i < (int)playerEntries.size(); i++) {
        if (!playerEntries[i].isDir && playerEntries[i].name == name) {
            playerSelIdx = i;
            break;
        }
    }
    playerDrawUI();
}

void playerAutoAdvance() {
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

void playerHandleKeys(const Keyboard_Class::KeysState &st) {
    if (isEscKey(st)) {
        if (playerState == PLAYER_PLAYING || playerState == PLAYER_PAUSED) {
            stopAudio();
            playerState = PLAYER_STOPPED;
            playerDrawUI();
        } else {
            playerGoBack();
        }
        return;
    }
    if (st.enter) { playerTogglePlay(); return; }
    bool changed = false;
    for (char c : st.word) {
        if      (c == 'j' || c == '.') { if (playerSelIdx < (int)playerEntries.size()-1) { playerSelIdx++; changed = true; } }
        else if (c == 'k' || c == ';') { if (playerSelIdx > 0) { playerSelIdx--; changed = true; } }
        else if (c == 'h' || c == ',') { playerGoBack(); return; }
        else if (c == 'l' || c == '/') { playerGoForward(); return; }
    }
    if (changed) playerDrawUI();
}
