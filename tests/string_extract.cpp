#include "core/action_vm.h"
#include "reference_action_vm.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace n32;
namespace n32 { void pspLog(const char*, ...) {} }

struct Instruction {
    Action op;
    std::string text;
    bool hasText, hasOffset;
    s16 offset;
    explicit Instruction(Action value)
        : op(value), hasText(false), hasOffset(false), offset(0) {}
};

struct Program {
    std::vector<Instruction> code;
    u32 next() const { return (u32)code.size() + 1; }
    u32 emit(Action op) { code.push_back(Instruction(op)); return (u32)code.size(); }
    void push(const std::string& text) {
        emit(ActionPush); code.back().text = text; code.back().hasText = true;
    }
    void get(const std::string& name) { push(name); emit(ActionGetVariable); }
    void set(const std::string& name, const std::string& value) {
        push(name); push(value); emit(ActionSetVariable);
    }
    void jumpTo(u32 pc, u32 destination) {
        const int offset = (int)destination - (int)pc - (destination > pc ? 1 : 0);
        assert(offset >= -32768 && offset <= 32767);
        code[pc - 1].hasOffset = true; code[pc - 1].offset = (s16)offset;
    }
    Native32Reader reader() const {
        std::vector<u8> data(code.size() * 8, 0);
        for (size_t i = 0; i < code.size(); ++i) {
            const Instruction& ins = code[i];
            u32 payload = 0;
            if (ins.hasText || ins.hasOffset) {
                payload = (u32)data.size();
                if (ins.hasText) {
                    data.insert(data.end(), ins.text.begin(), ins.text.end());
                    data.push_back(0);
                } else {
                    data.push_back((u8)ins.offset);
                    data.push_back((u8)((u16)ins.offset >> 8));
                }
            }
            for (unsigned j = 0; j < 4; ++j) {
                data[i * 8 + j] = (u8)((u32)ins.op >> (j * 8));
                data[i * 8 + 4 + j] = (u8)(payload >> (j * 8));
            }
        }
        return Native32Reader(data);
    }
};

struct Host : VmHost {
    std::map<std::string, std::string> x;
    Host() { for (unsigned i = 0; i < 4; ++i) x["choice" + std::to_string(i)] = "360"; }
    void stop(const std::string&) override {}
    void play(const std::string&) override {}
    u32 getFrame(const std::string&) override { return 0; }
    void gotoFrame(const std::string&, u32, bool) override {}
    void stopSounds(const std::string&) override {}
    void setProperty(const std::string& target, ActionProp prop, const std::string& value) override {
        assert(prop == ActionPropX);
        // Like the emulator, nonexistent sprite names do not create sprites.
        if (x.count(target)) x[target] = value;
    }
    std::string getProperty(const std::string&, ActionProp) override { return "0"; }
    void cloneSprite(const std::string&, const std::string&, s32) override {}
    void removeSprite(const std::string&) override {}
    void call(u32) override { assert(false); }
    u32 getTime() const override { return 0; }
    void getUrl(const std::string&, const std::string&) override {}
    void runFrameActions(u32) override { assert(false); }
    unsigned visibleChoices() const {
        unsigned result = 0;
        for (const auto& item : x) if (item.second == "100") ++result;
        return result;
    }
};

static unsigned extractionChecks;
static void checkExtract(const std::string& source, const char* first,
                         const char* length, const std::string& expected) {
    Program p;
    p.push("result"); p.push(source); p.push(first); p.push(length);
    p.emit(ActionStringExtract); p.emit(ActionSetVariable); p.emit(ActionEnd);
    Native32Reader reader = p.reader();
    ActionVM vm; Host host;
    vm.run(&reader, &host, 1, "");
    assert(vm.vars["result"] == expected);
    ++extractionChecks;
}

static void extractionCases() {
    checkExtract("hello", "2", "3", "ell");
    checkExtract("hello", "2", "0", "");
    checkExtract("hello", "0", "2", "he");
    checkExtract("hello", "-5", "2", "he");
    checkExtract("hello", "5", "8", "o");
    checkExtract("hello", "6", "8", "");
    checkExtract("", "1", "9", "");
    checkExtract("hello", "2", "4294967296", "ello");
    checkExtract("hello", "4294967298", "2", "");
    checkExtract("hello", "9007199254740991", "2", "");
    checkExtract("hello", "-9223372036854775808", "2", "he");
    checkExtract("hello", "2", "-17", "ello");
    checkExtract("hello", "1", "-1", "hello");
    // Rust's wrapping end can precede start for small negative lengths and
    // panic. The PSP policy consistently reads the remaining tail instead.
    checkExtract("hello", "4", "-1", "lo");
    checkExtract("hello", "4", "-3", "lo");
    checkExtract("hello", "2", "-9223372036854775808", "ello");
    checkExtract("hello", "-8", "-17", "hello");
    checkExtract("hello", "5", "-17", "o");
    checkExtract("hello", "6", "-17", "");
    checkExtract("hello", "4294967298", "-17", "");
    checkExtract("", "1", "-17", "");
    checkExtract("hello", "2.9", "2.9", "el");
    checkExtract("hello", "2", "-0", "");
    checkExtract("hello", "2", "-0.9", "");
}

// Original synthetic script, no resource file or copied game bytecode.
// The order is accumulated as digits, and the removal length subtracts that
// numeric order from the remaining pool size. Its second removal asks for -17.
static Program fourChoices() {
    Program p;
    p.set("pool", "0123"); p.set("order", "");
    const char* draws[] = {"3", "1", "1"};
    for (const char* draw : draws) {
        p.push("1"); p.get("pool"); p.emit(ActionStringLength);
        p.emit(ActionLess); p.emit(ActionNot);
        const u32 done = p.emit(ActionIf);
        p.set("pick", draw);
        p.push("order"); p.get("order"); p.get("pool"); p.get("pick");
        p.push("1"); p.emit(ActionStringExtract); p.emit(ActionStringAdd);
        p.emit(ActionSetVariable);
        p.push("pool");
        p.get("pool"); p.push("1"); p.get("pick"); p.push("1");
        p.emit(ActionSubtract); p.emit(ActionStringExtract);
        p.get("pool"); p.get("pick"); p.push("1"); p.emit(ActionAdd);
        p.get("pool"); p.emit(ActionStringLength); p.get("order");
        p.emit(ActionSubtract); p.emit(ActionStringExtract);
        p.emit(ActionStringAdd); p.emit(ActionSetVariable);
        p.jumpTo(done, p.next());
    }
    p.push("order"); p.get("order"); p.get("pool");
    p.emit(ActionStringAdd); p.emit(ActionSetVariable);
    for (unsigned i = 0; i < 4; ++i) {
        p.push("choice"); p.get("order"); p.push(std::to_string(i + 1));
        p.push("1"); p.emit(ActionStringExtract); p.emit(ActionStringAdd);
        p.push("0"); p.push("100"); p.emit(ActionSetProperty);
    }
    p.emit(ActionEnd);
    return p;
}

static void choicesRegression() {
    const Program p = fourChoices();
    Native32Reader currentReader = p.reader(), oldReader = p.reader();
    ActionVM current;
    n32_vm_reference::ActionVM old;
    Host currentHost, oldHost;
    current.run(&currentReader, &currentHost, 1, "");
    old.run(&oldReader, &oldHost, 1, "");
    assert(old.vars["order"] == "20" && oldHost.visibleChoices() == 2);
    assert(current.vars["order"] == "2013" && currentHost.visibleChoices() == 4);
    std::printf("PASS synthetic choices: old_order=%s old_visible=%u new_order=%s new_visible=%u\n",
                old.vars["order"].c_str(), oldHost.visibleChoices(),
                current.vars["order"].c_str(), currentHost.visibleChoices());
}

int main() {
    extractionCases(); choicesRegression();
    std::printf("PASS StringExtract: %u boundary cases\n", extractionChecks);
    return 0;
}
