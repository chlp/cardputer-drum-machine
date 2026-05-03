#pragma once

#include <M5Cardputer.h>
#include <vector>
#include <Arduino.h>

struct DirEntry { String name; bool isDir; };
enum PlayerState { PLAYER_STOPPED, PLAYER_PLAYING, PLAYER_PAUSED };

extern uint32_t    playerHintUntilMs;
extern String      playerPath;
extern String      playerFile;
extern int         playerSelIdx;
extern PlayerState playerState;
extern std::vector<DirEntry> playerEntries;

void playerLoadDir(const String &path);
void playerDrawUI();
void playerAutoAdvance();
void playerHandleKeys(const Keyboard_Class::KeysState &st);
void playerGoBack();
void playerShowCurrentFile();
