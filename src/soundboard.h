#pragma once

#include <M5Cardputer.h>
#include <vector>
#include <Arduino.h>

extern std::vector<String> boardPaths;
extern int                 soundboardBoardIdx;
extern String              soundboardDir;
extern bool                boardSplashActive;
extern bool                sdSoundActive;
extern char                sbCurKey;
extern bool                sbPrevComma;
extern bool                sbPrevSlash;

void scanSoundboardDirs();
void sbInitBrowseSelection();
void sbResetInputState(); // clears prevSbKeys, sbPrevComma, sbPrevSlash

void soundboardRefresh();
void soundboardDrawIdle();
void soundboardLoop();
void soundboardHandleKeyChange(const Keyboard_Class::KeysState &st);

void drawBoardSplash();
void drawPiano();

bool resolveMemeMp3ForKey(char key, char *path, size_t pathCap);
bool useSoundboardBrowseUI();
