#ifndef N32_LOAD_PROGRESS_H
#define N32_LOAD_PROGRESS_H
#include <string>
#include <stddef.h>
namespace n32 {
struct LoadProgress {
    typedef void (*Callback)(void*,const char*,const std::string&,size_t,size_t);
    Callback callback=0;void* context=0;
    void report(const char* stage,const std::string& path,size_t done=0,size_t total=0) const {
        if(callback)callback(context,stage,path,done,total);
    }
};
}
#endif
