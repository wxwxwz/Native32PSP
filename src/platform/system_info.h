#ifndef N32_SYSTEM_INFO_H
#define N32_SYSTEM_INFO_H
#include <psptypes.h>
#include <string>
namespace n32 {
struct SystemInfo {
    std::string model,firmware,storage;
    int cpuMHz=0,busMHz=0,cpuPercent=-1;
    u64 heapUsed=0,heapReserved=0,heapFree=0,systemFree=0,largestBlock=0;
    u64 storageTotal=0,storageFree=0;
    bool memoryValid=false,storageValid=false;
};
int cpuUsagePercent(u64 idleDelta,u64 wallDelta);
bool storageBytes(u32 total,u32 free,int sectorSize,int sectors,u64* totalBytes,u64* freeBytes);
void readSystemInfo(SystemInfo* info,u64* idle,bool* idleValid);
}
#endif
