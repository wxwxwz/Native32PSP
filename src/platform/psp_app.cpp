#include "platform/psp_app.h"
#include "core/content_loader.h"
#include "platform/logo_rgba.h"
#include "platform/psp_log.h"
#include "platform/menu_font.h"
#include "core/cheat_file.h"
#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <pspaudio.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspthreadman.h>
#include <psputility.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef PSP
#include <malloc.h>
#endif

namespace n32 {

static unsigned int __attribute__((aligned(16))) displayList[262144];
static const size_t kAudioRingSamples = 32768 * 2;
// A dedicated output thread feeds these aligned ~23 ms hardware blocks.
// Prime with two blocks so 30 Hz producer bursts do not starve the device.
static const size_t kAudioBlockFrames = 1024;
static const int kSettingsRows[4][4]={{0,9,1,3},{2,-1,-1,-1},{4,8,5,-1},{6,7,-1,-1}};
static const unsigned kSettingsCounts[4]={4,1,3,2};

static u32* vramPointer(void* buffer) {
    u8* edram = (u8*)sceGeEdramGetAddr();
    uintptr_t raw = (uintptr_t)buffer;
    if (raw < 0x00200000) {
        return (u32*)(edram + raw);
    }
    return (u32*)buffer;
}

static bool isGameFile(const std::string& path) {
    std::string ext = pathExtensionLower(path);
    return ext == "smf" || ext == "sgm" || ext == "ssl";
}

static const char* glyphRows(char ch) {
    switch ((char)toupper((unsigned char)ch)) {
    case 'A': return "111101111101101";
    case 'B': return "110101110101110";
    case 'C': return "111100100100111";
    case 'D': return "110101101101110";
    case 'E': return "111100110100111";
    case 'F': return "111100110100100";
    case 'G': return "111100101101111";
    case 'H': return "101101111101101";
    case 'I': return "111010010010111";
    case 'J': return "001001001101111";
    case 'K': return "101101110101101";
    case 'L': return "100100100100111";
    case 'M': return "101111111101101";
    case 'N': return "101111111111101";
    case 'O': return "111101101101111";
    case 'P': return "111101111100100";
    case 'Q': return "111101101111001";
    case 'R': return "111101111110101";
    case 'S': return "111100111001111";
    case 'T': return "111010010010010";
    case 'U': return "101101101101111";
    case 'V': return "101101101101010";
    case 'W': return "101101111111101";
    case 'X': return "101101010101101";
    case 'Y': return "101101010010010";
    case 'Z': return "111001010100111";
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001010010010";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    case '.': return "000000000000010";
    case '-': return "000000111000000";
    case '_': return "000000000000111";
    case '/': return "001001010100100";
    case '>': return "100010001010100";
    case ':': return "000010000010000";
    case ' ': return "000000000000000";
    default: return "111001011000010";
    }
}

// Called every few directory entries so the caller can repaint a progress
// screen; the scan itself does not care about the result.
typedef void (*ScanTickFn)(void* context, const std::string& dirPath, unsigned int found);

static void collectGamesInDir(const std::string& dirPath, std::vector<std::string>* out,
                              ScanTickFn tick, void* tickContext) {
    pspLog("collectGamesInDir: opendir %s", dirPath.c_str());
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        pspLog("collectGamesInDir: opendir failed %s", dirPath.c_str());
        return;
    }

    struct dirent* entry = 0;
    unsigned int seen = 0;
    while ((entry = readdir(dir)) != 0) {
        std::string name = entry->d_name;
        pspLog("collectGamesInDir: entry %s", name.c_str());
        if (name == "." || name == "..") {
            continue;
        }
        std::string full = pathJoin(dirPath, name);
        if (isGameFile(full)) {
            pspLog("collectGamesInDir: add %s", full.c_str());
            out->push_back(full);
        }
        if (tick && (++seen % 8) == 0) {
            tick(tickContext, dirPath, (unsigned int)out->size());
        }
    }
    closedir(dir);
    pspLog("collectGamesInDir: closed %s", dirPath.c_str());
}

PspApp::PspApp()
    : running(true), clearColor(0xff202020), gameLoaded(false), menuMode(true),
      settingsMode(false), pauseMode(false), cheatMode(false), screenshotMode(false), screenshotInfo(true), screenshotNoticeTicks(0), screenshotCount(0), pauseIndex(0), cheatIndex(0),
      settingsIndex(0), settings(), frameCounter(0),
      previousButtons(0), suppressedButtons(0), drawBuffer((void*)0), audioChannel(-1),
      audioThread(-1), audioMutex(-1), audioThreadRunning(false), audioGeneration(0), audioPrimeBlocks(2),
      audioUnderruns(0), audioErrors(0), audioLogTick(0),
      audioRing(kAudioRingSamples, 0), audioReadPos(0), audioWritePos(0), audioQueuedSamples(0),
      audioOutputBudget(0), audioDropped(0), audioDroppedLogged(0), scanAnimation(0),
      renderCadence(), coreClock(), coreTickCounter(0), lastCoreMicros(0), displayFps(),
      statusLine("boot"), selectedIndex(0), menuFrame(480 * 272, 0xff121418) {
    gameBackgroundDirty[0] = gameBackgroundDirty[1] = true;
    pspLog("PspApp: constructed");
}

void PspApp::initGu() {
    pspLog("initGu: sceGuInit");
    sceGuInit();
    pspLog("initGu: sceGuStart");
    sceGuStart(GU_DIRECT, displayList);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, 512);
    sceGuDispBuffer(480, 272, (void*)0x88000, 512);
    sceGuDepthBuffer((void*)0x110000, 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuFinish();
    sceGuSync(0, 0);
    memset(vramPointer((void*)0), 0, 272 * 512 * sizeof(u32));
    memset(vramPointer((void*)0x88000), 0, 272 * 512 * sizeof(u32));
    sceKernelDcacheWritebackRange(vramPointer((void*)0), 272 * 512 * sizeof(u32));
    sceKernelDcacheWritebackRange(vramPointer((void*)0x88000), 272 * 512 * sizeof(u32));
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    drawBuffer = (void*)0;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    pspLog("initGu: done");
}

void PspApp::initAudio() {
    int avcodec = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    int mp3 = sceUtilityLoadModule(PSP_MODULE_AV_MP3);
    pspLog("initAudio: avcodec=%d mp3=%d", avcodec, mp3);
    audioChannel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                     PSP_AUDIO_SAMPLE_ALIGN((int)kAudioBlockFrames),
                                     PSP_AUDIO_FORMAT_STEREO);
    if (audioChannel < 0) {
        pspLog("initAudio: sceAudioChReserve FAILED (%d)", audioChannel);
        return;
    }
    audioMutex = sceKernelCreateSema("n32_audio_lock", 0, 1, 1, 0);
    if (audioMutex < 0) {
        pspLog("initAudio: semaphore failed (%d)", audioMutex);
        sceAudioChRelease(audioChannel);
        audioChannel = -1;
        return;
    }
    clearAudioQueue();
    audioThreadRunning = true;
    audioThread = sceKernelCreateThread("n32_audio", audioThreadEntry, 0x12, 0x4000,
                                       PSP_THREAD_ATTR_USER, 0);
    PspApp* self = this;
    if (audioThread < 0 || sceKernelStartThread(audioThread, sizeof(self), &self) < 0) {
        pspLog("initAudio: audio thread failed");
        if (audioThread >= 0) sceKernelDeleteThread(audioThread);
        audioThread = -1;
        audioThreadRunning = false;
        shutdownAudio();
        return;
    }
    pspLog("initAudio: channel=%d", audioChannel);
}

void PspApp::shutdownGu() {
    pspLog("shutdownGu");
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
}

void PspApp::shutdownAudio() {
    if (audioMutex >= 0) {
        sceKernelWaitSema(audioMutex, 1, 0);
        audioThreadRunning = false;
        sceKernelSignalSema(audioMutex, 1);
    }
    if (audioThread >= 0) {
        sceKernelWaitThreadEnd(audioThread, 0);
        sceKernelDeleteThread(audioThread);
        audioThread = -1;
    }
    if (audioChannel >= 0) {
        while (sceAudioGetChannelRestLen(audioChannel) > 0) sceKernelDelayThread(1000);
        sceAudioChRelease(audioChannel);
        audioChannel = -1;
    }
    clearAudioQueue();
    if (audioMutex >= 0) {
        sceKernelDeleteSema(audioMutex);
        audioMutex = -1;
    }
}

void PspApp::clearAudioQueue() {
    if (audioMutex >= 0) sceKernelWaitSema(audioMutex, 1, 0);
    audioReadPos = 0;
    audioWritePos = 0;
    audioQueuedSamples = 0;
    ++audioGeneration;
    if (audioMutex >= 0) sceKernelSignalSema(audioMutex, 1);
}

void PspApp::readInput() {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    // PSP stick is 0..255, centered near 128. A broad dead zone avoids drift.
    if(pad.Lx<80) pad.Buttons|=PSP_CTRL_LEFT;
    else if(pad.Lx>176) pad.Buttons|=PSP_CTRL_RIGHT;
    if(pad.Ly<80) pad.Buttons|=PSP_CTRL_UP;
    else if(pad.Ly>176) pad.Buttons|=PSP_CTRL_DOWN;
    unsigned int pressed = pad.Buttons & ~previousButtons;
    previousButtons = pad.Buttons;
    suppressedButtons &= pad.Buttons;
    pressed &= ~suppressedButtons;
    pad.Buttons &= ~suppressedButtons;

    // HOME/PS belongs to the firmware exit dialog and registered exit callback.
    if (pressed & PSP_CTRL_SELECT) {
        closeScreenshots();systemInfoMode=false;aboutMode=false;
        pauseMode = cheatMode = false;
        if (settingsMode && !settings.save(settingsPath())) pspLog("settings: save failed");
        settingsMode = false;
        menuMode = true;
        statusLine = "menu";
        buttons.clear();
        emulator.setButtons(buttons);
        clearAudioQueue();
        return;
    }

    if (gameLoaded && !menuMode && !settingsMode && (pressed & PSP_CTRL_SQUARE)) {
        if (pauseMode) resumeGame();
        else {
            pauseMode = true; cheatMode = false; pauseIndex = 0; pauseStatus.clear();
            pspLog("pause: enter video=%u source=%ux%u transfer=%s", emulator.isVideoFrame()?1u:0u,
                   (unsigned)emulator.framebufferWidth(), (unsigned)emulator.framebufferHeight(),
                   gamePresentation.usesTexture()?"GU-texture":"GE-copy");
            buttons.clear(); emulator.setButtons(buttons); clearAudioQueue();
        }
        return;
    }
    if (pauseMode) { readPauseMenu(pressed); return; }

    if (settingsMode) {
        if(aboutMode) {if(pressed & PSP_CTRL_CROSS)aboutMode=false;return;}

        if(systemInfoMode) {
            if(pressed & PSP_CTRL_CROSS)systemInfoMode=false;
            if(pressed & PSP_CTRL_CIRCLE)systemInfoSampled=false;
            return;
        }
        if(!settingsDetail) {
            if(pressed & PSP_CTRL_UP)settingsCategory=(settingsCategory+3)%4;
            if(pressed & PSP_CTRL_DOWN)settingsCategory=(settingsCategory+1)%4;
            settingsIndex=kSettingsRows[settingsCategory][0];
            if(pressed & (PSP_CTRL_CIRCLE|PSP_CTRL_RIGHT)) {settingsDetail=true;return;}
            if(pressed & PSP_CTRL_CROSS) {
                settingsMode=false;menuMode=true;
                if(!settings.save(settingsPath()))pspLog("settings: save failed");
            }
            return;
        }
        if(pressed & PSP_CTRL_CROSS) {settingsDetail=false;return;}
        unsigned item=0;
        while(item+1<kSettingsCounts[settingsCategory] && kSettingsRows[settingsCategory][item]!=(int)settingsIndex)++item;
        if(pressed & PSP_CTRL_UP)item=(item+kSettingsCounts[settingsCategory]-1)%kSettingsCounts[settingsCategory];
        if(pressed & PSP_CTRL_DOWN)item=(item+1)%kSettingsCounts[settingsCategory];
        settingsIndex=kSettingsRows[settingsCategory][item];
        if(pressed & PSP_CTRL_CIRCLE) {
            if(settingsIndex==7) {aboutMode=true;return;}
            if(settingsIndex==6) {systemInfoMode=true;systemInfoSampled=false;systemIdleValid=false;return;}
        }
        if(pressed & PSP_CTRL_LEFT)adjustSetting((int)settingsIndex,-1);
        if(pressed & (PSP_CTRL_RIGHT|PSP_CTRL_CIRCLE))adjustSetting((int)settingsIndex,1);
        return;
    }

    if (menuMode) {
        if ((pressed & PSP_CTRL_CROSS) && gameLoaded) {
            menuMode = false;
            coreClock.reset(); displayFps.reset(); // Time spent choosing is a pause, not backlog.
            renderCadence.reset();
            gamePresentation.clear();
            return;
        }
        if (pressed & PSP_CTRL_TRIANGLE) {
            settingsMode = true;
            menuMode = false;
            settingsIndex = 0;settingsCategory=0;settingsDetail=false;
            return;
        }
        if ((pressed & PSP_CTRL_UP) && !gamePaths.empty()) {
            selectedIndex = selectedIndex == 0 ? (unsigned int)gamePaths.size() - 1 : selectedIndex - 1;
            pspLog("menu: selected %u", selectedIndex);
        }
        if ((pressed & PSP_CTRL_DOWN) && !gamePaths.empty()) {
            selectedIndex = (selectedIndex + 1) % (unsigned int)gamePaths.size();
            pspLog("menu: selected %u", selectedIndex);
        }
        if ((pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_START)) && !gamePaths.empty()) {
            loadSelectedGame();
        }
        return;
    }

    if (gameLoaded && (pressed & PSP_CTRL_TRIANGLE)) takeScreenshot();

    if (gameLoaded && (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_START)) &&
        emulator.skipCutscene()) {
        suppressedButtons |= pad.Buttons & (PSP_CTRL_CIRCLE | PSP_CTRL_START);
        buttons.clear();
        emulator.setButtons(buttons);
        clearAudioQueue();
        // skipCutscene may redraw outside tick(). Publish that image even if
        // the next fixed-skip tick is suppressed and its dimensions match.
        gamePresentation.clear();
        coreClock.reset(); displayFps.reset();
        renderCadence.reset();
        return;
    }

    buttons.clear();
    if (pad.Buttons & PSP_CTRL_LEFT) {
        buttons.push_back(KeyLeft);
    }
    if (pad.Buttons & PSP_CTRL_RIGHT) {
        buttons.push_back(KeyRight);
    }
    if (pad.Buttons & PSP_CTRL_UP) {
        buttons.push_back(KeyUp);
    }
    if (pad.Buttons & PSP_CTRL_DOWN) {
        buttons.push_back(KeyDown);
    }
    if (pad.Buttons & PSP_CTRL_CIRCLE) {
        buttons.push_back(KeyA);
    }
    if (pad.Buttons & PSP_CTRL_CROSS) {
        buttons.push_back(KeyB);
    }

    // Held-key repeat counters advance in drawGameFrame once per core tick.
}

void PspApp::drawFrame() {
    if (pauseMode) {
        drawPauseMenu();
    } else if (settingsMode) {
        drawSettingsFrame();
    } else if (menuMode) {
        drawMenuFrame();
    } else {
        drawGameFrame();
    }
}

bool PspApp::captureGameFrame(std::vector<u32>* pixels) {
    if(!pixels || gamePresentation.empty())return false;
    // Re-draw the retained game texture into the non-visible page without HUD
    // or notices. Read back only on demand; normal frames never pay this cost.
    u32* back=vramPointer(drawBuffer);
    gamePresentation.draw(drawBuffer,back,displayList);
    const u32 w=gamePresentation.width(),h=gamePresentation.height();
    const u32 x=(480-w)/2,y=(272-h)/2;
    pixels->resize((size_t)w*h);
    for(u32 row=0;row<h;++row)
        memcpy(pixels->data()+(size_t)row*w,back+(y+row)*512+x,w*sizeof(u32));
    gameBackgroundDirty[drawBuffer?1:0]=true;
    return true;
}

void PspApp::drawGameFrame() {
    // UI uploads are only needed while a menu/loading screen is visible.
    // Keep the retained game surface separate for pause-menu screenshots.
    if (!menuPresentation.empty()) menuPresentation.release();
    ++frameCounter;
    bool copiedFrame = false;
    bool measuredRender=false;
    u32 renderCost=0,copyBegin=0;
    bool frameUpdated = false;
    u32 srcW = 0;
    u32 srcH = 0;
    size_t fbPixels = 0;
    u32 copyW = 0;
    u32 copyH = 0;
    int dstX = 0;
    int dstY = 0;

    if (gameLoaded) {
        if (frameCounter <= 1) {
            pspLog("drawFrame: tick begin frame=%u", frameCounter);
        }
        const unsigned due = coreClock.due(sceKernelGetSystemTimeLow());
        bool rendered = false;
        for (unsigned step = 0; step < due; ++step) {
            ++coreTickCounter;
            bool render = step + 1 == due;
            const bool videoTick = emulator.isCutsceneActive();
            // A catch-up pass already omits its first tick's picture. Present
            // the last tick regardless of parity so repeated two-tick passes
            // cannot lock fixed frame skip onto an always-discarded phase.
            if (settings.frameSkip == SkipOne) render = render && (due > 1 || (coreTickCounter & 1u));
            // Advance cadence only at a presentation opportunity. Advancing on
            // catch-up ticks can phase-lock all draws to discarded frames.
            // MPEG has its own measured B-picture budget. Applying the game
            // cadence as well discards otherwise affordable video pictures.
            if (settings.frameSkip == SkipAuto && render && !videoTick) {
                const bool scheduled = renderCadence.next();
                render = render && scheduled;
            }
            if(loadingShown || emulator.tickCount==0)render=true;
            // Frame skip suppresses compositing only: logic, video decode and
            // audio still advance on every 30 Hz tick.
            emulator.setButtons(buttons);
            const bool startingContent = emulator.tickCount==0 ||
                (!emulator.videoPlayer && !emulator.pendingVideos.empty());
            u32 before = sceKernelGetSystemTimeLow();
            emulator.tick(render);
            if (!emulator.contentError.empty()) {
                pspLog("game: returning to menu after content failure '%s'", emulator.contentError.c_str());
                releaseGame();
                statusLine = "load failed";
                clearAudioQueue();
                return;
            }
            // A 25 Hz clip or a skipped B picture can retain the prior RGB
            // image even when presentation was requested on this 30 Hz tick.
            // Keep feeding audio, but do not upload/swap that image again.
            rendered = rendered || emulator.frameChanged;
            const u32 mixBegin=sceKernelGetSystemTimeLow();
            logicTiming.add(mixBegin-before);
            if (!videoTick && !emulator.isCutsceneActive() && !startingContent && emulator.tickCount > 1)
                recordGameProfile(mixBegin-before, mixBegin);
            if(emulator.frameChanged && !videoTick && !emulator.isCutsceneActive())
                drawTiming.add(emulator.lastDrawMicros);
            emulator.pendingAudioSamples(&audioSamples);
            // Streaming MP3 decoding is part of the tick budget as well.
            lastCoreMicros = sceKernelGetSystemTimeLow() - before;
            queueAudioSamples(audioSamples, emulator.audioSampleRate());
            const u32 end=sceKernelGetSystemTimeLow();
            mixTiming.add(end-mixBegin);
            // File reads, demux and scene initialization are not steady-state
            // frame costs. Do not let one load hold an entire short clip at 10 FPS.
            // Preserve the game's cadence across clips, including queued and
            // final video ticks. MPEG costs must not train the game estimator.
            if(videoTick || emulator.isCutsceneActive()) measuredRender=false;
            else if(startingContent || emulator.tickCount<=1) renderCadence.reset();
            // Only a tick that produced pixels can confirm cadence recovery;
            // cheap retained-image ticks must behave like skipped rendering.
            else if(emulator.frameChanged){measuredRender=true;renderCost=end-before;}
            else renderCadence.observe(false,end-before);
        }
        if (frameCounter <= 1) {
            pspLog("drawFrame: tick end frame=%u", frameCounter);
        }
        // A scene switch resets tickCount; keep its loading screen until the
        // next scene has actually run and produced its first frame.
        if(loadingShown && emulator.tickCount==0) {coreClock.reset();return;}
        if(loadingShown && rendered) {
            loadingShown=false;loadingStage.clear();std::vector<u32>().swap(loadingPanel);coreClock.reset();displayFps.reset();
            if(!emulator.isCutsceneActive())renderCadence.reset();
        }
        copyBegin=sceKernelGetSystemTimeLow();
        const std::vector<u32>& fb = emulator.framebuffer();
        srcW = emulator.framebufferWidth();
        srcH = emulator.framebufferHeight();
        const VideoScaling scaling = emulator.isVideoFrame() ? videoScaling(srcW,srcH) : settings.scaling;
        fbPixels = fb.size();
        if (!fb.empty() && srcW > 0 && srcH > 0 && srcW <= fb.size() / srcH) {
            scaledSize(srcW, srcH, scaling, &copyW, &copyH);
            dstX = (480 - (int)copyW) / 2;
            dstY = (272 - (int)copyH) / 2;
            if (dstX < 0) {
                dstX = 0;
            }
            if (dstY < 0) {
                dstY = 0;
            }
            if (rendered || gamePresentation.empty() || gamePresentation.width() != copyW || gamePresentation.height() != copyH) {
                if (gamePresentation.width() != copyW || gamePresentation.height() != copyH)
                    gameBackgroundDirty[0] = gameBackgroundDirty[1] = true;
                const u32 prepareBegin=sceKernelGetSystemTimeLow();
                frameUpdated=gamePresentation.prepare(fb,srcW,srcH,scaling,settings.smoothing);
                prepareTiming.add(sceKernelGetSystemTimeLow()-prepareBegin);
            }
        } else if (srcW > 0 && srcH > 0) {
            pspLog("drawFrame: invalid framebuffer %ux%u pixels=%u", (unsigned)srcW,
                   (unsigned)srcH, (unsigned)fb.size());
            releaseGame();
            statusLine = "load failed";
            clearAudioQueue();
            return;
        }
    }

    // The displayed buffer remains valid between core ticks. Re-copying and
    // swapping an identical frame only consumes VRAM bandwidth; keep polling
    // input, servicing audio and waiting for vblank at the original cadence.
    const bool newGameFrame = frameUpdated;
    const bool fpsChanged = displayFps.sample(sceKernelGetSystemTimeLow());
    if(settings.showFps && fpsChanged && !gamePresentation.empty()) frameUpdated=true;
    if(screenshotNoticeTicks && !gamePresentation.empty()) frameUpdated=true;
    if (frameUpdated && !gamePresentation.empty() && copyW > 0 && copyH > 0) {
        if (frameCounter <= 1) {
            pspLog("drawFrame: GE present begin frame=%u %ux%u at %d,%d srcW=%u",
                   frameCounter, (unsigned)copyW, (unsigned)copyH, dstX, dstY, (unsigned)srcW);
        }
        u32* drawBase = vramPointer(drawBuffer);
        unsigned slot = drawBase == vramPointer((void*)0) ? 0 : 1;
        char fps[24];
        unsigned fpsWidth=0,fpsHeight=0;
        if(settings.showFps) {
            snprintf(fps,sizeof(fps),"FPS %u",(unsigned)displayFps.value);
            // The 3x5 font has a four-pixel advance and four-pixel padding.
            fpsWidth=(unsigned)strlen(fps)*4-1+8;fpsHeight=13;
        }
        // The game redraw restores its own canvas. A shrinking HUD also needs
        // the old panel removed from margins in the GPU framebuffer.
        const bool smallerPanel=fpsPanelWidth[slot]>fpsWidth || fpsPanelHeight[slot]>fpsHeight;
        const bool clearBackground = gameBackgroundDirty[slot] ||
            (smallerPanel && (dstX>0 || dstY>0));
        if (clearBackground) {
            std::fill(drawBase, drawBase + 272 * 512, 0xff000000u);
            gameBackgroundDirty[slot] = false;
        }
        const u32 transferBegin=sceKernelGetSystemTimeLow();
        gamePresentation.draw(drawBuffer,drawBase,displayList,clearBackground);
        lastTransferGe=true;
        transferTiming.add(sceKernelGetSystemTimeLow()-transferBegin);
        if(screenshotNoticeTicks) {
            --screenshotNoticeTicks;
            // Copying the full canvas above erases the previous toast. Keep both
            // VRAM backgrounds dirty so its black-margin portion is erased too.
            gameBackgroundDirty[0]=gameBackgroundDirty[1]=true;
            if(screenshotNoticeTicks) {
                drawMenuRect(0,248,480,24,uiColor(0xff101319));
                drawMenuName(8,252,screenshotNotice,accent(),472);
            }
        }
        if(settings.showFps) {
            drawMenuRect(0,0,fpsWidth,fpsHeight,uiColor(0xff101319));
            drawMenuText(4,4,fps,accent(),1);
        }
        fpsPanelWidth[slot]=fpsWidth;fpsPanelHeight[slot]=fpsHeight;
        drawGameOverlays(drawBase,fpsWidth,fpsHeight,screenshotNoticeTicks!=0);
        copiedFrame = true;
        if (frameCounter <= 1) {
            pspLog("drawFrame: GE present end frame=%u", frameCounter);
        }
    }

    const u32 copyEnd=sceKernelGetSystemTimeLow();
    if(frameUpdated)copyTiming.add(copyEnd-copyBegin);
    if(measuredRender)renderCadence.observe(true,renderCost+(copyEnd-copyBegin));
    // A skipped presentation has no swap to synchronize. Sleep only until the
    // next core deadline (bounded to keep input responsive), not another vblank.
    if(copiedFrame) sceDisplayWaitVblankStart();
    else {
        const u32 remaining=coreClock.untilNext(copyEnd);
        if(remaining)sceKernelDelayThread(std::min<u32>(remaining,16667u));
    }
    waitTiming.add(sceKernelGetSystemTimeLow()-copyEnd);
    if (frameCounter <= 1) {
        pspLog("drawFrame: swap begin frame=%u", frameCounter);
    }
    if (copiedFrame) {
        drawBuffer = sceGuSwapBuffers();
        if(newGameFrame) displayFps.presented();
    }
    if (frameCounter <= 1) {
        pspLog("drawFrame: swap end frame=%u next=%p", frameCounter, drawBuffer);
    }

    outputAudio();

    if (frameCounter <= 1) {
        pspLog("drawFrame: done frame=%u loaded=%d copied=%d game=%ux%u fb=%u",
               frameCounter, gameLoaded ? 1 : 0, copiedFrame ? 1 : 0,
               (unsigned)srcW, (unsigned)srcH, (unsigned)fbPixels);
    }
}

void PspApp::drawMenuRect(int x, int y, int w, int h, u32 color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > 480 ? 480 : x + w;
    int y1 = y + h > 272 ? 272 : y + h;
    for (int py = y0; py < y1; ++py) {
        u32* row = &menuFrame[py * 480];
        for (int px = x0; px < x1; ++px) {
            row[px] = color;
        }
    }
}

void PspApp::drawLogo(int x, int y) {
    for (unsigned int ly = 0; ly < kLogoHeight; ++ly) {
        int dy = y + (int)ly;
        if (dy < 0 || dy >= 272) {
            continue;
        }
        for (unsigned int lx = 0; lx < kLogoWidth; ++lx) {
            int dx = x + (int)lx;
            if (dx < 0 || dx >= 480) {
                continue;
            }
            u32 src = kLogoPixels[ly * kLogoWidth + lx];
            u32 alpha = (src >> 24) & 0xff;
            if (alpha == 0) {
                continue;
            }
            if (alpha == 255) {
                menuFrame[dy * 480 + dx] = src;
            } else {
                u32 dst = menuFrame[dy * 480 + dx];
                u32 sr = src & 0xff;
                u32 sg = (src >> 8) & 0xff;
                u32 sb = (src >> 16) & 0xff;
                u32 dr = dst & 0xff;
                u32 dg = (dst >> 8) & 0xff;
                u32 db = (dst >> 16) & 0xff;
                u32 inv = 255 - alpha;
                u32 r = (sr * alpha + dr * inv) / 255;
                u32 g = (sg * alpha + dg * inv) / 255;
                u32 b = (sb * alpha + db * inv) / 255;
                menuFrame[dy * 480 + dx] = 0xff000000 | (b << 16) | (g << 8) | r;
            }
        }
    }
}

void PspApp::drawMenuChar(int x, int y, char ch, u32 color, int scale) {
    const char* glyph = glyphRows(ch);
    for (int gy = 0; gy < 5; ++gy) {
        for (int gx = 0; gx < 3; ++gx) {
            if (glyph[gy * 3 + gx] != '1') {
                continue;
            }
            drawMenuRect(x + gx * scale, y + gy * scale, scale, scale, color);
        }
    }
}

void PspApp::drawMenuText(int x, int y, const std::string& text, u32 color, int scale) {
    int cursor = x;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32 || ch > 126) {
            ch = '?';
        }
        drawMenuChar(cursor, y, (char)ch, color, scale);
        cursor += 4 * scale;
        if (cursor > 472) {
            break;
        }
    }
}

void PspApp::drawMenuName(int x, int y, const std::string& text, u32 color, int right) {
    std::vector<u32> chars = decodeMenuName(text);
    int total = 0;
    for (size_t i = 0; i < chars.size(); ++i) total += menuGlyph(chars[i])->width;
    bool truncate = total > right - x;
    for (size_t i = 0; i < chars.size(); ++i) {
        const MenuGlyph* glyph = menuGlyph(chars[i]);
        if (x + glyph->width > right - (truncate ? 24 : 0)) {
            for (int dot = 0; dot < 3 && x + 8 <= right; ++dot, x += 8)
                drawMenuRect(x + 3, y + 13, 2, 2, color);
            break;
        }
        for (int row = 0; row < 16; ++row) {
            for (int col = 0; col < glyph->width; ++col) {
                if (glyph->rows[row * 2 + col / 8] & (0x80 >> (col % 8)))
                    drawMenuRect(x + col, y + row, 1, 1, color);
            }
        }
        x += glyph->width;
    }
}

void PspApp::drawGameOverlays(u32* drawBase,unsigned fpsWidth,unsigned fpsHeight,bool notice) {
    const unsigned fpsStride=(fpsWidth+15u)&~15u;
    const size_t fpsPixels=(size_t)fpsStride*fpsHeight;
    const size_t count=fpsPixels+(notice?480u*24u:0u);
    if(!count)return;
    // A normal FPS panel uploads only 1.6--2.5 KiB. Pack an optional screenshot
    // notice after it so both overlays share one cache writeback and GE sync.
    if(overlayPixels.capacity()<count+3)overlayPixels.reserve(count+3);
    overlayPixels.resize(count+3);
    u32* upload=reinterpret_cast<u32*>((reinterpret_cast<uintptr_t>(overlayPixels.data())+15u)&~uintptr_t(15u));
    for(unsigned y=0;y<fpsHeight;++y)for(unsigned x=0;x<fpsWidth;++x) {
        const u32 c=menuFrame[y*480+x];
        upload[y*fpsStride+x]=(c&0xff00ff00u)|((c&255u)<<16)|((c>>16)&255u);
    }
    if(notice)for(unsigned y=0;y<24;++y)for(unsigned x=0;x<480;++x) {
        const u32 c=menuFrame[(y+248)*480+x];
        upload[fpsPixels+y*480+x]=(c&0xff00ff00u)|((c&255u)<<16)|((c>>16)&255u);
    }
    sceKernelDcacheWritebackRange(upload,count*sizeof(u32));
    // gamePresentation.draw() already invalidated this destination. Nothing
    // reads or writes CPU VRAM between that draw and these GPU-only overlays.
    sceGuStart(GU_DIRECT,displayList);
    if(fpsPixels)sceGuCopyImage(GU_PSM_8888,0,0,fpsWidth,fpsHeight,fpsStride,upload,0,0,512,drawBase);
    if(notice)sceGuCopyImage(GU_PSM_8888,0,0,480,24,480,upload+fpsPixels,0,248,512,drawBase);
    sceGuTexSync();sceGuFinish();sceGuSync(0,0);
}

void PspApp::presentFrame(const std::vector<u32>& pixels) {
    gameBackgroundDirty[0] = gameBackgroundDirty[1] = true;
    // Submit UI pixels through the GE too. Direct CPU stores to VRAM can be
    // hidden by PPSSPP's cached framebuffer after textured game/video draws.
    // A separate upload preserves the retained game image for screenshots.
    if (!menuPresentation.prepare(pixels, 480, 272, ScaleOriginal, false)) return;
    menuPresentation.draw(drawBuffer, vramPointer(drawBuffer), displayList);
    sceDisplayWaitVblankStart();
    drawBuffer = sceGuSwapBuffers();
}

void PspApp::queueAudioSamples(const std::vector<s16>& samples, u32 inputRate) {
    if (samples.empty() || inputRate == 0 || audioMutex < 0 || audioChannel < 0) {
        return;
    }

    size_t inputFrames = samples.size() / 2;
    if (inputFrames == 0) {
        return;
    }
    sceKernelWaitSema(audioMutex, 1, 0);

    // Game audio uses one of these rates.  They divide 44100 exactly, so
    audioPrimeBlocks = emulator.videoPlayer ? 3 : 2;
    // nearest-neighbour resampling is just sample duplication; keeping this
    // path free of 64-bit division matters on the Allegrex CPU.
    if (inputRate == 11025 || inputRate == 22050 || inputRate == 44100) {
        const size_t repeat = 44100 / inputRate;
        for (size_t srcFrame = 0; srcFrame < inputFrames; ++srcFrame) {
            const s16 left = samples[srcFrame * 2];
            const s16 right = samples[srcFrame * 2 + 1];
            const s16 pair[2] = { left, right };
            for (size_t r = 0; r < repeat; ++r) {
                for (size_t channel = 0; channel < 2; ++channel) {
                    if (audioQueuedSamples == audioRing.size()) {
                        ++audioReadPos;
                        if (audioReadPos == audioRing.size()) {
                            audioReadPos = 0;
                        }
                        --audioQueuedSamples;
                        ++audioDropped;
                    }
                    audioRing[audioWritePos] = pair[channel];
                    ++audioWritePos;
                    if (audioWritePos == audioRing.size()) {
                        audioWritePos = 0;
                    }
                    ++audioQueuedSamples;
                }
            }
        }
        sceKernelSignalSema(audioMutex, 1);
        return;
    }

    size_t outputFrames = (size_t)(((u64)inputFrames * 44100 + inputRate - 1) / inputRate);
    for (size_t frame = 0; frame < outputFrames; ++frame) {
        size_t srcFrame = (size_t)(((u64)frame * inputRate) / 44100);
        if (srcFrame >= inputFrames) {
            srcFrame = inputFrames - 1;
        }
        s16 pair[2] = { samples[srcFrame * 2], samples[srcFrame * 2 + 1] };
        for (size_t channel = 0; channel < 2; ++channel) {
            if (audioQueuedSamples == audioRing.size()) {
                // The ring size is a power of two, but an explicit wrap is
                // cheaper than a software modulo on the Allegrex CPU.
                ++audioReadPos;
                if (audioReadPos == audioRing.size()) {
                    audioReadPos = 0;
                }
                --audioQueuedSamples;
                ++audioDropped;
            }
            audioRing[audioWritePos] = pair[channel];
            ++audioWritePos;
            if (audioWritePos == audioRing.size()) {
                audioWritePos = 0;
            }
            ++audioQueuedSamples;
        }
    }
    sceKernelSignalSema(audioMutex, 1);
}

int PspApp::audioThreadEntry(unsigned int, void* argp) {
    PspApp* app = *static_cast<PspApp**>(argp);
    return app->audioThreadLoop();
}

int PspApp::audioThreadLoop() {
    // Alternate buffers: never rewrite PCM that the asynchronous device may
    // still own. Blocking output paces this thread, not the game/render loop.
    static s16 __attribute__((aligned(64))) chunks[2][kAudioBlockFrames * 2];
    const size_t blockSamples = kAudioBlockFrames * 2;
    unsigned slot = 0;
    u32 generation = 0;
    bool primed = false;
    bool starved = false;
    for (;;) {
        sceKernelWaitSema(audioMutex, 1, 0);
        if (!audioThreadRunning) {
            sceKernelSignalSema(audioMutex, 1);
            break;
        }
        if (generation != audioGeneration) {
            generation = audioGeneration;
            primed = false;
            starved = false;
        }
        const size_t required = primed ? blockSamples : blockSamples * audioPrimeBlocks;
        if (audioQueuedSamples < required) {
            if (primed && !starved && sceAudioGetChannelRestLen(audioChannel) == 0) {
                ++audioUnderruns;
                starved = true;
                primed = false; // Rebuild a cushion instead of repeated one-block restarts.
            }
            sceKernelSignalSema(audioMutex, 1);
            sceKernelDelayThread(1000);
            continue;
        }
        primed = true;
        starved = false;
        s16* chunk = chunks[slot];
        size_t first = std::min(blockSamples, audioRing.size() - audioReadPos);
        memcpy(chunk, &audioRing[audioReadPos], first * sizeof(s16));
        if (first < blockSamples)
            memcpy(chunk + first, &audioRing[0], (blockSamples - first) * sizeof(s16));
        audioReadPos = (audioReadPos + blockSamples) % audioRing.size();
        audioQueuedSamples -= blockSamples;
        sceKernelSignalSema(audioMutex, 1);

        sceKernelDcacheWritebackRange(chunk, blockSamples * sizeof(s16));
        int result = sceAudioOutputPannedBlocking(audioChannel, PSP_AUDIO_VOLUME_MAX,
                                                 PSP_AUDIO_VOLUME_MAX, chunk);
        if (result < 0) {
            sceKernelWaitSema(audioMutex, 1, 0);
            ++audioErrors;
            sceKernelSignalSema(audioMutex, 1);
            // A rejected enqueue does not transfer ownership or guarantee
            // that the previous block finished. Drain it before reusing slots.
            while (sceAudioGetChannelRestLen(audioChannel) > 0) sceKernelDelayThread(1000);
            sceKernelDelayThread(1000);
        }
        slot ^= 1u;
    }
    return 0;
}

void PspApp::recordGameProfile(u32 micros, u32 at) {
    ++gameSpikes.samples;
    if (micros >= 50000) ++gameSpikes.over50ms;
    if (gameSpikes.samples != 1 && micros <= gameSpikes.peakMicros) return;
    gameSpikes.peakMicros = micros;
    gameSpikes.peakAt = at;
    gameSpikes.slowest = emulator.lastGameProfile;
    // Keep only a bounded basename; no allocation or per-tick file writes.
    const size_t slash = emulator.filename.find_last_of("/\\");
    const char* name = emulator.filename.c_str() + (slash == std::string::npos ? 0 : slash + 1);
    size_t i = 0;
    for (; i + 1 < sizeof(gameSpikes.scene) && name[i]; ++i)
        gameSpikes.scene[i] = name[i] == '\n' || name[i] == '\r' ? '?' : name[i];
    gameSpikes.scene[i] = 0;
}

void PspApp::outputAudio() {
    // Snapshot diagnostics infrequently. File I/O never runs in the worker or
    // while the audio queue is locked.
    u32 now = sceKernelGetSystemTimeLow();
    if (audioMutex < 0 || now - audioLogTick < 5000000u) return;
    audioLogTick = now;
    sceKernelWaitSema(audioMutex, 1, 0);
    unsigned long underruns = audioUnderruns;
    unsigned long dropped = audioDropped;
    unsigned long errors = audioErrors;
    size_t queued = audioQueuedSamples;
    sceKernelSignalSema(audioMutex, 1);
    unsigned heapUsed=0,heapFree=0,heapArena=0;
#ifdef PSP
    struct mallinfo heap=mallinfo();
    heapUsed=heap.uordblks/1024;heapFree=heap.fordblks/1024;heapArena=heap.arena/1024;
#endif
    // All detailed peaks share this one append/open/close per snapshot.
    const u32 logBegin=sceKernelGetSystemTimeLow();
    const unsigned renderPeriod=settings.frameSkip==SkipAuto && !emulator.isCutsceneActive()?
        renderCadence.period():(settings.frameSkip==SkipOne?2u:1u);
    const GameTickProfile& spike = gameSpikes.slowest;
    pspLog("av: core_us=%u queue_ms=%u underruns=%lu dropped=%lu errors=%lu late_ticks=%u skip=%s render_period=%u transfer=%s fps=%u\n"
           "timing: logic_us=%u/%u mix_us=%u/%u copy_us=%u/%u wait_us=%u/%u log_us=%u/%u (avg/max)\n"
           "present: prepare_us=%u/%u transfer_us=%u/%u mode=%s filter=%s surface_kb=%u\n"
           "resources: image_kb=%u images=%u evictions=%u sprites=%u vars=%u image_limit_kb=%u file_kb=%u\n"
           "render: draw_us=%u/%u image_decodes=%u decode_total_us=%llu decode_peak_us=%u\n"
           "game_peak: samples=%u over50ms=%u scene=%s at_us=%u tick=%llu frame=%u input=%u rendered=%u total_us=%u timeline_us=%u movies_us=%u buttons_us=%u pending_us=%u cheats_us=%u draw_us=%u\n"
           "game_vm: total_us=%llu calls=%u instructions=%u depth=%u slow_us=%u action=%u target=%s\n"
           "game_sound: total_us=%llu calls=%u slow_us=%u value=%u format=%d bytes=%u\n"
           "audio: retained_kb=%u\nheap: used_kb=%u free_kb=%u arena_kb=%u",
           (unsigned)lastCoreMicros,(unsigned)(queued*1000/88200),underruns,dropped,errors,
           (unsigned)coreClock.droppedTicks,frameSkipName(settings.frameSkip),
           renderPeriod,lastTransferGe?"GE":"CPU",(unsigned)displayFps.value,
           logicTiming.average(),logicTiming.peak,mixTiming.average(),mixTiming.peak,
           copyTiming.average(),copyTiming.peak,waitTiming.average(),waitTiming.peak,
           logTiming.average(),logTiming.peak,
           prepareTiming.average(),prepareTiming.peak,transferTiming.average(),transferTiming.peak,
           gamePresentation.usesTexture()?"GU-texture":"GE-copy",settings.smoothing?"smooth":"sharp",
           (unsigned)(gamePresentation.retainedBytes()/1024),
           (unsigned)(emulator.reader.imageCacheBytes()/1024),(unsigned)emulator.reader.imageCacheCount(),
           (unsigned)emulator.reader.imageCacheEvictions(),(unsigned)emulator.sprites.sprites.size(),
           (unsigned)emulator.vm.vars.size(),(unsigned)(emulator.reader.imageCacheBudgetBytes()/1024),
           (unsigned)(emulator.reader.data.capacity()/1024),drawTiming.average(),drawTiming.peak,
           (unsigned)emulator.reader.imageDecodeCount(),(unsigned long long)emulator.reader.imageDecodeTotalMicros(),
           (unsigned)emulator.reader.imageDecodePeakMicros(),
           gameSpikes.samples,gameSpikes.over50ms,gameSpikes.scene,gameSpikes.peakAt,
           (unsigned long long)spike.tick,spike.frame,spike.inputMask,spike.rendered?1u:0u,gameSpikes.peakMicros,
           spike.timelineMicros,spike.movieMicros,spike.buttonMicros,spike.pendingMicros,spike.cheatMicros,spike.drawMicros,
           (unsigned long long)spike.vm.totalMicros,spike.vm.calls,spike.vm.instructions,spike.vm.maxDepth,
           spike.vm.maxMicros,spike.vm.slowAction,spike.vm.slowTarget,
           (unsigned long long)spike.sound.totalMicros,spike.sound.calls,spike.sound.maxMicros,
           (unsigned)spike.sound.slowSoundValue,(int)spike.sound.slowFormat,spike.sound.slowBytes,
           (unsigned)(emulator.audio.retainedAudioBytes()/1024),
           heapUsed,heapFree,heapArena);
    logicTiming.reset();mixTiming.reset();copyTiming.reset();prepareTiming.reset();transferTiming.reset();waitTiming.reset();logTiming.reset();
    drawTiming.reset();
    gameSpikes = GameSpikeWindow();
    logTiming.add(sceKernelGetSystemTimeLow()-logBegin);
}

void PspApp::loadingCallback(void* context,const char* stage,const std::string& path,size_t done,size_t total) {
    PspApp* app=static_cast<PspApp*>(context);
    const u32 now=sceKernelGetSystemTimeLow();
    bool changed=app->loadingStage!=stage;
    if(app->loadingShown && !changed && now-app->loadingTick<100000u)return;
    if(!app->loadingShown) {
        // Snapshot the visible VRAM page, not the back buffer or partially
        // initialized new scene. Reuse menuFrame, avoiding another full canvas.
        const u32* visible=vramPointer(app->drawBuffer==(void*)0?(void*)0x88000:(void*)0);
        for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x) {
            const u32 c=visible[y*512+x];
            app->menuFrame[y*480+x]=(c&0xff00ff00u)|((c&255)<<16)|((c>>16)&255);
        }
        app->loadingPanel.resize(320*44);
        for(unsigned y=0;y<44;++y)for(unsigned x=0;x<320;++x)
            app->loadingPanel[y*320+x]=blendPanel(app->menuFrame[(216+y)*480+80+x],app->uiColor(0xff101319),70);
        app->clearAudioQueue();app->gamePresentation.release();
    }
    app->loadingShown=true;app->loadingStage=stage;app->loadingTick=now;
    (void)path;
    for(unsigned y=0;y<44;++y)
        std::copy(app->loadingPanel.begin()+y*320,app->loadingPanel.begin()+(y+1)*320,
                  app->menuFrame.begin()+(216+y)*480+80);
    app->drawMenuRect(80,216,3,44,app->accent());
    app->drawMenuName(92,222,app->tr(stage),app->uiColor(0xffedf3f6),346);
    app->drawMenuRect(92,249,296,4,app->selectionColor());
    if(total) {
        unsigned percent=(unsigned)((u64)std::min(done,total)*100/total);
        app->drawMenuRect(92,249,(int)(296*percent/100),4,app->accent());
        char text[32];snprintf(text,sizeof(text),"%u%%",percent);
        app->drawMenuName(352,222,text,app->accent(),394);
    } else {
        unsigned offset=(app->loadingAnimation++*19)%232;
        app->drawMenuRect(92+offset,249,64,4,app->accent());
    }
    app->presentFrame(app->menuFrame);
}

void PspApp::drawScanProgress(const std::string& dirName, unsigned int found) {
    std::fill(menuFrame.begin(), menuFrame.end(), uiColor(0xff101319));
    drawMenuRect(0, 0, 480, 72, surfaceColor());
    drawMenuRect(24, 71, 432, 1, selectionColor());
    drawLogo(24, 8);
    drawMenuText(178, 18, "NATIVE32PSP", uiColor(0xffffffff), 3);
    drawMenuName(178, 44, tr("Scanning"), uiColor(0xffc8d0dc), 472);

    // Indeterminate bar: the total file count is unknown until the walk ends,
    // so animate a moving block rather than fake a percentage.
    int barX = 60;
    int barY = 140;
    int barW = 360;
    int barH = 4;
    drawMenuRect(barX, barY, barW, barH, selectionColor());
    int blockW = 60;
    int span = barW - blockW;
    int phase = (int)((scanAnimation * 6) % (unsigned int)(span * 2));
    int offset = phase <= span ? phase : span * 2 - phase;
    drawMenuRect(barX + offset, barY, blockW, barH, accent());
    ++scanAnimation;

    char line[64];
    snprintf(line, sizeof(line), "%s %u", tr("Games"), found);
    drawMenuName(barX, barY + 30, line, uiColor(0xffffffff), 448);
    drawMenuName(barX, barY + 46, dirName, uiColor(0xffa8b0bc), 448);

    presentFrame(menuFrame);
}

std::string PspApp::settingsPath() const {
    return "ms0:/PSP/GAME/Native32PSP/settings.txt";
}

void PspApp::adjustSetting(int row, int delta) {
    switch(row) {
    case 0: settings.scaling=(VideoScaling)(((int)settings.scaling+delta+3)%3); gamePresentation.clear(); break;
    case 9: settings.smoothing=!settings.smoothing; gamePresentation.clear(); break;
    case 1: settings.frameSkip=(FrameSkip)(((int)settings.frameSkip+delta+3)%3); renderCadence.reset(); break;
    case 2: settings.volume=(u32)std::max(0,std::min(100,(int)settings.volume+delta*10)); emulator.audio.setVolume(settings.volume); break;
    case 3: settings.showFps=!settings.showFps; displayFps.reset(); gameBackgroundDirty[0]=gameBackgroundDirty[1]=true; break;
    case 4: language.cycle(delta); settings.language=language.id(); break;
    case 8: settings.lightAppearance=!settings.lightAppearance;break;
    case 5: settings.theme=(settings.theme+delta+5)%5; break;
    default: break;
    }
}

void PspApp::drawAbout() {
    std::fill(menuFrame.begin(),menuFrame.end(),uiColor(0xff101319));
    drawMenuRect(24,18,3,22,accent());
    drawMenuName(38,20,tr("About"),uiColor(0xffedf3f6),460);
    drawMenuName(38,60,"Native32PSP",uiColor(0xffedf3f6),460);
    drawMenuName(38,84,buildVersion(),uiColor(0xff8e9aaa),460);
    drawMenuName(38,118,std::string(tr("Author"))+": pikawz",accent(),460);
    drawMenuName(38,142,"https://github.com/wxwxwz/",uiColor(0xffcad3de),472);
    drawMenuName(38,180,tr("Acknowledgements"),accent(),460);
    drawMenuName(38,204,"https://github.com/AloysHF/Native32Emu",uiColor(0xffcad3de),472);
    drawMenuRect(28,234,424,1,selectionColor());
    drawMenuName(28,244,std::string("× ")+tr("Back"),uiColor(0xff8e9aaa),472);
    presentFrame(menuFrame);
}

void PspApp::drawSettingsFrame() {
    if(aboutMode) {drawAbout();return;}
    if(systemInfoMode) {drawSystemInfo();return;}
    ++frameCounter;
    std::fill(menuFrame.begin(),menuFrame.end(),uiColor(0xff101319));
    drawMenuRect(24,18,3,22,accent());
    drawMenuName(38,20,tr("Settings"),uiColor(0xffedf3f6),230);
    drawMenuName(250,20,buildVersion(),uiColor(0xff8e9aaa),472);
    const char* labels[]={"Scaling","Frame skip","Volume","Show FPS","Language","Theme color","System info","About","Appearance","Filtering"};
    char volume[16];snprintf(volume,sizeof(volume),"%u",(unsigned)settings.volume);
    const char* values[]={tr(scalingName(settings.scaling)),tr(frameSkipName(settings.frameSkip)),volume,
        tr(settings.showFps?"On":"Off"),language.name(),tr(themeName(settings.theme)),tr("Open"),tr("Open"),tr(settings.lightAppearance?"Light":"Dark"),tr(settings.smoothing?"Smooth":"Sharp")};
    const char* categories[]={"Display","Sound","Interface","System"};
    drawMenuRect(20,48,96,182,surfaceColor());
    drawMenuRect(128,48,1,182,selectionColor());
    for(unsigned i=0;i<4;++i) {
        int y=62+i*38;bool selected=i==settingsCategory;
        if(selected) {
            drawMenuRect(20,y-6,96,30,selectionColor());
            if(!settingsDetail)drawMenuRect(20,y-6,3,30,accent());
        }
        drawMenuName(32,y,tr(categories[i]),selected?accent():uiColor(0xffcad3de),112);
    }
    drawMenuName(148,54,tr(categories[settingsCategory]),uiColor(0xff8e9aaa),452);
    for(unsigned i=0;i<kSettingsCounts[settingsCategory];++i) {
        unsigned row=kSettingsRows[settingsCategory][i];
        const bool compact=kSettingsCounts[settingsCategory]>3;
        const int valueY=compact?18:24;
        int y=(compact?76:82)+i*(compact?38:48);bool focused=settingsDetail && row==settingsIndex;
        drawMenuRect(144,y,312,compact?35:43,focused?selectionColor():surfaceColor());
        if(focused)drawMenuRect(144,y,3,compact?35:43,accent());
        drawMenuName(156,y+((row==6 || row==7)?14:(compact?1:4)),tr(labels[row]),focused?accent():uiColor(0xffedf3f6),444);
        if(focused && (row<6 || row==8 || row==9)) {
            drawMenuName(152,y+valueY,"←",accent(),170);
            drawMenuName(436,y+valueY,"→",accent(),454);
        }
        if(row!=6 && row!=7)drawMenuName(176,y+valueY,values[row],uiColor(0xffcad3de),row==5?336:428);
        if(row==5)for(int c=0;c<5;++c) {
            int x=342+c*18;
            if(c==settings.theme)drawMenuRect(x-1,y+24,14,14,uiColor(0xffedf3f6));
            drawMenuRect(x,y+25,12,12,themeAccent(c));
        }
        if(row==2) {
            drawMenuRect(222,y+30,200,4,selectionColor());
            drawMenuRect(222,y+30,(int)(200*settings.volume/100),4,accent());
        }
    }
    drawMenuRect(20,234,440,1,selectionColor());
    drawMenuName(24,244,(settingsDetail ? std::string((settingsIndex==6 || settingsIndex==7)?"○ ":"← → ○ ")+tr((settingsIndex==6 || settingsIndex==7)?"Open":"Change") : std::string("○ ")+tr("Open"))+"   × "+tr("Back"),uiColor(0xff8e9aaa),472);
    presentFrame(menuFrame);
}

void PspApp::drawMenuFrame() {
    ++frameCounter;
    std::fill(menuFrame.begin(), menuFrame.end(), uiColor(0xff101319));
    drawMenuRect(0, 0, 480, 72, surfaceColor());
    drawMenuRect(24, 71, 432, 1, selectionColor());
    drawLogo(24, 8);
    drawMenuText(178, 18, "NATIVE32PSP", uiColor(0xffffffff), 3);
    drawMenuRect(178,38,32,2,accent());
    drawMenuName(178, 52, (std::string("○/START ")+tr("Play")+"   △ "+tr("Settings")), uiColor(0xffc8d0dc), 472);

    int listTop = 84;
    int rowH = 24;
    unsigned int visible = 6;
    unsigned int start = 0;
    if (selectedIndex >= visible) {
        start = selectedIndex - visible + 1;
    }

    if (gamePaths.empty()) {
        drawMenuRect(40, 112, 400, 40, surfaceColor());
        drawMenuName(62, 126, tr("No game files"), uiColor(0xffffffff), 450);
    } else {
        for (unsigned int i = 0; i < visible && start + i < gamePaths.size(); ++i) {
            int y = listTop + (int)i * rowH;
            bool selected = (start + i) == selectedIndex;
            drawMenuRect(44, y, 392, rowH - 3, selected ? selectionColor() : uiColor(0xff101319));
            if(selected) drawMenuRect(44,y,3,rowH-3,accent());
            std::string name = menuFileName(gamePaths[start + i]);
            drawMenuName(60, y + 2, name, selected ? accent() : uiColor(0xffd8dee8), 428);
        }
    }
    char countLine[128];
    if (statusLine == "load failed")
        snprintf(countLine, sizeof(countLine), "%s", tr("Load failed - see log"));
    else
        snprintf(countLine, sizeof(countLine), "%u/%u", gamePaths.empty()?0:selectedIndex+1, (unsigned)gamePaths.size());
    drawMenuRect(24,234,432,1,selectionColor());
    drawMenuName(24, 244, countLine, uiColor(0xffc8d0dc), 255);
    drawMenuName(258, 244, (std::string("SELECT ")+tr("Menu")+"   PS "+tr("Exit")), uiColor(0xffc8d0dc), 472);

    presentFrame(menuFrame);
}

static void scanTickThunk(void* context, const std::string& dirPath, unsigned int found) {
    PspApp* app = (PspApp*)context;
    if (app) {
        app->drawScanProgress(dirPath, found);
    }
}

void PspApp::scanGames() {
    statusLine = "menu";
    pspLog("scanGames: scanning");
    const char* dirs[] = {
        "ms0:/PSP/GAME/Native32PSP/games",
#ifndef PSP
        "ms0:/PSP/GAME/Native32PSP",
        "games",
        ".",
#endif
    };

    gamePaths.clear();
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        size_t before = gamePaths.size();
        pspLog("scanGames: begin dir %s", dirs[i]);
        drawScanProgress(dirs[i], (unsigned int)gamePaths.size());
        collectGamesInDir(dirs[i], &gamePaths, &scanTickThunk, this);
        pspLog("scanGames: dir %s added=%u", dirs[i], (unsigned)(gamePaths.size() - before));
    }
    std::sort(gamePaths.begin(), gamePaths.end());
    gamePaths.erase(std::unique(gamePaths.begin(), gamePaths.end()), gamePaths.end());
    selectedIndex = 0;
    gameLoaded = false;
    menuMode = true;
    loadedPath.clear();
    pspLog("scanGames: total=%u", (unsigned)gamePaths.size());
}

void PspApp::loadSelectedGame() {
    if (gamePaths.empty() || selectedIndex >= gamePaths.size()) {
        return;
    }
    const std::string& candidate = gamePaths[selectedIndex];
    gameSpikes = GameSpikeWindow();
    pspLog("loadSelectedGame: load begin %s", candidate.c_str());
    clearAudioQueue();
    emulator.loadProgress.callback=&PspApp::loadingCallback;
    emulator.loadProgress.context=this;
    if (emulator.loadFromPath(candidate, settings.volume)) {
        loadedPath = candidate;
        pauseMode = cheatMode = false;
        emulator.cheats.clear();
        std::string cheatMessage;
        loadCheatFile(&emulator.cheats, loadedPath + ".cheats", &cheatMessage);
        gameLoaded = true;
        menuMode = false;
        clearColor = 0xff000000;
        statusLine = "loaded";
        frameCounter = 0;
        coreClock.reset(); displayFps.reset();
        coreTickCounter = 0;
        renderCadence.reset();
        previousButtons = 0;
        gamePresentation.release();
        clearAudioQueue();
        pspLog("loadSelectedGame: load ok size=%ux%u fb=%u",
               (unsigned)emulator.gameWidth(),
               (unsigned)emulator.gameHeight(),
               (unsigned)emulator.framebuffer().size());
    } else {
        releaseGame();
        statusLine = "load failed";
        clearAudioQueue();
        pspLog("loadSelectedGame: load failed %s", candidate.c_str());
    }
}

int PspApp::run() {
    pspLog("run: begin version=%s", buildVersion());
    if (settings.load(settingsPath())) {
        pspLog("run: settings loaded (decoder=%s volume=%u)",
               decoderName(settings.decoder), (unsigned)settings.volume);
    }
    std::string languageDir=settingsPath();
    languageDir=languageDir.substr(0,languageDir.find_last_of("/\\")+1)+"languages";
    language.load(languageDir,settings.language);
    settings.language=language.id();
    int originalCpu = scePowerGetCpuClockFrequencyInt();
    int originalBus = scePowerGetBusClockFrequencyInt();
    int clockResult = scePowerSetClockFrequency(333, 333, 166);
    pspLog("clock: request 333/166 result=%d actual=%d/%d", clockResult,
           scePowerGetCpuClockFrequencyInt(), scePowerGetBusClockFrequencyInt());
    initGu();
    initAudio();
    pspLog("run: after initGu");
    emulator.loadProgress.callback=&PspApp::loadingCallback;
    emulator.loadProgress.context=this;
    scanGames();
    pspLog("run: entering loop status=%s games=%u", statusLine.c_str(), (unsigned)gamePaths.size());
    while (running) {
        readInput();
        drawFrame();
    }
    shutdownAudio();
    shutdownGu();
    if (originalCpu > 0 && originalBus > 0)
        scePowerSetClockFrequency(originalCpu, originalCpu, originalBus);
    pspLog("run: exit");
    sceKernelExitGame();
    return 0;
}

}
