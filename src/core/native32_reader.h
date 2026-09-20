#ifndef NATIVE32_READER_H
#define NATIVE32_READER_H

#include "core/native32_types.h"
#include <map>

namespace n32 {

class Native32Reader {
public:
    Native32Reader();
    explicit Native32Reader(const std::vector<u8>& fileData);

    void setData(std::vector<u8> fileData);
    bool init();
    void skipThumbnail();
    bool findHeader();
    bool processHeader();
    bool expandPackedAssets(size_t blockCount, u32 binarySize);

    bool getAction(u32 index, ActionEntry* out);
    // Borrowed until the cache grows or the reader is replaced/reset. Callers
    // must consume the entry before callbacks can perform another lookup.
    const ActionEntry* getActionRef(u32 index);
    bool getActionCached(u32 index, ActionEntry* out) const;
    bool getFrame(u32 frame, std::vector<FrameObject>* out);
    const std::vector<FrameObject>* getFrameRef(u32 frame);
    void getMovie(u32 movie, std::vector<MovieFrame>* out);
    const std::vector<MovieFrame>* getMovieRef(u32 movie);
    bool getImage(u32 index, RgbaImage* out);
    // Reads cached dimensions or the asset header without decoding/caching pixels.
    bool getImageDimensions(u32 index, u32* imageWidth, u32* imageHeight) const;
    const RgbaImage* getImageRef(u32 index, bool* allPixelsVisible = 0,
                                const ImageDrawInfo** drawInfo = 0);
    // References remain valid until another image lookup can evict them.
    size_t imageCacheBytes() const { return cachedImageBytes; }
    size_t imageCacheBudgetBytes() const;
    size_t imageCacheCount() const { return imagesCache.size(); }
    u64 imageCacheEvictions() const { return imageEvictions; }
    u32 imageDecodeCount() const { return decodedImages; }
    u64 imageDecodeTotalMicros() const { return decodeMicros; }
    u32 imageDecodePeakMicros() const { return decodePeakMicros; }
    bool getSound(u32 index, SoundData* out);
    void getButtonEvents(u32 button, std::vector<ButtonEvent>* out);
    void cacheAllActions();

    std::vector<u8> data;
    size_t idx;
    Colorspace colorspace;
    u32 width;
    u32 height;
    size_t base;
    u32 frameIdx;
    u32 imageIdx;
    u32 actionIdx;
    u32 movieIdx;
    u32 buttonIdx;
    u32 buttonCondIdx;
    u32 mp3Offset;
    size_t soundTable;

private:
    struct ActionCacheEntry {
        bool valid;
        ActionEntry entry;
        ActionCacheEntry() : valid(false) {}
    };

    struct ImageCacheEntry {
        RgbaImage image;
        ImageDrawInfo drawInfo;
    };

    std::string getStr(size_t offset) const;
    bool disassembleAction(u32 index, ActionEntry* out) const;

    std::vector<ActionCacheEntry> actionsCache;
    std::map<u32, ImageCacheEntry> imagesCache;
    std::map<u32, u64> imageLastUsed;
    size_t cachedImageBytes;
    u64 imageClock;
    u64 imageEvictions;
    u32 decodedImages = 0, decodePeakMicros = 0;
    u64 decodeMicros = 0;
    std::map<u32, bool> imageValidCache;
    std::map<u32, std::vector<FrameObject> > framesCache;
    std::map<u32, std::vector<MovieFrame> > moviesCache;
    std::map<u32, SoundData> soundCache;
    std::map<u32, std::vector<ButtonEvent> > buttonEventsCache;
};

bool parseResolution(const std::string& generator, u32* width, u32* height);
void endianSwapPcm16(const std::vector<u8>& input, std::vector<u8>* out);

}

#endif
