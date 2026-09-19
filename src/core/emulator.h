#ifndef NATIVE32_EMULATOR_H
#define NATIVE32_EMULATOR_H

#include "core/action_vm.h"
#include "core/audio_engine.h"
#include "core/cheats.h"
#include "core/content_loader.h"
#include "core/file_browser.h"
#include "core/input_handler.h"
#include "core/mpeg/audio.h"
#include "core/mpeg/demux.h"
#include "core/mpeg/player.h"
#include "core/renderer.h"
#include "core/save_manager.h"
#include <memory>
#include "core/load_progress.h"

namespace n32 {

class Emulator : public VmHost {
public:
    Emulator();

    bool loadFromPath(const std::string& path, u32 volume);
    bool reloadFromPath(const std::string& path);
    void setButtons(const std::vector<u16>& keycodes);
    void loadFrame(u32 frame);
    void tick(bool renderFrame = true);
    bool skipCutscene();
    void drawCurrentFrame();
    void reset();
    bool switchContent(const std::string& filename);

    const std::vector<u32>& framebuffer() const;
    std::vector<s16> pendingAudioSamples();
    void pendingAudioSamples(std::vector<s16>* output);
    u32 audioSampleRate() const;
    u32 gameWidth() const;
    u32 gameHeight() const;

    virtual void stop(const std::string& target);
    virtual void play(const std::string& target);
    virtual u32 getFrame(const std::string& target);
    virtual void gotoFrame(const std::string& target, u32 frame, bool playing);
    virtual void stopSounds(const std::string& target);
    virtual void setProperty(const std::string& target, ActionProp prop, const std::string& value);
    virtual std::string getProperty(const std::string& target, ActionProp prop);
    virtual void cloneSprite(const std::string& src, const std::string& dest, s32 depth);
    virtual void removeSprite(const std::string& name);
    virtual void call(u32 frame);
    virtual u32 getTime() const;
    virtual void getUrl(const std::string& url, const std::string& target);
    virtual void runFrameActions(u32 frame);

    std::string filename;
    std::string contentRoot;
    Native32Reader reader;
    SpriteSystem sprites;
    FramePlayer framePlayer;
    ActionVM vm;
    AudioEngine audio;
    Renderer renderer;
    InputHandler input;
    SaveManager saveManager;
    CheatManager cheats;
    ContentLoader contentLoader;
    FileBrowser fileBrowser;
    bool hasMenuContext;
    std::string menuContext;
    std::vector<FrameObject> curFrameObjects;
    u64 tickCount;
    u32 timeMs;
    std::vector<std::string> pendingVideos;
    bool autoSkipCutscenes;
    bool hasActiveVideo;
    std::string activeVideoName;
    std::unique_ptr<mpeg::VideoPlayer> videoPlayer;
    std::unique_ptr<mpeg::Audio> cutsceneAudio;
    mpeg::Samples cutsceneAudioFrame;
    std::string contentError;
    LoadProgress loadProgress;

private:
    void handleButtons();
    void applyCheats();
    void processMovieFrames();
    void loadMenuImage(const std::string& spec);
    bool startVideo(const std::string& name);
    void cutsceneTick(bool renderFrame);
    bool isCutsceneActive() const;
    void processPendingContent();
};

u16 timelineSoundValue(u16 index, s16 loopCount);
bool readWholeFile(const std::string& path, std::vector<u8>* out, const LoadProgress& progress=LoadProgress());
std::vector<std::string> splitPlus(const std::string& value);
std::string fhuiStrSub(const std::vector<std::string>& parts);
bool isZipFile(const std::string& path);

}

#endif
