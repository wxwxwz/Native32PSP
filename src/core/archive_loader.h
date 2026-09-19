#ifndef NATIVE32_ARCHIVE_LOADER_H
#define NATIVE32_ARCHIVE_LOADER_H

#include <string>

namespace n32 {

bool extractZip(const std::string& zipPath, const std::string& extractPath);
bool findFhuiInDirectory(const std::string& dir, std::string* out);
bool loadZipGame(const std::string& zipPath, std::string* outFhuiPath);

}

#endif
