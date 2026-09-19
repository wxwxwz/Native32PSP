#ifndef NATIVE32_CONTENT_LOADER_H
#define NATIVE32_CONTENT_LOADER_H

#include <string>
#include <vector>

namespace n32 {

class ContentLoader {
public:
    ContentLoader();

    void queueLoad(const std::string& filename);
    bool hasPending() const;
    bool takePending(std::string* out);
    void clear();

    static bool findContentFile(const std::string& currentGamePath, const std::string& filename, std::string* out);
    static bool fuzzyResolve(const std::string& baseDir, const std::string& relativePath, std::string* out);

    bool hasPendingContent;
    std::string pendingContent;
};

std::string trimString(const std::string& value);
std::string normalizeContentPath(const std::string& filename);
std::string pathParent(const std::string& path);
std::string pathJoin(const std::string& base, const std::string& relative);
bool pathExists(const std::string& path);
bool pathIsDir(const std::string& path);
bool pathIsFile(const std::string& path);
std::string pathStem(const std::string& path);
std::string pathExtensionLower(const std::string& path);
std::vector<std::string> splitPathComponents(const std::string& path);

}

#endif
