#include "core/archive_loader.h"
#include "core/content_loader.h"
#include <ctype.h>
#include <dirent.h>

namespace n32 {

static std::string lowerArchive(const std::string& value) {
    std::string out = value;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

bool extractZip(const std::string&, const std::string&) {
    return false;
}

bool findFhuiInDirectory(const std::string& dirPath, std::string* out) {
    std::string exact = pathJoin(dirPath, "FHUI.smf");
    if (pathExists(exact)) {
        if (out) {
            *out = exact;
        }
        return true;
    }

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return false;
    }
    struct dirent* entry = 0;
    while ((entry = readdir(dir)) != 0) {
        std::string name = entry->d_name;
        std::string full = pathJoin(dirPath, name);
        if (pathIsFile(full) && pathExtensionLower(full) == "smf" && lowerArchive(pathStem(full)) == "fhui") {
            closedir(dir);
            if (out) {
                *out = full;
            }
            return true;
        }
    }
    closedir(dir);
    return false;
}

bool loadZipGame(const std::string&, std::string*) {
    return false;
}

}
