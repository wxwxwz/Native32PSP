#include "platform/system_info.h"
#include "platform/psp_app.h"
#include <algorithm>
#include <cstdio>
#include <unistd.h>
#include <pspkernel.h>
#ifdef PSP
#include <pspkernel.h>
#include <psppower.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_devctl.h>
#include <kubridge.h>
#include <malloc.h>
#endif
namespace n32 {
int cpuUsagePercent(u64 idle,u64 wall) {
    if(!wall || idle>wall)return -1;
    return (int)(100-(idle*100/wall));
}
bool storageBytes(u32 total,u32 free,int size,int sectors,u64* bytes,u64* available) {
    if(!total || free>total || size<=0 || sectors<=0)return false;
    u64 cluster=(u64)(unsigned)size*(unsigned)sectors;
    if(cluster>~(u64)0/total)return false;
    *bytes=cluster*total;*available=cluster*free;return true;
}
void readSystemInfo(SystemInfo* info,u64* idle,bool* idleValid) {
    *info=SystemInfo();*idleValid=false;
#ifdef PSP
    int model=kuKernelGetModel();
    const char* name=model==0?"PSP-1000":model==1?"PSP-2000":model==4?"PSP Go":
        (model==2 || model==3 || model==6 || model==8)?"PSP-3000":model==10?"PSP E1000":"PSP";
    char text[80];snprintf(text,sizeof(text),"%s (model %d)",name,model);info->model=text;
    unsigned version=(unsigned)sceKernelDevkitVersion();
    snprintf(text,sizeof(text),"%u.%u%u",version>>24,(version>>16)&255,(version>>8)&255);info->firmware=text;
    info->cpuMHz=scePowerGetCpuClockFrequencyInt();info->busMHz=scePowerGetBusClockFrequencyInt();
    SceKernelSystemStatus status={};status.size=sizeof(status);
    if(sceKernelReferSystemStatus(&status)>=0) {
        *idle=((u64)status.idleClocks.hi<<32)|status.idleClocks.low;*idleValid=true;
    }
    struct mallinfo heap=mallinfo();
    info->heapUsed=heap.uordblks;info->heapReserved=heap.arena;info->heapFree=heap.fordblks;
    info->systemFree=sceKernelTotalFreeMemSize();info->largestBlock=sceKernelMaxFreeMemSize();info->memoryValid=true;
    char cwd[1024];std::string device="ms0:";
    if(getcwd(cwd,sizeof(cwd))) {std::string path=cwd;size_t colon=path.find(':');if(colon!=std::string::npos)device=path.substr(0,colon+1);}
    info->storage=device;SceDevInf dev={};SceDevctlCmd arg={&dev};
    if(sceIoDevctl(device.c_str(),SCE_PR_GETDEV,&arg,sizeof(arg),0,0)>=0)
        info->storageValid=storageBytes(dev.maxClusters,dev.freeClusters,dev.sectorSize,dev.sectorCount,&info->storageTotal,&info->storageFree);
#else
    info->model="HOST TEST";info->firmware="N/A";info->storage="N/A";*idle=0;
#endif
}
void PspApp::drawSystemInfo() {
    const u32 now=sceKernelGetSystemTimeLow();
    if(!systemInfoSampled || now-systemInfoTick>=1000000u) {
        u64 idle=0;bool valid=false;
        readSystemInfo(&systemInfo,&idle,&valid);
        if(systemInfoSampled && systemIdleValid && valid && idle>=systemIdle)
            systemInfo.cpuPercent=cpuUsagePercent(idle-systemIdle,(u32)(now-systemInfoTick));
        systemInfoTick=now;systemIdle=idle;systemIdleValid=valid;systemInfoSampled=true;
    }
    std::fill(menuFrame.begin(),menuFrame.end(),uiColor(0xff101319));
    drawMenuRect(24,18,3,22,accent());drawMenuName(38,20,tr("System info"),uiColor(0xffedf3f6),460);
    char text[128];int y=54;
    auto row=[&](const char* label,const std::string& value) {drawMenuName(28,y,label,uiColor(0xffaebaca),180);drawMenuName(184,y,value,uiColor(0xffedf3f6),472);y+=22;};
    row(tr("Device"),systemInfo.model);row(tr("Firmware"),systemInfo.firmware);
    snprintf(text,sizeof(text),"%d MHz / BUS %d MHz",systemInfo.cpuMHz,systemInfo.busMHz);row(tr("CPU clock"),text);
    if(systemInfo.cpuPercent>=0)snprintf(text,sizeof(text),"%d%%",systemInfo.cpuPercent);
    else snprintf(text,sizeof(text),"%s",systemIdleValid?tr("Sampling"):tr("Unavailable"));
    row(tr("CPU usage"),text);
    auto mib=[](u64 bytes){return (double)bytes/1048576.0;};
    if(systemInfo.memoryValid)snprintf(text,sizeof(text),"%.1f / %.1f MiB",mib(systemInfo.heapUsed),mib(systemInfo.heapReserved));
    else snprintf(text,sizeof(text),"%s",tr("Unavailable"));
    row(tr("Heap used/reserved"),text);
    if(systemInfo.memoryValid)snprintf(text,sizeof(text),"%.1f + %.1f MiB",mib(systemInfo.heapFree),mib(systemInfo.systemFree));
    else snprintf(text,sizeof(text),"%s",tr("Unavailable"));
    row(tr("Heap/system free"),text);
    if(systemInfo.storageValid)snprintf(text,sizeof(text),"%.1f / %.1f MiB",mib(systemInfo.storageTotal-systemInfo.storageFree),mib(systemInfo.storageTotal));
    else snprintf(text,sizeof(text),"%s",tr("Unavailable"));
    row(tr("Storage used/total"),text);
    if(systemInfo.storageValid)snprintf(text,sizeof(text),"%s  %.1f MiB",systemInfo.storage.c_str(),mib(systemInfo.storageFree));
    else snprintf(text,sizeof(text),"%s",systemInfo.storage.c_str());
    row(tr("Storage free"),text);
    drawMenuName(28,234,tr("Page CPU usage; memory excludes VRAM"),uiColor(0xff8e9aaa),472);
    drawMenuName(28,254,(std::string("○ ")+tr("Resample")+"   × "+tr("Back")),accent(),472);
    presentFrame(menuFrame);
}
}
