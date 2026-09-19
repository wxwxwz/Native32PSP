#ifndef N32_CHEAT_FILE_H
#define N32_CHEAT_FILE_H
#include "core/emulator.h"
namespace n32 {
bool loadCheatFile(CheatManager* cheats, const std::string& path, std::string* message);
bool saveCheatFile(const CheatManager& cheats, const std::string& path);
}
#endif
