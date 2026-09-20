#include "core/emulator.h"
#include "core/dat_loader.h"
// Diagnostics only: the cutscene path is hard to debug without on-device logs.
#include "platform/psp_log.h"
#include <algorithm>
#include <stdio.h>
#include <utility>
#if defined(PSP) || defined(N32_TEST_CORE_PROFILE)
#include <pspkernel.h>
#endif

namespace n32 {

static u32 tickProfileClock() {
#if defined(PSP) || defined(N32_TEST_CORE_PROFILE)
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}

u16 timelineSoundValue(u16 index, s16 loopCount) {
    u16 repeat = 0;
    if (loopCount == 32767) {
        repeat = 0xff;
    } else if (loopCount < 0) {
        repeat = 0;
    } else if (loopCount > 0xfe) {
        repeat = 0xfe;
    } else {
        repeat = (u16)loopCount;
    }
    return (u16)((repeat << 8) | (index & 0xff));
}

bool readWholeFile(const std::string& path, std::vector<u8>* out, const LoadProgress& progress) {
    if (!out) {
        return false;
    }
    progress.report("Reading file",path);
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return false;
    }
    out->assign((size_t)size, 0);
    size_t done=0;
    progress.report("Reading file",path,0,(size_t)size);
    while(done<(size_t)size) {
        size_t chunk=std::min((size_t)size-done,(size_t)131072);
        size_t got=fread(&(*out)[done],1,chunk,file);
        done+=got;progress.report("Reading file",path,done,(size_t)size);
        if(got!=chunk)break;
    }
    bool ok=done==(size_t)size;
    fclose(file);
    if (!ok) {
        out->clear();
    }
    return ok;
}

std::vector<std::string> splitPlus(const std::string& value) {
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin <= value.size()) {
        size_t sep = value.find('+', begin);
        if (sep == std::string::npos) {
            sep = value.size();
        }
        parts.push_back(value.substr(begin, sep - begin));
        if (sep == value.size()) {
            break;
        }
        begin = sep + 1;
    }
    return parts;
}

std::string fhuiStrSub(const std::vector<std::string>& parts) {
    if (parts.size() < 4) {
        return std::string();
    }
    std::string source = parts[1];
    std::string delim = parts[2];
    size_t field = parseSize(parts[3]);
    if (field == 0) {
        return std::string();
    }
    if (delim.empty()) {
        return field == 1 ? source : std::string();
    }

    size_t begin = 0;
    size_t index = 1;
    while (begin <= source.size()) {
        size_t sep = source.find(delim, begin);
        if (sep == std::string::npos) {
            sep = source.size();
        }
        if (index == field) {
            return source.substr(begin, sep - begin);
        }
        if (sep == source.size()) {
            break;
        }
        begin = sep + delim.size();
        ++index;
    }
    return std::string();
}

bool isZipFile(const std::string& path) {
    if (pathExtensionLower(path) == "zip") {
        return true;
    }
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    u8 magic[4] = { 0, 0, 0, 0 };
    bool ok = fread(magic, 1, 4, file) == 4;
    fclose(file);
    return ok && magic[0] == 0x50 && magic[1] == 0x4b && magic[2] == 0x03 && magic[3] == 0x04;
}

Emulator::Emulator()
    : audio(ColorspaceYuv, 100), renderer(320, 240), hasMenuContext(false),
      tickCount(0), timeMs(0), autoSkipCutscenes(false), hasActiveVideo(false) {
}

bool Emulator::loadFromPath(const std::string& path, u32 volume) {
    logMpegProfile();
    frameChanged=false;
    std::vector<std::string>().swap(movieFrameNames);
    contentError.clear();
    loadProgress.report("Opening game",path);
    if (isZipFile(path)) {
        return false;
    }

    // Consecutive SSL scenes belong to one game and share its save data, so the
    // save binding is only refreshed when leaving (or entering) an SSL chain.
    bool wasSsl = pathExtensionLower(filename) == "ssl";
    bool nowSsl = pathExtensionLower(path) == "ssl";
    bool rebindSave = !wasSsl || !nowSsl;

    filename = path;
    contentRoot = pathParent(path);
    // Release the previous scene before reading its replacement. On a PSP,
    // retaining cached images/music plus both complete files can exhaust RAM.
    audio.stopAll();
    videoPlayer.reset();
    cutsceneAudio.reset();
    releaseVideoFrame();
    std::vector<float>().swap(cutsceneAudioFrame.interleaved);
    reader.setData(std::vector<u8>());
    renderer.clearSpriteOverrides();
    std::vector<u8> data;
    if (!readWholeFile(path, &data, loadProgress)) {
        pspLog("load: file read failed '%s'", path.c_str());
        return false;
    }

    pspLog("load: read %u bytes '%s'", (unsigned)data.size(), path.c_str());
    reader.setData(std::move(data));
    loadProgress.report("Parsing game",path);
    pspLog("load: header begin");
    if (!reader.init()) {
        pspLog("load: invalid/unsupported header '%s'", path.c_str());
        return false;
    }

    loadProgress.report("Preparing scene",path);
    pspLog("load: header ok; runtime reset begin");
    sprites.clear();
    framePlayer = FramePlayer();
    vm = ActionVM();
    audio = AudioEngine(reader.colorspace, volume);
    renderer.resize(reader.width, reader.height);
    pspLog("load: runtime ready %ux%u",(unsigned)reader.width,(unsigned)reader.height);
    if (rebindSave) {
        saveManager.setGamePath(filename);
    }
    contentLoader.clear();
    hasMenuContext = false;
    menuContext.clear();
    curFrameObjects.clear();
    tickCount = 0;
    timeMs = 0;
    pendingVideos.clear();
    videoPlayer.reset();
    hasActiveVideo = false;
    activeVideoName.clear();
    return true;
}

bool Emulator::reloadFromPath(const std::string& path) {
    u32 volumeValue = (u32)(audio.volume * 100.0f);
    audio.stopAll();
    renderer.clearSpriteOverrides();
    return loadFromPath(path, volumeValue);
}

void Emulator::setButtons(const std::vector<u16>& keycodes) {
    input.setButtons(keycodes);
    profileInputMask = 0;
    for (u16 key : keycodes) {
        switch (key) {
        case KeyLeft: profileInputMask |= 1; break;
        case KeyRight: profileInputMask |= 2; break;
        case KeyUp: profileInputMask |= 4; break;
        case KeyDown: profileInputMask |= 8; break;
        case KeyA: profileInputMask |= 16; break;
        case KeyB: profileInputMask |= 32; break;
        default: profileInputMask |= 64; break;
        }
    }
}

void Emulator::loadFrame(u32 frame) {
    const std::vector<FrameObject>* objects = reader.getFrameRef(frame);
    if (!objects) {
        curFrameObjects.clear();
        return;
    }
    curFrameObjects = *objects;
    sprites.updateForFrame(*objects);
    for (size_t i = 0; i < objects->size(); ++i) {
        if ((*objects)[i].type == ObjectSound) {
            audio.playSound(&reader, timelineSoundValue((*objects)[i].index, (*objects)[i].x), "");
        }
    }
}

void Emulator::tick(bool renderFrame) {
    frameChanged=false;
    lastDrawMicros=0;
    lastGameProfile = GameTickProfile();
    vm.resetProfile();
    audio.resetProfile();
    if(tickCount==0)loadProgress.report("Preparing scene",filename);
    ++tickCount;

    // While a cutscene is playing (or queued), drive video playback instead of
    // the normal timeline. Cheats and the clock still advance.
    if (isCutsceneActive()) {
        cutsceneTick(renderFrame);
        processPendingContent();
        applyCheats();
        timeMs += 1000 / 30;
        return;
    }

    lastGameProfile.tick = tickCount;
    lastGameProfile.inputMask = profileInputMask;
    u32 phaseBegin = tickProfileClock();
    framePlayer.tick();
    if (framePlayer.hasPendingFrame()) {
        u32 next = framePlayer.takeNextFrame();
        if (next != 0) {
            framePlayer.currentFrame = next;
            loadFrame(next);
            std::vector<u32> actions;
            actions.reserve(curFrameObjects.size());
            for (size_t i = 0; i < curFrameObjects.size(); ++i) {
                if (curFrameObjects[i].type == ObjectAction) {
                    actions.push_back(curFrameObjects[i].index);
                }
            }
            for (size_t i = 0; i < actions.size(); ++i) {
                vm.run(&reader, this, actions[i], "");
            }
        }
    }

    u32 phaseEnd = tickProfileClock();
    lastGameProfile.timelineMicros = phaseEnd - phaseBegin;
    phaseBegin = phaseEnd;
    processMovieFrames();
    phaseEnd = tickProfileClock();
    lastGameProfile.movieMicros = phaseEnd - phaseBegin;
    phaseBegin = phaseEnd;
    handleButtons();
    phaseEnd = tickProfileClock();
    lastGameProfile.buttonMicros = phaseEnd - phaseBegin;
    lastGameProfile.frame = framePlayer.currentFrame;
    // A pending scene load replaces VM/audio objects; keep this tick's work
    // before that replacement. Loading ticks are excluded from steady peaks.
    lastGameProfile.vm = vm.profile;
    lastGameProfile.sound = audio.profile;

    phaseBegin = tickProfileClock();
    processPendingContent();
    phaseEnd = tickProfileClock();
    lastGameProfile.pendingMicros = phaseEnd - phaseBegin;

    phaseBegin = phaseEnd;
    applyCheats();
    phaseEnd = tickProfileClock();
    lastGameProfile.cheatMicros = phaseEnd - phaseBegin;
    if (renderFrame) drawCurrentFrame();
    lastGameProfile.drawMicros = lastDrawMicros;
    lastGameProfile.rendered = frameChanged;
    timeMs += 1000 / 30;
}

void Emulator::processMovieFrames() {
    size_t nameCount = 0;
    for (SpriteMap::const_iterator it = sprites.sprites.begin(); it != sprites.sprites.end(); ++it) {
        if (nameCount == movieFrameNames.size()) movieFrameNames.push_back(it->first);
        else movieFrameNames[nameCount] = it->first;
        ++nameCount;
    }
    const std::vector<std::string>& names = movieFrameNames;

    for (size_t i = 0; i < nameCount; ++i) {
        MovieState* movie = sprites.getMutable(names[i]);
        if (!movie) {
            continue;
        }
        if (!movie->playing || movie->hasNextFrame) {
            continue;
        }
        // getMovie() copies the cached frame vector on every call. This path
        // runs for every sprite on every core tick, so use the cached view to
        // avoid repeated PSP heap allocations and frame-data copies.
        const std::vector<MovieFrame>* movieFrames = reader.getMovieRef(movie->movie);
        if (!movieFrames || movieFrames->empty()) {
            continue;
        }
        if ((tickCount % 2) == 0) {
            movie->hasNextFrame = true;
            movie->nextFrame = movie->frame < movieFrames->size() - 1
                ? (s32)movie->frame + 1
                : 0;  // loop
        }
    }

    for (size_t i = 0; i < nameCount; ++i) {
        MovieState* movie = sprites.getMutable(names[i]);
        if (!movie || !movie->hasNextFrame) {
            continue;
        }
        const std::vector<MovieFrame>* frames = reader.getMovieRef(movie->movie);
        if (!frames) {
            movie->hasNextFrame = false;
            continue;
        }
        if (movie->nextFrame == -1) {
            movie->frame = 0;
        } else if (movie->nextFrame >= 0 && (size_t)movie->nextFrame < frames->size()) {
            movie->frame = (size_t)movie->nextFrame;
        }
        movie->hasNextFrame = false;

        if (movie->frame < frames->size()) {
            MovieFrame mf = (*frames)[movie->frame];
            if (mf.sound != 0) {
                size_t channel = audio.playSound(&reader, mf.sound, names[i]);
                if (channel != 0) {
                    MovieState* updated = sprites.getMutable(names[i]);
                    if (updated) {
                        updated->hasSoundChannel = true;
                        updated->soundChannel = channel;
                    }
                }
            }
            if (mf.action != 0) {
                vm.run(&reader, this, mf.action, names[i]);
            }
        }
    }

    for (SpriteMap::iterator it = sprites.sprites.begin(); it != sprites.sprites.end(); ++it) {
        if (it->second.hasSoundChannel && !audio.isChannelPlaying(it->second.soundChannel)) {
            it->second.hasSoundChannel = false;
        }
    }
}

void Emulator::handleButtons() {
    std::vector<u16> pressed = input.pressedButtons();
    if (pressed.empty()) {
        return;
    }

    std::vector<u32> actions;
    for (size_t i = 0; i < curFrameObjects.size(); ++i) {
        const FrameObject& obj = curFrameObjects[i];
        if (obj.type != ObjectButton) {
            continue;
        }
        std::vector<ButtonEvent> events;
        reader.getButtonEvents(obj.index, &events);
        for (size_t e = 0; e < events.size(); ++e) {
            if (std::find(pressed.begin(), pressed.end(), events[e].keycode) != pressed.end()) {
                actions.push_back(events[e].event);
            }
        }
    }

    for (size_t i = 0; i < actions.size(); ++i) {
        vm.run(&reader, this, actions[i], "");
    }
}

void Emulator::applyCheats() {
    cheats.apply(&vm, &sprites, &framePlayer);
}

void Emulator::drawCurrentFrame() {
    const u32 begin = tickProfileClock();
    renderer.drawFrame(&reader, sprites, curFrameObjects);
    releaseVideoFrame();
    lastDrawMicros = tickProfileClock() - begin;
    frameChanged=true;
}

void Emulator::reset() {
    logMpegProfile();
    frameChanged=false;
    lastDrawMicros=0;
    lastGameProfile = GameTickProfile();
    profileInputMask = 0;
    std::vector<std::string>().swap(movieFrameNames);
    tickCount = 0;
    timeMs = 0;
    sprites.clear();
    framePlayer = FramePlayer();
    vm = ActionVM();
    pendingVideos.clear();
    videoPlayer.reset();
    cutsceneAudio.reset();
    releaseVideoFrame();
    std::vector<float>().swap(cutsceneAudioFrame.interleaved);
    contentError.clear();
    audio.stopAll();
    decltype(curFrameObjects)().swap(curFrameObjects);
    std::vector<u32>().swap(renderer.buffer);
    renderer.clearSpriteOverrides();
    contentLoader.clear();hasActiveVideo=false;activeVideoName.clear();
    hasMenuContext=false;menuContext.clear();
}

bool Emulator::switchContent(const std::string& content) {
    std::string path;
    if (!ContentLoader::findContentFile(filename, content, &path)) {
        pspLog("content: missing '%s' relative to '%s'", content.c_str(), filename.c_str());
        return false;
    }
    pspLog("content: resolved '%s' -> '%s'", content.c_str(), path.c_str());
    return reloadFromPath(path);
}

const std::vector<u32>& Emulator::framebuffer() const {
    return displayVideoFrame ? videoFrameBuffer : renderer.buffer;
}

u32 Emulator::framebufferWidth() const {
    return displayVideoFrame ? videoFrameWidth : gameWidth();
}

u32 Emulator::framebufferHeight() const {
    return displayVideoFrame ? videoFrameHeight : gameHeight();
}

void Emulator::releaseVideoFrame() {
    displayVideoFrame = false;
    videoFrameWidth = videoFrameHeight = 0;
    if (!videoFrameBuffer.empty()) std::vector<u32>().swap(videoFrameBuffer);
}

std::vector<s16> Emulator::pendingAudioSamples() {
    return audio.getPendingSamples();
}

void Emulator::pendingAudioSamples(std::vector<s16>* output) {
    audio.getPendingSamples(output);
}

u32 Emulator::audioSampleRate() const {
    return audio.outputSampleRate();
}

u32 Emulator::gameWidth() const {
    return reader.width;
}

u32 Emulator::gameHeight() const {
    return reader.height;
}

void Emulator::stop(const std::string& target) {
    if (target.empty()) {
        framePlayer.playing = false;
    } else {
        MovieState* movie = sprites.getMutable(target);
        if (movie) {
            movie->playing = false;
        }
    }
}

void Emulator::play(const std::string& target) {
    if (target.empty()) {
        framePlayer.playing = true;
    } else {
        MovieState* movie = sprites.getMutable(target);
        if (movie) {
            movie->playing = true;
        }
    }
}

u32 Emulator::getFrame(const std::string& target) {
    if (target.empty()) {
        return framePlayer.currentFrame;
    }
    const MovieState* movie = sprites.get(target);
    return movie ? (u32)movie->frame + 1 : 0;
}

void Emulator::gotoFrame(const std::string& target, u32 frame, bool playing) {
    if (target.empty()) {
        framePlayer.gotoFrame(frame, playing);
    } else {
        MovieState* movie = sprites.getMutable(target);
        if (movie) {
            movie->hasNextFrame = true;
            movie->nextFrame = (s32)frame - 1;
            movie->playing = playing;
        }
    }
}

void Emulator::stopSounds(const std::string& target) {
    if (target.empty()) {
        audio.stopAll();
        for (SpriteMap::iterator it = sprites.sprites.begin(); it != sprites.sprites.end(); ++it) {
            it->second.hasSoundChannel = false;
        }
    } else {
        audio.stopForMovie(target);
        MovieState* movie = sprites.getMutable(target);
        if (movie) {
            movie->hasSoundChannel = false;
        }
    }
}

void Emulator::setProperty(const std::string& target, ActionProp prop, const std::string& value) {
    MovieState* movie = sprites.getMutable(target);
    if (!movie) {
        return;
    }
    switch (prop) {
    case ActionPropX: movie->x = (s16)strToFloat(value); break;
    case ActionPropY: movie->y = (s16)strToFloat(value); break;
    case ActionPropVisible: movie->visible = strToFloat(value) != 0.0; break;
    case ActionPropCurrentFrame:
        movie->hasNextFrame = true;
        movie->nextFrame = (s32)strToFloat(value);
        break;
    case ActionPropName: {
        MovieState state;
        if (sprites.remove(target, &state)) {
            sprites.insert(value, state);
        }
        break;
    }
    default:
        break;
    }
}

std::string Emulator::getProperty(const std::string& target, ActionProp prop) {
    const MovieState* movie = sprites.get(target);
    if (!movie) {
        return "0";
    }
    switch (prop) {
    case ActionPropX: return intToString(movie->x);
    case ActionPropY: return intToString(movie->y);
    case ActionPropVisible: return movie->visible ? "1" : "0";
    case ActionPropCurrentFrame:
        if (movie->hasNextFrame) {
            return intToString(std::max<s32>(0, movie->nextFrame) + 1);
        }
        return intToString((s64)movie->frame + (movie->playing ? 2 : 1));
    case ActionPropTotalFrames: {
        const std::vector<MovieFrame>* frames = reader.getMovieRef(movie->movie);
        return intToString(frames ? (s64)frames->size() : 0);
    }
    case ActionPropName:
        return target;
    default:
        return "0";
    }
}

void Emulator::cloneSprite(const std::string& src, const std::string& dest, s32 depth) {
    const MovieState* orig = sprites.get(src);
    if (!orig) {
        return;
    }
    MovieState state(orig->movie, orig->x, orig->y, (u16)std::max<s32>(0, depth));
    state.frame = 0;
    state.visible = true;
    state.playing = true;
    state.cloned = true;
    state.hasNextFrame = true;
    state.nextFrame = 0;
    sprites.insert(dest, state);
}

void Emulator::removeSprite(const std::string& name) {
    MovieState movie;
    if (sprites.remove(name, &movie) && movie.hasSoundChannel) {
        audio.stopForMovie(name);
    }
}

void Emulator::call(u32 frame) {
    runFrameActions(frame);
}

u32 Emulator::getTime() const {
    return timeMs;
}

void Emulator::getUrl(const std::string& url, const std::string& target) {
    std::vector<std::string> parts = splitPlus(target);
    std::string cmd = parts.empty() ? std::string() : parts[0];
    std::string arg;
    size_t firstPlus = target.find('+');
    if (firstPlus != std::string::npos) {
        arg = target.substr(firstPlus + 1);
    }

    if (cmd == "GetFileNum") {
        vm.vars[lowerString(url)] = intToString((s64)fileBrowser.fileCount(filename, arg));
    } else if (cmd == "GetFirstFile") {
        vm.vars[lowerString(url)] = fileBrowser.firstFile(filename, arg);
    } else if (cmd == "GetNextFile") {
        vm.vars[lowerString(url)] = fileBrowser.nextFile();
    } else if (cmd == "GetContext") {
        vm.vars[lowerString(url)] = hasMenuContext ? menuContext : "NULL";
    } else if (cmd == "SaveContext") {
        hasMenuContext = true;
        menuContext = url;
    } else if (cmd == "FHUI_StrSub") {
        vm.vars[lowerString(url)] = fhuiStrSub(parts);
    } else if (cmd == "StartGame") {
        contentLoader.queueLoad(url + ".smf");
    } else if (cmd == "LoadImage") {
        loadMenuImage(url);
    } else if ((cmd == "SSL" || cmd == "NAV") && parts.size() >= 2) {
        std::string sub = parts[1];
        if (sub == "SSL_PlayNext") {
            // All parts except the last are MPEG-1 pre-content (logo / cutscene
            // videos) to play before loading the final SSL content.
            std::vector<std::string> urlParts = splitPlus(url);
            if (urlParts.size() > 1) {
                for (size_t i = 0; i + 1 < urlParts.size(); ++i) {
                    std::string normalized = normalizeContentPath(urlParts[i]);
                    if (!normalized.empty()) {
                        pendingVideos.push_back(normalized);
                    }
                }
            }
            if (!urlParts.empty()) {
                contentLoader.queueLoad(urlParts[urlParts.size() - 1]);
            }
            pspLog("SSL_PlayNext: %u parts -> %u videos queued, content='%s'",
                   (unsigned)urlParts.size(), (unsigned)pendingVideos.size(),
                   urlParts.empty() ? "" : urlParts[urlParts.size() - 1].c_str());
        } else if (sub == "SSL_GetSSLData" && parts.size() >= 3) {
            std::string data;
            if (saveManager.load(&data)) {
                vm.vars[lowerString(url)] = data;
                vm.vars[lowerString(parts[2])] = "S";
            } else {
                vm.vars[lowerString(parts[2])] = "N";
            }
        } else if (sub == "SSL_SaveSSLData" && parts.size() >= 3) {
            if (saveManager.save(url)) {
                vm.vars[lowerString(parts[2])] = "S";
            }
        } else if (sub == "SSL_PlayPlan" || sub == "SSL_PlayProg") {
            pspLog("emulator: ignoring %s('%s')", sub.c_str(), url.c_str());
        } else if (sub == "NAV_SelectNES") {
            // NES ROM browsing is handled by the original platform's NES
            // emulator, which is out of scope for this core.
            pspLog("emulator: ignoring NAV_SelectNES('%s')", url.c_str());
        } else if (sub == "NAV_SelectNa32") {
            // The platform selector hands control to the sibling Native32 menu
            // after its selection animation finishes.
            pspLog("emulator: NAV_SelectNa32");
            contentLoader.queueLoad("NA32UI.smf");
        } else if (sub == "NAV_SelectKOK") {
            // Karaoke requires a separate firmware service that is not part of
            // the Native32 runtime. Restart the selector because its
            // confirmation animation no longer accepts input.
            pspLog("emulator: NAV_SelectKOK unsupported; restarting selector");
            contentLoader.queueLoad("SELECT.SSL");
        } else if (sub == "NAV_ScreenMove") {
            std::vector<std::string> coords = splitPlus(url);
            if (coords.size() >= 2) {
                renderer.screenX = (s32)strToFloat(coords[0]);
                renderer.screenY = (s32)strToFloat(coords[1]);
            }
        }
    }
}

void Emulator::runFrameActions(u32 frame) {
    const std::vector<FrameObject>* objects = reader.getFrameRef(frame);
    if (!objects) {
        return;
    }
    std::vector<u32> actions;
    actions.reserve(objects->size());
    for (size_t i = 0; i < objects->size(); ++i) {
        if ((*objects)[i].type == ObjectAction) {
            actions.push_back((*objects)[i].index);
        }
    }
    for (size_t i = 0; i < actions.size(); ++i) {
        vm.run(&reader, this, actions[i], "");
    }
}

bool Emulator::isCutsceneActive() const {
    return videoPlayer.get() != 0 || !pendingVideos.empty();
}

bool Emulator::skipCutscene() {
    if (!isCutsceneActive()) return false;
    logMpegProfile();
    if (videoPlayer) {
        pspLog("cutscene: user skipped '%s'", activeVideoName.c_str());
    } else {
        pspLog("cutscene: user skipped queued '%s'", pendingVideos.front().c_str());
        pendingVideos.erase(pendingVideos.begin());
    }
    videoPlayer.reset();
    cutsceneAudio.reset();
    std::vector<float>().swap(cutsceneAudioFrame.interleaved);
    hasActiveVideo = false;
    activeVideoName.clear();
    audio.stopAll();
    // Consume the pending destination now; don't run the old scene's actions
    // for another tick (which could enqueue the intro again).
    processPendingContent();
    if (!isCutsceneActive() && contentError.empty() && tickCount != 0) drawCurrentFrame();
    return true;
}

void Emulator::processPendingContent() {
    // SSL_PlayNext queues video and content in the same VM call. Do not consume
    // the destination until every pre-content clip has finished or failed.
    if (isCutsceneActive() || !contentLoader.hasPending()) return;
    std::string next;
    if (contentLoader.takePending(&next)) {
        pspLog("content: switching to '%s'", next.c_str());
        if (!switchContent(next)) {
            contentError = next;
            audio.stopAll();
            pspLog("content: switch failed '%s'", next.c_str());
        }
    }
}

void Emulator::cutsceneTick(bool renderFrame) {
    // Load the next queued video if none is playing.
    if (!videoPlayer) {
        if (pendingVideos.empty()) {
            return;
        }
        std::string name = pendingVideos.front();
        pendingVideos.erase(pendingVideos.begin());
        if (!startVideo(name)) {
            pspLog("cutscene: startVideo failed '%s', %u still queued",
                   name.c_str(), (unsigned)pendingVideos.size());
            return;
        }
    }

    size_t w, h;
    mpeg::videoOutputSize(videoPlayer->width(), videoPlayer->height(), &w, &h);
    const u32 audioBegin = tickProfileClock();
    u32 audioFrames = 0;
    // Decode at most two MP2 frames per tick; avoid a multi-second blocking
    // decode of the entire track before the first video frame is shown.
    for (unsigned i = 0; cutsceneAudio && i < 2 &&
         audio.pendingStreamFrames() < audio.outputSampleRate() / 10; ++i) {
        u32 rate = cutsceneAudio->sampleRate();
        if (!cutsceneAudio->decode(&cutsceneAudioFrame)) {
            cutsceneAudio.reset();
    std::vector<float>().swap(cutsceneAudioFrame.interleaved);
            break;
        }
        audio.appendPcmStream(cutsceneAudioFrame.interleaved, 2, rate, false);
        ++audioFrames;
    }
    const u32 videoBegin = tickProfileClock();
    frameChanged=videoPlayer->advanceAndRender(1.0 / 30.0, renderFrame ? &videoFrameBuffer : 0, w, h);
    if (frameChanged) {
        videoFrameWidth = (u32)w;
        videoFrameHeight = (u32)h;
        displayVideoFrame = true;
    }
    const u32 end = tickProfileClock();
    const mpeg::AdvanceDiagnostics& advance = videoPlayer->advanceDiagnostics();
    ++mpegProfile.ticks;
    mpegProfile.audioFrames += audioFrames;
    mpegProfile.slots += advance.slotsAdvanced;
    mpegProfile.rgbFrames += advance.wroteRgb ? 1 : 0;
    mpegProfile.skipped += advance.pictures.skippedB;
    mpegProfile.skippedCumulative = videoPlayer->skippedFrames();
    mpegProfile.audioMicros += videoBegin - audioBegin;
    mpegProfile.videoMicros += end - videoBegin;
    mpegProfile.decodeMicros += advance.decodeMicros;
    mpegProfile.rgbMicros += advance.rgbMicros;
    if (mpegProfile.ticks == 1 || end - audioBegin > mpegProfile.maxMicros) {
        mpegProfile.maxMicros = end - audioBegin;
        mpegProfile.peakAudioMicros = videoBegin - audioBegin;
        mpegProfile.peakVideoMicros = end - videoBegin;
        mpegProfile.peakAudioFrames = audioFrames;
        mpegProfile.peakTick = tickCount;
        mpegProfile.peakAt = end;
        mpegProfile.peak = advance;
    }
    if (mpegProfile.ticks == 60) logMpegProfile();
    if (videoPlayer->isFinished()) {
        logMpegProfile();
        pspLog("cutscene: '%s' finished after %.2fs, %u queued",
               activeVideoName.c_str(), videoPlayer->elapsed(),
               (unsigned)pendingVideos.size());
        videoPlayer.reset();
        cutsceneAudio.reset();
    std::vector<float>().swap(cutsceneAudioFrame.interleaved);
        hasActiveVideo = false;
        activeVideoName.clear();
        audio.stopAll();
    }
}

void Emulator::logMpegProfile() {
    if (!mpegProfile.ticks) return;
    const MpegProfileWindow& p = mpegProfile;
    const mpeg::AdvanceDiagnostics& a = p.peak;
    const mpeg::DecodeDiagnostics& d = a.pictures;
    pspLog("mpeg: ticks=%u audio_avg_us=%u video_rgb_avg_us=%u decode_avg_us=%u rgb_avg_us=%u max_us=%u skipped_b=%u audio_frames=%u slots=%u rgb_frames=%u skipped_delta=%u\n"
           "mpeg_peak: at_us=%u tick=%llu audio_us=%u video_us=%u audio_frames=%u decode_us=%u rgb_us=%u calls=%u slots=%u output=%u caller_reduce=%u budget_reduce=%u wrote_rgb=%u I=%u/%u/%u P=%u/%u/%u B=%u/%u/%u other=%u/%u/%u skip=%u/%u/%u (attempts/total_us/max_us)",
           p.ticks,(unsigned)(p.audioMicros/p.ticks),(unsigned)(p.videoMicros/p.ticks),
           (unsigned)(p.decodeMicros/p.ticks),(unsigned)(p.rgbMicros/p.ticks),p.maxMicros,p.skippedCumulative,
           p.audioFrames,p.slots,p.rgbFrames,p.skipped,p.peakAt,(unsigned long long)p.peakTick,
           p.peakAudioMicros,p.peakVideoMicros,p.peakAudioFrames,a.decodeMicros,a.rgbMicros,a.decodeCalls,a.slotsAdvanced,
           a.outputRequested?1u:0u,a.callerReduce?1u:0u,a.budgetReduce?1u:0u,a.wroteRgb?1u:0u,
           d.pictureAttempts[1],d.pictureMicros[1],d.pictureMaxMicros[1],
           d.pictureAttempts[2],d.pictureMicros[2],d.pictureMaxMicros[2],
           d.pictureAttempts[3],d.pictureMicros[3],d.pictureMaxMicros[3],
           d.pictureAttempts[0],d.pictureMicros[0],d.pictureMaxMicros[0],
           d.skippedB,d.skipMicros,d.skipMaxMicros);
    mpegProfile = MpegProfileWindow();
}

bool Emulator::startVideo(const std::string& name) {
    // A short clip must never share its partial window with the next clip.
    logMpegProfile();
    loadProgress.report("Opening video",name);
    pspLog("startVideo: opening '%s'", name.c_str());
    std::string path;
    if (!ContentLoader::findContentFile(filename, name, &path)) {
        pspLog("startVideo: not found '%s'", name.c_str());
        return false;
    }

    audio.stopAll();cutsceneAudio.reset();
    std::vector<float>().swap(cutsceneAudioFrame.interleaved);
    loadProgress.report("Preparing video",path);
    auto videoSource=mpeg::openProgramStream(path,mpeg::PACKET_VIDEO_1);
    if(!videoSource) {pspLog("startVideo: cannot open video stream '%s'",path.c_str());return false;}
    std::unique_ptr<mpeg::VideoPlayer> player(new mpeg::VideoPlayer(mpeg::Buffer(videoSource)));
    if(!player->valid()){pspLog("startVideo: invalid streaming video header");return false;}
    auto audioSource=mpeg::openProgramStream(path,mpeg::PACKET_AUDIO_1);
    if(audioSource) {
        cutsceneAudio.reset(new mpeg::Audio(mpeg::Buffer(audioSource)));
        if(!cutsceneAudio->hasHeader())cutsceneAudio.reset();
    }
    size_t outputW, outputH;
    mpeg::videoOutputSize(player->width(), player->height(), &outputW, &outputH);
    pspLog("startVideo: file streaming, bounded compressed buffers, audio=%uHz source=%ux%u rgb=%ux%u",
           cutsceneAudio?(unsigned)cutsceneAudio->sampleRate():0u,
           (unsigned)player->width(), (unsigned)player->height(),
           (unsigned)outputW, (unsigned)outputH);
    pspLog("startVideo: playing '%s'", name.c_str());
    videoPlayer = std::move(player);
    hasActiveVideo = true;
    activeVideoName = name;
    return true;
}

void Emulator::loadMenuImage(const std::string& spec) {
    std::vector<std::string> parts = splitPlus(spec);
    if (parts.size() < 3) {
        return;
    }
    std::string spriteName = parts[0];
    DatImage which = datImageFromFlag(parts[1]);
    std::string datPath = parts[2];
    if (pathExtensionLower(datPath) != "dat") {
        datPath += ".dat";
    }
    std::string resolved;
    if (!ContentLoader::findContentFile(filename, datPath, &resolved)) {
        return;
    }
    std::vector<u8> data;
    RgbaImage image;
    if (readWholeFile(resolved, &data) && decodeDatImage(data, reader.colorspace, which, &image)) {
        renderer.setSpriteOverride(spriteName, image);
    }
}

}
