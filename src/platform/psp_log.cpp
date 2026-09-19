#include "platform/psp_log.h"
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace n32 {

static const char* kLogPaths[] = {
    "ms0:/PSP/GAME/Native32PSP/native32psp.log",
    "ms0:/native32psp.log",
};

static int openFirstLog(int flags) {
    for (unsigned i = 0; i < sizeof(kLogPaths) / sizeof(kLogPaths[0]); ++i) {
        int fd = sceIoOpen(kLogPaths[i], flags, 0777);
        if (fd >= 0) {
            return fd;
        }
    }
    return -1;
}

void pspLogReset() {
    const char* text = "Native32PSP log start\n";
    int fd = openFirstLog(PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC);
    if (fd >= 0) {
        sceIoWrite(fd, text, strlen(text));
        sceIoClose(fd);
    }
}

void pspLog(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(buffer, sizeof(buffer) - 2, fmt, args);
    va_end(args);
    if (length < 0) {
        return;
    }
    if (length > static_cast<int>(sizeof(buffer) - 2)) {
        length = sizeof(buffer) - 2;
    }
    buffer[length++] = '\n';
    buffer[length] = '\0';

    int fd = openFirstLog(PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND);
    if (fd >= 0) {
        sceIoWrite(fd, buffer, length);
        sceIoClose(fd);
    }
}

}
