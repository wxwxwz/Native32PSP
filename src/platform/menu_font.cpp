#include "platform/menu_font.h"
namespace n32 {
struct CodePair { u16 encoded; u16 unicode; };
#include "menu_font_data.inc"

const MenuGlyph* menuGlyph(u32 code) {
    size_t low = 0, high = sizeof(glyphs) / sizeof(glyphs[0]);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (glyphs[mid].code < code) low = mid + 1; else high = mid;
    }
    if (low < sizeof(glyphs) / sizeof(glyphs[0]) && glyphs[low].code == code) return &glyphs[low];
    return code == 0xfffd ? 0 : menuGlyph(0xfffd);
}

static bool decodeUtf8(const std::string& text, std::vector<u32>* out) {
    for (size_t i = 0; i < text.size();) {
        unsigned char first = text[i++];
        u32 value; unsigned count; u32 minimum;
        if (first < 0x80) { value = first; count = 0; minimum = 0; }
        else if (first >= 0xc2 && first <= 0xdf) { value = first & 31; count = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { value = first & 15; count = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { value = first & 7; count = 3; minimum = 0x10000; }
        else return false;
        if (count > text.size() - i) return false;
        while (count--) {
            unsigned char next = text[i++];
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
        out->push_back(value < 32 ? 0xfffd : value);
    }
    return true;
}

std::vector<u32> decodeMenuName(const std::string& text) {
    std::vector<u32> out;
    if (decodeUtf8(text, &out)) return out;
    out.clear();
    for (size_t i = 0; i < text.size();) {
        u32 code = (unsigned char)text[i++];
        if (code < 128) { out.push_back(code < 32 ? 0xfffd : code); continue; }
        if ((code >= 0x81 && code <= 0x9f) || (code >= 0xe0 && code <= 0xfc)) {
            if (i == text.size()) { out.push_back(0xfffd); break; }
            unsigned char tail = text[i];
            if (tail >= 0x40 && tail <= 0xfc && tail != 0x7f) { code = (code << 8) | tail; ++i; }
        }
        size_t low = 0, high = sizeof(cp932) / sizeof(cp932[0]);
        while (low < high) {
            size_t mid = low + (high - low) / 2;
            if (cp932[mid].encoded < code) low = mid + 1; else high = mid;
        }
        out.push_back(low < sizeof(cp932) / sizeof(cp932[0]) && cp932[low].encoded == code ? cp932[low].unicode : 0xfffd);
    }
    return out;
}

std::string menuFileName(const std::string& path) {
    // PSP paths use '/', while 0x5c can be a valid CP932 trailing byte.
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
}
