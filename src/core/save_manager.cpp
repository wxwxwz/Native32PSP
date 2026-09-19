#include "core/save_manager.h"
#include <stdio.h>
#include "core/content_loader.h"

namespace n32 {
SaveManager::SaveManager() {
}

SaveManager::SaveManager(const std::string& gamePath) {
    setGamePath(gamePath);
}

void SaveManager::setGamePath(const std::string& gamePath) {
    savePath = gamePath + ".ssl_sav";
}

bool SaveManager::save(const std::string& data) const {
    if (savePath.empty()) return false;
    const std::string temp=savePath+".tmp", backup=savePath+".bak";
    FILE* f = fopen(temp.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    if (fclose(f)!=0) ok=false;
    bool exists=pathIsFile(savePath);
    if(ok && exists) { remove(backup.c_str()); ok=rename(savePath.c_str(),backup.c_str())==0; }
    if(ok && rename(temp.c_str(),savePath.c_str())!=0) {
        if(exists) rename(backup.c_str(),savePath.c_str());
        ok=false;
    }
    if(!ok) remove(temp.c_str());
    return ok;
}

bool SaveManager::load(std::string* out) const {
    if (!out) {
        return false;
    }
    FILE* f = fopen(savePath.c_str(), "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 1024*1024) {
        fclose(f);
        return false;
    }
    out->assign((size_t)size, '\0');
    bool ok = size == 0 || fread(&(*out)[0], 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok) {
        out->clear();
    }
    return ok;
}

}
