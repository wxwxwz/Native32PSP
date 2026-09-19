#include "platform/psp_app.h"
#include "platform/menu_font.h"
#include "core/cheat_file.h"
#include <pspctrl.h>
#include <algorithm>
#include <iterator>

namespace n32 {
void PspApp::releaseGame() {
    loadingShown=false;loadingStage.clear();std::vector<u32>().swap(loadingPanel);
    emulator.reset();emulator.reader.setData(std::vector<u8>());emulator.cheats.clear();
    closeScreenshots();clearAudioQueue();buttons.clear();
    std::vector<u32>().swap(pspFrame);pspFrameWidth=pspFrameHeight=0;
    screenshotNoticeTicks=0;loadedPath.clear();
    gameLoaded=false;pauseMode=cheatMode=false;menuMode=true;
}
void PspApp::resumeGame() {
    closeScreenshots();
    pauseMode = cheatMode = false;
    suppressedButtons |= previousButtons & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS | PSP_CTRL_START);
    buttons.clear(); emulator.setButtons(buttons);
    clearAudioQueue(); coreClock.reset(); displayFps.reset(); renderCadence.reset();
    pspFrame.clear();
}
void PspApp::readPauseMenu(unsigned pressed) {
    if(screenshotMode) {
        if(screenshotDeleteConfirm) {
            if(pressed & PSP_CTRL_CROSS)screenshotDeleteConfirm=false;
            else if(pressed & PSP_CTRL_CIRCLE)deleteCurrentScreenshot();
            return;
        }
        if((pressed & PSP_CTRL_TRIANGLE) && !screenshotPath.empty()) {
            screenshotDeleteConfirm=true;screenshotError.clear();return;
        }
        if(pressed & PSP_CTRL_CROSS) closeScreenshots();
        else if(pressed & (PSP_CTRL_LEFT|PSP_CTRL_UP)) browseScreenshots(-1);
        else if(pressed & (PSP_CTRL_RIGHT|PSP_CTRL_DOWN)) browseScreenshots(1);
        else if(pressed & PSP_CTRL_CIRCLE) screenshotInfo=!screenshotInfo;
        return;
    }
    if (pressed & PSP_CTRL_CROSS) {
        if (cheatMode) { cheatMode = false; pauseStatus.clear(); }
        else resumeGame();
        return;
    }
    if (cheatMode) {
        if (pressed & PSP_CTRL_TRIANGLE) {
            loadCheatFile(&emulator.cheats, loadedPath + ".cheats", &pauseStatus);
            cheatIndex = 0;
        }
        size_t count = emulator.cheats.len();
        if (!count) return;
        if (pressed & PSP_CTRL_UP) cheatIndex = cheatIndex ? cheatIndex-1 : count-1;
        if (pressed & PSP_CTRL_DOWN) cheatIndex = (cheatIndex+1)%count;
        if (pressed & PSP_CTRL_CIRCLE) {
            auto it = emulator.cheats.slots.begin(); std::advance(it, cheatIndex);
            it->second.enabled = !it->second.enabled;
            if (saveCheatFile(emulator.cheats, loadedPath + ".cheats")) pauseStatus = tr("Saved");
            else { it->second.enabled = !it->second.enabled; pauseStatus = tr("Cannot save cheat settings"); }
        }
        return;
    }
    if (pressed & PSP_CTRL_UP) pauseIndex = pauseIndex ? pauseIndex-1 : 3;
    if (pressed & PSP_CTRL_DOWN) pauseIndex = (pauseIndex+1)%4;
    if (!(pressed & PSP_CTRL_CIRCLE)) return;
    switch (pauseIndex) {
    case 0: resumeGame(); break;
    case 1:
        cheatMode=true; cheatIndex=0;
        if (emulator.cheats.isEmpty()) loadCheatFile(&emulator.cheats, loadedPath+".cheats", &pauseStatus);
        else pauseStatus=(std::string("○ ")+tr("Toggle")+"   △ "+tr("Reload"));
        break;
    case 2:
        screenshotMode=true; screenshotInfo=true; browseScreenshots(0); break;
    case 3:
        releaseGame();statusLine="menu";
        break;
    }
}
void PspApp::drawPauseMenu() {
    if(screenshotMode) { drawScreenshots(); return; }
    std::fill(menuFrame.begin(),menuFrame.end(),uiColor(0xff101319u));
    drawMenuRect(72,14,336,244,surfaceColor());
    drawMenuRect(88,26,3,22,accent());
    drawMenuRect(88,207,304,1,selectionColor());
    drawMenuName(96,28,cheatMode ? tr("Cheats") : tr("Paused"),uiColor(0xffedf3f6),390);
    drawMenuName(96,51,menuFileName(loadedPath),uiColor(0xff8e9aaa),390);
    if (!cheatMode) {
        const char* labels[]={tr("Resume"),tr("Cheats"),tr("Screenshots"),tr("Return to menu")};
        for (unsigned i=0;i<4;++i) {
            int y=86+i*28;
            if(i==pauseIndex) drawMenuRect(88,y-3,304,24,selectionColor());
            drawMenuName(104,y,labels[i],i==pauseIndex?accent():uiColor(0xffcad3de),382);
        }
    } else {
        unsigned begin=cheatIndex/5*5, i=0;
        for (auto& p:emulator.cheats.slots) {
            if(i>=begin && i<begin+5) {
                int y=78+(i-begin)*27;
                if(i==cheatIndex) drawMenuRect(88,y-3,304,24,selectionColor());
                std::string text=std::string("[")+tr(p.second.enabled?"On":"Off")+"] "+p.second.code;
                drawMenuName(96,y,text,i==cheatIndex?accent():uiColor(0xffcad3de),384);
            }
            ++i;
        }
        if (!i) drawMenuName(96,94,tr("No cheat rules"),uiColor(0xffcad3de),390);
    }
    drawMenuName(88,216,(pauseStatus=="O 开关 · 三角键重新读取规则" ? (std::string("○ ")+tr("Toggle")+"   △ "+tr("Reload")) : std::string(tr(pauseStatus.c_str()))),uiColor(0xffaebaca),397);
    drawMenuName(88,238,(std::string("○ ")+tr("Confirm")+"   × "+tr("Back")+"   □ "+tr("Resume")),uiColor(0xff8e9aaa),397);
    presentFrame(menuFrame);
}
}
