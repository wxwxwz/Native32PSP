#ifndef NATIVE32_PSP_APP_H
#define NATIVE32_PSP_APP_H

#include "core/emulator.h"
#include "platform/psp_settings.h"
#include "platform/frame_clock.h"
#include "platform/presentation.h"
#include "platform/game_presentation.h"
#include "platform/system_info.h"
#include <string>
#include <vector>

namespace n32 {

class PspApp {
public:
    PspApp();
    int run();

    // Public so the C-style scan callback can repaint during scanGames().
    void drawScanProgress(const std::string& dirName, unsigned int found);

private:
    void initGu();
    void initAudio();
    void shutdownGu();
    void shutdownAudio();
    void drawFrame();
    void drawMenuFrame();
    void drawSettingsFrame();
    void drawSystemInfo();
    void drawAbout();
    bool aboutMode=false;
    void drawPauseMenu();
    void readPauseMenu(unsigned pressed);
    void resumeGame();
    void releaseGame();
    void takeScreenshot();
    bool captureGameFrame(std::vector<u32>* pixels);
    void browseScreenshots(int direction);
    void drawScreenshots();
    void closeScreenshots();
    void drawGameFrame();
    static void loadingCallback(void*,const char*,const std::string&,size_t,size_t);
    bool loadingShown=false;
    u32 loadingTick=0;
    unsigned loadingAnimation=0;
    std::string loadingStage;
    std::vector<u32> loadingPanel;
    void adjustSetting(int row, int delta);
    std::string settingsPath() const;
    void readInput();
    void outputAudio();
    static int audioThreadEntry(unsigned int args, void* argp);
    int audioThreadLoop();
    void clearAudioQueue();
    void queueAudioSamples(const std::vector<s16>& samples, u32 inputRate);
    void scanGames();
    void loadSelectedGame();
    void presentFrame(const std::vector<u32>& pixels);
    void drawGameOverlays(u32* drawBase, unsigned fpsWidth, unsigned fpsHeight, bool notice);
    void drawMenuRect(int x, int y, int w, int h, u32 color);
    void drawLogo(int x, int y);
    void drawMenuChar(int x, int y, char ch, u32 color, int scale);
    void drawMenuName(int x, int y, const std::string& text, u32 color, int right);
    void drawMenuText(int x, int y, const std::string& text, u32 color, int scale);

    bool running;
    unsigned int clearColor;
    bool gameLoaded;
    bool menuMode;
    bool systemInfoMode=false,systemInfoSampled=false,systemIdleValid=false;
    u32 systemInfoTick=0;
    u64 systemIdle=0;
    SystemInfo systemInfo;
    bool settingsMode;
    bool pauseMode;
    bool cheatMode;
    bool screenshotMode;
    bool screenshotInfo;
    bool screenshotDeleteConfirm=false;
    std::string screenshotError;
    void deleteCurrentScreenshot();
    unsigned screenshotNoticeTicks;
    std::string screenshotNotice;
    std::string screenshotPath;
    size_t screenshotCount;
    size_t screenshotIndex=0;
    std::vector<u32> screenshotPixels;
    unsigned pauseIndex;
    unsigned cheatIndex;
    std::string pauseStatus;
    unsigned int settingsIndex;
    unsigned int settingsCategory=0;
    bool settingsDetail=false;
    Settings settings;
    u32 accent() const {return appearanceAccent(settings.theme,settings.lightAppearance);}
    u32 uiColor(u32 color) const {return appearanceColor(color,settings.lightAppearance);}
    u32 surfaceColor() const {return blendPanel(uiColor(0xff101319),accent(),5);}
    u32 selectionColor() const {return blendPanel(uiColor(0xff101319),accent(),16);}
    Language language;
    const char* tr(const char* key) const { return language.text(key); }
    unsigned int frameCounter;
    unsigned int previousButtons;
    unsigned int suppressedButtons;
    void* drawBuffer;
    int audioChannel;
    int audioThread;
    int audioMutex;
    bool audioThreadRunning; // Protected by audioMutex.
    u32 audioGeneration;
    unsigned audioPrimeBlocks; // Protected by audioMutex.
    unsigned long audioUnderruns;
    unsigned long audioErrors;
    u32 audioLogTick;
    std::vector<s16> audioRing;
    std::vector<s16> audioSamples;
    size_t audioReadPos;
    size_t audioWritePos;
    size_t audioQueuedSamples;
    u32 audioOutputBudget;
    unsigned long audioDropped;
    unsigned long audioDroppedLogged;
    unsigned int scanAnimation;
    RenderCadence renderCadence;
    FrameClock coreClock;
    u32 coreTickCounter;
    u32 lastCoreMicros;
    bool lastTransferGe=false;
    TimingWindow logicTiming,mixTiming,copyTiming,prepareTiming,transferTiming,waitTiming,logTiming;
    TimingWindow drawTiming;
    struct GameSpikeWindow {
        unsigned samples = 0, over50ms = 0;
        u32 peakMicros = 0, peakAt = 0;
        GameTickProfile slowest;
        char scene[96] = {};
    } gameSpikes;
    void recordGameProfile(u32 micros, u32 at);
    DisplayFps displayFps;
    unsigned fpsPanelWidth[2]={0,0};
    unsigned fpsPanelHeight[2]={0,0};
    std::string loadedPath;
    std::string statusLine;
    std::vector<std::string> gamePaths;
    unsigned int selectedIndex;
    std::vector<u32> menuFrame;
    std::vector<u32> overlayPixels;
    GamePresentation menuPresentation;
    GamePresentation gamePresentation;
    bool gameBackgroundDirty[2];
    std::vector<u16> buttons;
    Emulator emulator;
};

}

#endif
