#ifndef NATIVE32_PSP_SETTINGS_H
#define NATIVE32_PSP_SETTINGS_H

#include <psptypes.h>
#include <string>
#include <map>
#include <vector>

namespace n32 {

// Build version, derived from the build date/time so each build is identifiable
// on device without bumping a number by hand.
const char* buildVersion();

enum VideoDecoder {
    DecoderSoftware = 0,   // Portable MPEG-1 decoder in core/mpeg
    DecoderHardware = 1    // PSP sceMpeg hardware decoder
};

enum VideoScaling {
    ScaleOriginal = 0,     // 1:1, centred
    ScaleFit = 1,          // Fit to screen, keep aspect
    ScaleFull = 2          // Stretch to 480x272
};

enum FrameSkip {
    SkipNone = 0,
    SkipAuto = 1,
    SkipOne = 2
};

struct Settings {
    VideoDecoder decoder;
    VideoScaling scaling;
    FrameSkip frameSkip;
    u32 volume;          // 0..100
    bool smoothing;      // Scaling filter: false = Sharp (nearest), true = Smooth (linear)
    bool showFps;
    int theme=0;
    bool lightAppearance=false;
    std::string language = "zh_CN";

    Settings()
        : decoder(DecoderSoftware), scaling(ScaleOriginal), frameSkip(SkipAuto),
          volume(100), smoothing(false), showFps(false) {}

    // Persisted next to the EBOOT so settings survive a restart.
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

class Language {
public:
    Language(){load("","zh_CN");}
    void load(const std::string& directory,const std::string& selected);
    void cycle(int delta);
    const char* text(const char* key) const;
    const char* name() const {return choices[index].name.c_str();}
    const std::string& id() const {return choices[index].id;}
private:
    struct Choice {std::string id,name;};
    void select();
    std::string directory;
    std::vector<Choice> choices;
    size_t index;
    std::map<std::string,std::string> strings;
};

// Display strings for the settings rows.
u32 themeAccent(int theme);
u32 appearanceColor(u32 darkColor,bool light);
u32 appearanceAccent(int theme,bool light);
const char* themeName(int theme);
u32 blendPanel(u32 background,u32 foreground,unsigned opacity);
const char* decoderName(VideoDecoder value);
const char* scalingName(VideoScaling value);
const char* frameSkipName(FrameSkip value);

}

#endif
