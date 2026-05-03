#pragma once

// SD card SPI pins for M5 Cardputer ADV
#define SD_SCK  40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS   12

#define BOARDS_DIR  "/boards"
#define BOARDS_MEME "/boards/meme"
#define MP3_DIR     "/mp3"

#define BOARD_TITLE_MAX 10
#define PLAYER_HINT_MS  2200

#define SETTINGS_PATH  "/settings.cfg"
#define VOL_STEP       16
#define VOL_DISPLAY_MS 1500

#define SCREEN_W 240
#define SCREEN_H 135
#define IMG_H    110
#define STATUS_Y 110

#define NOTE_CH_BASE 1
#define NOTE_CH_CNT  7

enum AppMode { SOUNDBOARD, MP3_PLAYER };
