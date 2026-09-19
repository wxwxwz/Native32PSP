#ifndef N32_MENU_FONT_H
#define N32_MENU_FONT_H
#include <psptypes.h>
#include <string>
#include <vector>
namespace n32 {
struct MenuGlyph { u16 code; u8 width; u8 rows[32]; };
const MenuGlyph* menuGlyph(u32 code);
// Prefer valid UTF-8; otherwise decode PSP legacy directory names as CP932.
// Only display text is converted; paths used for file I/O remain unchanged.
std::vector<u32> decodeMenuName(const std::string& text);
std::string menuFileName(const std::string& path);
}
#endif
