#ifndef N32_SCREENSHOT_STORE_H
#define N32_SCREENSHOT_STORE_H
#include <psptypes.h>
#include <string>
#include <vector>
namespace n32 {
std::string screenshotDirectory();
bool saveScreenshot(const std::string& directory,const std::vector<u32>& abgr,u32 width,u32 height,std::string* path);
bool deleteScreenshot(const std::string& directory,const std::string& path);
bool loadScreenshot(const std::string& path,std::vector<u32>* abgr);
// Scans without retaining an unbounded list. direction 0 selects the newest.
std::string findScreenshot(const std::string& directory,const std::string& current,int direction,size_t* count,size_t* index=0);
}
#endif
