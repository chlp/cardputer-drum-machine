#include "sd_serial_xfer.h"
#include <SD.h>
#include <cstring>
#include <cstdlib>

// Commands (one line, end with \n). Prefix avoids accidental monitor keystrokes.
//   SD> PING
//   SD> LS /boards/meme
//   SD> STAT /path/file.mp3
//   SD> GET /path/file.bin     → +BIN <size>\n then raw bytes
//   SD> RM /path/file.mp3
//   SD> MKDIR /path/newdir
//   SD> PUT /path/file <size>  → reply +GO\n then host sends <size> raw bytes → +OK\n / -ERR\n

#define SDX_PREFIX "SD>"
#define SDX_PREFIX_LEN 3
#define SDX_MAX_LINE 256
#define SDX_MAX_PATH 128
#define SDX_PUT_CHUNK 512

static String       sdxLineBuf;
static bool         sdxPutActive = false;
static File         sdxPutFile;
static size_t      sdxPutRemain = 0;

static bool sdxSdOk() { return SD.cardType() != CARD_NONE; }

static String sdxBaseName(const String &entryName) {
    int p = entryName.lastIndexOf('/');
    return (p >= 0) ? entryName.substring(p + 1) : entryName;
}

static bool sdxSanePath(const String &p) {
    if (p.length() == 0 || p[0] != '/') return false;
    if (p.length() > SDX_MAX_PATH) return false;
    if (p.indexOf("..") >= 0) return false;
    if (p.indexOf('\\') >= 0) return false;
    return true;
}

static bool sdxMkdirP(const String &dir) {
    if (dir.length() == 0 || dir[0] != '/') return false;
    if (dir == "/") return true;
    File ex = SD.open(dir);
    if (ex) {
        bool isDir = ex.isDirectory();
        ex.close();
        return isDir;
    }
    int slash = dir.lastIndexOf('/');
    if (slash <= 0) return SD.mkdir(dir);
    String parent = dir.substring(0, slash);
    if (!sdxMkdirP(parent)) return false;
    return SD.mkdir(dir);
}

static bool sdxParentsForFile(const String &filePath) {
    int ls = filePath.lastIndexOf('/');
    if (ls <= 0) return true;
    return sdxMkdirP(filePath.substring(0, ls));
}

static void sdxReplyErr(const char *msg) { Serial.print("-ERR "); Serial.println(msg); }

static void sdxCmdPing() { Serial.println("+OK pong"); }

static void sdxCmdLs(const String &arg) {
    if (!sdxSdOk()) { sdxReplyErr("no_sd"); return; }
    if (!sdxSanePath(arg)) { sdxReplyErr("bad_path"); return; }
    File dir = SD.open(arg.c_str());
    if (!dir) { sdxReplyErr("open"); return; }
    if (!dir.isDirectory()) { dir.close(); sdxReplyErr("not_dir"); return; }
    Serial.println("+LIST");
    while (true) {
        File f = dir.openNextFile();
        if (!f) break;
        String child = sdxBaseName(String(f.name()));
        String full = arg;
        if (!full.endsWith("/")) full += "/";
        full += child;
        if (f.isDirectory()) {
            Serial.print("D\t");
            Serial.println(full);
        } else {
            Serial.print("F\t");
            Serial.print(full);
            Serial.print('\t');
            Serial.println((uint32_t)f.size());
        }
        f.close();
    }
    dir.close();
    Serial.println(".");
}

static void sdxCmdStat(const String &arg) {
    if (!sdxSdOk()) { sdxReplyErr("no_sd"); return; }
    if (!sdxSanePath(arg)) { sdxReplyErr("bad_path"); return; }
    File f = SD.open(arg.c_str(), FILE_READ);
    if (!f) { sdxReplyErr("no_file"); return; }
    if (f.isDirectory()) { f.close(); sdxReplyErr("is_dir"); return; }
    Serial.print("+STAT ");
    Serial.println((uint32_t)f.size());
    f.close();
}

static void sdxCmdGet(const String &arg) {
    if (!sdxSdOk()) { sdxReplyErr("no_sd"); return; }
    if (!sdxSanePath(arg)) { sdxReplyErr("bad_path"); return; }
    File f = SD.open(arg.c_str(), FILE_READ);
    if (!f) { sdxReplyErr("no_file"); return; }
    if (f.isDirectory()) { f.close(); sdxReplyErr("is_dir"); return; }
    size_t sz = f.size();
    Serial.print("+BIN ");
    Serial.println((uint32_t)sz);
    Serial.flush();
    const size_t chunk = 1024;
    uint8_t buf[chunk];
    while (sz > 0) {
        size_t n = (sz > chunk) ? chunk : sz;
        size_t r = f.read(buf, n);
        if (r == 0) break;
        Serial.write(buf, r);
        sz -= r;
        yield();
    }
    f.close();
    Serial.flush();
}

static void sdxCmdRm(const String &arg) {
    if (!sdxSdOk()) { sdxReplyErr("no_sd"); return; }
    if (!sdxSanePath(arg)) { sdxReplyErr("bad_path"); return; }
    if (arg == "/" || arg.length() < 2) { sdxReplyErr("unsafe"); return; }
    File f = SD.open(arg.c_str(), FILE_READ);
    if (f) {
        if (f.isDirectory()) { f.close(); sdxReplyErr("is_dir"); return; }
        f.close();
    }
    if (!SD.remove(arg.c_str())) { sdxReplyErr("remove"); return; }
    Serial.println("+OK");
}

static void sdxCmdMkdir(const String &arg) {
    if (!sdxSdOk()) { sdxReplyErr("no_sd"); return; }
    if (!sdxSanePath(arg)) { sdxReplyErr("bad_path"); return; }
    if (arg == "/") { sdxReplyErr("unsafe"); return; }
    if (!sdxMkdirP(arg)) { sdxReplyErr("mkdir"); return; }
    Serial.println("+OK");
}

static void sdxStartPut(const String &path, size_t total) {
    if (!sdxSdOk()) { sdxReplyErr("no_sd"); return; }
    if (!sdxSanePath(path)) { sdxReplyErr("bad_path"); return; }
    if (path == "/" || path.length() < 2) { sdxReplyErr("unsafe"); return; }
    if (total == 0) { sdxReplyErr("zero_size"); return; }
    if (!sdxParentsForFile(path)) { sdxReplyErr("mkdir"); return; }
    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    sdxPutFile = SD.open(path.c_str(), FILE_WRITE);
    if (!sdxPutFile) { sdxReplyErr("open_write"); return; }
    sdxPutRemain = total;
    sdxPutActive = true;
    Serial.println("+GO");
    Serial.flush();
}

static void sdxDrainPut() {
    if (!sdxPutActive) return;
    uint8_t buf[SDX_PUT_CHUNK];
    while (sdxPutRemain > 0 && Serial.available() > 0) {
        size_t want = (sdxPutRemain > SDX_PUT_CHUNK) ? SDX_PUT_CHUNK : sdxPutRemain;
        int n = Serial.readBytes(buf, want);
        if (n <= 0) break;
        size_t w = sdxPutFile.write(buf, (size_t)n);
        if (w != (size_t)n) {
            sdxPutFile.close();
            sdxPutActive = false;
            sdxReplyErr("write");
            return;
        }
        sdxPutRemain -= (size_t)n;
        yield();
    }
    if (sdxPutRemain == 0) {
        sdxPutFile.close();
        sdxPutActive = false;
        Serial.println("+OK");
    }
}

static void sdxProcessLine(const String &line) {
    if (sdxPutActive) return;
    if (!line.startsWith(SDX_PREFIX)) return;
    String rest = line.substring(SDX_PREFIX_LEN);
    rest.trim();
    int sp = rest.indexOf(' ');
    String cmd = (sp < 0) ? rest : rest.substring(0, sp);
    String arg = (sp < 0) ? String("") : rest.substring(sp + 1);
    arg.trim();
    cmd.toUpperCase();

    if (cmd == "PING") {
        sdxCmdPing();
        return;
    }
    if (cmd == "LS") {
        sdxCmdLs(arg);
        return;
    }
    if (cmd == "STAT") {
        sdxCmdStat(arg);
        return;
    }
    if (cmd == "GET") {
        sdxCmdGet(arg);
        return;
    }
    if (cmd == "RM") {
        sdxCmdRm(arg);
        return;
    }
    if (cmd == "MKDIR") {
        sdxCmdMkdir(arg);
        return;
    }
    if (cmd == "PUT") {
        int sp2 = arg.lastIndexOf(' ');
        if (sp2 < 0) { sdxReplyErr("usage"); return; }
        String path = arg.substring(0, sp2);
        path.trim();
        String szStr = arg.substring(sp2 + 1);
        szStr.trim();
        char *endp = nullptr;
        unsigned long total = strtoul(szStr.c_str(), &endp, 10);
        if (endp == szStr.c_str() || *endp != '\0') { sdxReplyErr("bad_size"); return; }
        const unsigned long maxPut = 512UL * 1024 * 1024;
        if (total > maxPut) { sdxReplyErr("too_large"); return; }
        sdxStartPut(path, (size_t)total);
        return;
    }
    if (cmd == "HELP") {
        Serial.println("+OK commands: PING LS STAT GET RM MKDIR PUT");
        return;
    }
    sdxReplyErr("unknown_cmd");
}

void sdSerialXferSetup() {
    Serial.println();
    Serial.println("+SDX ready (prefix " SDX_PREFIX " …)");
}

void sdSerialXferLoop() {
    if (sdxPutActive) {
        sdxDrainPut();
        return;
    }
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            if (sdxLineBuf.length() > 0) {
                sdxProcessLine(sdxLineBuf);
                sdxLineBuf = "";
            }
            continue;
        }
        if (sdxLineBuf.length() >= SDX_MAX_LINE) {
            sdxLineBuf = "";
            sdxReplyErr("line_too_long");
            continue;
        }
        sdxLineBuf += c;
    }
    if (sdxPutActive) sdxDrainPut();
}
