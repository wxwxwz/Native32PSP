#include "core/cheat_file.h"
#include <cstdio>
#include <utility>
namespace n32 {
static bool replaceFile(const std::string& temp, const std::string& path) {
    const std::string backup=path+".bak";
    const bool exists=pathIsFile(path);
    if(exists) { std::remove(backup.c_str()); if(std::rename(path.c_str(),backup.c_str())!=0) return false; }
    if(std::rename(temp.c_str(),path.c_str())==0) return true;
    if(exists) std::rename(backup.c_str(),path.c_str());
    return false;
}
bool loadCheatFile(CheatManager* cheats,const std::string& path,std::string* message) {
    FILE* f=std::fopen(path.c_str(),"rb");
    if (!f) { *message="未找到本游戏的 .cheats 文件"; return false; }
    CheatManager next; char line[1024]; bool good=true;
    while (std::fgets(line,sizeof(line),f)) {
        std::string code=trimString(line);
        if (code.empty() || code[0]=='#') continue;
        bool enabled=false;
        if (code.size()>2 && (code[0]=='0'||code[0]=='1') && code[1]=='\t') { enabled=code[0]=='1'; code=code.substr(2); }
        if (next.len()>=256 || next.setSlot((u32)next.len(),enabled,code)!=CheatParseOk) { good=false; break; }
    }
    if (std::ferror(f)) good=false;
    std::fclose(f);
    if (good) *cheats=std::move(next);
    *message=good ? "O 开关 · 三角键重新读取规则" : "金手指格式错误，原规则保留";
    return good;
}
bool saveCheatFile(const CheatManager& cheats,const std::string& path) {
    std::string temp=path+".tmp"; FILE* f=std::fopen(temp.c_str(),"wb"); if(!f) return false;
    bool good=true;
    for (auto& p: cheats.slots) if(std::fprintf(f,"%d\t%s\n",p.second.enabled?1:0,p.second.code.c_str())<0) good=false;
    if(std::fclose(f)!=0) good=false;
    if(good) good=replaceFile(temp,path);
    if(!good) std::remove(temp.c_str());
    return good;
}
}
