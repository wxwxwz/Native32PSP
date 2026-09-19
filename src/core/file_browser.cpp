#include "core/file_browser.h"
#include "core/content_loader.h"
#include <algorithm>
#include <ctype.h>
#include <dirent.h>

namespace n32 {

static std::string lowerFileBrowser(const std::string& value) {
    std::string out = value;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

static bool lessCaseInsensitive(const std::string& a, const std::string& b) {
    return lowerFileBrowser(a) < lowerFileBrowser(b);
}

FileBrowser::FileBrowser() : cursor(0) {
}

void FileBrowser::refresh(const std::string& baseFile, const std::string& menuPath) {
    entries = listGameFiles(baseFile, menuPath);
    cursor = 0;
}

size_t FileBrowser::fileCount(const std::string& baseFile, const std::string& menuPath) {
    refresh(baseFile, menuPath);
    return entries.size();
}

std::string FileBrowser::firstFile(const std::string& baseFile, const std::string& menuPath) {
    refresh(baseFile, menuPath);
    return cursor < entries.size() ? entries[cursor] : std::string();
}

std::string FileBrowser::nextFile() {
    ++cursor;
    return cursor < entries.size() ? entries[cursor] : std::string();
}

bool resolveMenuDir(const std::string& baseFile, const std::string& menuPath, std::string* out) {
    std::string normalized = normalizeContentPath(menuPath);
    if (normalized.empty()) {
        std::string parent = pathParent(baseFile);
        if (parent.empty()) {
            return false;
        }
        if (out) {
            *out = parent;
        }
        return true;
    }

    std::string resolved;
    if (!ContentLoader::findContentFile(baseFile, normalized, &resolved) || !pathIsDir(resolved)) {
        return false;
    }
    if (out) {
        *out = resolved;
    }
    return true;
}

std::vector<std::string> listGameFiles(const std::string& baseFile, const std::string& menuPath) {
    std::vector<std::string> names;
    std::string dirPath;
    if (!resolveMenuDir(baseFile, menuPath, &dirPath)) {
        return names;
    }

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return names;
    }
    struct dirent* entry = 0;
    while ((entry = readdir(dir)) != 0) {
        std::string name = entry->d_name;
        std::string full = pathJoin(dirPath, name);
        if (pathIsFile(full) && pathExtensionLower(full) == "smf") {
            names.push_back(pathStem(full));
        }
    }
    closedir(dir);

    std::sort(names.begin(), names.end(), lessCaseInsensitive);
    return names;
}

}
