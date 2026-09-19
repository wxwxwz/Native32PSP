#include "platform/psp_settings.h"
#include "platform/version.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <algorithm>
#include "platform/language_builtin.h"

namespace n32 {

const char* buildVersion() {
    // __DATE__ is "Mmm dd yyyy", __TIME__ is "hh:mm:ss". Reformat once into
    // yyyyMMdd.hhmm so the on-screen string sorts chronologically.
    static char version[40];
    static bool ready = false;
    if (ready) {
        return version;
    }

    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char monthName[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
    const char* found = strstr(months, monthName);
    int month = found ? (int)((found - months) / 3 + 1) : 0;

    int day = 0;
    sscanf(__DATE__ + 4, "%d", &day);
    int year = 0;
    sscanf(__DATE__ + 7, "%d", &year);
    int hour = 0;
    int minute = 0;
    sscanf(__TIME__, "%d:%d", &hour, &minute);

    snprintf(version, sizeof(version), "%s %04d%02d%02d.%02d%02d", N32_APP_VERSION, year, month, day, hour, minute);
    ready = true;
    return version;
}

u32 themeAccent(int theme) {
    static const u32 colors[]={0xff76e5df,0xff78baff,0xffc4a0ff,0xffffbd78,0xff8bdda0};
    return colors[theme>=0 && theme<5?theme:0];
}
u32 appearanceAccent(int theme,bool light) {
    static const u32 colors[]={0xff006d68,0xff155da8,0xff7441a6,0xff995000,0xff24703c};
    return light?colors[theme>=0 && theme<5?theme:0]:themeAccent(theme);
}
u32 appearanceColor(u32 color,bool light) {
    if(!light)return color;
    switch(color) {
    case 0xff101319: case 0xff121418:return 0xfff4f5f7;
    case 0xffedf3f6: case 0xffffffff:return 0xff1c2430;
    case 0xffcad3de: case 0xffd8dee8: case 0xffc8d0dc:return 0xff354252;
    case 0xff8e9aaa: case 0xffa8b0bc: case 0xffaebaca:return 0xff526173;
    case 0xffffbd78:return 0xff995000;
    default:return color;
    }
}
const char* themeName(int theme) {
    static const char* names[]={"Cyan","Blue","Purple","Orange","Green"};
    return names[theme>=0 && theme<5?theme:0];
}
u32 blendPanel(u32 background,u32 foreground,unsigned opacity) {
    if(opacity>100)opacity=100;
    u32 result=0xff000000;
    for(unsigned shift=0;shift<24;shift+=8)
        result|=((((background>>shift)&255)*(100-opacity)+((foreground>>shift)&255)*opacity+50)/100)<<shift;
    return result;
}

const char* decoderName(VideoDecoder value) {
    return value == DecoderHardware ? "HARDWARE" : "SOFTWARE";
}

const char* scalingName(VideoScaling value) {
    switch (value) {
    case ScaleFit: return "FIT";
    case ScaleFull: return "FULL";
    default: return "ORIGINAL";
    }
}

const char* frameSkipName(FrameSkip value) {
    switch (value) {
    case SkipAuto: return "AUTO";
    case SkipOne: return "ONE";
    default: return "OFF";
    }
}

bool Settings::load(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    int decoderValue = 0;
    int scalingValue = 0;
    int skipValue = 0;
    unsigned volumeValue = 100;
    int smoothingValue = 0;
    int fpsValue = 0;
    int read = fscanf(file, "%d %d %d %u %d %d", &decoderValue, &scalingValue, &skipValue,
                      &volumeValue, &smoothingValue, &fpsValue);
    char lang[64]={0};
    const bool hasLanguage=fscanf(file,"%63s",lang)==1;
    int themeValue=0;
    if(hasLanguage)fscanf(file,"%d",&themeValue);
    int appearanceValue=0;
    if(hasLanguage)fscanf(file,"%d",&appearanceValue);
    fclose(file);
    if (read < 6) {
        return false;
    }
    decoder = decoderValue == 1 ? DecoderHardware : DecoderSoftware;
    scaling = scalingValue == 1 ? ScaleFit : (scalingValue == 2 ? ScaleFull : ScaleOriginal);
    frameSkip = skipValue == 1 ? SkipAuto : (skipValue == 2 ? SkipOne : SkipNone);
    volume = volumeValue > 100 ? 100 : volumeValue;
    smoothing = smoothingValue != 0;
    showFps = fpsValue != 0;
    language = hasLanguage ? lang : "zh_CN";
    lightAppearance=appearanceValue==1;
    theme=themeValue>=0 && themeValue<5?themeValue:0;
    return true;
}

bool Settings::save(const std::string& path) const {
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    fprintf(file, "%d %d %d %u %d %d\n", (int)decoder, (int)scaling, (int)frameSkip,
            (unsigned)volume, smoothing ? 1 : 0, showFps ? 1 : 0);
    const bool ok=fprintf(file,"%s\n%d\n%d\n",language.c_str(),theme,lightAppearance?1:0)>=0 && !ferror(file);
    return fclose(file)==0 && ok;
}

static std::string trimLanguage(const std::string& value) {
    size_t a=value.find_first_not_of(" \t\r\n"), b=value.find_last_not_of(" \t\r\n");
    return a==std::string::npos ? "" : value.substr(a,b-a+1);
}
static std::map<std::string,std::string> readLanguage(const std::string& path,std::string* name) {
    std::map<std::string,std::string> result;
    FILE* f=fopen(path.c_str(),"rb");if(!f)return result;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<0 || size>65536){fclose(f);return result;}
    std::string data((size_t)size,'\0');
    if(fread(size?&data[0]:0,1,size,f)!=(size_t)size){fclose(f);return result;}fclose(f);
    if(data.compare(0,3,"\xef\xbb\xbf")==0)data.erase(0,3);
    std::string section;size_t pos=0;
    while(pos<data.size()) {
        size_t end=data.find('\n',pos);if(end==std::string::npos)end=data.size();
        std::string line=trimLanguage(data.substr(pos,end-pos));pos=end+1;
        if(line.empty() || line[0]=='#' || line[0]==';')continue;
        if(line[0]=='[' && line.back()==']'){section=line.substr(1,line.size()-2);continue;}
        size_t eq=line.find('=');if(eq==std::string::npos)continue;
        std::string key=trimLanguage(line.substr(0,eq)),value=trimLanguage(line.substr(eq+1));
        if(value.empty() || value.size()>512)continue;
        if(section=="language" && key=="name" && value.size()<=64)*name=value;
        if(section=="strings")for(const auto& entry:languageStrings)
            if(key==entry[0]){result[key]=value;break;}
    }
    return result;
}
void Language::load(const std::string& dir,const std::string& selected) {
    directory=dir;choices.clear();choices.push_back({"zh_CN","简体中文"});choices.push_back({"en","English"});
    DIR* d=dir.empty()?0:opendir(dir.c_str());
    if(d){while(dirent* e=readdir(d)) {
        std::string file=e->d_name;
        if(file.size()<5 || file.size()>63 || file.substr(file.size()-4)!=".ini")continue;
        std::string id=file.substr(0,file.size()-4);
        if(id=="zh_CN" || id=="en" || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)continue;
        std::string name=id;readLanguage(dir+"/"+file,&name);
        if(choices.size()<64)choices.push_back({id,name});
    }closedir(d);}
    std::sort(choices.begin()+2,choices.end(),[](const Choice& a,const Choice& b){return a.id<b.id;});
    index=0;for(size_t i=0;i<choices.size();++i)if(choices[i].id==selected)index=i;
    select();
}
void Language::select() {
    strings.clear();
    if(id()=="zh_CN")for(const auto& e:languageStrings)strings[e[0]]=e[1];
    if(!directory.empty()) {
        std::string name=choices[index].name;
        auto overrides=readLanguage(directory+"/"+id()+".ini",&name);
        choices[index].name=name;
        for(const auto& entry:overrides)strings[entry.first]=entry.second;
    }
}
void Language::cycle(int delta) {
    index=(index+choices.size()+(delta<0?-1:1))%choices.size();select();
}
const char* Language::text(const char* key) const {
    auto found=strings.find(key);
    if(found!=strings.end())return found->second.c_str();
    // Core cheat diagnostics predate localization and use Chinese text.
    for(const auto& entry:languageStrings)if(strcmp(key,entry[1])==0)return text(entry[0]);
    return key;
}

}
