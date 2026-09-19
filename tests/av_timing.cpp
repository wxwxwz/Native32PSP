#include "core/emulator.h"
#include "core/cheat_file.h"
#include "core/screenshot_store.h"
#include <unistd.h>
#include "core/image_decoder.h"
#include "core/raster_limits.h"
#include "platform/frame_clock.h"
#include "platform/psp_settings.h"
#include "platform/menu_font.h"
// Expose only PspApp internals so these tests drive the production queue and
// draw loop; the emulator and STL headers have already been included normally.
#define private public
#include "platform/psp_app.h"
#undef private
#include "psp_test_api.h"
#include <cassert>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <zlib.h>
#include <sys/stat.h>

using namespace n32;
static std::mutex queueMutex, deviceMutex;
static std::thread worker;
static TestThreadEntry entry;
static std::atomic<u32> now(0);
static std::vector<s16> played, expectedPending;
static const s16* pending = 0;
static bool failCreate = false, failStart = false;
static bool rejectSecondOutput = false;
static std::atomic<unsigned> outputs(0);
static std::chrono::steady_clock::time_point finishAt;
namespace n32 { void pspLog(const char*, ...) {} }

int sceKernelCreateSema(const char*, unsigned, int, int, void*) { return 1; }
int sceKernelWaitSema(int, int, void*) { queueMutex.lock(); return 0; }
int sceKernelSignalSema(int, int) { queueMutex.unlock(); return 0; }
int sceKernelDeleteSema(int) { return 0; }
int sceKernelCreateThread(const char*, TestThreadEntry fn, int, int, unsigned, void*) {
    entry = fn; return failCreate ? -1 : 2;
}
int sceKernelStartThread(int, unsigned size, void* data) {
    if (failStart) return -1;
    std::vector<u8> args((u8*)data, (u8*)data + size);
    worker = std::thread([args]() mutable { entry((unsigned)args.size(), args.data()); });
    return 0;
}
int sceKernelWaitThreadEnd(int, void*) { worker.join(); return 0; }
int sceKernelDeleteThread(int) { return 0; }
int sceKernelDelayThread(unsigned us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us)); return 0;
}
u32 sceKernelGetSystemTimeLow() { return now.load(); }
int sceAudioChReserve(int, int, int) { return 0; }
int sceAudioChRelease(int) { return 0; }
static void completeDma() {
    if (!pending) return;
    // Catch rewriting an in-flight buffer, which the old single-buffer path did.
    assert(std::equal(expectedPending.begin(), expectedPending.end(), pending));
    played.insert(played.end(), pending, pending + 2048);
    pending = 0;
}
int sceAudioGetChannelRestLen(int) {
    std::lock_guard<std::mutex> lock(deviceMutex);
    if (std::chrono::steady_clock::now() >= finishAt) completeDma();
    return pending ? 1024 : 0;
}
int sceAudioOutputPannedBlocking(int, int, int, void* data) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard<std::mutex> lock(deviceMutex);
    if (rejectSecondOutput && outputs == 1) {
        rejectSecondOutput = false;
        finishAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
        return -1; // Prior DMA remains in flight after this rejected enqueue.
    }
    completeDma();
    pending = (const s16*)data;
    expectedPending.assign(pending, pending + 2048);
    finishAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
    ++outputs;
    return 1024;
}

static void waitOutputs(unsigned count) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (outputs < count && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(outputs >= count);
}
static std::vector<s16> pattern(size_t frames) {
    std::vector<s16> result(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        result[2*i] = (s16)(i % 30000);
        result[2*i+1] = (s16)-(int)(i % 30000);
    }
    return result;
}

static void testClock() {
    FrameClock clock;
    assert(clock.due(0) == 1);
    unsigned ticks = 0;
    for (unsigned i = 1; i <= 600; ++i) ticks += clock.due(i * 10000000ull / 600);
    assert(ticks == 300 && clock.droppedTicks == 0);
    clock.reset();
    assert(clock.due(0xfffffff0u) == 1);
    assert(clock.due(0xfffffff0u + 33334u) == 1);
    assert(clock.due(0xfffffff0u + 50000u) == 0);
    assert(clock.due(0xfffffff0u + 100000u) == 2);
    assert(clock.due(0xfffffff0u + 5100000u) == 2);
    assert(clock.droppedTicks == 148);
}

static void testRenderCadence() {
    RenderCadence cadence;
    unsigned longest = 0, skipped = 0;
    for (unsigned i = 0; i < 300; ++i) {
        bool render = cadence.next();
        skipped = render ? 0 : skipped + 1;
        longest = std::max(longest, skipped);
        cadence.observe(render, render ? 45000 : 5000);
        if (i > 30) assert(cadence.period() == 2);
    }
    assert(longest == 1);
    // Jitter around the old 33 ms threshold must not cause mode chatter.
    for (unsigned i = 0; i < 100; ++i) {
        bool render = cadence.next();
        cadence.observe(render, render ? (i % 4 ? 35000 : 31000) : 4000);
        assert(cadence.period() == 2);
    }
    // Cheap skipped ticks alone never signal recovery.
    for (unsigned i = 0; i < 100; ++i) cadence.observe(false, 1);
    assert(cadence.period() == 2);
    for (unsigned i = 0; i < 200; ++i) {
        bool render = cadence.next(); cadence.observe(render, 10000);
    }
    assert(cadence.period() == 1);
    for (unsigned i = 0; i < 300; ++i) {
        bool render = cadence.next();
        skipped = render ? 0 : skipped + 1;
        assert(skipped <= 2);
        cadence.observe(render, 120000);
    }
    assert(cadence.period() == 3);
    cadence.reset(); assert(cadence.period() == 1 && cadence.next());
    std::puts("PASS: render cadence overload, jitter hysteresis, recovery and bounded skips");
}

static void testFrameSkip() {
    for (int mode = 0; mode < 3; ++mode) {
        PspApp app;
        app.audioMutex = 1; app.audioChannel = 0; app.gameLoaded = true;
        app.settings.frameSkip = (FrameSkip)mode;
        app.emulator.reader.width = app.emulator.reader.height = 0;
        unsigned draws = 0;
        for (unsigned i = 0; i < 60; ++i) {
            now = i * 1000000ull / 60;
            app.emulator.renderer.buffer[0] = 0x12345678;
            app.drawGameFrame();
            if (app.emulator.renderer.buffer[0] != 0x12345678) ++draws;
        }
        assert(app.emulator.tickCount == 30);
        assert(app.audioQueuedSamples + app.audioDropped == 44100 * 2);
        assert(draws == (mode == SkipNone ? 30 : mode == SkipOne ? 15 : 30));
    }
}

static void testCatchupCadence() {
    PspApp app;
    app.audioMutex = 1; app.audioChannel = 0; app.gameLoaded = true;
    app.settings.frameSkip = SkipAuto;
    app.emulator.reader.width = app.emulator.reader.height = 0;
    app.emulator.tickCount=10; // Already playing, not a scene initialization tick.
    for (unsigned i = 0; i < 3; ++i) app.renderCadence.observe(true, 45000);
    assert(app.renderCadence.period() == 2);
    unsigned draws = 0;
    for (unsigned i = 0; i < 20; ++i) {
        now = i * 66667ull;
        app.emulator.renderer.buffer[0] = 0x12345678;
        app.drawGameFrame();
        if (app.emulator.renderer.buffer[0] != 0x12345678) ++draws;
    }
    assert(app.emulator.tickCount == 49);
    assert(draws == 10); // No phase locking to discarded catch-up frames.
    const unsigned rate = app.emulator.audioSampleRate();
    assert(app.audioQueuedSamples + app.audioDropped == (39 * rate / 30) * (44100 / rate) * 2);
    std::puts("PASS: two-tick catch-up keeps drawing and preserves every audio block");
}

static void testQueueAndThread() {
    PspApp app;
    app.initAudio();
    assert(app.audioThread >= 0);
    // A single block must not start output before the two-block prebuffer.
    auto small = pattern(1024);
    app.queueAudioSamples(small, 44100);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(outputs == 0);
    app.clearAudioQueue();
    // Start near the ring end to exercise the two-copy wrap path.
    sceKernelWaitSema(1, 1, 0);
    app.audioReadPos = app.audioWritePos = app.audioRing.size() - 100;
    sceKernelSignalSema(1, 1);
    auto input = pattern(1024 * 8);
    app.queueAudioSamples(input, 44100);
    // The display loop is idle throughout: the worker must drain independently.
    waitOutputs(8);
    app.shutdownAudio();
    assert(played == input);
    assert(app.audioMutex == -1 && app.audioThread == -1 && app.audioChannel == -1);

    played.clear(); outputs=0;
    PspApp videoAudio;
    videoAudio.emulator.videoPlayer.reset(new mpeg::VideoPlayer(std::vector<u8>()));
    videoAudio.initAudio();
    videoAudio.queueAudioSamples(pattern(2048),44100);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(outputs==0); // Video starts with three blocks, not two.
    videoAudio.queueAudioSamples(pattern(1024),44100); waitOutputs(3);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    videoAudio.queueAudioSamples(pattern(1024),44100);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(outputs==3); // After starvation, a single block must not restart.
    videoAudio.queueAudioSamples(pattern(2048),44100); waitOutputs(6);
    videoAudio.shutdownAudio();
    assert(videoAudio.audioUnderruns>=1 && played.size()==6*2048);

    played.clear(); outputs = 0; rejectSecondOutput = true;
    PspApp rejected;
    rejected.initAudio();
    rejected.queueAudioSamples(input, 44100);
    waitOutputs(7);
    rejected.shutdownAudio();
    auto afterRejection = input;
    afterRejection.erase(afterRejection.begin() + 2048, afterRejection.begin() + 4096);
    assert(played == afterRejection && rejected.audioErrors == 1);

    // Overflow retains newest samples in stereo order.
    PspApp overflow;
    overflow.audioMutex = 1; overflow.audioChannel = 0;
    auto large = pattern(40000);
    overflow.queueAudioSamples(large, 44100);
    assert(overflow.audioDropped == large.size() - overflow.audioRing.size());
    for (size_t i = 0; i < overflow.audioRing.size(); ++i)
        assert(overflow.audioRing[(overflow.audioReadPos + i) % overflow.audioRing.size()] ==
               large[large.size() - overflow.audioRing.size() + i]);
    overflow.clearAudioQueue();
    assert(overflow.audioQueuedSamples == 0);
    for (u32 rate : {11025u, 22050u, 44100u, 48000u}) {
        overflow.clearAudioQueue();
        auto samples = pattern(111);
        overflow.queueAudioSamples(samples, rate);
        size_t frames = (111ull * 44100 + rate - 1) / rate;
        assert(overflow.audioQueuedSamples == frames * 2);
        for (size_t i = 0; i < frames; ++i) for (size_t ch = 0; ch < 2; ++ch)
            assert(overflow.audioRing[i * 2 + ch] == samples[(i * rate / 44100) * 2 + ch]);
    }
    for (int failure = 0; failure < 2; ++failure) {
        failCreate = failure == 0; failStart = failure == 1;
        PspApp failed;
        failed.initAudio();
        assert(failed.audioThread == -1 && failed.audioChannel == -1 && failed.audioMutex == -1);
        failed.shutdownAudio();
    }
    failCreate = failStart = false;
}

static void testBlackBackground() {
    u32* vram = (u32*)sceGeEdramGetAddr();
    const size_t page = 512 * 272;
    std::fill(vram, vram + page * 2, 0xffbada55u); // Recognisable old menu pixels.
    testDrawOffset() = 0;
    PspApp app;
    app.gameLoaded = true;
    now = 0;
    app.coreClock.due(0); // This test controls the framebuffer directly.
    for (int sizeChange = 0; sizeChange < 2; ++sizeChange) {
        u32 w = sizeChange ? 160 : 320, h = sizeChange ? 120 : 240;
        app.emulator.reader.width = w;
        app.emulator.reader.height = h;
        app.emulator.renderer.resize(w, h);
        std::fill(app.emulator.renderer.buffer.begin(), app.emulator.renderer.buffer.end(), 0xff123456u);
        for (int pass = 0; pass < 2; ++pass) {
            unsigned slot = app.drawBuffer ? 1 : 0;
            app.pspFrame.clear(); // Force a newly supplied game frame.
            app.drawGameFrame();
            u32* screen = vram + slot * page;
            for (u32 y = 0; y < 272; ++y) for (u32 x = 0; x < 512; ++x) {
                bool inside = x >= (480-w)/2 && x < (480+w)/2 &&
                              y >= (272-h)/2 && y < (272+h)/2;
                assert(screen[y*512+x] == (inside ? 0xff563412u : 0xff000000u));
            }
        }
    }
    unsigned menuSlot = app.drawBuffer ? 1 : 0;
    app.presentFrame(std::vector<u32>(480*272, 0xffaabbcc));
    assert(vram[menuSlot * page] == 0xffccbbaau);
    assert(app.gameBackgroundDirty[0] && app.gameBackgroundDirty[1]);
    app.pspFrame.clear();
    unsigned slot = app.drawBuffer ? 1 : 0;
    app.drawGameFrame();
    assert(vram[slot * page] == 0xff000000u);
    // Corrupt dimensions must return to the menu, not read beyond framebuffer.
    app.emulator.reader.width = 0xffffffffu;
    app.emulator.reader.height = 240;
    app.drawGameFrame();
    assert(!app.gameLoaded && app.menuMode && app.statusLine == "load failed");
    assert(app.emulator.reader.data.capacity()==0 && app.emulator.renderer.buffer.capacity()==0 && app.pspFrame.capacity()==0);
}

static void put32(std::vector<u8>& data, size_t offset, u32 value) {
    for (size_t i = 0; i < 4; ++i) data[offset+i] = (u8)(value >> (i*8));
}
static void testResourceBounds() {
    Native32Reader reader;
    std::vector<u8> bytes(64, 0);
    const u8* original = bytes.data();
    reader.setData(std::move(bytes));
    assert(reader.data.data() == original); // Whole-file ownership transfers.
    ActionEntry action;
    assert(reader.getAction(1, &action) && action.action == ActionEnd);
    assert(!reader.getAction(0xffffffffu, &action));
    assert(!reader.getActionCached(0xffffffffu, &action));
    assert(!reader.getAction(9, &action));
    std::vector<FrameObject> frame;
    std::vector<MovieFrame> movie;
    std::vector<ButtonEvent> buttons;
    SoundData sound;
    reader.base = 0xfffffff0u;
    assert(!reader.getAction(1, &action));
    assert(!reader.getFrame(0xffffffffu, &frame));
    reader.getMovie(0xffffffffu, &movie); assert(movie.empty());
    reader.getButtonEvents(0xffffffffu, &buttons); assert(buttons.empty());
    assert(!reader.getImageRef(0xffffffffu));
    reader.soundTable = 0xfffffff0u;
    assert(!reader.getSound(0xffffffffu, &sound));
    reader.setData(std::vector<u8>(64, 0));
    // A valid small ARGB image, then a wrapped payload length.
    reader.colorspace = ColorspaceArgb;
    put32(reader.data, 0, 8);
    put32(reader.data, 8, 0x00020002u); // 2x2
    put32(reader.data, 12, 4);
    reader.data[16] = 4; reader.data[17] = 0xc0;
    reader.data[18] = 0xff; reader.data[19] = 0xff;
    const RgbaImage* image = reader.getImageRef(1);
    assert(image && image->pixels.size() == 4);
    assert(reader.getImageRef(1) == image);
    RgbaImage copy;
    assert(reader.getImage(1, &copy) && copy.pixels == image->pixels);
    auto malformed = reader.data;
    put32(malformed, 12, 0xffffffffu);
    reader.setData(std::move(malformed));
    assert(!reader.getImageRef(1));

    u32 w = 0, h = 0;
    assert(parseResolution("Resolution_320_240", &w, &h) && w == 320 && h == 240);
    assert(!parseResolution("Resolution_4294967295_4294967295", &w, &h));
    assert(!parseResolution("Resolution_0_240", &w, &h));
    std::vector<u8> huge(8, 0);
    put32(huge, 0, 0xffffffffu);
    assert(!decodeImageArgb(huge, &copy) && !decodeImageYuv(huge, &copy));
    put32(huge, 0, 0x00020002u);
    put32(huge, 4, 0xffffffffu);
    assert(!decodeImageArgb(huge, &copy) && !decodeImageYuv(huge, &copy));
    Renderer renderer(16, 16);
    copy.width = copy.height = 2; copy.pixels.assign(4, 0xffffffffu);
    for (s32 coordinate : {s32(0x80000000u), s32(0x7fffffffu)}) {
        renderer.blitImage(copy, coordinate, 0);
        renderer.blitImage(copy, 0, coordinate);
    }
    assert(renderer.buffer == std::vector<u32>(256, 0xff000000u));
}

static void press(PspApp& app, u32 button) {
    testButtons() = 0; app.readInput();
    testButtons() = button; app.readInput();
}

static void testControls() {
    PspApp app;
    app.menuMode = false; app.gameLoaded = true;
    press(app, PSP_CTRL_CIRCLE);
    assert(app.buttons == std::vector<u16>(1, KeyA) && !app.menuMode && app.running);
    press(app, PSP_CTRL_CROSS);
    assert(app.buttons == std::vector<u16>(1, KeyB) && !app.menuMode && app.running);
    press(app, PSP_CTRL_HOME);
    assert(app.running && !app.menuMode); // Firmware handles confirmation.
    press(app, PSP_CTRL_SELECT);
    assert(app.menuMode && app.gameLoaded && app.running && app.buttons.empty());
    press(app, PSP_CTRL_CROSS);
    assert(!app.menuMode && app.gameLoaded && app.running);
    press(app, PSP_CTRL_SELECT);
    press(app, PSP_CTRL_TRIANGLE);
    assert(app.settingsMode && !app.menuMode);
    app.settingsIndex = 4;app.settingsCategory=2;app.settingsDetail=true;
    std::string selectedLanguage = app.settings.language;
    press(app, PSP_CTRL_CIRCLE);
    assert(app.settings.language != selectedLanguage && app.settingsMode);
    press(app, PSP_CTRL_CROSS);
    assert(app.settingsMode && !app.settingsDetail);
    press(app, PSP_CTRL_CROSS);
    assert(!app.settingsMode && app.menuMode && app.running);
    // Missing file is intentional: status proves both start/confirm take the
    // loader path, while cancel and HOME never trigger it or terminate the app.
    app.gamePaths.push_back("tests/out/intentionally-missing-game.smf");
    for (u32 key : {u32(PSP_CTRL_START), u32(PSP_CTRL_CIRCLE)}) {
        app.statusLine = "menu";
        press(app, key);
        assert(app.statusLine == "load failed" && app.menuMode && app.running);
    }
    press(app, PSP_CTRL_CROSS);
    assert(app.menuMode && app.running);
    testButtons() = 0;
}

static void testSystemInfo() {
    assert(cpuUsagePercent(0,1000)==100 && cpuUsagePercent(1000,1000)==0 && cpuUsagePercent(750,1000)==25);
    assert(cpuUsagePercent(0,0)==-1 && cpuUsagePercent(1001,1000)==-1);
    u64 total=0,free=0;
    assert(storageBytes(4000000,1000000,512,64,&total,&free) && total==131072000000ull && free==32768000000ull);
    assert(!storageBytes(2,3,512,64,&total,&free) && !storageBytes(2,1,0,64,&total,&free));
    PspApp app;app.menuMode=false;app.settingsMode=true;app.settingsIndex=6;app.settingsCategory=3;app.settingsDetail=true;
    press(app,PSP_CTRL_CIRCLE);assert(app.systemInfoMode);
    app.drawFrame();assert(app.systemInfoSampled && !app.systemInfo.storageValid);
    // Render representative values through the actual frontend, without pretending host values are PSP measurements.
    app.systemInfo.model="PSP-3000 (model 2)";app.systemInfo.firmware="6.61";
    app.systemInfo.cpuMHz=333;app.systemInfo.busMHz=166;app.systemInfo.cpuPercent=12;
    app.systemInfo.heapUsed=12*1048576;app.systemInfo.heapReserved=15*1048576;app.systemInfo.heapFree=3*1048576;
    app.systemInfo.systemFree=30*1048576;app.systemInfo.memoryValid=true;
    app.systemInfo.storage="ms0:";app.systemInfo.storageTotal=32ull*1024*1048576;app.systemInfo.storageFree=12ull*1024*1048576;app.systemInfo.storageValid=true;
    app.systemInfoTick=sceKernelGetSystemTimeLow();app.drawFrame();
    FILE* f=fopen("tests/out/system-info.ppm","wb");assert(f);fprintf(f,"P6\n480 272\n255\n");
    for(u32 c:app.menuFrame){u8 rgb[]={(u8)(c>>16),(u8)(c>>8),(u8)c};fwrite(rgb,1,3,f);}fclose(f);
    press(app,PSP_CTRL_CROSS);assert(!app.systemInfoMode && app.settingsMode && app.settingsIndex==6);
    press(app,PSP_CTRL_CIRCLE);press(app,PSP_CTRL_SELECT);assert(!app.systemInfoMode && !app.settingsMode && app.menuMode);
    testButtons()=0;puts("PASS: CPU sampling, 64-bit storage arithmetic, unavailable data, system info navigation and rendering");
}

static void testStickAndScreenshots() {
    char cwd[1024];assert(getcwd(cwd,sizeof(cwd)));
    char dir[]="tests/out/screens-XXXXXX";assert(mkdtemp(dir));assert(chdir(dir)==0);
    PspApp app;app.gameLoaded=true;app.menuMode=false;
    testStickX()=79;testStickY()=177;app.readInput();
    assert(app.buttons.size()==2 && app.buttons[0]==KeyLeft && app.buttons[1]==KeyDown);
    testStickX()=80;testStickY()=176;app.readInput();assert(app.buttons.empty());
    testStickX()=177;testStickY()=79;app.readInput();
    assert(app.buttons.size()==2 && app.buttons[0]==KeyRight && app.buttons[1]==KeyUp);
    testStickX()=testStickY()=128;app.readInput();assert(app.buttons.empty());
    app.pspFrameWidth=2;app.pspFrameHeight=2;
    app.pspFrame={0xff0000ff,0xff00ff00,0xffff0000,0xffffffff};
    press(app,PSP_CTRL_TRIANGLE);assert(app.screenshotNotice=="截图已保存");
    size_t count=0;std::string first=findScreenshot(screenshotDirectory(),"",0,&count);assert(count==1);
    app.readInput();findScreenshot(screenshotDirectory(),"",0,&count);assert(count==1);
    press(app,PSP_CTRL_TRIANGLE);std::string last=findScreenshot(screenshotDirectory(),"",0,&count);
    assert(count==2 && first!=last);
    std::vector<u32> pixels;assert(loadScreenshot(first,&pixels) && pixels.size()==480*272);
    assert(pixels[0]==0xff000000 && pixels[135*480+239]==0xff0000ff && pixels[136*480+239]==0xffff0000);
    press(app,PSP_CTRL_SQUARE);app.pauseIndex=2;press(app,PSP_CTRL_CIRCLE);
    assert(app.screenshotMode && app.screenshotCount==2 && app.screenshotPath==last);
    u64 tick=app.emulator.tickCount;unsigned slot=app.drawBuffer?1:0;
    app.drawFrame();assert(app.emulator.tickCount==tick);
    u32* displayed=(u32*)sceGeEdramGetAddr()+slot*512*272;
    assert(displayed[135*512+239]==0xff0000ff && displayed[136*512+239]==0xffff0000);
    press(app,PSP_CTRL_LEFT);assert(app.screenshotPath==first);
    press(app,PSP_CTRL_LEFT);assert(app.screenshotPath==last);
    press(app,PSP_CTRL_CIRCLE);assert(!app.screenshotInfo);
    press(app,PSP_CTRL_CROSS);assert(!app.screenshotMode && app.pauseMode && app.screenshotPixels.empty());
    // No count cap, and the gallery keeps only one image instead of a file list.
    for(int i=0;i<1100;++i) {char name[64];snprintf(name,sizeof(name),"shot-test-%06d.bmp",i);
        FILE* f=fopen((screenshotDirectory()+"/"+name).c_str(),"wb");assert(f);fputs("bad",f);fclose(f);}
    last=findScreenshot(screenshotDirectory(),"",0,&count);assert(count==1102);
    assert(!loadScreenshot(last,&pixels) && pixels.empty());
    assert(!saveScreenshot("/missing-parent/no-screens",app.pspFrame,2,2,0));
    testButtons()=0;assert(chdir(cwd)==0);
    puts("PASS: stick dead zone/diagonals/release, one capture per press, BMP colors/margins, gallery pause/wrap, 1102 files without a cap and corrupt-image rejection");
}

static void testPauseAndState() {
    PspApp app;
    app.gameLoaded=true; app.menuMode=false; app.loadedPath="tests/out/native-game.smf";
    std::vector<u8> fixture(512,0); std::memcpy(fixture.data(),"_YUVGamemaker 1.3.12",19);
    // Synthetic DES header: zero indexes and the required "8202" marker,
    // encrypted with the format's "11111111" key. Contains no game assets.
    const u8 header[] = {0x0f,0xb4,0x5d,0x2a,0x01,0x7a,0x92,0xe9,
        0x8c,0xa5,0x64,0x33,0x02,0x91,0x26,0xb9,
        0x8c,0xa5,0x64,0x33,0x02,0x91,0x26,0xb9,
        0x8c,0xa5,0x64,0x33,0x02,0x91,0x26,0xb9};
    std::memcpy(fixture.data()+0x78,header,sizeof(header));
    FILE* f=std::fopen(app.loadedPath.c_str(),"wb"); assert(f);
    assert(std::fwrite(fixture.data(),1,fixture.size(),f)==fixture.size()); std::fclose(f);
    assert(app.emulator.loadFromPath(app.loadedPath,60));
    auto& em=app.emulator;
    std::remove(em.saveManager.savePath.c_str());
    press(app,PSP_CTRL_SQUARE); assert(app.pauseMode && !app.menuMode);
    const u64 tick=em.tickCount; app.drawFrame(); assert(em.tickCount==tick);
    FILE* preview=fopen("tests/out/pause-capture.ppm","wb");assert(preview);
    fprintf(preview,"P6\n480 272\n255\n");
    for(u32 c:app.menuFrame) {u8 rgb[]={(u8)(c>>16),(u8)(c>>8),(u8)c};fwrite(rgb,1,3,preview);} fclose(preview);

    // Four menu items; native progress is written/read only by game scripts.
    press(app,PSP_CTRL_UP);assert(app.pauseIndex==3);
    press(app,PSP_CTRL_DOWN);assert(app.pauseIndex==0);
    em.getUrl("progress","SSL+SSL_GetSSLData+loaded");assert(em.vm.vars["loaded"]=="N");
    em.getUrl("1109600000002","SSL+SSL_SaveSSLData+saved");assert(em.vm.vars["saved"]=="S");
    em.getUrl("1109600000003","SSL+SSL_SaveSSLData+saved");
    std::string progress;
    assert(em.saveManager.load(&progress) && progress=="1109600000003");
    assert(pathIsFile(em.saveManager.savePath+".bak"));
    em.vm.vars["unsaved"]="kept while paused";
    em.getUrl("progress","SSL+SSL_GetSSLData+loaded");
    assert(em.vm.vars["loaded"]=="S" && em.vm.vars["progress"]=="1109600000003");
    assert(em.vm.vars["unsaved"]=="kept while paused");
    em.saveManager.setGamePath("tests/out/native-legacy.smf");
    std::remove(em.saveManager.savePath.c_str());
    f=std::fopen(em.saveManager.savePath.c_str(),"wb");assert(f);
    std::fputs("legacy progress",f);std::fclose(f);
    em.getUrl("progress","SSL+SSL_GetSSLData+loaded");assert(em.vm.vars["progress"]=="legacy progress");
    em.getUrl("updated progress","SSL+SSL_SaveSSLData+saved");
    assert(em.saveManager.savePath=="tests/out/native-legacy.smf.ssl_sav");
    assert(SaveManager("a/game.ssl").savePath=="a/game.ssl.ssl_sav");
    assert(SaveManager("b/game.ssl").savePath=="b/game.ssl.ssl_sav");
    em.cheats.setSlot(0,false,"var:score=999");
    assert(saveCheatFile(em.cheats,app.loadedPath+".cheats"));
    std::string message; em.cheats.clear(); assert(loadCheatFile(&em.cheats,app.loadedPath+".cheats",&message));
    app.pauseIndex=1; press(app,PSP_CTRL_CIRCLE); assert(app.cheatMode);
    press(app,PSP_CTRL_CIRCLE); assert(em.cheats.slots[0].enabled);
    CheatManager loaded; assert(loadCheatFile(&loaded,app.loadedPath+".cheats",&message) && loaded.slots[0].enabled);
    em.cheats.apply(&em.vm,&em.sprites,&em.framePlayer); assert(em.vm.vars["score"]=="999");
    press(app,PSP_CTRL_CROSS); assert(!app.cheatMode && app.pauseMode);
    press(app,PSP_CTRL_SQUARE); assert(!app.pauseMode);
    press(app,PSP_CTRL_SQUARE); app.pauseIndex=3; press(app,PSP_CTRL_CIRCLE);
    assert(app.menuMode && !app.gameLoaded && !app.pauseMode && em.reader.data.empty());
    assert(em.reader.data.capacity()==0 && em.renderer.buffer.capacity()==0 && app.pspFrame.capacity()==0);
    assert(!em.videoPlayer && !em.cutsceneAudio && em.cutsceneAudioFrame.interleaved.capacity()==0 && em.audio.retainedAudioBytes()==0);
    testButtons()=0;
    std::puts("PASS: square pause, four-item menu, script-only ssl_sav protocol, game-directory saves and cheat toggles");
}

static void testMpegPlayback() {
    // Skip through the actual PSP input path, including held-key suppression,
    // audio queue clearing, queued clips and deferred scene handoff.
    {
        PspApp app;
        app.audioMutex = 1; app.audioChannel = 0; app.gameLoaded = true; app.menuMode = false;
        app.emulator.filename = "tests/fixtures/launcher.smf";
        app.emulator.getUrl("mpeg-test.mpg+mpeg-test.mpg+missing-next.ssl", "SSL+SSL_PlayNext");
        testButtons() = PSP_CTRL_CIRCLE;
        app.previousButtons = PSP_CTRL_CIRCLE; // Held launch key must not skip.
        app.readInput();
        assert(app.emulator.pendingVideos.size() == 2);
        app.emulator.tick();
        assert(app.emulator.videoPlayer && app.emulator.pendingVideos.size() == 1);
        app.audioRing.assign(8192, 1); app.audioQueuedSamples = 2048;
        testButtons() = 0; app.readInput();
        testButtons() = PSP_CTRL_CIRCLE; app.readInput();
        assert(!app.emulator.videoPlayer && !app.emulator.cutsceneAudio);
        assert(app.audioQueuedSamples == 0 && app.buttons.empty());
        assert(app.emulator.pendingVideos.size() == 1 && app.emulator.contentLoader.hasPending());
        app.readInput(); // Held O must not skip the next queued clip.
        assert(app.buttons.empty() && app.emulator.pendingVideos.size() == 1);
        app.emulator.tick();
        assert(app.emulator.videoPlayer);
        app.readInput(); assert(app.emulator.videoPlayer && app.buttons.empty());
        testButtons() = 0; app.readInput();
        testButtons() = PSP_CTRL_START; app.readInput();
        assert(!app.emulator.videoPlayer && !app.emulator.cutsceneAudio);
        assert(app.emulator.contentError == "missing-next.ssl");
        assert(!app.emulator.contentLoader.hasPending());
        assert(!app.emulator.skipCutscene());
        testButtons() = 0;
        Emulator queued;
        queued.filename = "tests/fixtures/launcher.smf";
        queued.getUrl("mpeg-test.mpg+missing-next.ssl", "SSL+SSL_PlayNext");
        assert(queued.skipCutscene());
        assert(queued.pendingVideos.empty() && queued.contentError == "missing-next.ssl");
        std::puts("PASS: O/START skip active/queued clips, held-key isolation, audio flush and scene handoff");
    }
    Emulator em;
    em.filename = "tests/fixtures/launcher.smf";
    // Execute SSL_PlayNext from a real timeline action in the initial tick.
    em.reader.setData(std::vector<u8>(1024, 0));
    em.reader.base = 0; em.reader.frameIdx = 0; em.reader.actionIdx = 128;
    put32(em.reader.data, 0, 32);
    put32(em.reader.data, 32, 0x00010004u);
    put32(em.reader.data, 128, 0x96); put32(em.reader.data, 132, 512);
    put32(em.reader.data, 136, 0x96); put32(em.reader.data, 140, 700);
    put32(em.reader.data, 144, 0x9a);
    std::strcpy((char*)&em.reader.data[512], "mpeg-test.mpg+mpeg-test.mpg+missing-next.ssl");
    std::strcpy((char*)&em.reader.data[700], "SSL+SSL_PlayNext");
    em.tick();
    assert(em.contentLoader.hasPending() && em.pendingVideos.size() == 2);
    assert(em.contentError.empty());
    unsigned ticks = 0, audible = 0, visible = 0;
    size_t peakQueue = 0;
    while (em.contentError.empty() && ++ticks < 240) {
        em.tick();
        peakQueue = std::max(peakQueue, em.audio.pendingStreamFrames());
        std::vector<s16> pcm = em.pendingAudioSamples();
        for (s16 sample : pcm) if (sample) { ++audible; break; }
        for (u32 pixel : em.renderer.buffer) if ((pixel & 0xffffff) != 0) { ++visible; break; }
        if (em.videoPlayer || !em.pendingVideos.empty()) {
            assert(em.contentLoader.hasPending() && em.contentError.empty());
        }
    }
    assert(ticks >= 170 && ticks < 200 && audible > 160 && visible > 160);
    assert(peakQueue <= em.audio.outputSampleRate() / 10 + 1152);
    assert(em.contentError == "missing-next.ssl" && !em.contentLoader.hasPending());
    assert(!em.videoPlayer && !em.cutsceneAudio);

    PspApp app;
    app.audioMutex = 1; app.audioChannel = 0; app.gameLoaded = true; app.menuMode = false;
    app.emulator.filename = "tests/fixtures/launcher.smf";
    app.emulator.getUrl("missing-video.mpg+missing-next.ssl", "SSL+SSL_PlayNext");
    now = 0; app.drawGameFrame();
    assert(app.menuMode && !app.gameLoaded && app.statusLine == "load failed");
    std::printf("PASS: two MPEG clips (%u ticks, %u audible), bounded PCM queue (%zu frames), deferred content and failed-load menu\n", ticks, audible, peakQueue);
}

static std::vector<u8> packedFixture(bool large = false) {
    std::vector<u8> data(0x820, 0);
    put32(data, 0x60, 0x83);
    const std::string chunks[] = {large ? std::string(9000, 'A') : "first asset block", large ? std::string(9000, 'B') : "second asset block"};
    for (const std::string& chunk : chunks) {
        z_stream stream = {};
        assert(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
        u8 output[128];
        stream.next_in = (Bytef*)chunk.data(); stream.avail_in = chunk.size();
        stream.next_out = output; stream.avail_out = sizeof(output);
        assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
        size_t size = sizeof(output) - stream.avail_out;
        deflateEnd(&stream);
        size_t pos = data.size(); data.resize(pos + 4);
        put32(data, pos, size + 4);
        data.insert(data.end(), output, output + size);
    }
    return data;
}

static void testSourceSync() {
    for (unsigned variant = 0; variant < 8; ++variant) {
        Native32Reader reader;
        std::vector<u8> input = packedFixture();
        if (variant == 1) input.pop_back();
        if (variant == 2) put32(input, 0x820, 0xffffffffu);
        if (variant == 3) put32(input, 0x60, 0xffffffffu);
        if (variant == 4) input.push_back(0);
        if (variant == 7) input[0x824] = 7; // Reserved DEFLATE block type.
        reader.setData(input); reader.base = 0x20; reader.imageIdx = 0x40;
        u32 binarySize = variant == 5 ? 0x84 : variant == 6 ? 0xffffffffu : 0x83 + 36;
        bool ok = reader.expandPackedAssets(2, binarySize);
        assert(ok == (variant == 0));
        if (ok) {
            const char expected[] = "first asset blocksecond asset block";
            assert(reader.data.size() == 0xa3 + sizeof(expected) - 1);
            assert(std::memcmp(&reader.data[0xa3], expected, sizeof(expected) - 1) == 0);
        } else assert(reader.data == input);
    }
    Native32Reader large;
    large.setData(packedFixture(true)); large.base = 0x20; large.imageIdx = 0x40;
    assert(large.expandPackedAssets(2, 0x83 + 18000));
    assert(large.data.size() == 0xa3 + 18000);
    assert(std::count(large.data.begin() + 0xa3, large.data.begin() + 0xa3 + 9000, 'A') == 9000);
    assert(std::count(large.data.begin() + 0xa3 + 9000, large.data.end(), 'B') == 9000);
    for (u16 color : {u16(0xfc00), u16(0x83e0), u16(0x801f), u16(0x7fff)}) {
        std::vector<u8> data(12, 0); put32(data, 0, 0x00010001); put32(data, 4, 4);
        put32(data, 8, ((u32)color << 16) | 0xc001);
        RgbaImage image; assert(decodeImageArgb(data, &image));
        u32 expected = color == 0xfc00 ? 0xfff80000 : color == 0x83e0 ? 0xff00f800 : color == 0x801f ? 0xff0000f8 : 0;
        assert(image.pixels[0] == expected);
    }
    const std::string root = "tests/out/language";
    for (const char* suffix : {"", "/NA32SSL", "/NA32SSL/CHINESE", "/NA32SSL/ENGLISH", "/NA32SSL/CHINESE/BBLADE", "/NA32SSL/ENGLISH/BBLADE", "/nested"})
        mkdir((root + suffix).c_str(), 0700);
    const std::string chinese = root + "/NA32SSL/CHINESE/BBLADE/BBSTART.SSL";
    const std::string english = root + "/NA32SSL/ENGLISH/BBLADE/BBSTART.SSL";
    std::remove(english.c_str());
    FILE* file = std::fopen(chinese.c_str(), "wb"); assert(file); std::fclose(file);
    std::string found;
    assert(ContentLoader::findContentFile(root + "/nested/launcher.smf", "/NA32SSL /ENGLISH /BBLADE /BBSTART.SSL", &found));
    assert(found == chinese);
    file = std::fopen(english.c_str(), "wb"); assert(file); std::fclose(file);
    assert(ContentLoader::findContentFile(root + "/nested/launcher.smf", "na32ssl/english/bblade/bbstart.ssl", &found));
    assert(found == english);
    assert(!ContentLoader::findContentFile(root + "/launcher.smf", "NA32SSL/ENGLISH/BBLADE/MISSING.SSL", &found));
    assert(!ContentLoader::findContentFile(root + "/launcher.smf", "NA32SSL/ENGLISH/BBLADE", &found));
    std::remove(english.c_str()); std::remove(chinese.c_str());
    std::puts("PASS: packed DEFLATE blocks, malformed size/bounds rejection, ARGB channel layout, language fallback and original-language priority");
}

static void testChineseMenu() {
    const std::string legacy = "\x95\xf3\x90\xce\x90\x58\x97\xd1.smf";
    const std::string unicode = u8"宝石森林.smf";
    assert(decodeMenuName(legacy) == decodeMenuName(unicode));
    assert(decodeMenuName(legacy)[0] == 0x5b9d);
    assert(menuFileName("games/\x83\x5c.smf") == "\x83\x5c.smf");
    assert(decodeMenuName("\x83\x5c")[0] == 0x30bd);
    assert(decodeMenuName("\x81")[0] == 0xfffd);
    assert(decodeMenuName("\x81/")[1] == '/');
    assert(decodeMenuName("\xf0\x9f\x98\x80")[0] == 0x1f600);
    assert(menuGlyph(0x1f600)->code == 0xfffd);
    for (u32 code : decodeMenuName(u8"中文简体繁體宝石森林打地鼠赤刃"))
        assert(menuGlyph(code)->code == code);
    PspApp app;
    std::fill(app.menuFrame.begin(), app.menuFrame.end(), 0);
    app.drawMenuName(96, 86, legacy, 0xffffffff, 428);
    auto legacyPixels = app.menuFrame;
    std::fill(app.menuFrame.begin(), app.menuFrame.end(), 0);
    app.drawMenuName(96, 86, unicode, 0xffffffff, 428);
    assert(app.menuFrame == legacyPixels);
    assert(std::count(app.menuFrame.begin(), app.menuFrame.end(), 0xffffffffu) > 100);
    std::fill(app.menuFrame.begin(), app.menuFrame.end(), 0);
    app.drawMenuName(96, 86, std::string(100, 'A') + unicode, 0xffffffff, 428);
    for (int y = 0; y < 272; ++y) for (int x = 0; x < 480; ++x)
        if (x < 96 || x >= 428 || y < 86 || y >= 102) assert(app.menuFrame[y * 480 + x] == 0);
    app.gamePaths = {"games/" + legacy, u8"games/打地鼠.smf", u8"games/大富翁.smf", u8"games/中文游戏名称很长时自动省略不会切断汉字.smf", "games/Bloody Blade.smf", u8"games/繁體中文遊戲.sgm"};
    auto paths = app.gamePaths;
    app.drawMenuFrame();
    assert(app.gamePaths == paths);
    FILE* preview = std::fopen("tests/out/chinese-menu.ppm", "wb"); assert(preview);
    std::fprintf(preview, "P6\n480 272\n255\n");
    for (u32 pixel : app.menuFrame) {
        const unsigned char rgb[] = {(unsigned char)(pixel >> 16), (unsigned char)(pixel >> 8), (unsigned char)pixel};
        std::fwrite(rgb, 1, 3, preview);
    }
    std::fclose(preview);
    std::puts("PASS: CP932/UTF-8 Chinese glyphs, multibyte filename boundaries, malformed encoding, clipping and unchanged I/O paths");
}

static std::vector<u8> cacheFixture(unsigned count, unsigned side) {
    std::vector<u8> data(count * 4, 0);
    for (unsigned i = 0; i < count; ++i) {
        size_t start = data.size(); put32(data, i * 4, start);
        data.resize(start + 8); put32(data, start, side | (side << 16));
        unsigned remaining = side * side;
        while (remaining) {
            unsigned run = std::min(remaining, 0x3fffu);
            size_t pos = data.size(); data.resize(pos + 4);
            put32(data, pos, ((u32)(0x8000 | (i & 31)) << 16) | 0xc000 | run);
            remaining -= run;
        }
        put32(data, start + 4, data.size() - start - 8);
    }
    return data;
}

static void testBoundedImageCache() {
    Native32Reader reader;
    reader.setData(cacheFixture(40, 256)); reader.colorspace = ColorspaceArgb;
    for (unsigned i = 1; i <= 24; ++i) assert(reader.getImageRef(i));
    assert(reader.imageCacheBytes() == 6 * 1024 * 1024 && reader.imageCacheEvictions() == 0);
    assert(reader.getImageRef(1)); // Most recently used image survives.
    assert(reader.getImageRef(25));
    assert(reader.imageCacheEvictions() == 1);
    assert(reader.getImageRef(1) && reader.imageCacheEvictions() == 1);
    assert(reader.getImageRef(2) && reader.imageCacheEvictions() == 2);
    for (unsigned i = 0; i < 4000; ++i) {
        unsigned id = i % 40 + 1;
        const RgbaImage* image = reader.getImageRef(id);
        assert(image && image->pixels.size() == 65536);
        assert(image->pixels.front() == (0xff000000u | (((id - 1) & 31) << 3)));
        assert(image->pixels.back() == image->pixels.front());
        assert(reader.imageCacheBytes() <= 6 * 1024 * 1024 && reader.imageCacheCount() <= 24);
    }
    assert(reader.imageCacheEvictions() > 3900);
    reader.setData(cacheFixture(600, 1)); reader.colorspace = ColorspaceArgb;
    assert(reader.imageCacheBytes() == 0 && reader.imageCacheCount() == 0);
    for (unsigned i = 1; i <= 600; ++i) assert(reader.getImageRef(i));
    assert(reader.imageCacheCount() == 512 && reader.imageCacheBytes() == 512 * 4);
    for (unsigned i = 0; i < 10000; ++i) assert(!reader.getImageRef(0xffffffffu - i));
    reader.setData(std::vector<u8>());
    assert(reader.imageCacheBytes() == 0 && reader.imageCacheCount() == 0 && reader.imageCacheEvictions() == 0);
    std::puts("PASS: 4000 animation lookups stay below 6 MiB, LRU eviction/redecode preserves pixels, 512-entry limit, invalid indices and scene reset");
}

struct ProgressRecord { std::string stage;size_t done,total; };
static void recordProgress(void* p,const char* stage,const std::string&,size_t done,size_t total) {
    assert(!total || done<=total);
    auto* records=static_cast<std::vector<ProgressRecord>*>(p);
    if(!records->empty() && records->back().stage==stage && total && records->back().total==total)
        assert(done>=records->back().done);
    records->push_back({stage,done,total});
}
static void testGalleryOrdinalAndFpsPanel() {
    char dir[]="tests/out/ordinal-XXXXXX";assert(mkdtemp(dir));
    for(int i=0;i<10;++i){char name[80];snprintf(name,sizeof(name),"%s/shot-%02d.bmp",dir,i);FILE* f=fopen(name,"wb");assert(f);fclose(f);}
    size_t count=0,index=0;std::string selected=findScreenshot(dir,"",0,&count,&index);
    assert(count==10 && index==1);
    for(size_t i=2;i<=10;++i){selected=findScreenshot(dir,selected,-1,&count,&index);assert(index==i && count==10);}
    selected=findScreenshot(dir,selected,-1,&count,&index);assert(index==1);
    testDrawOffset()=0;PspApp app;app.gameLoaded=true;app.settings.showFps=true;
    app.settings.scaling=ScaleFull;app.emulator.reader.width=2;app.emulator.reader.height=2;
    app.emulator.renderer.buffer.assign(4,0xff123456);now=0;app.coreClock.due(0);
    for(unsigned value:{100u,9u})for(int pass=0;pass<2;++pass) {
        unsigned slot=app.drawBuffer?1:0;app.displayFps.value=value;app.pspFrame.clear();app.drawGameFrame();
        unsigned width=value==100?35:27;
        assert(app.fpsPanelWidth[slot]==width && app.fpsPanelHeight[slot]==13);
        const u32* page=(u32*)sceGeEdramGetAddr()+slot*512*272;
        assert(page[12*512+width-1]==0xff191310);
        assert(page[12*512+width]==0xff563412); // Shrinking restores the game, not black.
    }
    puts("PASS: 1/10 newest-first gallery wrap and FPS panel grows/shrinks on both VRAM pages");
}

static void testScreenshotDeletion() {
    char cwd[1024];assert(getcwd(cwd,sizeof(cwd)));
    char dir[]="tests/out/delete-shots-XXXXXX";assert(mkdtemp(dir));assert(chdir(dir)==0);
    std::vector<u32> pixels(4,0xffabcdef);std::string first,last;
    assert(saveScreenshot(screenshotDirectory(),pixels,2,2,&first));
    assert(saveScreenshot(screenshotDirectory(),pixels,2,2,&last));
    assert(!deleteScreenshot(screenshotDirectory(),screenshotDirectory()+"/../shot-test.bmp"));
    PspApp app;app.pauseMode=true;app.screenshotMode=true;app.browseScreenshots(0);
    app.readPauseMenu(PSP_CTRL_TRIANGLE);assert(app.screenshotDeleteConfirm && pathIsFile(last));
    app.readPauseMenu(PSP_CTRL_LEFT);assert(app.screenshotPath==last);
    app.readPauseMenu(PSP_CTRL_CROSS);assert(!app.screenshotDeleteConfirm && pathIsFile(last));
    app.readPauseMenu(PSP_CTRL_TRIANGLE);app.readPauseMenu(PSP_CTRL_CIRCLE);
    assert(!pathIsFile(last) && pathIsFile(first) && app.screenshotCount==1 && app.screenshotPath==first);
    app.readPauseMenu(PSP_CTRL_CIRCLE);assert(pathIsFile(first)); // No second deletion from confirm.
    app.readPauseMenu(PSP_CTRL_TRIANGLE);app.drawScreenshots();
    FILE* out=fopen("delete-confirm.ppm","wb");assert(out);fprintf(out,"P6\n480 272\n255\n");
    for(u32 c:app.menuFrame){u8 rgb[]={(u8)(c>>16),(u8)(c>>8),(u8)c};fwrite(rgb,1,3,out);}fclose(out);
    app.readPauseMenu(PSP_CTRL_CIRCLE);
    assert(app.screenshotCount==0 && app.screenshotPath.empty() && app.screenshotPixels.empty());
    app.readPauseMenu(PSP_CTRL_TRIANGLE);assert(!app.screenshotDeleteConfirm);
    app.screenshotPath=first;app.deleteCurrentScreenshot();assert(!app.screenshotError.empty());
    app.closeScreenshots();assert(!app.screenshotDeleteConfirm && app.screenshotError.empty());
    assert(chdir(cwd)==0);
    puts("PASS: screenshot deletion confirm/cancel, next image, final image, failure and path containment");
}

static void testLoadingProgress() {
    std::vector<ProgressRecord> records;LoadProgress progress;progress.callback=recordProgress;progress.context=&records;
    std::vector<u8> data(400000);for(size_t i=0;i<data.size();++i)data[i]=(u8)i;
    FILE* f=fopen("tests/out/load-progress.bin","wb");assert(f);assert(fwrite(data.data(),1,data.size(),f)==data.size());fclose(f);
    std::vector<u8> loaded;assert(readWholeFile("tests/out/load-progress.bin",&loaded,progress));
    assert(data==loaded && records.size()>=5 && records.back().done==data.size());
    records.clear();assert(!readWholeFile("tests/out/missing-loading-file",&loaded,progress));
    assert(records.size()==1 && records.back().total==0);
    assert(readWholeFile("tests/fixtures/mpeg-test.mpg",&data));
    records.clear();auto expected=mpeg::demuxAll(data);auto actual=mpeg::demuxAll(data,progress,"test.mpg");
    assert(expected.video==actual.video && expected.audio==actual.audio);
    assert(records.front().stage=="Indexing video");
    bool separate=false;for(const auto& e:records)separate|=e.stage=="Separating streams";assert(separate);
    testDrawOffset()=0; // New app starts with buffer 0, like initGu on device.
    PspApp app;app.gamePaths.push_back("Storm Wind.smf");app.drawMenuFrame();
    auto background=app.menuFrame;
    now=1000000;PspApp::loadingCallback(&app,"Reading file","scene.ssl",250,1000);
    for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x)
        if(x<80 || x>=400 || y<216 || y>=260)assert(app.menuFrame[y*480+x]==background[y*480+x]);
    assert(app.loadingShown && app.pspFrame.empty());
    const u32 expectedTint=blendPanel(background[258*480+100],0xff101319,70);
    assert(app.menuFrame[258*480+100]==expectedTint);
    now+=100001;PspApp::loadingCallback(&app,"Reading file","scene.ssl",500,1000);
    assert(app.menuFrame[258*480+100]==expectedTint); // Never darkens cumulatively.
    FILE* image=fopen("tests/out/loading.ppm","wb");assert(image);fprintf(image,"P6\n480 272\n255\n");
    for(u32 c:app.menuFrame){u8 rgb[]={(u8)(c>>16),(u8)(c>>8),(u8)c};fwrite(rgb,1,3,image);}fclose(image);
    auto frame=app.menuFrame;PspApp::loadingCallback(&app,"Reading file","scene.ssl",750,1000);
    assert(frame==app.menuFrame); // Redraw is throttled, not each I/O block.
    app.gameLoaded=true;app.drawGameFrame();assert(!app.loadingShown);
    PspApp::loadingCallback(&app,"Preparing scene","next.ssl",0,0);app.releaseGame();assert(!app.loadingShown);
    puts("PASS: chunked read progress, failed read, byte-identical demux, loading throttle/first frame/release");
}

static void testFrameDeadlines() {
    FrameClock clock;
    assert(clock.untilNext(123)==0);
    clock.due(0);assert(clock.untilNext(0)==33334);
    assert(clock.untilNext(33000)==334 && clock.untilNext(34000)==0);
    assert(clock.due(34000)==1 && clock.untilNext(34000)==32667);
    clock.reset();clock.due(0xffff0000u);
    assert(clock.untilNext((u32)(0xffff0000u+33000))==334);
    assert(clock.untilNext((u32)(0xffff0000u+100000))==0);
    TimingWindow timing;timing.add(20000);timing.add(60000);
    assert(timing.average()==40000 && timing.peak==60000);timing.reset();assert(timing.average()==0);
    RenderCadence cadence;
    // A 24 ms core plus 10 ms copy must count as overload, not 24 ms idle headroom.
    for(int i=0;i<3;++i)cadence.observe(true,24000+10000);
    assert(cadence.period()==2);
    puts("PASS: no-render deadline sleep, wrap, full presentation cost and timing windows");
}

static void testPresentationAndLanguage() {
    u32 w,h;scaledSize(320,240,ScaleFit,&w,&h);assert(w==362 && h==272);
    scaledSize(320,240,ScaleFull,&w,&h);assert(w==480 && h==272);
    scaledSize(640,240,ScaleFit,&w,&h);assert(w==480 && h==180);
    scaledSize(640,480,ScaleOriginal,&w,&h);assert(w==480 && h==272);
    DisplayFps fps;fps.sample(0xffff0000u);
    for(int i=0;i<15;++i)fps.presented();
    assert(fps.sample((u32)(0xffff0000u+1000000u)) && fps.value==15);
    assert(fps.sample((u32)(0xffff0000u+2000000u)) && fps.value==0);
    fps.reset();assert(!fps.sample(123) && fps.value==0);
    char dir[]="tests/out/language-XXXXXX";assert(mkdtemp(dir));
    std::string file=std::string(dir)+"/test.ini";
    FILE* out=fopen(file.c_str(),"wb");assert(out);
    fputs("\xef\xbb\xbf[language]\r\nname=Test language\r\n[strings]\r\nSettings=Custom settings\r\nVolume=100% test\r\nScaling=\r\n",out);fclose(out);
    Language lang;lang.load(dir,"test");assert(lang.id()=="test");
    assert(std::string(lang.text("Settings"))=="Custom settings");
    assert(std::string(lang.text("Volume"))=="100% test");
    assert(std::string(lang.text("Scaling"))=="Scaling");
    lang.cycle(1);assert(lang.id()=="zh_CN");lang.cycle(-1);assert(lang.id()=="test");
    lang.load(dir,"../../missing");assert(lang.id()=="zh_CN");
    Settings saved;saved.scaling=ScaleFull;saved.showFps=true;saved.language="test";saved.theme=3;saved.lightAppearance=true;
    file=std::string(dir)+"/settings.txt";assert(saved.save(file));Settings loaded;assert(loaded.load(file));
    assert(loaded.scaling==ScaleFull && loaded.showFps && loaded.language=="test" && loaded.theme==3 && loaded.lightAppearance);
    out=fopen(file.c_str(),"wb");fputs("0 1 2 80 0 1\n",out);fclose(out);
    assert(loaded.load(file) && loaded.language=="zh_CN" && loaded.scaling==ScaleFit && loaded.theme==0 && !loaded.lightAppearance);
    for(int mode=0;mode<3;++mode) {
        PspApp app;app.gameLoaded=true;app.settings.scaling=(VideoScaling)mode;
        app.emulator.reader.width=2;app.emulator.reader.height=2;
        app.emulator.renderer.buffer={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
        now=0;app.coreClock.due(0);app.drawGameFrame();
        scaledSize(2,2,(VideoScaling)mode,&w,&h);
        assert(app.pspFrameWidth==w && app.pspFrameHeight==h);
        assert(app.pspFrame.front()==0xff0000ff && app.pspFrame[w-1]==0xff00ff00);
        assert(app.pspFrame[(h-1)*w]==0xffff0000 && app.pspFrame.back()==0xffffffff);
    }
    {
        PspApp fpsApp;fpsApp.gameLoaded=true;fpsApp.settings.frameSkip=SkipOne;fpsApp.settings.showFps=true;
        for(unsigned i=0;i<=60;++i){now=(u32)((u64)i*1000000/60);fpsApp.drawGameFrame();}
        assert(fpsApp.displayFps.value==15);
        fpsApp.adjustSetting(3,1); // Disable overlay; both old HUDs must disappear.
        fpsApp.settings.frameSkip=SkipNone;
        for(unsigned i=0;i<2;++i){now=1033334+i*33334;fpsApp.drawGameFrame();}
        const u32* screens=(const u32*)sceGeEdramGetAddr();
        for(unsigned slot=0;slot<2;++slot)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<76;++x)
            assert(screens[slot*512*272+y*512+x]==0xff000000u);
    }
    PspApp app;
    assert(blendPanel(0xffffffff,0xff000000,70)==0xff4d4d4d);
    for(int theme=0;theme<5;++theme) {
        app.settings.theme=theme;app.drawSettingsFrame();
        assert(app.menuFrame[18*480+24]==themeAccent(theme));
        app.adjustSetting(5,1);assert(app.settings.theme==(theme+1)%5);
    }
    app.settings.theme=0;app.adjustSetting(5,-1);assert(app.settings.theme==4);
    app.settings.theme=0;app.language.load("languages","en");app.drawSettingsFrame();
    FILE* preview=fopen("tests/out/settings-en.ppm","wb");assert(preview);
    fprintf(preview,"P6\n480 272\n255\n");for(u32 c:app.menuFrame){u8 rgb[]={(u8)(c>>16),(u8)(c>>8),(u8)c};fwrite(rgb,1,3,preview);}fclose(preview);
    app.language.load("languages","zh_CN");app.drawSettingsFrame();
    preview=fopen("tests/out/settings-zh.ppm","wb");assert(preview);
    fprintf(preview,"P6\n480 272\n255\n");for(u32 c:app.menuFrame){u8 rgb[]={(u8)(c>>16),(u8)(c>>8),(u8)c};fwrite(rgb,1,3,preview);}fclose(preview);
    puts("PASS: scaling geometry/integration, real presentation FPS/wrap/reset, INI/BOM/fallback/cycling and legacy settings");
}

int main() {
    {
        PspApp app;app.settings.lightAppearance=true;app.screenshotInfo=false;
        app.screenshotPixels.assign(480*272,0xff123456);
        app.drawScreenshots();assert(app.menuFrame[100*480+100]==0xff563412);
        for(int theme=0;theme<5;++theme)assert(appearanceAccent(theme,true)!=themeAccent(theme));
        app.settingsCategory=3;app.settingsIndex=6;app.settingsDetail=true;
        app.drawSettingsFrame();
        testDrawOffset()=0;
    }

    {
        PspApp app;app.settingsMode=true;app.menuMode=false;
        press(app,PSP_CTRL_DOWN);assert(app.settingsCategory==1 && app.settingsIndex==2);
        unsigned volume=app.settings.volume;
        press(app,PSP_CTRL_CIRCLE);assert(app.settingsDetail && app.settings.volume==volume);
        press(app,PSP_CTRL_LEFT);assert(app.settings.volume==volume-10);
        press(app,PSP_CTRL_CROSS);assert(!app.settingsDetail && app.settingsMode);
        press(app,PSP_CTRL_DOWN);assert(app.settingsCategory==2);
        press(app,PSP_CTRL_CIRCLE);press(app,PSP_CTRL_DOWN);assert(app.settingsIndex==8);
        press(app,PSP_CTRL_RIGHT);assert(app.settings.lightAppearance);
        app.drawSettingsFrame();assert(app.menuFrame[0]==0xfff4f5f7);
        press(app,PSP_CTRL_DOWN);assert(app.settingsIndex==5);
        press(app,PSP_CTRL_CROSS);press(app,PSP_CTRL_DOWN);press(app,PSP_CTRL_CIRCLE);
        assert(app.settingsIndex==6);press(app,PSP_CTRL_CIRCLE);assert(app.systemInfoMode);
        press(app,PSP_CTRL_CROSS);assert(!app.systemInfoMode && app.settingsDetail);
        testButtons()=0;
    }

    {
        testDrawOffset()=0;PspApp app;app.settingsMode=true;app.menuMode=false;app.settingsIndex=7;app.settingsCategory=3;app.settingsDetail=true;
        press(app,PSP_CTRL_CIRCLE);assert(app.aboutMode);app.drawSettingsFrame();
        FILE* f=fopen("tests/out/about.ppm","wb");assert(f);fprintf(f,"P6\n480 272\n255\n");
        for(u32 c:app.menuFrame){u8 rgb[]={(u8)(c>>16),(u8)(c>>8),(u8)c};fwrite(rgb,1,3,f);}fclose(f);
        press(app,PSP_CTRL_CROSS);assert(!app.aboutMode && app.settingsMode && app.settingsIndex==7);
        press(app,PSP_CTRL_CIRCLE);press(app,PSP_CTRL_SELECT);assert(!app.aboutMode && app.menuMode);
        testButtons()=0;
    }
    testGalleryOrdinalAndFpsPanel();
    testScreenshotDeletion();
    testLoadingProgress();
    testFrameDeadlines();
    testPresentationAndLanguage();
    testBoundedImageCache();
    testChineseMenu();
    testSourceSync();
    testMpegPlayback();
    testClock();
    testRenderCadence();
    testFrameSkip();
    testCatchupCadence();
    testQueueAndThread();
    testBlackBackground();
    testResourceBounds();
    testControls();
    testSystemInfo();
    testStickAndScreenshots();
    testPauseAndState();
    std::puts("PASS: O confirms/A, X cancels/B, SELECT chooser/pause, X resume, START/O launch, PS delegated to firmware");
    std::puts("PASS: both VRAM buffers, menu return and canvas resize have black margins; malformed framebuffer/resources rejected; moved file storage and image cache agree");
    std::puts("PASS: 30 Hz timing, all frame-skip modes keep audio, asynchronous DMA ownership, rejected enqueue, ring wrap/overflow/reset, resampling, worker startup failure/shutdown");
}
