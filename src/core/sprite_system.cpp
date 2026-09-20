#include "core/sprite_system.h"

namespace n32 {

MovieState::MovieState()
    : movie(0), x(0), y(0), depth(0), frame(0), visible(true), playing(true),
      cloned(false), hasSoundChannel(false), soundChannel(0), hasNextFrame(true), nextFrame(0),
      timelinePresent(false) {
}

MovieState::MovieState(u32 movieValue, s16 xValue, s16 yValue, u16 depthValue)
    : movie(movieValue), x(xValue), y(yValue), depth(depthValue), frame(0), visible(true),
      playing(true), cloned(false), hasSoundChannel(false), soundChannel(0),
      hasNextFrame(true), nextFrame(0), timelinePresent(false) {
}

SpriteSystem::SpriteSystem() {
}

void SpriteSystem::updateForFrame(const std::vector<FrameObject>& frameObjects) {
    for (SpriteMap::iterator it = sprites.begin(); it != sprites.end(); ++it)
        it->second.timelinePresent = false;

    for (size_t i = 0; i < frameObjects.size(); ++i) {
        const FrameObject& obj = frameObjects[i];
        if (obj.type != ObjectMovie || !obj.hasName) {
            continue;
        }

        SpriteMap::iterator named = sprites.find(obj.name);
        if (named != sprites.end()) {
            named->second.timelinePresent = true;
            continue;
        }

        SpriteMap::iterator renamed = sprites.end();
        for (SpriteMap::iterator it = sprites.begin(); it != sprites.end(); ++it) {
            const MovieState& movie = it->second;
            if (!movie.cloned && movie.movie == obj.index && movie.depth == obj.depth) {
                renamed = it;
                break;
            }
        }

        if (renamed != sprites.end()) {
            renamed->second.timelinePresent = true;
        } else {
            MovieState state(obj.index, obj.x, obj.y, obj.depth);
            state.hasNextFrame = true;
            state.nextFrame = 0;
            state.timelinePresent = true;
            sprites[obj.name] = state;
        }
    }

    for (SpriteMap::iterator it = sprites.begin(); it != sprites.end();) {
        if (!it->second.cloned && !it->second.timelinePresent)
            it = sprites.erase(it);
        else
            ++it;
    }
}

void SpriteSystem::tick(u64 tickCount) {
    for (SpriteMap::iterator it = sprites.begin(); it != sprites.end(); ++it) {
        MovieState& movie = it->second;
        if (!movie.playing || movie.hasNextFrame) {
            continue;
        }
        if ((tickCount % 2) == 0) {
            movie.hasNextFrame = true;
            movie.nextFrame = (s32)movie.frame + 1;
        }
    }
}

const MovieState* SpriteSystem::get(const std::string& name) const {
    SpriteMap::const_iterator it = sprites.find(name);
    return it == sprites.end() ? 0 : &it->second;
}

MovieState* SpriteSystem::getMutable(const std::string& name) {
    SpriteMap::iterator it = sprites.find(name);
    return it == sprites.end() ? 0 : &it->second;
}

bool SpriteSystem::remove(const std::string& name, MovieState* out) {
    SpriteMap::iterator it = sprites.find(name);
    if (it == sprites.end()) {
        return false;
    }
    if (out) {
        *out = it->second;
    }
    sprites.erase(it);
    return true;
}

void SpriteSystem::insert(const std::string& name, const MovieState& state) {
    sprites[name] = state;
}

bool SpriteSystem::contains(const std::string& name) const {
    return sprites.find(name) != sprites.end();
}

void SpriteSystem::clear() {
    sprites.clear();
}

}
