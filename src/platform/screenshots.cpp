#include "platform/psp_app.h"
#include "platform/menu_font.h"
#include "platform/psp_log.h"
#include "core/screenshot_store.h"
#include <cstdio>
#include <algorithm>
namespace n32 {
void PspApp::takeScreenshot() {
    std::string path;
    std::vector<u32> pixels;
    bool ok=captureGameFrame(&pixels) && saveScreenshot(screenshotDirectory(),pixels,
        gamePresentation.width(),gamePresentation.height(),&path);
    screenshotNotice=ok?tr("Screenshot saved"):tr("Screenshot failed: check free space");
    screenshotNoticeTicks=60; pauseStatus=screenshotNotice;
    pspLog("screenshot: %s %s",ok?"saved":"failed",path.c_str());
    coreClock.reset(); displayFps.reset();renderCadence.reset();
}
void PspApp::closeScreenshots() {
    screenshotDeleteConfirm=false;screenshotError.clear();
    screenshotMode=false; screenshotPath.clear(); screenshotCount=0;screenshotIndex=0;
    std::vector<u32>().swap(screenshotPixels);
}
void PspApp::deleteCurrentScreenshot() {
    screenshotDeleteConfirm=false;
    if(deleteScreenshot(screenshotDirectory(),screenshotPath)) {
        screenshotError.clear();browseScreenshots(1);
    } else screenshotError=tr("Cannot delete screenshot");
}
void PspApp::browseScreenshots(int direction) {
    screenshotError.clear();
    screenshotPath=findScreenshot(screenshotDirectory(),screenshotPath,-direction,&screenshotCount,&screenshotIndex);
    screenshotPixels.clear();
    if(!screenshotPath.empty() && !loadScreenshot(screenshotPath,&screenshotPixels))
        pspLog("screenshot: read failed %s",screenshotPath.c_str());
}
void PspApp::drawScreenshots() {
    if(screenshotPixels.empty()) {
        std::fill(menuFrame.begin(),menuFrame.end(),uiColor(0xff101319));
        drawMenuName(32,100,screenshotPath.empty()?(std::string(tr("No screenshots"))+"   △ "+tr("Capture")):tr("Cannot read screenshot. Try another."),uiColor(0xffcad3de),464);
    } else for(size_t i=0;i<screenshotPixels.size();++i) {
        // Stored capture pixels are PSP ABGR; the menu presenter accepts ARGB.
        const u32 c=screenshotPixels[i];
        menuFrame[i]=(c&0xff00ff00u)|((c&0xffu)<<16)|((c>>16)&0xffu);
    }
    if(screenshotInfo || screenshotPixels.empty()) {
        drawMenuRect(0,0,480,24,uiColor(0xff101319));
        drawMenuName(8,4,menuFileName(screenshotPath),uiColor(0xffcad3de),472);
        drawMenuRect(0,248,480,24,uiColor(0xff101319));
        char count[64];snprintf(count,sizeof(count),"%u/%u",(unsigned)screenshotIndex,(unsigned)screenshotCount);
        drawMenuName(8,252,std::string(count)+"   "+(std::string("← → ")+tr("Browse")+"   ○ "+tr("Info")+"   △ "+tr("Delete")+"   × "+tr("Back")),accent(),472);
    }
    if(!screenshotError.empty()) {
        drawMenuRect(0,218,480,26,uiColor(0xff101319));
        drawMenuName(12,222,screenshotError,uiColor(0xffffbd78),468);
    }
    if(screenshotDeleteConfirm) {
        drawMenuRect(48,88,384,104,uiColor(0xff101319));
        drawMenuRect(48,88,3,104,accent());
        drawMenuName(64,102,tr("Delete this screenshot?"),uiColor(0xffedf3f6),416);
        drawMenuName(64,130,menuFileName(screenshotPath),uiColor(0xffaebaca),416);
        drawMenuName(64,164,std::string("○ ")+tr("Delete")+"   × "+tr("Cancel"),accent(),416);
    }
    presentFrame(menuFrame);
}
}
