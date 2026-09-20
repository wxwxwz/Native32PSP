#include "core/action_vm.h"
#include "core/audio_engine.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace n32;

static u32 fakeNow = 0, clockStep = 0, clockReads = 0;
u32 sceKernelGetSystemTimeLow() {
    const u32 result = fakeNow;
    fakeNow += clockStep;
    ++clockReads;
    return result;
}
namespace n32 { void pspLog(const char*, ...) {} }

static u64 timed(u64 micros) {
#ifdef N32_TEST_CORE_PROFILE
    return micros;
#else
    (void)micros;
    return 0;
#endif
}

static u32 reads(u32 count) {
#ifdef N32_TEST_CORE_PROFILE
    return count;
#else
    (void)count;
    return 0;
#endif
}

static void store32(std::vector<u8>* bytes, size_t pos, u32 value) {
    for (size_t i = 0; i < 4; ++i) (*bytes)[pos + i] = (u8)(value >> (i * 8));
}

static u32 appendText(std::vector<u8>* bytes, const char* value) {
    const u32 offset = (u32)bytes->size();
    bytes->insert(bytes->end(), value, value + std::strlen(value) + 1);
    return offset;
}

static void setAction(std::vector<u8>* bytes, u32 index, u32 opcode, u32 payload = 0) {
    store32(bytes, (index - 1) * 8, opcode);
    store32(bytes, (index - 1) * 8 + 4, payload);
}

static Native32Reader actionReader() {
    std::vector<u8> bytes(12 * 8, 0);
    const u32 two = appendText(&bytes, "2");
    const u32 score = appendText(&bytes, "score");
    const u32 seven = appendText(&bytes, "7");
    setAction(&bytes, 1, ActionPush, two);
    setAction(&bytes, 2, ActionCall);
    setAction(&bytes, 3, ActionStop);
    setAction(&bytes, 4, ActionEnd);
    setAction(&bytes, 5, ActionPush, score);
    setAction(&bytes, 6, ActionPush, seven);
    setAction(&bytes, 7, ActionSetVariable);
    setAction(&bytes, 8, ActionStop);
    setAction(&bytes, 9, ActionEnd);
    setAction(&bytes, 10, ActionStop);
    setAction(&bytes, 11, ActionEnd);
    setAction(&bytes, 12, 0xdeadbeefu);
    return Native32Reader(bytes);
}

class Host : public VmHost {
public:
    ActionVM* vm;
    Native32Reader* reader;
    std::string* mutableEntry;
    bool copyDuringCall;
    VmTickProfile copyProfile;

    Host(ActionVM* vmValue, Native32Reader* readerValue)
        : vm(vmValue), reader(readerValue), mutableEntry(0), copyDuringCall(false) {}

    void stop(const std::string& target) override {
        fakeNow += target == "child" ? 70 : 10;
        if (mutableEntry && target != "child") *mutableEntry = "renamed";
    }
    void play(const std::string&) override {}
    u32 getFrame(const std::string&) override { return 0; }
    void gotoFrame(const std::string&, u32, bool) override {}
    void stopSounds(const std::string&) override {}
    void setProperty(const std::string&, ActionProp, const std::string&) override {}
    std::string getProperty(const std::string&, ActionProp) override { return "0"; }
    void cloneSprite(const std::string&, const std::string&, s32) override {}
    void removeSprite(const std::string&) override {}
    void call(u32 frame) override {
        assert(frame == 2);
        fakeNow += 5;
        if (copyDuringCall) {
            // A snapshot preserves script state/profile counters, but must not
            // inherit the source VM's live profile-scope stack pointer.
            ActionVM copy = *vm;
            copy.run(reader, this, 10, "copy");
            copyProfile = copy.profile;
        }
        vm->run(reader, this, 5, "child");
        fakeNow += 15;
    }
    u32 getTime() const override { return fakeNow; }
    void getUrl(const std::string&, const std::string&) override {}
    void runFrameActions(u32) override {}
};

static void assertVmCleared(const VmTickProfile& profile) {
    assert(profile.calls == 0 && profile.instructions == 0 && profile.maxDepth == 0);
    assert(profile.totalMicros == 0 && profile.maxMicros == 0 && profile.slowAction == 0);
    for (size_t i = 0; i < sizeof(profile.slowTarget); ++i) assert(profile.slowTarget[i] == 0);
}

static void testVm() {
    Native32Reader reader = actionReader();
    ActionVM vm;
    Host host(&vm, &reader);
    assertVmCleared(vm.profile);
    const u64 initialRandom = vm.rngState;
    std::string entry(96, 'x');
    host.mutableEntry = &entry;
    fakeNow = 1000; clockStep = clockReads = 0;
    vm.run(&reader, &host, 1, entry);
    assert(vm.vars["score"] == "7" && vm.rngState == initialRandom);
    assert(entry == "renamed");
    assert(vm.profile.calls == 2 && vm.profile.instructions == 9 && vm.profile.maxDepth == 2);
    assert(vm.profile.totalMicros == timed(100) && vm.profile.maxMicros == timed(100));
    assert(vm.profile.slowAction == 1 && std::strlen(vm.profile.slowTarget) == 47);
    assert(std::string(vm.profile.slowTarget) == std::string(47, 'x'));
    assert(clockReads == reads(2)); // No nested or per-instruction clock reads.

    host.mutableEntry = 0;
    vm.run(&reader, &host, 10, "short");
    assert(vm.profile.calls == 3 && vm.profile.instructions == 11 && vm.profile.maxDepth == 2);
    assert(vm.profile.totalMicros == timed(110) && vm.profile.slowAction == 1);
    assert(clockReads == reads(4));
    vm.run(0, &host, 1, "ignored");
    vm.run(&reader, 0, 1, "ignored");
    vm.run(&reader, &host, 0, "ignored");
    assert(vm.profile.calls == 3 && clockReads == reads(4));

    clockStep = 37;
    vm.run(&reader, &host, 12, "bad opcode");
    assert(vm.profile.calls == 4 && vm.profile.instructions == 11);
    assert(vm.profile.totalMicros == timed(147) && vm.profile.maxMicros == timed(100));
    assert(clockReads == reads(6));

    vm.vars["keep"] = "saved";
    vm.rngState = 0x12345678;
    vm.resetProfile();
    assertVmCleared(vm.profile);
    assert(vm.vars["keep"] == "saved" && vm.rngState == 0x12345678);
    fakeNow = 0xfffffff8u; clockStep = clockReads = 0;
    vm.run(&reader, &host, 10, "");
    assert(vm.profile.totalMicros == timed(10) && vm.profile.maxMicros == timed(10));
    assert(vm.profile.calls == 1 && vm.profile.instructions == 2 && vm.profile.maxDepth == 1);
    assert(vm.profile.slowAction == 10 && vm.profile.slowTarget[0] == 0);
    assert(clockReads == reads(2));

    vm = ActionVM();
    assertVmCleared(vm.profile);
    assert(vm.vars.empty() && vm.rngState == initialRandom);
    host.copyDuringCall = true;
    fakeNow = 0; clockStep = clockReads = 0;
    vm.run(&reader, &host, 1, "root");
    assert(vm.profile.calls == 2 && vm.profile.instructions == 9 && vm.profile.maxDepth == 2);
    assert(vm.profile.totalMicros == timed(110));
    assert(host.copyProfile.maxDepth == 1 && host.copyProfile.slowAction == 10);
    assert(host.copyProfile.totalMicros == timed(10));
    assert(clockReads == reads(4));

    vm.resetProfile();
    const std::string multiline("line\r\nbreak");
    vm.run(&reader, &host, 10, multiline);
    assert(std::string(vm.profile.slowTarget) == "line??break");
    assert(multiline == "line\r\nbreak");
}

static Native32Reader rawReader() {
    std::vector<u8> bytes(16, 0);
    store32(&bytes, 0, 4); // Sound 1: raw PCM size at byte 4.
    store32(&bytes, 4, 8);
    const u8 samples[] = { 123, 0, 249, 255, 0, 0, 232, 3 };
    std::memcpy(&bytes[8], samples, sizeof(samples));
    Native32Reader reader(bytes);
    reader.colorspace = ColorspaceArgb;
    return reader;
}

static void assertSoundCleared(const SoundTickProfile& profile) {
    assert(profile.calls == 0 && profile.totalMicros == 0 && profile.maxMicros == 0);
    assert(profile.slowSoundValue == 0 && profile.slowBytes == 0 && profile.slowFormat == -1);
}

static void testSound() {
    Native32Reader reader = rawReader();
    AudioEngine audio(ColorspaceArgb, 80);
    assertSoundCleared(audio.profile);
    fakeNow = 0xfffffff0u; clockStep = 40; clockReads = 0;
    const size_t channel = audio.playSound(&reader, 0x0101, "effect");
    assert(channel != 0 && audio.isChannelPlaying(channel));
    assert(audio.profile.calls == 1 && audio.profile.totalMicros == timed(40));
    assert(audio.profile.maxMicros == timed(40) && audio.profile.slowSoundValue == 0x0101);
    assert(audio.profile.slowFormat == AudioRaw && audio.profile.slowBytes == 8);
    assert(clockReads == reads(2));

    clockStep = 5;
    assert(audio.playSound(0, 0x2222, "missing reader") == 0);
    clockStep = 7;
    assert(audio.playSound(&reader, 0x0300, "zero index") == 0);
    clockStep = 9;
    assert(audio.playSound(&reader, 9, "missing sound") == 0);
    assert(audio.profile.calls == 4 && audio.profile.totalMicros == timed(61));
    assert(audio.profile.maxMicros == timed(40) && audio.profile.slowSoundValue == 0x0101);
    assert(audio.profile.slowFormat == AudioRaw && audio.profile.slowBytes == 8);
    assert(clockReads == reads(8));

    clockStep = 60;
    assert(audio.playSound(0, 0x4321, "slow failed lookup") == 0);
    assert(audio.profile.calls == 5 && audio.profile.totalMicros == timed(121));
#ifdef N32_TEST_CORE_PROFILE
    assert(audio.profile.maxMicros == 60 && audio.profile.slowSoundValue == 0x4321);
    assert(audio.profile.slowFormat == -1 && audio.profile.slowBytes == 0);
#else
    assert(audio.profile.maxMicros == 0 && audio.profile.slowSoundValue == 0x0101);
#endif
    audio.resetProfile();
    assertSoundCleared(audio.profile);
    assert(audio.isChannelPlaying(channel)); // Reset does not stop sound.
    audio = AudioEngine(ColorspaceYuv, 70);
    assertSoundCleared(audio.profile);
    assert(!audio.isPlaying() && audio.outputSampleRate() == 11025);
}

int main() {
    testVm();
    testSound();
#ifdef N32_TEST_CORE_PROFILE
    std::puts("core profile: fake-clock recursion, returns, reset, copy, sound and timer-wrap passed");
#else
    std::puts("core profile: host counters and unchanged script/audio state; all times zero passed");
#endif
    return 0;
}
