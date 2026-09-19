#include "core/screenshot_store.h"
#include "core/content_loader.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
namespace n32 {
static void put32(u8* p,u32 n) { for(int i=0;i<4;++i) p[i]=(u8)(n>>(i*8)); }
static u32 get32(const u8* p) { return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
std::string screenshotDirectory() { char cwd[1024]; return getcwd(cwd,sizeof(cwd))?pathJoin(cwd,"screenshots"):std::string(); }
bool saveScreenshot(const std::string& dir,const std::vector<u32>& pixels,u32 w,u32 h,std::string* path) {
    if(dir.empty() || !w || !h || w>480 || h>272 || pixels.size()!=(size_t)w*h) return false;
    if(!pathIsDir(dir) && mkdir(dir.c_str(),0777)!=0) return false;
    time_t now=time(0); struct tm* clock=localtime(&now); char stamp[32]="unknown-time";
    if(clock) strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",clock);
    std::string target; int fd=-1;
    for(unsigned long long serial=0;;++serial) {
        char suffix[80]; snprintf(suffix,sizeof(suffix),"shot-%s-%06llu.bmp",stamp,serial);
        target=pathJoin(dir,suffix); fd=open(target.c_str(),O_WRONLY|O_CREAT|O_EXCL,0666);
        if(fd>=0) break;
        if(errno!=EEXIST) return false;
    }
    FILE* f=fdopen(fd,"wb"); if(!f) {close(fd);remove(target.c_str());return false;}
    u8 header[54]={}; header[0]='B';header[1]='M';put32(header+2,54+480*272*3);put32(header+10,54);
    put32(header+14,40);put32(header+18,480);put32(header+22,272);header[26]=1;header[28]=24;put32(header+34,480*272*3);
    bool ok=fwrite(header,1,54,f)==54;
    const int dx=(480-w)/2,dy=(272-h)/2; u8 row[480*3];
    for(int y=271;y>=0 && ok;--y) {
        memset(row,0,sizeof(row));
        if(y>=dy && y<dy+(int)h) for(u32 x=0;x<w;++x) {
            u32 c=pixels[(y-dy)*w+x];size_t i=(x+dx)*3;
            row[i]=(u8)(c>>16);row[i+1]=(u8)(c>>8);row[i+2]=(u8)c;
        }
        ok=fwrite(row,1,sizeof(row),f)==sizeof(row);
    }
    if(fclose(f)!=0)ok=false;
    if(!ok)remove(target.c_str()); else if(path)*path=target;
    return ok;
}
bool deleteScreenshot(const std::string& dir,const std::string& path) {
    const std::string prefix=dir+(dir.empty() || dir.back()!='/' ? "/" : "");
    if(dir.empty() || path.compare(0,prefix.size(),prefix)!=0)return false;
    const std::string name=path.substr(prefix.size());
    if(name.find_first_of("/\\:")!=std::string::npos || name.compare(0,5,"shot-") ||
       name.size()<9 || name.compare(name.size()-4,4,".bmp") || !pathIsFile(path))return false;
    return std::remove(path.c_str())==0;
}
bool loadScreenshot(const std::string& path,std::vector<u32>* output) {
    if(!output)return false;
    output->clear();FILE* f=fopen(path.c_str(),"rb");if(!f)return false;
    u8 header[54]; bool ok=fread(header,1,54,f)==54;
    ok=ok && header[0]=='B' && header[1]=='M' && get32(header+10)==54 && get32(header+14)==40 &&
        get32(header+18)==480 && get32(header+22)==272 && header[26]==1 && header[27]==0 && header[28]==24 && header[29]==0 && get32(header+30)==0;
    if(ok) {
        output->resize(480*272);u8 row[480*3];
        for(int y=271;y>=0 && ok;--y) {
            ok=fread(row,1,sizeof(row),f)==sizeof(row);
            if(ok)for(int x=0;x<480;++x)(*output)[y*480+x]=0xff000000u|((u32)row[x*3]<<16)|((u32)row[x*3+1]<<8)|row[x*3+2];
        }
    }
    fclose(f);if(!ok)output->clear();return ok;
}
std::string findScreenshot(const std::string& dir,const std::string& current,int direction,size_t* count,size_t* index) {
    if(count)*count=0;
    if(index)*index=0;
    DIR* d=opendir(dir.c_str());
    if(!d)return std::string();
    std::string first,last,candidate;struct dirent* entry;
    while((entry=readdir(d))) {
        std::string name=entry->d_name;
        if(name.compare(0,5,"shot-") || name.size()<9 || name.compare(name.size()-4,4,".bmp"))continue;
        std::string path=pathJoin(dir,name);if(!pathIsFile(path))continue;
        if(count)++*count;
        if(first.empty() || path<first)first=path;
        if(last.empty() || path>last)last=path;
        if(direction>0 && path>current && (candidate.empty() || path<candidate))candidate=path;
        if(direction<0 && path<current && (candidate.empty() || path>candidate))candidate=path;
    }
    std::string selected=direction==0?last:!candidate.empty()?candidate:direction>0?first:last;
    // Newest-first ordinal, without retaining a growing file list.
    if(index && !selected.empty()) {
        *index=1;rewinddir(d);
        while((entry=readdir(d))) {
            std::string name=entry->d_name;
            if(name.compare(0,5,"shot-") || name.size()<9 || name.compare(name.size()-4,4,".bmp"))continue;
            std::string path=pathJoin(dir,name);
            if(path>selected && pathIsFile(path))++*index;
        }
    }
    closedir(d);return selected;
}
}
