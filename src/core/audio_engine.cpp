#include "core/mp3_software.h"
#include "core/audio_engine.h"
#include "core/endian.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#if defined(PSP) || defined(N32_TEST_PSP_MP3)
#include "platform/psp_log.h"
#include <stdint.h>
#ifdef PSP
#include <pspiofilemgr.h> // Defines SceOff required by the SDK's pspmp3.h.
#endif
#include <pspmp3.h>
#endif

namespace n32 {

static const size_t MAX_SOUND_EFFECTS = 8;

static s16 clampI16(s32 value) {
    if (value < -32768) {
        return -32768;
    }
    if (value > 32767) {
        return 32767;
    }
    return (s16)value;
}

AudioEngine::AudioEngine()
    : volume(1.0f), colorspace(ColorspaceYuv), nextChannelId(1), sampleFrameRemainder(0),
      tonePhase(0.0), toneActive(false) {
}

AudioEngine::AudioEngine(Colorspace colorspaceValue, u32 volumeValue)
    : volume((float)volumeValue / 100.0f), colorspace(colorspaceValue), nextChannelId(1),
      sampleFrameRemainder(0), tonePhase(0.0), toneActive(false) {
}

u32 AudioEngine::outputSampleRate() const {
    return colorspace == ColorspaceYuv ? 11025 : 22050;
}

size_t AudioEngine::allocateChannelId() {
    size_t id = nextChannelId;
    ++nextChannelId;
    if (nextChannelId == 0) {
        nextChannelId = 1;
    }
    return id == 0 ? 1 : id;
}

bool AudioEngine::nextFrame(PlaybackChannel* channel, s16* left, s16* right) {
    if (!channel || channel->finished || (channel->samples.empty() && !channel->mp3)) {
        if (channel) {
            channel->finished = true;
        }
        return false;
    }
    while (channel->position * 2 >= channel->samples.size()) {
        if(channel->mp3 && channel->mp3->readBlock(&channel->samples)) {channel->position=0;break;}
        if (channel->infiniteLoops) {
            if(channel->mp3)channel->mp3->rewind();
            channel->position = 0;
        } else if (channel->loopsRemaining > 0) {
            --channel->loopsRemaining;
            if(channel->mp3)channel->mp3->rewind();
            channel->position = 0;
        } else {
            channel->finished = true;
            return false;
        }
        if(channel->mp3) {
            if(!channel->mp3->readBlock(&channel->samples)) {channel->finished=true;return false;}
            break;
        }
    }
    size_t offset = channel->position * 2;
    ++channel->position;
    *left = channel->samples[offset];
    *right = channel->samples[offset + 1];
    return true;
}

std::vector<s16> AudioEngine::getPendingSamples() {
    std::vector<s16> out;
    getPendingSamples(&out);
    return out;
}

void AudioEngine::getPendingSamples(std::vector<s16>* output) {
    if (!output) {
        return;
    }
    u32 sampleRate = outputSampleRate();
    sampleFrameRemainder += sampleRate;
    size_t frames = sampleFrameRemainder / 30;
    sampleFrameRemainder %= 30;

    mixBuffer.resize(frames * 2);
    std::fill(mixBuffer.begin(), mixBuffer.end(), 0);
    std::vector<s32>& mixed = mixBuffer;
    for (size_t c = 0; c < channels.size(); ++c) {
        for (size_t frame = 0; frame < frames; ++frame) {
            s16 left = 0;
            s16 right = 0;
            if (!nextFrame(&channels[c], &left, &right)) {
                break;
            }
            mixed[frame * 2] += left;
            mixed[frame * 2 + 1] += right;
        }
    }

    // Compact in place, preserving capacity and PCM ownership between ticks.
    channels.erase(std::remove_if(channels.begin(), channels.end(),
        [](const PlaybackChannel& channel) { return channel.finished; }), channels.end());

    output->resize(frames * 2);
    std::vector<s16>& out = *output;
    for (size_t i = 0; i < mixed.size(); ++i) {
        out[i] = clampI16((s32)(mixed[i] * volume));
    }

    if (toneActive) {
        for (size_t i = 0; i < frames; ++i) {
            double sample = sin(tonePhase * 2.0 * 3.14159265358979323846 * 440.0 / (double)sampleRate);
            s16 sampleI16 = (s16)(sample * 16000.0 * (double)volume);
            out[i * 2] = clampI16((s32)out[i * 2] + sampleI16);
            out[i * 2 + 1] = clampI16((s32)out[i * 2 + 1] + sampleI16);
            tonePhase += 1.0;
            if (tonePhase >= (double)sampleRate) {
                tonePhase -= (double)sampleRate;
            }
        }
    }

}

void AudioEngine::startTone() {
    toneActive = true;
    tonePhase = 0.0;
}

void AudioEngine::stopTone() {
    toneActive = false;
}

size_t AudioEngine::addChannel(std::vector<s16> samples, u8 repeat, const std::string& owner, bool isMusic) {
    if (samples.empty()) {
        return 0;
    }

    size_t effects = 0;
    if (!isMusic) {
        for (size_t i = 0; i < channels.size(); ++i) {
            if (!channels[i].finished && !channels[i].isMusic) {
                ++effects;
            }
        }
        if (effects >= MAX_SOUND_EFFECTS) {
            return 0;
        }
    }

    channels.erase(std::remove_if(channels.begin(), channels.end(),
        [isMusic](const PlaybackChannel& channel) {
            return channel.finished || (isMusic && channel.isMusic);
        }), channels.end());

    PlaybackChannel channel;
    channel.id = allocateChannelId();
    channel.owner = owner;
    channel.samples = std::move(samples);
    channel.position = 0;
    channel.infiniteLoops = repeat == 0xff;
    channel.loopsRemaining = repeat == 0xff ? 0 : repeat;
    channel.isMusic = isMusic;
    channel.finished = false;
    size_t channelId = channel.id;
    channels.push_back(std::move(channel));
    toneActive = false;
    return channelId;
}

size_t AudioEngine::playSound(Native32Reader* reader, u16 soundValue, const std::string& movieName) {
    if (!reader) {
        return 0;
    }
    u8 repeat = (u8)((soundValue >> 8) & 0xff);
    u32 index = soundValue & 0xff;
    if (index == 0) {
        return 0;
    }
    SoundData sound;
    if (!reader->getSound(index, &sound)) {
        return 0;
    }
    return sound.format == AudioMp3 ? playMp3(sound.data, repeat, movieName) : playRaw(sound.data, repeat, movieName);
}

size_t AudioEngine::playRaw(const std::vector<u8>& data, u8 repeat, const std::string& movieName) {
    return addChannel(rawPcmToStereo(data), repeat, movieName, false);
}

size_t AudioEngine::playSoftwareMp3(const std::vector<u8>& data,u8 repeat,const std::string& owner) {
    std::unique_ptr<SoftwareMp3Stream> stream(new SoftwareMp3Stream(data,outputSampleRate()));
    std::vector<s16> first;
    if(!stream->readBlock(&first))return 0;
    size_t id=addChannel(std::move(first),repeat,owner,true);
    channels.back().mp3=std::move(stream);
    return id;
}
size_t AudioEngine::retainedAudioBytes() const {
    size_t bytes=0;
    for(const auto& channel:channels) bytes+=channel.samples.capacity()*sizeof(s16)+(channel.mp3?channel.mp3->retainedBytes():0);
    return bytes;
}

size_t AudioEngine::playMp3(const std::vector<u8>& data, u8 repeat, const std::string& movieName) {
#if defined(PSP) || defined(N32_TEST_PSP_MP3)
    if (data.empty()) return 0;
    auto softwareFallback=[&]() -> size_t {
        const size_t id=playSoftwareMp3(data,repeat,movieName);
        pspLog("mp3: streaming input=%u output=%u retained_kb=%u channel=%u",(unsigned)data.size(),(unsigned)outputSampleRate(),(unsigned)(retainedAudioBytes()/1024),(unsigned)id);
        return id;
    };
    // These native-game tracks were confirmed as MPEG-2/2.5 Layer III.
    // Avoid the PSP initialization rejection/delay on a valid low-rate header.
    if(data.size()>=4 && data[0]==0xff && (data[1]&0xe0)==0xe0 &&
       (data[1]&6)==2 && ((data[1]>>3)&3)!=3 && ((data[1]>>3)&3)!=1 &&
       (data[2]>>4)>0 && (data[2]>>4)<15 && ((data[2]>>2)&3)!=3)
        return softwareFallback();
    SceInt32 result=sceMp3InitResource();
    if (result<0) { pspLog("mp3: resource failed %08x",(unsigned)result); return softwareFallback(); }
    // Aligned working buffers; keep only final mixer-rate PCM, not multiple
    // full-rate integer/float copies of the complete background track.
    const size_t streamSize=64*1024+1472, pcmSize=16*1024;
    std::vector<u8> streamStorage(streamSize+63), pcmStorage(pcmSize+63);
    SceMp3InitArg args; memset(&args,0,sizeof(args));
    args.mp3StreamEnd=(SceOff)data.size();
    args.mp3Buf=(u8*)(((uintptr_t)streamStorage.data()+63)&~(uintptr_t)63);
    args.mp3BufSize=streamSize;
    args.pcmBuf=(u8*)(((uintptr_t)pcmStorage.data()+63)&~(uintptr_t)63);
    args.pcmBufSize=pcmSize;
    SceInt32 handle=sceMp3ReserveMp3Handle(&args);
    if(handle<0) { pspLog("mp3: reserve failed %08x",(unsigned)handle); sceMp3TermResource(); return softwareFallback(); }
    bool exhausted=false;
    auto feed=[&]() -> bool {
        SceUChar8* dst=0; SceInt32 space=0,pos=0;
        SceInt32 status=sceMp3GetInfoToAddStreamData(handle,&dst,&space,&pos);
        if(status<0 || pos<0 || space<0) {
            pspLog("mp3: feed info failed %08x pos=%d space=%d",(unsigned)status,(int)pos,(int)space); return false;
        }
        if((size_t)pos>=data.size()) { exhausted=true; return true; }
        if(space==0) return true; // A full buffer must be decoded, not treated as EOF.
        if(!dst) return false;
        size_t count=std::min((size_t)space,data.size()-(size_t)pos);
        memcpy(dst,data.data()+pos,count);
        status=sceMp3NotifyAddStreamData(handle,(SceInt32)count);
        if(status<0) { pspLog("mp3: notify failed %08x",(unsigned)status); return false; }
        if((size_t)pos+count==data.size()) exhausted=true;
        return true;
    };
    bool good=feed();
    if(good) { result=sceMp3Init(handle); good=result>=0;
        if(!good) pspLog("mp3: init failed %08x bytes=%u",(unsigned)result,(unsigned)data.size()); }
    if(good) { result=sceMp3SetLoopNum(handle,0); good=result>=0;
        if(!good) pspLog("mp3: loop setup failed %08x",(unsigned)result); }
    const SceInt32 rate=good?sceMp3GetSamplingRate(handle):0;
    const SceInt32 channelsValue=good?sceMp3GetMp3ChannelNum(handle):0;
    good=good && rate>0 && rate<=48000 && (channelsValue==1 || channelsValue==2);
    std::vector<s16> decoded;
    const u32 outRate=outputSampleRate();
    u64 sourceBoundary=0,nextOutput=0;
    s16 previousLeft=0,previousRight=0;
    const size_t maxPcmSamples=4*1024*1024; // 8 MiB at mixer rate.
    while(good) {
        SceInt32 needed=sceMp3CheckStreamDataNeeded(handle);
        if(needed<0) { pspLog("mp3: stream check failed %08x",(unsigned)needed); good=false; break; }
        if(needed>0 && !exhausted && !feed()) { good=false; break; }
        // Always drain buffered frames even after every input byte was supplied.
        SceShort16* pcm=0;
        SceInt32 bytes=sceMp3Decode(handle,&pcm);
        if(bytes==0) break;
        if(bytes<0 || !pcm || bytes>(SceInt32)pcmSize || bytes%(2*channelsValue)) {
            pspLog("mp3: decode failed %08x eof=%d samples=%u",(unsigned)bytes,exhausted?1:0,(unsigned)decoded.size());
            good=false; break;
        }
        size_t frames=(size_t)bytes/(2*channelsValue);
        for(size_t i=0;i<frames && good;++i) {
            const s16 left=pcm[i*channelsValue], right=pcm[i*channelsValue+(channelsValue==2?1:0)];
            while(nextOutput<=sourceBoundary) {
                if(decoded.size()+2>maxPcmSamples) { pspLog("mp3: PCM exceeds 8 MiB limit"); good=false; break; }
                const s32 fraction=sourceBoundary ? (s32)(nextOutput-(sourceBoundary-outRate)) : (s32)outRate;
                const s32 remainder=(s32)outRate-fraction;
                decoded.push_back((s16)(((s32)previousLeft*remainder+(s32)left*fraction)/(s32)outRate));
                decoded.push_back((s16)(((s32)previousRight*remainder+(s32)right*fraction)/(s32)outRate));
                nextOutput+=(u32)rate;
            }
            previousLeft=left; previousRight=right; sourceBoundary+=outRate;
        }
    }
    // Final fractional outputs repeat the last sample, matching the mixer resampler.
    while(good && nextOutput<sourceBoundary) {
        if(decoded.size()+2>maxPcmSamples) { good=false; break; }
        decoded.push_back(previousLeft); decoded.push_back(previousRight); nextOutput+=(u32)rate;
    }
    sceMp3ReleaseMp3Handle(handle); sceMp3TermResource();
    if(!good || decoded.empty()) { pspLog("mp3: hardware unavailable rate=%d channels=%d",(int)rate,(int)channelsValue); std::vector<s16>().swap(decoded); return softwareFallback(); }
    pspLog("mp3: ready input=%u rate=%d channels=%d output=%u frames=%u repeat=%u",
        (unsigned)data.size(),(int)rate,(int)channelsValue,(unsigned)outRate,(unsigned)(decoded.size()/2),(unsigned)repeat);
    return addChannel(std::move(decoded),repeat,movieName,true);
#else
    return playSoftwareMp3(data,repeat,movieName);
#endif
}

size_t AudioEngine::playPcmStream(const std::vector<float>& samples, u16 channelsValue, u32 sampleRate) {
    return addChannel(resampleToStereo(samples, channelsValue, sampleRate, outputSampleRate()), 0, "__cutscene__", true);
}

void AudioEngine::appendPcmStream(const std::vector<float>& samples, u16 channelsValue,
                                  u32 sampleRate, bool create) {
    resampleToStereo(samples, channelsValue, sampleRate, outputSampleRate(), &streamScratch);
    std::vector<s16>& block=streamScratch;
    if (block.empty()) {
        return;
    }
    if (create) {
        addChannel(std::move(block), 0, "__cutscene__", true);
        return;
    }
    for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i].owner == "__cutscene__" && !channels[i].finished) {
            // Streaming must not retain already-played PCM for the full movie.
            if (channels[i].position >= 4096) {
                size_t consumed = std::min(channels[i].position * 2, channels[i].samples.size());
                channels[i].samples.erase(channels[i].samples.begin(), channels[i].samples.begin() + consumed);
                channels[i].position = 0;
            }
            channels[i].samples.insert(channels[i].samples.end(), block.begin(), block.end());
            return;
        }
    }
    // The channel went away (stopped or already drained); start a fresh one.
    addChannel(std::move(block), 0, "__cutscene__", true);
}

void AudioEngine::stopAll() {
    channels.clear();
    std::vector<s16>().swap(streamScratch);
    toneActive = false;
}

size_t AudioEngine::pendingStreamFrames() const {
    for (size_t i = 0; i < channels.size(); ++i) {
        const PlaybackChannel& channel = channels[i];
        if (channel.owner == "__cutscene__" && !channel.finished)
            return channel.samples.size() / 2 - std::min(channel.position, channel.samples.size() / 2);
    }
    return 0;
}

void AudioEngine::stopForMovie(const std::string& movieName) {
    channels.erase(std::remove_if(channels.begin(), channels.end(),
        [&movieName](const PlaybackChannel& channel) { return channel.owner == movieName; }),
        channels.end());
}

bool AudioEngine::isChannelPlaying(size_t channelId) const {
    for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i].id == channelId && !channels[i].finished) {
            return true;
        }
    }
    return false;
}

bool AudioEngine::isPlaying() const {
    if (toneActive) {
        return true;
    }
    for (size_t i = 0; i < channels.size(); ++i) {
        if (!channels[i].finished) {
            return true;
        }
    }
    return false;
}

void AudioEngine::setVolume(u32 volumeValue) {
    volume = (float)volumeValue / 100.0f;
}

std::vector<s16> rawPcmToStereo(const std::vector<u8>& data) {
    std::vector<s16> out;
    out.reserve(data.size());
    for (size_t i = 0; i + 1 < data.size(); i += 2) {
        s16 sample = (s16)read_u16_le(&data[0], i);
        out.push_back(sample);
        out.push_back(sample);
    }
    return out;
}

std::vector<s16> resampleToStereo(const std::vector<float>& samples, size_t channels, u32 inputRate, u32 outputRate) {
    std::vector<s16> out;
    resampleToStereo(samples,channels,inputRate,outputRate,&out);
    return out;
}
void resampleToStereo(const std::vector<float>& samples,size_t channels,u32 inputRate,u32 outputRate,std::vector<s16>* result) {
    if(!result)return;
    result->clear();std::vector<s16>& out=*result;
    if (samples.empty() || channels == 0 || inputRate == 0 || outputRate == 0) {
        return;
    }
    size_t inputFrames = samples.size() / channels;
    if (inputFrames == 0) {
        return;
    }
    size_t outputFrames = (size_t)(((u64)inputFrames * outputRate + inputRate - 1) / inputRate);
    out.reserve(outputFrames * 2);
    // Exact quotient/remainder stepping replaces the two 64-bit divisions per
    // output frame. Keep the same float division for bit-identical interpolation.
    const size_t wholeStep = inputRate / outputRate;
    const u32 fractionalStep = inputRate % outputRate;
    size_t sourceFrame = 0;
    u64 phase = 0;
    for (size_t frame = 0; frame < outputFrames; ++frame) {
        size_t nextFrame = std::min(sourceFrame + 1, inputFrames - 1);
        float fraction = (float)phase / (float)outputRate;
        for (size_t ch = 0; ch < 2; ++ch) {
            size_t sourceChannel = std::min(ch, channels - 1);
            float first = samples[std::min(sourceFrame, inputFrames - 1) * channels + sourceChannel];
            float second = samples[nextFrame * channels + sourceChannel];
            float sample = first + (second - first) * fraction;
            out.push_back(clampI16((s32)(sample * 32767.0f)));
        }
        sourceFrame += wholeStep;
        phase += fractionalStep;
        if (phase >= outputRate) {
            phase -= outputRate;
            ++sourceFrame;
        }
    }
    return;
}

}
