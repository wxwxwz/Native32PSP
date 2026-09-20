// Synthetic game-loop regression. No commercial game data is required.
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
// Only expose Emulator scratch and its movie pass, after dependency headers.
#define private public
#include "core/emulator.h"
#undef private
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <tuple>

static size_t allocations = 0;
#ifndef N32_SANITIZE
void* operator new(size_t size) {
    ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
#endif

using namespace n32;
namespace n32 { void pspLog(const char*, ...) {} }

// Independent prior algorithm: names are tracked by a set, then removed in a
// separate pass. Do not share the production membership bookkeeping here.
static void referenceUpdate(SpriteMap& sprites, const std::vector<FrameObject>& objects) {
    std::set<std::string> names;
    for (const FrameObject& obj : objects) {
        if (obj.type != ObjectMovie || !obj.hasName) continue;
        if (sprites.find(obj.name) != sprites.end()) {
            names.insert(obj.name);
            continue;
        }
        std::string renamed;
        bool found = false;
        for (const auto& item : sprites) {
            const MovieState& movie = item.second;
            if (!movie.cloned && movie.movie == obj.index && movie.depth == obj.depth) {
                renamed = item.first;
                found = true;
                break;
            }
        }
        if (found) names.insert(renamed);
        else {
            sprites[obj.name] = MovieState(obj.index, obj.x, obj.y, obj.depth);
            names.insert(obj.name);
        }
    }
    std::vector<std::string> removed;
    for (const auto& item : sprites)
        if (!item.second.cloned && names.find(item.first) == names.end()) removed.push_back(item.first);
    for (const auto& name : removed) sprites.erase(name);
}

static bool sameState(const MovieState& a, const MovieState& b) {
    return std::tie(a.movie,a.x,a.y,a.depth,a.frame,a.visible,a.playing,a.cloned,
                    a.hasSoundChannel,a.soundChannel,a.hasNextFrame,a.nextFrame) ==
           std::tie(b.movie,b.x,b.y,b.depth,b.frame,b.visible,b.playing,b.cloned,
                    b.hasSoundChannel,b.soundChannel,b.hasNextFrame,b.nextFrame);
}
static void sameSprites(const SpriteMap& a, const SpriteMap& b) {
    assert(a.size() == b.size());
    auto x = a.begin(), y = b.begin();
    for (; x != a.end(); ++x, ++y) {
        assert(x->first == y->first);
        assert(sameState(x->second, y->second));
    }
}
static FrameObject object(const std::string& name, u16 index, u16 depth) {
    FrameObject out;
    out.type=ObjectMovie;out.hasName=true;out.name=name;out.index=index;out.depth=depth;
    out.x=(s16)(index*3);out.y=(s16)(depth*2);
    return out;
}
static std::string longName(unsigned index) {
    char number[16];std::snprintf(number,sizeof(number),"%04u",index);
    return std::string(number)+"_long_native32_sprite_name_exceeding_small_string_storage";
}
static void apply(SpriteSystem& fast, SpriteMap& old, const std::vector<FrameObject>& objects) {
    fast.updateForFrame(objects);referenceUpdate(old,objects);sameSprites(fast.sprites,old);
}

static void testTimelineMembership() {
    SpriteSystem fast;SpriteMap old;
    std::vector<FrameObject> frame={object("first",1,2),object("second",2,3)};
    apply(fast,old,frame);
    fast.sprites["first"].frame=old["first"].frame=17;
    fast.sprites["first"].playing=old["first"].playing=false;
    frame[0].index=9;frame[0].depth=99;
    apply(fast,old,frame); // Exact name retains state despite changed movie/depth.
    assert(fast.sprites["first"].movie==1 && fast.sprites["first"].frame==17);

    MovieState renamed=fast.sprites["second"];
    fast.sprites.erase("second");old.erase("second");
    fast.insert("renamed",renamed);old["renamed"]=renamed;
    apply(fast,old,frame);
    assert(fast.contains("renamed") && !fast.contains("second"));
    fast.insert("a_renamed",renamed);old["a_renamed"]=renamed;
    apply(fast,old,{object("second",2,3)});
    assert(fast.contains("a_renamed") && !fast.contains("renamed")); // First map match.

    MovieState clone(2,7,8,3);clone.cloned=true;
    fast.insert("clone",clone);old["clone"]=clone;
    apply(fast,old,{});assert(fast.sprites.size()==1 && fast.contains("clone"));
    apply(fast,old,{object("new",2,3)});
    assert(fast.contains("new") && fast.contains("clone")); // Clones do not satisfy rename fallback.
    apply(fast,old,{object("clone",77,88)});
    assert(fast.sprites.size()==1 && fast.get("clone")->movie==2); // Exact clone name does.

    fast.clear();old.clear();
    apply(fast,old,{object("a",3,4),object("a",9,8),object("b",3,4)});
    assert(fast.sprites.size()==1 && fast.get("a")->movie==3);
    FrameObject ignored=object("ignored",4,5);ignored.hasName=false;
    FrameObject image=object("image",4,5);image.type=ObjectImage;
    apply(fast,old,{ignored,image});assert(fast.sprites.empty());

    // Mix additions, deletions, renames, collisions, clones and duplicate frame
    // objects. Compare every public movie field after each independent update.
    u32 random=0x31535052;
    auto next=[&random]() {random=random*1664525u+1013904223u;return random;};
    for (unsigned turn=0;turn<1000;++turn) {
        std::string from=longName(next()%20),to=longName(next()%20);
        switch (next()%3) {
        case 0: fast.sprites.erase(from);old.erase(from);break;
        case 1: {
            auto it=old.find(from);
            if(it!=old.end()) {MovieState state=it->second;old.erase(from);old[to]=state;
                fast.sprites.erase(from);fast.insert(to,state);}
            break;
        }
        default: {
            MovieState state(next()%6,(s16)next(),(s16)next(),(u16)(next()%5));
            state.cloned=next()%2;state.frame=next()%9;state.playing=next()%2;
            state.visible=next()%2;state.hasNextFrame=next()%2;state.nextFrame=(s32)(next()%9)-1;
            state.hasSoundChannel=next()%2;state.soundChannel=next()%7;
            fast.insert(from,state);old[from]=state;break;
        }
        }
        frame.clear();
        unsigned count=next()%25;
        for(unsigned i=0;i<count;++i) {
            FrameObject obj=object(longName(next()%20),(u16)(next()%6),(u16)(next()%5));
            obj.hasName=next()%7!=0;if(next()%11==0)obj.type=ObjectImage;
            frame.push_back(obj);
        }
        apply(fast,old,frame);
    }
    puts("PASS: timeline membership matches old set algorithm for names, rename ties, clones, duplicates and 1000 mutation rounds");
}

// The old pass deliberately owns a fresh snapshot. This checks behavior when
// real ActionVM scripts delete/rename/replace map entries during iteration.
static void referenceMoviePass(Emulator& em) {
    std::vector<std::string> names;
    names.reserve(em.sprites.sprites.size());
    for(const auto& item:em.sprites.sprites)names.push_back(item.first);
    for(const std::string& name:names) {
        MovieState* movie=em.sprites.getMutable(name);
        if(!movie || !movie->playing || movie->hasNextFrame)continue;
        const auto* frames=em.reader.getMovieRef(movie->movie);
        if(!frames || frames->empty())continue;
        if(em.tickCount%2==0) {
            movie->hasNextFrame=true;
            movie->nextFrame=movie->frame<frames->size()-1?(s32)movie->frame+1:0;
        }
    }
    for(const std::string& name:names) {
        MovieState* movie=em.sprites.getMutable(name);
        if(!movie || !movie->hasNextFrame)continue;
        const auto* frames=em.reader.getMovieRef(movie->movie);
        if(!frames){movie->hasNextFrame=false;continue;}
        if(movie->nextFrame==-1)movie->frame=0;
        else if(movie->nextFrame>=0 && (size_t)movie->nextFrame<frames->size())movie->frame=(size_t)movie->nextFrame;
        movie->hasNextFrame=false;
        if(movie->frame<frames->size()) {
            MovieFrame mf=(*frames)[movie->frame];
            if(mf.sound) {
                size_t channel=em.audio.playSound(&em.reader,mf.sound,name);
                if(channel) {MovieState* updated=em.sprites.getMutable(name);
                    if(updated){updated->hasSoundChannel=true;updated->soundChannel=channel;}}
            }
            if(mf.action)em.vm.run(&em.reader,&em,mf.action,name);
        }
    }
    for(auto& item:em.sprites.sprites)
        if(item.second.hasSoundChannel && !em.audio.isChannelPlaying(item.second.soundChannel))
            item.second.hasSoundChannel=false;
}

static void put16(std::vector<u8>& bytes,size_t pos,u16 value) {
    assert(pos+2<=bytes.size());bytes[pos]=(u8)value;bytes[pos+1]=(u8)(value>>8);
}
static void put32(std::vector<u8>& bytes,size_t pos,u32 value) {
    put16(bytes,pos,(u16)value);put16(bytes,pos+2,(u16)(value>>16));
}
struct ScriptData {
    std::vector<u8> bytes;
    u16 nextAction;
    size_t nextString;
    ScriptData():bytes(16384,0),nextAction(1),nextString(4096) {}
    void action(Action opcode,const std::string& text=std::string()) {
        size_t pos=1024+(nextAction++-1)*8;
        put32(bytes,pos,(u32)opcode);
        if(!text.empty()) {
            put32(bytes,pos+4,(u32)nextString);
            assert(nextString+text.size()+1<=bytes.size());
            std::memcpy(bytes.data()+nextString,text.c_str(),text.size()+1);
            nextString+=text.size()+1;
        }
    }
    void movie(unsigned index,u16 code,unsigned count=1) {
        size_t start=512+index*64;put32(bytes,128+(index-1)*4,(u32)start);
        for(unsigned frame=0;frame<count;++frame) {
            put16(bytes,start+frame*12,1);put16(bytes,start+frame*12+6,code);
        }
    }
    void bind(Emulator& em) const {em.reader.setData(bytes);em.reader.movieIdx=128;em.reader.actionIdx=1024;}
};
struct ObservedEmulator:Emulator {
    std::vector<std::string> visits;
    void stop(const std::string& target) override {visits.push_back(target);Emulator::stop(target);}
};

static void testMovieSnapshot() {
    const std::string a=longName(1),b=longName(2),c=longName(3),d=longName(4);
    const std::string moved=longName(30),added=longName(40),self=longName(10);
    ScriptData code;
    const u16 mutator=code.nextAction;
    code.action(ActionPush,b);code.action(ActionRemoveSprite);
    code.action(ActionPush,c);code.action(ActionPush,"13");code.action(ActionPush,moved);code.action(ActionSetProperty);
    code.action(ActionPush,d);code.action(ActionPush,added);code.action(ActionPush,"9");code.action(ActionCloneSprite);
    code.action(ActionPush,a);code.action(ActionPush,"13");code.action(ActionPush,self);code.action(ActionSetProperty);
    code.action(ActionStop);code.action(ActionEnd);
    const u16 stopped=code.nextAction;code.action(ActionStop);code.action(ActionEnd);
    code.movie(1,mutator);code.movie(2,stopped);
    ObservedEmulator fast,old;code.bind(fast);code.bind(old);
    for(Emulator* em:{(Emulator*)&fast,(Emulator*)&old}) {
        em->sprites.insert(a,MovieState(1,0,0,1));
        for(const auto& name:{b,c,d})em->sprites.insert(name,MovieState(2,0,0,2));
    }
    for(unsigned tick=1;tick<=6;++tick) {
        fast.tickCount=old.tickCount=tick;
        fast.processMovieFrames();referenceMoviePass(old);
        sameSprites(fast.sprites.sprites,old.sprites.sprites);assert(fast.visits==old.visits);
        assert(fast.vm.vars==old.vm.vars);
        if(tick==1) {
            assert((fast.visits==std::vector<std::string>{a,d}));
            assert(!fast.sprites.contains(a) && !fast.sprites.contains(b) && !fast.sprites.contains(c));
            assert(fast.sprites.get(moved)->hasNextFrame && fast.sprites.get(added)->hasNextFrame);
        }
    }
    // Warm slots beyond the new snapshot length must not act as live names.
    fast.sprites.clear();old.sprites.clear();fast.visits.clear();old.visits.clear();
    fast.sprites.insert(d,MovieState(2,0,0,2));old.sprites.insert(d,MovieState(2,0,0,2));
    fast.processMovieFrames();referenceMoviePass(old);
    assert(fast.visits==old.visits && fast.visits.size()==1);
    sameSprites(fast.sprites.sprites,old.sprites.sprites);
    puts("PASS: real VM deletion, self/peer rename and clone during movie iteration match independently owned snapshots");
}

static void writeScene(const char* path) {
    std::vector<u8> bytes(512,0);std::memcpy(bytes.data(),"_YUVGamemaker 1.3.12",19);
    const u8 header[]={0x0f,0xb4,0x5d,0x2a,0x01,0x7a,0x92,0xe9,
        0x8c,0xa5,0x64,0x33,0x02,0x91,0x26,0xb9,0x8c,0xa5,0x64,0x33,0x02,0x91,0x26,0xb9,
        0x8c,0xa5,0x64,0x33,0x02,0x91,0x26,0xb9};
    std::memcpy(bytes.data()+0x78,header,sizeof(header));
    FILE* file=std::fopen(path,"wb");assert(file);
    assert(std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size());std::fclose(file);
}
static void warmSnapshot(Emulator& em) {
    em.sprites.insert(longName(1),MovieState());em.processMovieFrames();
    assert(em.movieFrameNames.capacity()>0 && em.movieFrameNames[0].capacity()>15);
}
static void testScratchRelease() {
    Emulator em;warmSnapshot(em);em.reset();assert(em.movieFrameNames.capacity()==0);
    warmSnapshot(em);assert(!em.loadFromPath("tests/out/game-loop-does-not-exist.smf",100));
    assert(em.movieFrameNames.capacity()==0);
    const char* first="tests/out/game-loop-start.smf";
    const char* next="tests/out/game-loop-scene.ssl";
    writeScene(first);writeScene(next);
    warmSnapshot(em);assert(em.loadFromPath(first,100));assert(em.movieFrameNames.capacity()==0);
    warmSnapshot(em);assert(em.switchContent("game-loop-scene.ssl"));assert(em.movieFrameNames.capacity()==0);
    std::remove(first);std::remove(next);
    puts("PASS: snapshot storage released on reset, failed/successful load and content switch");
}

template<class Work> static long long measure(Work work,unsigned count,size_t* allocated) {
    const size_t before=allocations;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<count;++i)work();
    const auto stop=std::chrono::steady_clock::now();
    *allocated=allocations-before;
    return std::chrono::duration_cast<std::chrono::microseconds>(stop-start).count();
}
static void testSteadyAllocations() {
    const unsigned count=2000;
    std::vector<FrameObject> frame;
    for(unsigned i=0;i<128;++i)frame.push_back(object(longName(i),(u16)(i+1),(u16)i));
    SpriteSystem fast;SpriteMap old;apply(fast,old,frame);
    size_t oldAlloc,newAlloc;
    const long long oldTime=measure([&](){referenceUpdate(old,frame);},count,&oldAlloc);
    const long long newTime=measure([&](){fast.updateForFrame(frame);},count,&newAlloc);
    sameSprites(fast.sprites,old);
#ifndef N32_SANITIZE
    assert(oldAlloc>=count*128 && newAlloc==0);
#endif
    printf("timeline: sprites=128 long_names updates=%u old_alloc=%zu new_alloc=%zu old_us=%lld new_us=%lld\n",
           count,oldAlloc,newAlloc,oldTime,newTime);

    ScriptData code;code.movie(1,0,3);
    Emulator em,reference;code.bind(em);code.bind(reference);
    for(const auto& obj:frame) {
        MovieState state(1,0,0,obj.depth);state.hasNextFrame=false;
        em.sprites.insert(obj.name,state);reference.sprites.insert(obj.name,state);
    }
    em.tickCount=reference.tickCount=2;em.processMovieFrames();referenceMoviePass(reference);
    const long long oldNames=measure([&](){referenceMoviePass(reference);},count,&oldAlloc);
    const long long newNames=measure([&](){em.processMovieFrames();},count,&newAlloc);
    sameSprites(em.sprites.sprites,reference.sprites.sprites);
#ifndef N32_SANITIZE
    assert(oldAlloc>=count*128 && newAlloc==0);
#endif
    printf("movie_snapshot: sprites=128 ticks=%u old_alloc=%zu new_alloc=%zu old_us=%lld new_us=%lld\n",
           count,oldAlloc,newAlloc,oldNames,newNames);

    const std::string& name=frame.front().name;
    assert(em.getProperty(name,ActionPropTotalFrames)=="3");
    const long long oldProperty=measure([&](){std::vector<MovieFrame> copied;
        em.reader.getMovie(1,&copied);assert(intToString((s64)copied.size())=="3");},count,&oldAlloc);
    const long long newProperty=measure([&](){assert(em.getProperty(name,ActionPropTotalFrames)=="3");},count,&newAlloc);
#ifndef N32_SANITIZE
    assert(oldAlloc==count && newAlloc==0);
#endif
    em.sprites.getMutable(name)->movie=0;assert(em.getProperty(name,ActionPropTotalFrames)=="0");
    em.sprites.getMutable(name)->movie=0xffffffffu;assert(em.getProperty(name,ActionPropTotalFrames)=="0");
    printf("total_frames: queries=%u old_alloc=%zu new_alloc=%zu old_us=%lld new_us=%lld\n",
           count,oldAlloc,newAlloc,oldProperty,newProperty);
    puts("PASS: warm stable timeline/movie snapshots and cached TotalFrames perform zero allocations (normal build)");
}

int main() {
    testTimelineMembership();testMovieSnapshot();testScratchRelease();testSteadyAllocations();
    puts("PASS: game-loop regressions complete; host timings are not PSP frame rates");
}
