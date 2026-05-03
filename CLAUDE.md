# Cardputer Meme Soundboard — CLAUDE.md

## Project Overview
Firmware for **M5 Cardputer ADV** (ESP32-S3): dual-mode device.
- **Mode 1 — Soundboard**: полифоническая музыкальная клавиатура (36 нот, C3–B5). Если на SD-карте есть `.mp3` для клавиши — играет мем-звук + показывает картинку.
- **Mode 2 — MP3 Player**: файловый менеджер с навигацией по папкам SD-карты.

## Hardware
- Board: M5Stack Cardputer ADV (ESP32-S3, 240×135 ST7789 display, physical QWERTY keyboard)
- Audio: built-in I2S speaker via M5Unified
- Storage: microSD via SPI — SCK=40, MISO=39, MOSI=14, CS=12
- Partition: `default_8MB.csv` (устройство 8MB, не 16MB!)

## Build System
- **PlatformIO** + Arduino framework, board: `m5stack-stamps3`
- `pio run` — сборка
- `pio run -t upload --upload-port /dev/tty.usbmodem201101` — прошивка
- `pio device monitor` — serial monitor

## Key Libraries
- `m5stack/M5Cardputer` — hardware abstraction (клавиатура, дисплей, I2S)
- ESP8266Audio — MP3 декодинг, **локальная копия** в `lib/ESP8266AudioLocal/`
  - Из неё удалены AudioOutputI2S*, AudioOutputPDM*, AudioOutputSPDIF*, AudioOutputULP* — они требуют `driver/i2s_std.h` (IDF5), а Arduino-ESP32 2.x использует IDF4
- `src/AudioOutputM5Speaker.h` — кастомный бридж ESP8266Audio → M5Unified Speaker (двойной буфер, `playRaw()` на канале 0)
- LovyanGFX (bundled with M5Cardputer) — дисплей, JPEG/PNG из SD

## SD Card Structure
```
/sounds/
  a.mp3    ← мем-звук для клавиши 'a' (опционально)
  a.jpg    ← картинка 240×110 px (опционально)
  ...      ← по одной паре на клавишу a-z, 0-9
/mp3/
  classic/
    01-Ode_to_Joy.mp3
    ...
  background/
    01-Lofi_Chill.mp3
    ...
  ← любая вложенность папок поддерживается
```
Готовый контент: `sd_card_content/` — скопировать на SD-карту целиком.

## Soundboard — Note Mapping
Строки клавиатуры снизу → сверху соответствуют низким → высоким нотам:

| Ряд | Клавиши | Ноты |
|-----|---------|------|
| Нижний | `z x c v b n m` | C3 C#3 D3 D#3 E3 F3 F#3 |
| Средний | `a s d f g h j k l` | G3 G#3 A3 A#3 B3 C4 C#4 D4 D#4 |
| Верхний | `q w e r t y u i o p` | E4 F4 F#4 G4 G#4 A4 A#4 B4 C5 C#5 |
| Цифры | `1 2 3 4 5 6 7 8 9 0` | D5 D#5 E5 F5 F#5 G5 G#5 A5 A#5 B5 |

- До 7 голосов одновременно (M5.Speaker каналы 1–7)
- Нота звучит пока клавиша удерживается (press → `noteOn`, release → `noteOff`)
- На экране рисуется мини-пианино; нажатые ноты подсвечиваются жёлтым
- Если для клавиши есть `/sounds/<key>.mp3` на SD — играет мем-звук (ноты при этом останавливаются)

## MP3 Player — Controls
| Клавиша | Действие |
|---------|----------|
| `j` / `,` | Следующий элемент |
| `k` / `;` | Предыдущий элемент |
| `l` или ENTER на папке | Войти в папку |
| `h` | Выйти на уровень выше |
| ENTER на файле | Играть / пауза / продолжить |
| `` ` `` (ESC) | Стоп |
| TAB | Переключить режим |

## Universal Controls
| Клавиша | Soundboard | MP3 Player |
|---------|-----------|------------|
| TAB | → MP3 Player | → Soundboard |
| `` ` `` | Стоп + сброс нот | Стоп воспроизведения |

## Source Layout
```
src/
  main.cpp               ← вся логика прошивки
  AudioOutputM5Speaker.h ← ESP8266Audio → M5Unified bridge
lib/
  ESP8266AudioLocal/     ← ESP8266Audio без I2S-output файлов
sd_card_content/
  sounds/                ← 36 мем-звуков + картинок (опционально)
  mp3/
    classic/             ← 10 классических произведений
    background/          ← 10 фоновых треков
platformio.ini
```

## Critical Implementation Notes
- **IDF4/IDF5**: `driver/i2s_std.h` недоступен в IDF4 → убраны I2S output файлы из ESP8266Audio
- **Изображения**: `drawJpgFile(SD, path)` не работает (нет `DataWrapperT<fs::SDFS>`). Решение: читать файл в heap через `malloc`, затем `drawJpg(buf, len)`
- **Аудио**: `gen->loop()` вызывается каждый кадр (non-blocking). Каждый новый звук → `delete gen/src`, создать заново
- **Полифония**: M5.Speaker канал 0 занят AudioOutputM5Speaker (MP3). Ноты используют каналы 1–7
- **`M5.Speaker.tone(freq, 0, ch, false)`**: duration=0 → бесконечно, stop_current=false → не прерывает другие каналы
- **Клавиатура**: press/release — оба события через `isChange()` (не только `isPressed()`). Для MP3 Player достаточно только `isPressed()`
- **Partition**: обязательно `default_8MB.csv`, иначе устройство не загрузится

## Language
Общаться с пользователем на русском языке.
