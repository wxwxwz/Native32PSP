#include "core/content_loader.h"
#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

namespace n32 {

static std::string lowerAscii(const std::string& value) {
    std::string out = value;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

std::string trimString(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && isspace((unsigned char)value[begin])) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && isspace((unsigned char)value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::vector<std::string> splitPathComponents(const std::string& path) {
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin <= path.size()) {
        size_t slash = path.find('/', begin);
        if (slash == std::string::npos) {
            slash = path.size();
        }
        std::string part = trimString(path.substr(begin, slash - begin));
        if (!part.empty()) {
            parts.push_back(part);
        }
        if (slash == path.size()) {
            break;
        }
        begin = slash + 1;
    }
    return parts;
}

std::string normalizeContentPath(const std::string& filename) {
    std::vector<std::string> parts = splitPathComponents(filename);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out += "/";
        }
        out += parts[i];
    }
    return out;
}

std::string pathParent(const std::string& path) {
    size_t end = path.find_last_not_of('/');
    if (end == std::string::npos) {
        return std::string();
    }
    size_t slash = path.find_last_of('/', end);
    if (slash == std::string::npos) {
        return std::string();
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string pathJoin(const std::string& base, const std::string& relative) {
    if (base.empty()) {
        return relative;
    }
    if (relative.empty()) {
        return base;
    }
    if (relative[0] == '/') {
        return relative;
    }
    return base[base.size() - 1] == '/' ? base + relative : base + "/" + relative;
}

bool pathExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool pathIsDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool pathIsFile(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string pathStem(const std::string& path) {
    size_t slash = path.find_last_of('/');
    size_t begin = slash == std::string::npos ? 0 : slash + 1;
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < begin) {
        dot = path.size();
    }
    return path.substr(begin, dot - begin);
}

std::string pathExtensionLower(const std::string& path) {
    size_t slash = path.find_last_of('/');
    size_t begin = slash == std::string::npos ? 0 : slash + 1;
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < begin) {
        return std::string();
    }
    return lowerAscii(path.substr(dot + 1));
}

ContentLoader::ContentLoader() : hasPendingContent(false) {
}

void ContentLoader::queueLoad(const std::string& filename) {
    std::string normalized = normalizeContentPath(filename);
    if (normalized.empty()) {
        return;
    }
    pendingContent = normalized;
    hasPendingContent = true;
}

bool ContentLoader::hasPending() const {
    return hasPendingContent;
}

bool ContentLoader::takePending(std::string* out) {
    if (!hasPendingContent) {
        return false;
    }
    if (out) {
        *out = pendingContent;
    }
    pendingContent.clear();
    hasPendingContent = false;
    return true;
}

void ContentLoader::clear() {
    pendingContent.clear();
    hasPendingContent = false;
}

bool ContentLoader::fuzzyResolve(const std::string& baseDir, const std::string& relativePath, std::string* out) {
    std::string current = baseDir;
    std::vector<std::string> parts = splitPathComponents(relativePath);
    for (size_t i = 0; i < parts.size(); ++i) {
        DIR* dir = opendir(current.c_str());
        if (!dir) {
            return false;
        }
        std::string target = trimString(lowerAscii(parts[i]));
        bool found = false;
        struct dirent* entry = 0;
        while ((entry = readdir(dir)) != 0) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            if (trimString(lowerAscii(name)) == target) {
                current = pathJoin(current, name);
                found = true;
                break;
            }
        }
        closedir(dir);
        if (!found) {
            return false;
        }
    }
    if (!pathExists(current)) {
        return false;
    }
    if (out) {
        *out = current;
    }
    return true;
}

static bool findRelativeContent(const std::string& currentGamePath, const std::string& relative, std::string* out) {
    std::string dir = pathParent(currentGamePath);
    while (!dir.empty()) {
        std::string exact = pathJoin(dir, relative);
        if (pathIsFile(exact)) {
            if (out) {
                *out = exact;
            }
            return true;
        }
        std::string fuzzy;
        if (ContentLoader::fuzzyResolve(dir, relative, &fuzzy) && pathIsFile(fuzzy)) {
            if (out) *out = fuzzy;
            return true;
        }
        std::string parent = pathParent(dir);
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return false;
}

bool ContentLoader::findContentFile(const std::string& currentGamePath, const std::string& filename, std::string* out) {
    std::string relative = normalizeContentPath(filename);
    if (relative.empty()) return false;
    // Always prefer the requested language, across every ancestor directory.
    if (findRelativeContent(currentGamePath, relative, out)) return true;
    std::vector<std::string> parts = splitPathComponents(relative);
    // Some game bundles contain an English launcher but only Chinese SSL data.
    // Limit fallback to the known language component; never substitute basenames.
    if (parts.size() >= 3 && lowerAscii(parts[0]) == "na32ssl") {
        std::string language = lowerAscii(parts[1]);
        if (language != "english" && language != "chinese") return false;
        parts[1] = language == "english" ? "CHINESE" : "ENGLISH";
        std::string alternate = parts[0];
        for (size_t i = 1; i < parts.size(); ++i) alternate += "/" + parts[i];
        return findRelativeContent(currentGamePath, alternate, out);
    }
    return false;
}

}
