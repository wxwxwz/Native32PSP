#include "platform/psp_app.h"
#include "platform/psp_log.h"
#include <pspkernel.h>

PSP_MODULE_INFO("Native32PSP", 0, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

static int exitCallback(int, int, void*) {
    sceKernelExitGame();
    return 0;
}

static int callbackThread(SceSize, void*) {
    int callbackId = sceKernelCreateCallback("Exit Callback", exitCallback, 0);
    sceKernelRegisterExitCallback(callbackId);
    sceKernelSleepThreadCB();
    return 0;
}

static int setupCallbacks() {
    int threadId = sceKernelCreateThread("update_thread", callbackThread, 0x11, 0xfa0, 0, 0);
    if (threadId >= 0) {
        sceKernelStartThread(threadId, 0, 0);
    }
    return threadId;
}

int main() {
    n32::pspLogReset();
    n32::pspLog("main: begin");
    setupCallbacks();
    n32::pspLog("main: callbacks ready");
    n32::PspApp app;
    int result = app.run();
    n32::pspLog("main: app returned %d", result);
    return result;
}
