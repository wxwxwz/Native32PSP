#ifndef NATIVE32_SAVE_MANAGER_H
#define NATIVE32_SAVE_MANAGER_H

#include <string>

namespace n32 {

class SaveManager {
public:
    SaveManager();
    explicit SaveManager(const std::string& gamePath);

    void setGamePath(const std::string& gamePath);
    bool save(const std::string& data) const;
    bool load(std::string* out) const;

    std::string savePath;
};

}

#endif
