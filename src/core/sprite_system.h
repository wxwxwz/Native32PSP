#ifndef NATIVE32_SPRITE_SYSTEM_H
#define NATIVE32_SPRITE_SYSTEM_H

#include "core/native32_types.h"
#include <map>
#include <set>

namespace n32 {

struct MovieState {
    u32 movie;
    s16 x;
    s16 y;
    u16 depth;
    size_t frame;
    bool visible;
    bool playing;
    bool cloned;
    bool hasSoundChannel;
    size_t soundChannel;
    bool hasNextFrame;
    s32 nextFrame;

    MovieState();
    MovieState(u32 movieValue, s16 xValue, s16 yValue, u16 depthValue);
};

typedef std::map<std::string, MovieState> SpriteMap;

class SpriteSystem {
public:
    SpriteSystem();

    void updateForFrame(const std::vector<FrameObject>& frameObjects);
    void tick(u64 tickCount);

    const MovieState* get(const std::string& name) const;
    MovieState* getMutable(const std::string& name);
    bool remove(const std::string& name, MovieState* out);
    void insert(const std::string& name, const MovieState& state);
    bool contains(const std::string& name) const;
    void clear();

    SpriteMap sprites;
};

}

#endif
