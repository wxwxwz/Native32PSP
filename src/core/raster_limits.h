#ifndef NATIVE32_RASTER_LIMITS_H
#define NATIVE32_RASTER_LIMITS_H
#include <psptypes.h>
namespace n32 {
// One decoded raster is at most 4 MiB on the PSP. Reject corrupt dimensions
// before any multiplication/allocation; native game canvases are much smaller.
inline bool validRasterSize(u32 width, u32 height) {
    return width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
           width <= (1024u * 1024u) / height;
}
}
#endif
