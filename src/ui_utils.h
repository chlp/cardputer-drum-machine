#pragma once

#include <M5Cardputer.h>
#include "config.h"
#include <stddef.h>
#include <stdint.h>

bool     isEscKey(const Keyboard_Class::KeysState &st);
void     clearStatusBar();
uint8_t *readFileToBuffer(const char *path, size_t &outLen);
