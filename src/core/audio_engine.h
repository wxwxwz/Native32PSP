#ifndef NATIVE32_AUDIO_ENGINE_H
#define NATIVE32_AUDIO_ENGINE_H

#include "core/native32_reader.h"
#include "core/mp3_software.h"

namespace n32 {

class AudioEngine {
public:
    AudioEngine();
    AudioEngine(Colorspace colorspaceValue, u32 volumeValue);

    std::vector<s16> getPendingSamples();
    // Advances audio by one tick, retaining the caller's output capacity.
    void getPendingSamples(std::vector<s16>* output);
    void startTone();
    void stopTone();
    u32 outputSampleRate() const;
    size_t playSound(Native32Reader* reader, u16 soundValue, const std::string& movieName);
    size_t playRaw(const std::vector<u8>& data, u8 repeat, const std::string& movieName);
    size_t playMp3(const std::vector<u8>& data, u8 repeat, const std::string& movieName);
    size_t playPcmStream(const std::vector<float>& samples, u16 channels, u32 sampleRate);
    // Append a block of PCM to the cutscene channel, creating it when `create`
    // is set. Lets a long track be decoded in pieces instead of all at once.
    void appendPcmStream(const std::vector<float>& samples, u16 channels, u32 sampleRate, bool create);
    size_t pendingStreamFrames() const;
    void stopAll();
    size_t retainedAudioBytes() const;
    void stopForMovie(const std::string& movieName);
    bool isChannelPlaying(size_t channelId) const;
    bool isPlaying() const;
    void setVolume(u32 volumeValue);

    float volume;
    Colorspace colorspace;
    size_t nextChannelId;
    u32 sampleFrameRemainder;
    double tonePhase;
    bool toneActive;

private:
    struct PlaybackChannel {
        size_t id;
        std::string owner;
        std::vector<s16> samples;
        std::unique_ptr<SoftwareMp3Stream> mp3;
        size_t position;
        bool infiniteLoops;
        u32 loopsRemaining;
        bool isMusic;
        bool finished;

        PlaybackChannel()
            : id(0), position(0), infiniteLoops(false), loopsRemaining(0), isMusic(false), finished(false) {}
    };

    size_t allocateChannelId();
    size_t playSoftwareMp3(const std::vector<u8>& data,u8 repeat,const std::string& owner);
    size_t addChannel(std::vector<s16> samples, u8 repeat, const std::string& owner, bool isMusic);
    bool nextFrame(PlaybackChannel* channel, s16* left, s16* right);

    std::vector<PlaybackChannel> channels;
    std::vector<s32> mixBuffer;
    std::vector<s16> streamScratch;
};

std::vector<s16> rawPcmToStereo(const std::vector<u8>& data);
void resampleToStereo(const std::vector<float>& samples, size_t channels, u32 inputRate, u32 outputRate, std::vector<s16>* out);
std::vector<s16> resampleToStereo(const std::vector<float>& samples, size_t channels, u32 inputRate, u32 outputRate);

}

#endif
