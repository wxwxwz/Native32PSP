#include "core/action_vm.h"
#include "reference_action_vm.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
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
    explicit Instruction(Action value) : op(value), hasText(false), hasOffset(false), offset(0) {}
};
struct Program {
    std::vector<Instruction> code;
    u32 next() const { return (u32)code.size() + 1; }
    u32 emit(Action op) { code.push_back(Instruction(op)); return (u32)code.size(); }
    void push(const std::string& text) {
        emit(ActionPush); code.back().text=text; code.back().hasText=true;
    }
    void get(const std::string& name) { push(name); emit(ActionGetVariable); }
    void set(const std::string& name, const std::string& value) {
        push(name); push(value); emit(ActionSetVariable);
    }
    void jumpTo(u32 pc, u32 destination) {
        const int offset=(int)destination-(int)pc-(destination>pc?1:0);
        assert(offset>=-32768 && offset<=32767);
        code[pc-1].hasOffset=true; code[pc-1].offset=(s16)offset;
    }
    Native32Reader reader() const {
        std::vector<u8> data(code.size()*8,0);
        for(size_t i=0;i<code.size();++i) {
            const Instruction& ins=code[i];
            u32 payload=0;
            if(ins.hasText || ins.hasOffset) {
                payload=(u32)data.size();
                if(ins.hasText) {
                    data.insert(data.end(),ins.text.begin(),ins.text.end()); data.push_back(0);
                } else {
                    data.push_back((u8)ins.offset); data.push_back((u8)((u16)ins.offset>>8));
                }
            }
            for(unsigned j=0;j<4;++j) {
                data[i*8+j]=(u8)((u32)ins.op>>(j*8));
                data[i*8+4+j]=(u8)(payload>>(j*8));
            }
        }
        return Native32Reader(data);
    }
};

template<class VM> struct Host : VmHost {
    VM& vm;
    Native32Reader& reader;
    u32 child;
    std::map<std::string,std::string> properties;
    std::vector<std::string> events;
    Host(VM& v,Native32Reader& r,u32 c=0) : vm(v),reader(r),child(c) {}
    static std::string key(const std::string& target,ActionProp prop) {
        return target+":"+std::to_string((unsigned)prop);
    }
    void stop(const std::string& t) override { events.push_back("stop:"+t); }
    void play(const std::string& t) override { events.push_back("play:"+t); }
    u32 getFrame(const std::string&) override { return 7; }
    void gotoFrame(const std::string& t,u32 f,bool play) override {
        events.push_back(t+":"+std::to_string(f)+(play?":play":":stop"));
    }
    void stopSounds(const std::string& t) override { events.push_back("sound:"+t); }
    void setProperty(const std::string& t,ActionProp p,const std::string& v) override {
        properties[key(t,p)]=v;
    }
    std::string getProperty(const std::string& t,ActionProp p) override { return properties[key(t,p)]; }
    void cloneSprite(const std::string& src,const std::string& dst,s32 depth) override {
        events.push_back(src+">"+dst+":"+std::to_string(depth));
    }
    void removeSprite(const std::string& t) override { events.push_back("remove:"+t); }
    void call(u32 frame) override {
        assert(frame==2 && child); vm.run(&reader,this,child,"nested-child");
    }
    u32 getTime() const override { return 1234567; }
    void getUrl(const std::string& url,const std::string& t) override { events.push_back(url+">"+t); }
    void runFrameActions(u32) override { assert(false); }
};

static unsigned checks;
static void compare(const Program& p,u32 child=0,bool grid=false) {
    Native32Reader a=p.reader(),b=p.reader();
    ActionVM actual;
    n32_vm_reference::ActionVM before;
    Host<ActionVM> ah(actual,a,child);
    Host<n32_vm_reference::ActionVM> bh(before,b,child);
    for(unsigned run=0;run<3;++run) {
        actual.resetProfile(); before.resetProfile();
        actual.run(&a,&ah,1,"root-target"); before.run(&b,&bh,1,"root-target");
        assert(actual.vars==before.vars && actual.rngState==before.rngState);
        assert(ah.events==bh.events && ah.properties==bh.properties);
        assert(actual.profile.calls==before.profile.calls);
        assert(actual.profile.instructions==before.profile.instructions);
        assert(actual.profile.maxDepth==before.profile.maxDepth);
        if(grid) {
            assert(actual.vars["sum"]=="155232");
            assert(actual.vars["i"]=="99");
            assert(actual.vars["childvalue"]==std::string(100,'q')+"98");
            assert(ah.properties.size()==99);
        }
        ++checks;
    }
}

static void arithmetic() {
    const char* pairs[][2]={
        {"0","-0"},{"1","-7"},{"999999999","1"},{"-999999999","-1"},
        {"000000001","+23"},{"1e3","2.5"},{"0x1p4"," 12tail"},
        {"","text"},{"0.00001","1e-5"},{"123456789012345","9"},
        {"nan","1"},{"inf","-inf"}
    };
    const Action ops[]={ActionAdd,ActionSubtract,ActionMultiply,ActionDivide,ActionEquals,ActionLess};
    for(const auto& pair:pairs) for(Action op:ops) {
        Program p; p.push("Result");p.push(pair[0]);p.push(pair[1]);p.emit(op);
        p.emit(ActionSetVariable);p.emit(ActionEnd);compare(p);
    }
    const Action integers[]={ActionAnd,ActionOr,ActionNot,ActionToInteger,ActionAsciiToChar};
    for(const char* value:{"","-0","00042","-123","2147483647","-2147483648","12.75","1e3","0x10","words"}) {
        for(Action op:integers) {
            Program p;p.push("result");p.push(value);
            if(op==ActionAnd || op==ActionOr)p.push("2");
            p.emit(op);p.emit(ActionSetVariable);p.emit(ActionEnd);compare(p);
        }
    }
}

static std::pair<Program,u32> gridProgram() {
    // Original synthetic workload: repeated numeric/name work while updating
    // 99 cells. It contains no copied game script or commercial game data.
    Program p;p.set("Sum","0");p.set("I","0");
    const u32 loop=p.next();p.get("i");p.push("99");p.emit(ActionLess);p.emit(ActionNot);
    const u32 done=p.emit(ActionIf);
    p.push("sum");p.get("SUM");p.get("i");p.push("32");p.emit(ActionMultiply);
    p.emit(ActionAdd);p.emit(ActionSetVariable);
    p.push("field");p.get("i");p.emit(ActionStringAdd);p.push("0");p.get("i");p.emit(ActionSetProperty);
    p.push("slice");p.push(std::string(120,'x'));p.get("i");p.push("3");p.emit(ActionStringExtract);p.emit(ActionSetVariable);
    // A value must survive a nested run on the same VM, including cache growth.
    p.push("saved");p.push(std::string(96,'s'));p.push("2");p.emit(ActionCall);p.emit(ActionSetVariable);
    p.push("i");p.get("i");p.push("1");p.emit(ActionAdd);p.emit(ActionSetVariable);
    p.jumpTo(p.emit(ActionJump),loop);p.jumpTo(done,p.next());p.emit(ActionEnd);
    const u32 child=p.next();p.push("childValue");p.push(std::string(100,'q'));p.get("i");
    p.emit(ActionStringAdd);p.emit(ActionSetVariable);p.emit(ActionEnd);
    return {p,child};
}

static void stringsAndHost() {
    Program p;
    p.emit(ActionPop);p.emit(ActionTrace); // Empty stack remains well-defined.
    p.set("MiXeD",std::string(200,'a'));p.push("copied");p.get("MIXED");p.emit(ActionSetVariable);
    p.push("joined");p.get("mixed");p.push("suffix");p.emit(ActionStringAdd);p.emit(ActionSetVariable);
    p.push("equal");p.get("mixed");p.get("copied");p.emit(ActionStringEquals);p.emit(ActionSetVariable);
    p.push("length");p.get("joined");p.emit(ActionStringLength);p.emit(ActionSetVariable);
    p.push("less");p.push("alpha");p.push("beta");p.emit(ActionStringLess);p.emit(ActionSetVariable);
    p.push("nul");p.push("0");p.emit(ActionAsciiToChar);p.emit(ActionSetVariable);
    p.push("ascii");p.get("nul");p.emit(ActionCharToAscii);p.emit(ActionSetVariable);
    p.push("random");p.push("123");p.emit(ActionRandomNumber);p.emit(ActionSetVariable);
    p.push("time");p.emit(ActionGetTime);p.emit(ActionSetVariable);
    p.push(std::string(80,'t'));p.emit(ActionSetTarget2);p.emit(ActionStop);p.emit(ActionPlay);
    p.emit(ActionNextFrame);p.emit(ActionPreviousFrame);p.emit(ActionStopSounds);
    p.push("sprite");p.push("1");p.push("+123.5");p.emit(ActionSetProperty);
    p.push("property");p.push("sprite");p.push("1");p.emit(ActionGetProperty);p.emit(ActionSetVariable);
    p.push("sprite");p.push(std::string(88,'d'));p.push("-5");p.emit(ActionCloneSprite);
    p.push(std::string(88,'d'));p.emit(ActionRemoveSprite);
    p.push("local-url");p.push("next-target");p.emit(ActionGetUrl2);p.emit(ActionEnd);
    compare(p);
}

static void cachedActionViews() {
    Program p;p.push(std::string(120,'p'));p.emit(ActionEnd);
    Native32Reader reader=p.reader();
    assert(!reader.getActionRef(0) && !reader.getActionRef(0xffffffffu));
    assert(!reader.getAction(1,0));
    const ActionEntry* view=reader.getActionRef(1);
    assert(view && view->action==ActionPush && view->payload.text==std::string(120,'p'));
    assert(reader.getActionRef(1)==view);
    ActionEntry copy;
    assert(reader.getAction(1,&copy) && copy.payload.text==view->payload.text);
    copy.payload.text[0]='x';
    assert(view->payload.text[0]=='p');
    assert(reader.getActionCached(1,&copy) && copy.payload.text==view->payload.text);
    // A reset invalidates borrowed entries, but the owning API still owns text.
    reader.setData(std::vector<u8>());
    assert(!reader.getActionRef(1) && !reader.getAction(1,&copy));
    assert(copy.payload.text==std::string(120,'p'));
    Native32Reader invalid(std::vector<u8>(8,0xff));
    assert(!invalid.getActionRef(1));
    reader=p.reader();reader.base=reader.data.size()+1;
    assert(!reader.getActionRef(1));
    reader.base=0;reader.actionIdx=(u32)reader.data.size()+1;
    assert(!reader.getActionRef(1));
}

template<class VM> struct ResetReaderHost : Host<VM> {
    using Host<VM>::Host;
    void gotoFrame(const std::string& target,u32 frame,bool playing) override {
        // The caller must read its immediate argument before this invalidates
        // the current instruction. Target/stack strings must remain owned.
        this->reader.setData(std::vector<u8>());
        Host<VM>::gotoFrame(target,frame,playing);
    }
    void getUrl(const std::string& url,const std::string& target) override {
        this->reader.setData(std::vector<u8>());
        Host<VM>::getUrl(url,target);
    }
};

static void callbacksInvalidateCache() {
    for(unsigned mode=0;mode<3;++mode) {
        Program p;p.emit(ActionSetTarget);
        p.code.back().hasText=true;p.code.back().text=std::string(80,'t');
        if(mode==2) {
            p.push(std::string(100,'u'));p.push(std::string(100,'d'));p.emit(ActionGetUrl2);
        } else {
            if(mode==1)p.push("7");
            p.emit(mode==0?ActionGotoFrame:ActionGotoFrame2);
            p.code.back().hasOffset=true;p.code.back().offset=mode==0?6:1;
        }
        p.set("unreached","bad");p.emit(ActionEnd);
        Native32Reader a=p.reader(),b=p.reader();
        ActionVM actual;n32_vm_reference::ActionVM before;
        ResetReaderHost<ActionVM> ah(actual,a);
        ResetReaderHost<n32_vm_reference::ActionVM> bh(before,b);
        actual.run(&a,&ah,1,"root");before.run(&b,&bh,1,"root");
        assert(actual.vars.empty() && actual.vars==before.vars);
        assert(ah.events==bh.events && ah.events.size()==1);
        assert(actual.profile.instructions==before.profile.instructions);
        assert(a.data.empty() && b.data.empty());
        ++checks;
    }
    // Force cache reallocation during a recursive host callback. The parent
    // resumes after it with its own long stack value and target intact.
    Program p;p.push("saved");p.push(std::string(160,'s'));p.push("2");p.emit(ActionCall);
    p.emit(ActionSetVariable);p.emit(ActionStop);p.emit(ActionEnd);
    while(p.next()<4097)p.emit(ActionEnd);
    const u32 child=p.next();p.set("child",std::string(160,'c'));p.emit(ActionEnd);
    compare(p,child);
}

template<class VM> static void bench(const char* name,const Program& p,u32 child) {
    Native32Reader reader=p.reader(); VM vm; Host<VM> host(vm,reader,child);
    for(unsigned i=0;i<8;++i)vm.run(&reader,&host,1,"");
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<512;++i)vm.run(&reader,&host,1,"");
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    assert(vm.vars["sum"]=="155232" && host.properties.size()==99);
    std::printf("vm_grid_%s iterations=512 total_us=%lld sum=%s properties=%zu instructions=%u\n",name,(long long)us,vm.vars["sum"].c_str(),host.properties.size(),vm.profile.instructions);
}

int main(int argc,char** argv) {
    arithmetic();stringsAndHost();cachedActionViews();callbacksInvalidateCache();
    const auto grid=gridProgram();compare(grid.first,grid.second,true);
    std::printf("PASS VM semantics: %u runs, frozen 0125 state/host/RNG/instruction comparison\n",checks);
    if(argc==2 && std::strcmp(argv[1],"--before")==0)bench<n32_vm_reference::ActionVM>("before",grid.first,grid.second);
    if(argc==2 && std::strcmp(argv[1],"--after")==0)bench<ActionVM>("after",grid.first,grid.second);
    return 0;
}
