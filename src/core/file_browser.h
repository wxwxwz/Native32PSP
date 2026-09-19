#ifndef NATIVE32_FILE_BROWSER_H
#define NATIVE32_FILE_BROWSER_H

#include <string>
#include <vector>

namespace n32 {

class FileBrowser {
public:
    FileBrowser();

    size_t fileCount(const std::string& baseFile, const std::string& menuPath);
    std::string firstFile(const std::string& baseFile, const std::string& menuPath);
    std::string nextFile();
    void refresh(const std::string& baseFile, const std::string& menuPath);

    std::vector<std::string> entries;
    size_t cursor;
};

bool resolveMenuDir(const std::string& baseFile, const std::string& menuPath, std::string* out);
std::vector<std::string> listGameFiles(const std::string& baseFile, const std::string& menuPath);

}

#endif
