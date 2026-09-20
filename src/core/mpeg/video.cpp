#include "core/mpeg/video.h"
#include "core/mpeg/buffer.h"
#include <algorithm>
#include <utility>
#include <cmath>
#include <cstring>
#ifdef PSP
#include <pspkernel.h>
#endif

namespace n32 {
namespace mpeg {

static const int PICTURE_TYPE_INTRA = 1;
static const int PICTURE_TYPE_PREDICTIVE = 2;
static const int PICTURE_TYPE_B = 3;

static const int START_SEQUENCE = 0xb3;
static const int START_SLICE_FIRST = 0x01;
static const int START_SLICE_LAST = 0xaf;
static const int START_PICTURE = 0x00;
static const int START_EXTENSION = 0xb5;
static const int START_USER_DATA = 0xb2;

static const double PIXEL_ASPECT_RATIO[14] = {
    1.0000, 0.6735, 0.7031, 0.7615, 0.8055, 0.8437, 0.8935,
    0.9157, 0.9815, 1.0255, 1.0695, 1.0950, 1.1575, 1.2051,
};

static const double PICTURE_RATE[16] = {
    0.000, 23.976, 24.000, 25.000, 29.970, 30.000, 50.000, 59.940,
    60.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000,
};

static const int ZIG_ZAG[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14,
    21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53,
    60, 61, 54, 47, 55, 62, 63,
};

static const int INTRA_QUANT_MATRIX[64] = {
    8, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26,
    27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58, 26, 27, 29, 34, 38, 46,
    56, 69, 27, 29, 35, 38, 46, 56, 69, 83,
};

static const int PREMULTIPLIER_MATRIX[64] = {
    32, 44, 42, 38, 32, 25, 17, 9, 44, 62, 58, 52, 44, 35, 24, 12, 42, 58, 55, 49, 42, 33, 23, 12, 38, 52, 49,
    44, 38, 30, 20, 10, 32, 44, 42, 38, 32, 25, 17, 9, 25, 35, 33, 30, 25, 20, 14, 7, 17, 24, 23, 20, 17, 14, 9,
    5, 9, 12, 12, 10, 9, 7, 5, 2,
};

static const VlcEntry MACROBLOCK_ADDRESS_INCREMENT[] = {
    VlcEntry(2, 0), VlcEntry(0, 1), VlcEntry(4, 0), VlcEntry(6, 0), VlcEntry(8, 0), VlcEntry(10, 0), VlcEntry(0, 3),
    VlcEntry(0, 2), VlcEntry(12, 0), VlcEntry(14, 0), VlcEntry(0, 5), VlcEntry(0, 4), VlcEntry(16, 0),
    VlcEntry(18, 0), VlcEntry(0, 7), VlcEntry(0, 6), VlcEntry(20, 0), VlcEntry(22, 0), VlcEntry(24, 0),
    VlcEntry(26, 0), VlcEntry(28, 0), VlcEntry(30, 0), VlcEntry(32, 0), VlcEntry(34, 0), VlcEntry(36, 0),
    VlcEntry(38, 0), VlcEntry(0, 9), VlcEntry(0, 8), VlcEntry(-1, 0), VlcEntry(40, 0), VlcEntry(-1, 0),
    VlcEntry(42, 0), VlcEntry(44, 0), VlcEntry(46, 0), VlcEntry(0, 15), VlcEntry(0, 14), VlcEntry(0, 13),
    VlcEntry(0, 12), VlcEntry(0, 11), VlcEntry(0, 10), VlcEntry(48, 0), VlcEntry(50, 0), VlcEntry(52, 0),
    VlcEntry(54, 0), VlcEntry(56, 0), VlcEntry(58, 0), VlcEntry(60, 0), VlcEntry(62, 0), VlcEntry(64, 0),
    VlcEntry(-1, 0), VlcEntry(-1, 0), VlcEntry(66, 0), VlcEntry(68, 0), VlcEntry(70, 0), VlcEntry(72, 0),
    VlcEntry(74, 0), VlcEntry(76, 0), VlcEntry(78, 0), VlcEntry(0, 21), VlcEntry(0, 20), VlcEntry(0, 19),
    VlcEntry(0, 18), VlcEntry(0, 17), VlcEntry(0, 16), VlcEntry(0, 35), VlcEntry(-1, 0), VlcEntry(-1, 0),
    VlcEntry(0, 34), VlcEntry(0, 33), VlcEntry(0, 32), VlcEntry(0, 31), VlcEntry(0, 30), VlcEntry(0, 29),
    VlcEntry(0, 28), VlcEntry(0, 27), VlcEntry(0, 26), VlcEntry(0, 25), VlcEntry(0, 24), VlcEntry(0, 23),
    VlcEntry(0, 22),
};

static const VlcEntry MACROBLOCK_TYPE_INTRA[] = {
    VlcEntry(2, 0), VlcEntry(0, 0x01), VlcEntry(-1, 0), VlcEntry(0, 0x11),
};

static const VlcEntry MACROBLOCK_TYPE_PREDICTIVE[] = {
    VlcEntry(2, 0), VlcEntry(0, 0x0a), VlcEntry(4, 0), VlcEntry(0, 0x02), VlcEntry(6, 0), VlcEntry(0, 0x08),
    VlcEntry(8, 0), VlcEntry(10, 0), VlcEntry(12, 0), VlcEntry(0, 0x12), VlcEntry(0, 0x1a), VlcEntry(0, 0x01),
    VlcEntry(-1, 0), VlcEntry(0, 0x11),
};

static const VlcEntry MACROBLOCK_TYPE_B[] = {
    VlcEntry(2, 0), VlcEntry(4, 0), VlcEntry(6, 0), VlcEntry(8, 0), VlcEntry(0, 0x0c), VlcEntry(0, 0x0e),
    VlcEntry(10, 0), VlcEntry(12, 0), VlcEntry(0, 0x04), VlcEntry(0, 0x06), VlcEntry(14, 0), VlcEntry(16, 0),
    VlcEntry(0, 0x08), VlcEntry(0, 0x0a), VlcEntry(18, 0), VlcEntry(20, 0), VlcEntry(0, 0x1e), VlcEntry(0, 0x01),
    VlcEntry(-1, 0), VlcEntry(0, 0x11), VlcEntry(0, 0x16), VlcEntry(0, 0x1a),
};

static const VlcEntry CODE_BLOCK_PATTERN[] = {
    VlcEntry(2, 0), VlcEntry(4, 0), VlcEntry(6, 0), VlcEntry(8, 0), VlcEntry(10, 0), VlcEntry(12, 0), VlcEntry(14, 0),
    VlcEntry(16, 0), VlcEntry(18, 0), VlcEntry(20, 0), VlcEntry(22, 0), VlcEntry(24, 0), VlcEntry(26, 0),
    VlcEntry(0, 60), VlcEntry(28, 0), VlcEntry(30, 0), VlcEntry(32, 0), VlcEntry(34, 0), VlcEntry(36, 0),
    VlcEntry(38, 0), VlcEntry(40, 0), VlcEntry(42, 0), VlcEntry(44, 0), VlcEntry(46, 0), VlcEntry(0, 32),
    VlcEntry(0, 16), VlcEntry(0, 8), VlcEntry(0, 4), VlcEntry(48, 0), VlcEntry(50, 0), VlcEntry(52, 0),
    VlcEntry(54, 0), VlcEntry(56, 0), VlcEntry(58, 0), VlcEntry(60, 0), VlcEntry(62, 0), VlcEntry(0, 62),
    VlcEntry(0, 2), VlcEntry(0, 61), VlcEntry(0, 1), VlcEntry(0, 56), VlcEntry(0, 52), VlcEntry(0, 44),
    VlcEntry(0, 28), VlcEntry(0, 40), VlcEntry(0, 20), VlcEntry(0, 48), VlcEntry(0, 12), VlcEntry(64, 0),
    VlcEntry(66, 0), VlcEntry(68, 0), VlcEntry(70, 0), VlcEntry(72, 0), VlcEntry(74, 0), VlcEntry(76, 0),
    VlcEntry(78, 0), VlcEntry(80, 0), VlcEntry(82, 0), VlcEntry(84, 0), VlcEntry(86, 0), VlcEntry(0, 63),
    VlcEntry(0, 3), VlcEntry(0, 36), VlcEntry(0, 24), VlcEntry(88, 0), VlcEntry(90, 0), VlcEntry(92, 0),
    VlcEntry(94, 0), VlcEntry(96, 0), VlcEntry(98, 0), VlcEntry(100, 0), VlcEntry(102, 0), VlcEntry(104, 0),
    VlcEntry(106, 0), VlcEntry(108, 0), VlcEntry(110, 0), VlcEntry(112, 0), VlcEntry(114, 0), VlcEntry(116, 0),
    VlcEntry(118, 0), VlcEntry(0, 34), VlcEntry(0, 18), VlcEntry(0, 10), VlcEntry(0, 6), VlcEntry(0, 33),
    VlcEntry(0, 17), VlcEntry(0, 9), VlcEntry(0, 5), VlcEntry(-1, 0), VlcEntry(120, 0), VlcEntry(122, 0),
    VlcEntry(124, 0), VlcEntry(0, 58), VlcEntry(0, 54), VlcEntry(0, 46), VlcEntry(0, 30), VlcEntry(0, 57),
    VlcEntry(0, 53), VlcEntry(0, 45), VlcEntry(0, 29), VlcEntry(0, 38), VlcEntry(0, 26), VlcEntry(0, 37),
    VlcEntry(0, 25), VlcEntry(0, 43), VlcEntry(0, 23), VlcEntry(0, 51), VlcEntry(0, 15), VlcEntry(0, 42),
    VlcEntry(0, 22), VlcEntry(0, 50), VlcEntry(0, 14), VlcEntry(0, 41), VlcEntry(0, 21), VlcEntry(0, 49),
    VlcEntry(0, 13), VlcEntry(0, 35), VlcEntry(0, 19), VlcEntry(0, 11), VlcEntry(0, 7), VlcEntry(0, 39),
    VlcEntry(0, 27), VlcEntry(0, 59), VlcEntry(0, 55), VlcEntry(0, 47), VlcEntry(0, 31),
};

static const VlcEntry MOTION[] = {
    VlcEntry(2, 0), VlcEntry(0, 0), VlcEntry(4, 0), VlcEntry(6, 0), VlcEntry(8, 0), VlcEntry(10, 0), VlcEntry(0, 1),
    VlcEntry(0, -1), VlcEntry(12, 0), VlcEntry(14, 0), VlcEntry(0, 2), VlcEntry(0, -2), VlcEntry(16, 0),
    VlcEntry(18, 0), VlcEntry(0, 3), VlcEntry(0, -3), VlcEntry(20, 0), VlcEntry(22, 0), VlcEntry(24, 0),
    VlcEntry(26, 0), VlcEntry(-1, 0), VlcEntry(28, 0), VlcEntry(30, 0), VlcEntry(32, 0), VlcEntry(34, 0),
    VlcEntry(36, 0), VlcEntry(0, 4), VlcEntry(0, -4), VlcEntry(-1, 0), VlcEntry(38, 0), VlcEntry(40, 0),
    VlcEntry(42, 0), VlcEntry(0, 7), VlcEntry(0, -7), VlcEntry(0, 6), VlcEntry(0, -6), VlcEntry(0, 5),
    VlcEntry(0, -5), VlcEntry(44, 0), VlcEntry(46, 0), VlcEntry(48, 0), VlcEntry(50, 0), VlcEntry(52, 0),
    VlcEntry(54, 0), VlcEntry(56, 0), VlcEntry(58, 0), VlcEntry(60, 0), VlcEntry(62, 0), VlcEntry(64, 0),
    VlcEntry(66, 0), VlcEntry(0, 10), VlcEntry(0, -10), VlcEntry(0, 9), VlcEntry(0, -9), VlcEntry(0, 8),
    VlcEntry(0, -8), VlcEntry(0, 16), VlcEntry(0, -16), VlcEntry(0, 15), VlcEntry(0, -15), VlcEntry(0, 14),
    VlcEntry(0, -14), VlcEntry(0, 13), VlcEntry(0, -13), VlcEntry(0, 12), VlcEntry(0, -12), VlcEntry(0, 11),
    VlcEntry(0, -11),
};

static const VlcEntry DCT_SIZE_LUMINANCE[] = {
    VlcEntry(2, 0), VlcEntry(4, 0), VlcEntry(0, 1), VlcEntry(0, 2), VlcEntry(6, 0), VlcEntry(8, 0), VlcEntry(0, 0),
    VlcEntry(0, 3), VlcEntry(0, 4), VlcEntry(10, 0), VlcEntry(0, 5), VlcEntry(12, 0), VlcEntry(0, 6), VlcEntry(14, 0),
    VlcEntry(0, 7), VlcEntry(16, 0), VlcEntry(0, 8), VlcEntry(-1, 0),
};

static const VlcEntry DCT_SIZE_CHROMINANCE[] = {
    VlcEntry(2, 0), VlcEntry(4, 0), VlcEntry(0, 0), VlcEntry(0, 1), VlcEntry(0, 2), VlcEntry(6, 0), VlcEntry(0, 3),
    VlcEntry(8, 0), VlcEntry(0, 4), VlcEntry(10, 0), VlcEntry(0, 5), VlcEntry(12, 0), VlcEntry(0, 6), VlcEntry(14, 0),
    VlcEntry(0, 7), VlcEntry(16, 0), VlcEntry(0, 8), VlcEntry(-1, 0),
};

static const VlcUintEntry DCT_COEFF[] = {
    VlcUintEntry(2, 0), VlcUintEntry(0, 0x0001), VlcUintEntry(4, 0), VlcUintEntry(6, 0), VlcUintEntry(8, 0),
    VlcUintEntry(10, 0), VlcUintEntry(12, 0), VlcUintEntry(0, 0x0101), VlcUintEntry(14, 0), VlcUintEntry(16, 0),
    VlcUintEntry(18, 0), VlcUintEntry(20, 0), VlcUintEntry(0, 0x0002), VlcUintEntry(0, 0x0201), VlcUintEntry(22, 0),
    VlcUintEntry(24, 0), VlcUintEntry(26, 0), VlcUintEntry(28, 0), VlcUintEntry(30, 0), VlcUintEntry(0, 0x0003),
    VlcUintEntry(0, 0x0401), VlcUintEntry(0, 0x0301), VlcUintEntry(32, 0), VlcUintEntry(0, 0xffff),
    VlcUintEntry(34, 0), VlcUintEntry(36, 0), VlcUintEntry(0, 0x0701), VlcUintEntry(0, 0x0601),
    VlcUintEntry(0, 0x0102), VlcUintEntry(0, 0x0501), VlcUintEntry(38, 0), VlcUintEntry(40, 0), VlcUintEntry(42, 0),
    VlcUintEntry(44, 0), VlcUintEntry(0, 0x0202), VlcUintEntry(0, 0x0901), VlcUintEntry(0, 0x0004),
    VlcUintEntry(0, 0x0801), VlcUintEntry(46, 0), VlcUintEntry(48, 0), VlcUintEntry(50, 0), VlcUintEntry(52, 0),
    VlcUintEntry(54, 0), VlcUintEntry(56, 0), VlcUintEntry(58, 0), VlcUintEntry(60, 0), VlcUintEntry(0, 0x0d01),
    VlcUintEntry(0, 0x0006), VlcUintEntry(0, 0x0c01), VlcUintEntry(0, 0x0b01), VlcUintEntry(0, 0x0302),
    VlcUintEntry(0, 0x0103), VlcUintEntry(0, 0x0005), VlcUintEntry(0, 0x0a01), VlcUintEntry(62, 0),
    VlcUintEntry(64, 0), VlcUintEntry(66, 0), VlcUintEntry(68, 0), VlcUintEntry(70, 0), VlcUintEntry(72, 0),
    VlcUintEntry(74, 0), VlcUintEntry(76, 0), VlcUintEntry(78, 0), VlcUintEntry(80, 0), VlcUintEntry(82, 0),
    VlcUintEntry(84, 0), VlcUintEntry(86, 0), VlcUintEntry(88, 0), VlcUintEntry(90, 0), VlcUintEntry(92, 0),
    VlcUintEntry(0, 0x1001), VlcUintEntry(0, 0x0502), VlcUintEntry(0, 0x0007), VlcUintEntry(0, 0x0203),
    VlcUintEntry(0, 0x0104), VlcUintEntry(0, 0x0f01), VlcUintEntry(0, 0x0e01), VlcUintEntry(0, 0x0402),
    VlcUintEntry(94, 0), VlcUintEntry(96, 0), VlcUintEntry(98, 0), VlcUintEntry(100, 0), VlcUintEntry(102, 0),
    VlcUintEntry(104, 0), VlcUintEntry(106, 0), VlcUintEntry(108, 0), VlcUintEntry(110, 0), VlcUintEntry(112, 0),
    VlcUintEntry(114, 0), VlcUintEntry(116, 0), VlcUintEntry(118, 0), VlcUintEntry(120, 0), VlcUintEntry(122, 0),
    VlcUintEntry(124, 0), VlcUintEntry(-1, 0), VlcUintEntry(126, 0), VlcUintEntry(128, 0), VlcUintEntry(130, 0),
    VlcUintEntry(132, 0), VlcUintEntry(134, 0), VlcUintEntry(136, 0), VlcUintEntry(138, 0), VlcUintEntry(140, 0),
    VlcUintEntry(142, 0), VlcUintEntry(144, 0), VlcUintEntry(146, 0), VlcUintEntry(148, 0), VlcUintEntry(150, 0),
    VlcUintEntry(152, 0), VlcUintEntry(154, 0), VlcUintEntry(0, 0x000b), VlcUintEntry(0, 0x0802),
    VlcUintEntry(0, 0x0403), VlcUintEntry(0, 0x000a), VlcUintEntry(0, 0x0204), VlcUintEntry(0, 0x0702),
    VlcUintEntry(0, 0x1501), VlcUintEntry(0, 0x1401), VlcUintEntry(0, 0x0009), VlcUintEntry(0, 0x1301),
    VlcUintEntry(0, 0x1201), VlcUintEntry(0, 0x0105), VlcUintEntry(0, 0x0303), VlcUintEntry(0, 0x0008),
    VlcUintEntry(0, 0x0602), VlcUintEntry(0, 0x1101), VlcUintEntry(156, 0), VlcUintEntry(158, 0),
    VlcUintEntry(160, 0), VlcUintEntry(162, 0), VlcUintEntry(164, 0), VlcUintEntry(166, 0), VlcUintEntry(168, 0),
    VlcUintEntry(170, 0), VlcUintEntry(172, 0), VlcUintEntry(174, 0), VlcUintEntry(176, 0), VlcUintEntry(178, 0),
    VlcUintEntry(180, 0), VlcUintEntry(182, 0), VlcUintEntry(0, 0x0a02), VlcUintEntry(0, 0x0902),
    VlcUintEntry(0, 0x0503), VlcUintEntry(0, 0x0304), VlcUintEntry(0, 0x0205), VlcUintEntry(0, 0x0107),
    VlcUintEntry(0, 0x0106), VlcUintEntry(0, 0x000f), VlcUintEntry(0, 0x000e), VlcUintEntry(0, 0x000d),
    VlcUintEntry(0, 0x000c), VlcUintEntry(0, 0x1a01), VlcUintEntry(0, 0x1901), VlcUintEntry(0, 0x1801),
    VlcUintEntry(0, 0x1701), VlcUintEntry(0, 0x1601), VlcUintEntry(184, 0), VlcUintEntry(186, 0),
    VlcUintEntry(188, 0), VlcUintEntry(190, 0), VlcUintEntry(192, 0), VlcUintEntry(194, 0), VlcUintEntry(196, 0),
    VlcUintEntry(198, 0), VlcUintEntry(200, 0), VlcUintEntry(202, 0), VlcUintEntry(204, 0), VlcUintEntry(206, 0),
    VlcUintEntry(0, 0x001f), VlcUintEntry(0, 0x001e), VlcUintEntry(0, 0x001d), VlcUintEntry(0, 0x001c),
    VlcUintEntry(0, 0x001b), VlcUintEntry(0, 0x001a), VlcUintEntry(0, 0x0019), VlcUintEntry(0, 0x0018),
    VlcUintEntry(0, 0x0017), VlcUintEntry(0, 0x0016), VlcUintEntry(0, 0x0015), VlcUintEntry(0, 0x0014),
    VlcUintEntry(0, 0x0013), VlcUintEntry(0, 0x0012), VlcUintEntry(0, 0x0011), VlcUintEntry(0, 0x0010),
    VlcUintEntry(208, 0), VlcUintEntry(210, 0), VlcUintEntry(212, 0), VlcUintEntry(214, 0), VlcUintEntry(216, 0),
    VlcUintEntry(218, 0), VlcUintEntry(220, 0), VlcUintEntry(222, 0), VlcUintEntry(0, 0x0028),
    VlcUintEntry(0, 0x0027), VlcUintEntry(0, 0x0026), VlcUintEntry(0, 0x0025), VlcUintEntry(0, 0x0024),
    VlcUintEntry(0, 0x0023), VlcUintEntry(0, 0x0022), VlcUintEntry(0, 0x0021), VlcUintEntry(0, 0x0020),
    VlcUintEntry(0, 0x010e), VlcUintEntry(0, 0x010d), VlcUintEntry(0, 0x010c), VlcUintEntry(0, 0x010b),
    VlcUintEntry(0, 0x010a), VlcUintEntry(0, 0x0109), VlcUintEntry(0, 0x0108), VlcUintEntry(0, 0x0112),
    VlcUintEntry(0, 0x0111), VlcUintEntry(0, 0x0110), VlcUintEntry(0, 0x010f), VlcUintEntry(0, 0x0603),
    VlcUintEntry(0, 0x1002), VlcUintEntry(0, 0x0f02), VlcUintEntry(0, 0x0e02), VlcUintEntry(0, 0x0d02),
    VlcUintEntry(0, 0x0c02), VlcUintEntry(0, 0x0b02), VlcUintEntry(0, 0x1f01), VlcUintEntry(0, 0x1e01),
    VlcUintEntry(0, 0x1d01), VlcUintEntry(0, 0x1c01), VlcUintEntry(0, 0x1b01),
};

static const VlcUintPrefix DCT_COEFF_PREFIX(DCT_COEFF);

// Most coefficients, their sign and the end-of-block marker fit in one byte.
// Keep separate tables for the first non-intra coefficient, where a leading
// one is a coefficient rather than the start of the two-bit EOB marker.
struct DctCoefficientPrefix {
    struct Entry {
        short level;
        unsigned char run;
        unsigned char bits;
    };
    Entry entries[2][256];
    DctCoefficientPrefix() : entries() {
        for (unsigned haveCoefficient = 0; haveCoefficient < 2; ++haveCoefficient) {
            for (unsigned key = 0; key < 256; ++key) {
                const VlcUintPrefix::Entry& code = DCT_COEFF_PREFIX.entries[key];
                if (code.next > 0 || code.value == 0xffff) continue;
                unsigned bits = code.bits;
                Entry& result = entries[haveCoefficient][key];
                if (code.value == 1 && haveCoefficient) {
                    if (bits == 8) continue;
                    const unsigned more = (key >> (7 - bits)) & 1;
                    ++bits;
                    if (!more) {
                        result.run = 255;
                        result.bits = (unsigned char)bits;
                        continue;
                    }
                }
                if (bits == 8) continue;
                const bool negative = ((key >> (7 - bits)) & 1) != 0;
                const int level = code.value & 255;
                result.level = (short)(negative ? -level : level);
                result.run = (unsigned char)(code.value >> 8);
                result.bits = (unsigned char)(bits + 1);
            }
        }
    }
};

static const DctCoefficientPrefix DCT_COEFFICIENT_PREFIX;

static inline bool readDctCoefficient(Buffer& buffer, bool haveCoefficient,
                                      int* run, int* level) {
    // Never refill for the lookup. A short tail, escape or long code follows
    // the original reader, including its exact EOF and refill behaviour.
    const size_t bits = buffer.data.size() << 3;
    if (buffer.bitIndex <= bits && bits - buffer.bitIndex >= 8) {
        const size_t byte = buffer.bitIndex >> 3;
        const unsigned offset = buffer.bitIndex & 7;
        unsigned key = buffer.data[byte];
        if (offset) key = ((key << 8) | buffer.data[byte + 1]) >> (8 - offset);
        const DctCoefficientPrefix::Entry& entry =
            DCT_COEFFICIENT_PREFIX.entries[haveCoefficient ? 1 : 0][key & 255];
        if (entry.bits) {
            buffer.bitIndex += entry.bits;
            if (entry.run == 255) return false;
            *run = entry.run;
            *level = entry.level;
            return true;
        }
    }

    const int coeff = buffer.readVlcUint(DCT_COEFF_PREFIX);
    if (coeff == 0x0001 && haveCoefficient && buffer.read(1) == 0) return false;
    if (coeff == 0xffff) {
        *run = (int)buffer.read(6);
        *level = (int)buffer.read(8);
        if (*level == 0) {
            *level = (int)buffer.read(8);
        } else if (*level == 128) {
            *level = (int)buffer.read(8) - 256;
        } else if (*level > 128) {
            *level -= 256;
        }
    } else {
        *run = coeff >> 8;
        *level = coeff & 0xff;
        if (buffer.read(1) != 0) *level = -*level;
    }
    return true;
}

static const VlcEntry* macroblockTypeTable(int pictureType) {
    if (pictureType == PICTURE_TYPE_INTRA) {
        return MACROBLOCK_TYPE_INTRA;
    }
    if (pictureType == PICTURE_TYPE_PREDICTIVE) {
        return MACROBLOCK_TYPE_PREDICTIVE;
    }
    return MACROBLOCK_TYPE_B;
}

static const VlcEntry* dctSizeTable(int planeIndex) {
    return planeIndex == 0 ? DCT_SIZE_LUMINANCE : DCT_SIZE_CHROMINANCE;
}

static bool isSliceStartCode(int code) {
    return code >= START_SLICE_FIRST && code <= START_SLICE_LAST;
}

static int clampI32(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static int clampU8(int value) {
    return clampI32(value, 0, 255);
}

// 8x8 block writers and helpers, defined below but used by the Video methods.
static void blockSetConst(std::vector<u8>& d, int di, int dw, int value);
static void blockSetOverwrite(std::vector<u8>& d, int di, int dw, const int s[64]);
static void blockSetAddConst(std::vector<u8>& d, int di, int dw, int value);
static void blockSetAdd(std::vector<u8>& d, int di, int dw, const int s[64]);
static void processMacroblock(std::vector<u8>& s, std::vector<u8>& d, int mbRow, int mbCol,
                              int mbWidth, int mbHeight, int motionH, int motionV, int blockSize,
                              bool interpolate);
static void idct(int block[64]);

Video::Video(std::vector<u8> videoEs) : Video(Buffer(std::move(videoEs))) {}

Video::Video(Buffer input)
    : buffer(std::move(input)), hasSequenceHeader(false), mWidth(0), mHeight(0), mbWidth(0),
      mbHeight(0), mbSize(0), lumaWidth(0), lumaHeight(0), chromaWidth(0), chromaHeight(0),
      mFramerate(0.0), mPixelAspectRatio(0.0), time(0.0), mLastFrameTime(0.0), framesDecoded(0),
      startCode(-1), pictureType(0),
      quantizerScale(0), sliceBegin(false), macroblockAddress(0), mbRow(0), mbCol(0),
      macroblockType(0), macroblockIntra(false), cur(0), fwd(1), bwd(2),
      hasReferenceFrame(false), assumeNoBFrames(false) {
    dcPredictor[0] = dcPredictor[1] = dcPredictor[2] = 128;
    for (int i = 0; i < 64; ++i) {
        blockData[i] = 0;
        intraQuantMatrix[i] = 0;
        nonIntraQuantMatrix[i] = 0;
    }
    startCode = buffer.findStartCode(START_SEQUENCE);
    if (startCode != -1) {
        decodeSequenceHeader();
    }
}

bool Video::hasHeader() {
    if (hasSequenceHeader) {
        return true;
    }
    if (startCode != START_SEQUENCE) {
        startCode = buffer.findStartCode(START_SEQUENCE);
    }
    if (startCode == -1) {
        return false;
    }
    decodeSequenceHeader();
    return hasSequenceHeader;
}

bool Video::decode(size_t* frameIndex, bool skipB, bool* skipped,
                   DecodeDiagnostics* diagnostics) {
    buffer.discardReadBytes();
    if (skipped) *skipped = false;
    if (!hasHeader()) {
        return false;
    }
    for (;;) {
        if (startCode != START_PICTURE) {
            startCode = buffer.findStartCode(START_PICTURE);
            if (startCode == -1) {
                if (hasReferenceFrame && !assumeNoBFrames && buffer.hasEnded() &&
                    (pictureType == PICTURE_TYPE_INTRA || pictureType == PICTURE_TYPE_PREDICTIVE)) {
                    hasReferenceFrame = false;
                    *frameIndex = (size_t)bwd;
                    return true;
                }
                return false;
            }
        }

        if (buffer.hasStartCode(START_PICTURE) == -1 && !buffer.hasEnded()) {
            return false;
        }

        if (skipB && skipped && buffer.has(29)) {
            const size_t saved = buffer.bitIndex;
            buffer.skip(10);
            const int type = (int)buffer.read(3);
            buffer.bitIndex = saved;
            if (type == PICTURE_TYPE_B) {
#ifdef PSP
                const unsigned skipBegin = diagnostics ? sceKernelGetSystemTimeLow() : 0;
#endif
                // B pictures never become prediction references. Leave all
                // frame buffers/rotation untouched and consume one display slot.
                pictureType = type;
                startCode = buffer.findStartCode(START_PICTURE);
                mLastFrameTime = time;
                ++framesDecoded;
                time = (double)framesDecoded / mFramerate;
                *skipped = true;
                if (diagnostics) {
                    ++diagnostics->skippedB;
#ifdef PSP
                    const unsigned micros = sceKernelGetSystemTimeLow() - skipBegin;
                    diagnostics->skipMicros += micros;
                    diagnostics->skipMaxMicros = std::max(diagnostics->skipMaxMicros, micros);
#endif
                }
                return true;
            }
        }
#ifdef PSP
        const unsigned pictureBegin = diagnostics ? sceKernelGetSystemTimeLow() : 0;
#endif
        decodePicture();
        if (diagnostics) {
            const unsigned type = pictureType >= PICTURE_TYPE_INTRA && pictureType <= PICTURE_TYPE_B
                                ? (unsigned)pictureType : 0;
            ++diagnostics->pictureAttempts[type];
#ifdef PSP
            const unsigned micros = sceKernelGetSystemTimeLow() - pictureBegin;
            diagnostics->pictureMicros[type] += micros;
            diagnostics->pictureMaxMicros[type] = std::max(diagnostics->pictureMaxMicros[type], micros);
#endif
        }
        size_t frame = 0;
        bool hasFrame = false;
        if (assumeNoBFrames) {
            frame = (size_t)bwd;
            hasFrame = true;
        } else if (pictureType == PICTURE_TYPE_B) {
            frame = (size_t)cur;
            hasFrame = true;
        } else if (hasReferenceFrame) {
            frame = (size_t)fwd;
            hasFrame = true;
        } else {
            hasReferenceFrame = true;
            hasFrame = false;
        }
        if (hasFrame) {
            mLastFrameTime = time;
            ++framesDecoded;
            time = (double)framesDecoded / mFramerate;
            *frameIndex = frame;
            return true;
        }
    }
}

void Video::decodeSequenceHeader() {
    int maxHeaderSize = 64 + 2 * 64 * 8;
    if (!buffer.has(maxHeaderSize)) {
        return;
    }

    mWidth = (int)buffer.read(12);
    mHeight = (int)buffer.read(12);
    if (mWidth == 0 || mHeight == 0) {
        return;
    }

    int parCode = (int)buffer.read(4) - 1;
    if (parCode < 0) {
        parCode = 0;
    }
    if (parCode >= 14) {
        parCode = 13;
    }
    mPixelAspectRatio = PIXEL_ASPECT_RATIO[parCode];
    mFramerate = PICTURE_RATE[buffer.read(4) & 0x0f];

    buffer.skip(18 + 1 + 10 + 1);
    if (buffer.read(1) != 0) {
        for (int i = 0; i < 64; ++i) {
            intraQuantMatrix[ZIG_ZAG[i]] = (int)buffer.read(8);
        }
    } else {
        for (int i = 0; i < 64; ++i) {
            intraQuantMatrix[i] = INTRA_QUANT_MATRIX[i];
        }
    }

    if (buffer.read(1) != 0) {
        for (int i = 0; i < 64; ++i) {
            nonIntraQuantMatrix[ZIG_ZAG[i]] = (int)buffer.read(8);
        }
    } else {
        for (int i = 0; i < 64; ++i) {
            nonIntraQuantMatrix[i] = 16;
        }
    }

    mbWidth = (mWidth + 15) >> 4;
    mbHeight = (mHeight + 15) >> 4;
    mbSize = mbWidth * mbHeight;
    lumaWidth = mbWidth << 4;
    lumaHeight = mbHeight << 4;
    chromaWidth = mbWidth << 3;
    chromaHeight = mbHeight << 3;

    frames.clear();
    frames.push_back(makeFrame());
    frames.push_back(makeFrame());
    frames.push_back(makeFrame());
    cur = 0;
    fwd = 1;
    bwd = 2;
    hasReferenceFrame = false;
    time = 0.0;
    mLastFrameTime = 0.0;
    framesDecoded = 0;
    hasSequenceHeader = true;
}

Frame Video::makeFrame() const {
    std::vector<u8> luma((size_t)lumaWidth * (size_t)lumaHeight, 0);
    std::vector<u8> chroma((size_t)chromaWidth * (size_t)chromaHeight, 0);
    return Frame((size_t)mWidth, (size_t)mHeight,
                 Plane((size_t)lumaWidth, (size_t)lumaHeight, luma),
                 Plane((size_t)chromaWidth, (size_t)chromaHeight, chroma),
                 Plane((size_t)chromaWidth, (size_t)chromaHeight, chroma));
}

void Video::decodePicture() {
    buffer.skip(10);
    pictureType = (int)buffer.read(3);
    buffer.skip(16);
    if (pictureType <= 0 || pictureType > PICTURE_TYPE_B) {
        return;
    }

    if (pictureType == PICTURE_TYPE_PREDICTIVE || pictureType == PICTURE_TYPE_B) {
        motionForward.fullPx = buffer.read(1) != 0;
        int fCode = (int)buffer.read(3);
        if (fCode == 0) {
            return;
        }
        motionForward.rSize = fCode - 1;
    }

    if (pictureType == PICTURE_TYPE_B) {
        motionBackward.fullPx = buffer.read(1) != 0;
        int fCode = (int)buffer.read(3);
        if (fCode == 0) {
            return;
        }
        motionBackward.rSize = fCode - 1;
    }

    int frameTemp = fwd;
    if (pictureType == PICTURE_TYPE_INTRA || pictureType == PICTURE_TYPE_PREDICTIVE) {
        fwd = bwd;
    }

    do {
        startCode = buffer.nextStartCode();
    } while (startCode == START_EXTENSION || startCode == START_USER_DATA);

    while (isSliceStartCode(startCode)) {
        decodeSlice(startCode & 0xff);
        if (macroblockAddress >= mbSize - 1) {
            break;
        }
        startCode = buffer.nextStartCode();
    }

    if (pictureType == PICTURE_TYPE_INTRA || pictureType == PICTURE_TYPE_PREDICTIVE) {
        bwd = cur;
        cur = frameTemp;
    }
}

void Video::decodeSlice(int slice) {
    sliceBegin = true;
    macroblockAddress = (slice - 1) * mbWidth - 1;
    motionForward.h = 0;
    motionForward.v = 0;
    motionBackward.h = 0;
    motionBackward.v = 0;
    dcPredictor[0] = 128;
    dcPredictor[1] = 128;
    dcPredictor[2] = 128;
    quantizerScale = (int)buffer.read(5);
    while (buffer.read(1) != 0) {
        buffer.skip(8);
    }
    for (;;) {
        decodeMacroblock();
        if (macroblockAddress >= mbSize - 1 || !buffer.peekNonZero(23)) {
            break;
        }
    }
}

void Video::decodeMacroblock() {
    int increment = 0;
    int t = (int)buffer.readVlc(MACROBLOCK_ADDRESS_INCREMENT);
    while (t == 34) {
        t = (int)buffer.readVlc(MACROBLOCK_ADDRESS_INCREMENT);
    }
    while (t == 35) {
        increment += 33;
        t = (int)buffer.readVlc(MACROBLOCK_ADDRESS_INCREMENT);
    }
    increment += t;

    if (sliceBegin) {
        sliceBegin = false;
        macroblockAddress += increment;
    } else {
        if (macroblockAddress + increment >= mbSize) {
            return;
        }
        if (increment > 1) {
            dcPredictor[0] = 128;
            dcPredictor[1] = 128;
            dcPredictor[2] = 128;
            if (pictureType == PICTURE_TYPE_PREDICTIVE) {
                motionForward.h = 0;
                motionForward.v = 0;
            }
        }
        while (increment > 1) {
            ++macroblockAddress;
            mbRow = macroblockAddress / mbWidth;
            mbCol = macroblockAddress % mbWidth;
            predictMacroblock();
            --increment;
        }
        ++macroblockAddress;
    }

    if (macroblockAddress < 0) {
        return;
    }
    mbRow = macroblockAddress / mbWidth;
    mbCol = macroblockAddress % mbWidth;
    if (mbCol >= mbWidth || mbRow >= mbHeight) {
        return;
    }

    macroblockType = (int)buffer.readVlc(macroblockTypeTable(pictureType));
    macroblockIntra = (macroblockType & 0x01) != 0;
    motionForward.isSet = (macroblockType & 0x08) != 0;
    motionBackward.isSet = (macroblockType & 0x04) != 0;

    if ((macroblockType & 0x10) != 0) {
        quantizerScale = (int)buffer.read(5);
    }

    if (macroblockIntra) {
        motionForward.h = 0;
        motionForward.v = 0;
        motionBackward.h = 0;
        motionBackward.v = 0;
    } else {
        dcPredictor[0] = 128;
        dcPredictor[1] = 128;
        dcPredictor[2] = 128;
        decodeMotionVectors();
        predictMacroblock();
    }

    int cbp;
    if ((macroblockType & 0x02) != 0) {
        cbp = (int)buffer.readVlc(CODE_BLOCK_PATTERN);
    } else if (macroblockIntra) {
        cbp = 0x3f;
    } else {
        cbp = 0;
    }

    int mask = 0x20;
    for (int block = 0; block < 6; ++block) {
        if ((cbp & mask) != 0) {
            decodeBlock(block);
        }
        mask >>= 1;
    }
}

void Video::decodeMotionVectors() {
    if (motionForward.isSet) {
        int rSize = motionForward.rSize;
        motionForward.h = decodeMotionVector(rSize, motionForward.h);
        motionForward.v = decodeMotionVector(rSize, motionForward.v);
    } else if (pictureType == PICTURE_TYPE_PREDICTIVE) {
        motionForward.h = 0;
        motionForward.v = 0;
    }

    if (motionBackward.isSet) {
        int rSize = motionBackward.rSize;
        motionBackward.h = decodeMotionVector(rSize, motionBackward.h);
        motionBackward.v = decodeMotionVector(rSize, motionBackward.v);
    }
}

int Video::decodeMotionVector(int rSize, int motion) {
    int fscale = 1 << rSize;
    int mCode = (int)buffer.readVlc(MOTION);
    int d;
    if (mCode != 0 && fscale != 1) {
        int r = (int)buffer.read(rSize);
        int dd = ((mCode < 0 ? -mCode : mCode) - 1) << rSize;
        dd += r + 1;
        if (mCode < 0) {
            dd = -dd;
        }
        d = dd;
    } else {
        d = mCode;
    }
    motion += d;
    if (motion > (fscale << 4) - 1) {
        motion -= fscale << 5;
    } else if (motion < -(fscale << 4)) {
        motion += fscale << 5;
    }
    return motion;
}

void Video::predictMacroblock() {
    int fwH = motionForward.h;
    int fwV = motionForward.v;
    if (motionForward.fullPx) {
        fwH <<= 1;
        fwV <<= 1;
    }

    if (pictureType == PICTURE_TYPE_B) {
        int bwH = motionBackward.h;
        int bwV = motionBackward.v;
        if (motionBackward.fullPx) {
            bwH <<= 1;
            bwV <<= 1;
        }
        if (motionForward.isSet) {
            copyOrInterpolateMacroblock(fwd, fwH, fwV, false);
            if (motionBackward.isSet) {
                copyOrInterpolateMacroblock(bwd, bwH, bwV, true);
            }
        } else {
            copyOrInterpolateMacroblock(bwd, bwH, bwV, false);
        }
    } else {
        copyOrInterpolateMacroblock(fwd, fwH, fwV, false);
    }
}

void Video::copyOrInterpolateMacroblock(int srcIdx, int mh, int mv, bool interpolate) {
    if (srcIdx == cur || srcIdx < 0 || (size_t)srcIdx >= frames.size()) {
        return;
    }
    if ((size_t)cur >= frames.size()) {
        return;
    }
    Frame* dst = &frames[(size_t)cur];
    Frame* src = &frames[(size_t)srcIdx];
    processMacroblock(src->y.data, dst->y.data, mbRow, mbCol, mbWidth, mbHeight, mh, mv, 16, interpolate);
    processMacroblock(src->cr.data, dst->cr.data, mbRow, mbCol, mbWidth, mbHeight, mh / 2, mv / 2, 8, interpolate);
    processMacroblock(src->cb.data, dst->cb.data, mbRow, mbCol, mbWidth, mbHeight, mh / 2, mv / 2, 8, interpolate);
}

void Video::decodeBlock(int block) {
    int n = 0;
    bool intra = macroblockIntra;

    if (intra) {
        int planeIndex = block > 3 ? block - 3 : 0;
        int predictor = dcPredictor[planeIndex];
        int dctSize = (int)buffer.readVlc(dctSizeTable(planeIndex));
        if (dctSize > 0) {
            int differential = (int)buffer.read(dctSize);
            if ((differential & (1 << (dctSize - 1))) != 0) {
                blockData[0] = predictor + differential;
            } else {
                blockData[0] = predictor + (-(1 << dctSize) | (differential + 1));
            }
        } else {
            blockData[0] = predictor;
        }
        dcPredictor[planeIndex] = blockData[0];
        blockData[0] <<= 8;
        n = 1;
    }

    const int* quantMatrix = intra ? intraQuantMatrix : nonIntraQuantMatrix;
    for (;;) {
        int run;
        int level;
        if (!readDctCoefficient(buffer, n > 0, &run, &level)) break;

        n += run;
        if (n >= 64) {
            return;
        }
        int deZigZagged = ZIG_ZAG[n];
        ++n;

        // Coefficients can be negative; signed left shift is undefined in C++.
        level *= 2;
        if (!intra) {
            level += level < 0 ? -1 : 1;
        }
        level = (level * quantizerScale * quantMatrix[deZigZagged]) >> 4;
        if ((level & 1) == 0) {
            level -= level > 0 ? 1 : -1;
        }
        level = clampI32(level, -2048, 2047);
        blockData[deZigZagged] = level * PREMULTIPLIER_MATRIX[deZigZagged];
    }

    // Transform in place; no second 64-coefficient array is needed. Clear all
    // entries after output, including stale AC values from an invalid block.
    bool nIsOne = n == 1;
    if (!nIsOne) {
        idct(blockData);
    }

    int dw;
    int di;
    if (block < 4) {
        di = (mbRow * lumaWidth + mbCol) << 4;
        if ((block & 1) != 0) {
            di += 8;
        }
        if ((block & 2) != 0) {
            di += lumaWidth << 3;
        }
        dw = lumaWidth;
    } else {
        di = ((mbRow * lumaWidth) << 2) + (mbCol << 3);
        dw = chromaWidth;
    }

    if ((size_t)cur >= frames.size()) {
        std::memset(blockData, 0, sizeof(blockData));
        return;
    }
    Frame* frame = &frames[(size_t)cur];
    std::vector<u8>* d = 0;
    if (block < 4) {
        d = &frame->y.data;
    } else if (block == 4) {
        d = &frame->cb.data;
    } else {
        d = &frame->cr.data;
    }

    if (intra) {
        if (nIsOne) {
            blockSetConst(*d, di, dw, clampU8((blockData[0] + 128) >> 8));
        } else {
            blockSetOverwrite(*d, di, dw, blockData);
        }
    } else if (nIsOne) {
        blockSetAddConst(*d, di, dw, (blockData[0] + 128) >> 8);
    } else {
        blockSetAdd(*d, di, dw, blockData);
    }
    std::memset(blockData, 0, sizeof(blockData));
}

// Preserve the exact integer conversion, including green's single rounding
// after adding both chroma products. About 5 KiB replaces repeated multiplies.
struct YuvTables {
    int y[256], r[256], b[256], gc[256], gr[256];
    u8 clamp[1024];
    YuvTables() {
        for (int i = 0; i < 256; ++i) {
            y[i] = ((i - 16) * 76309) >> 16;
            r[i] = ((i - 128) * 104597) >> 16;
            b[i] = ((i - 128) * 132201) >> 16;
            gc[i] = (i - 128) * 25674;
            gr[i] = (i - 128) * 53278;
        }
        // All converted components lie in [-278, 534]. Bias by 320 so every
        // lookup stays inside this small table, including extreme chroma.
        for (int i = 0; i < 1024; ++i) clamp[i] = (u8)clampU8(i - 320);
    }
};
static const YuvTables yuv;

static inline u32 rgbFromLuma(u8 luma, int r, int g, int b) {
    const int yy = yuv.y[luma];
    return 0xff000000u | ((u32)yuv.clamp[yy + r + 320] << 16) |
        ((u32)yuv.clamp[yy - g + 320] << 8) | (u32)yuv.clamp[yy + b + 320];
}

static const size_t RGB_MAPPED_COLUMNS = 512;

// Keep the bounded 1 KiB column map off the unscaled/fallback call paths.
// Only the output vector may allocate, before this helper is entered.
static __attribute__((noinline)) void writeRgbMapped(const Frame& frame,
                                                     std::vector<u32>* dst,
                                                     size_t dstW, size_t dstH) {
    u16 sourceX[RGB_MAPPED_COLUMNS];
    const size_t xStep = frame.width / dstW;
    const size_t xRemainder = frame.width % dstW;
    size_t sx = 0, xError = 0;
    for (size_t tx = 0; tx < dstW; ++tx) {
        sourceX[tx] = (u16)sx;
        sx += xStep;
        xError += xRemainder;
        if (xError >= dstW) {
            xError -= dstW;
            ++sx;
        }
    }

    for (size_t ty = 0; ty < dstH;) {
        size_t sy0 = ty * frame.height / dstH;
        if (sy0 >= frame.height) sy0 = frame.height - 1;
        size_t sy1 = sy0;
        if (ty + 1 < dstH) {
            sy1 = (ty + 1) * frame.height / dstH;
            if (sy1 >= frame.height) sy1 = frame.height - 1;
        }
        const u8* yRow0 = frame.y.data.data() + sy0 * frame.y.width;
        const u8* crRow = frame.cr.data.data() + (sy0 >> 1) * frame.cr.width;
        const u8* cbRow = frame.cb.data.data() + (sy0 >> 1) * frame.cb.width;
        u32* out0 = dst->data() + ty * dstW;
        if (ty + 1 < dstH && (sy0 >> 1) == (sy1 >> 1)) {
            const u8* yRow1 = frame.y.data.data() + sy1 * frame.y.width;
            u32* out1 = out0 + dstW;
            for (size_t tx = 0; tx < dstW; ++tx) {
                const size_t x = sourceX[tx];
                const int crValue = crRow[x >> 1];
                const int cbValue = cbRow[x >> 1];
                const int r = yuv.r[crValue];
                const int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
                const int b = yuv.b[cbValue];
                out0[tx] = rgbFromLuma(yRow0[x], r, g, b);
                out1[tx] = rgbFromLuma(yRow1[x], r, g, b);
            }
            ty += 2;
        } else {
            for (size_t tx = 0; tx < dstW; ++tx) {
                const size_t x = sourceX[tx];
                const int crValue = crRow[x >> 1];
                const int cbValue = cbRow[x >> 1];
                const int r = yuv.r[crValue];
                const int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
                const int b = yuv.b[cbValue];
                out0[tx] = rgbFromLuma(yRow0[x], r, g, b);
            }
            ++ty;
        }
    }
}

void Frame::writeRgbScaled(std::vector<u32>* dst, size_t dstW, size_t dstH) const {
    if (!dst || width == 0 || height == 0 || dstW == 0 || dstH == 0) {
        return;
    }
    size_t sourceWidth = width > 1 ? width : 1;
    size_t sourceHeight = height > 1 ? height : 1;
    const size_t total = dstW * dstH;
    if (dst->size() != total) {
        dst->resize(total);
    }
    const size_t chromaWidth = sourceWidth / 2 + sourceWidth % 2;
    const size_t chromaHeight = sourceHeight / 2 + sourceHeight % 2;
    if (y.width < sourceWidth || cr.width < chromaWidth || cb.width < chromaWidth ||
        y.data.size() / y.width < sourceHeight ||
        cr.data.size() / cr.width < chromaHeight || cb.data.size() / cb.width < chromaHeight) {
        std::fill(dst->begin(), dst->end(), 0xff000000u);
        return;
    }

    // Unscaled frames share chroma directly across each 2x2 luma block.
    if (dstW == sourceWidth && dstH == sourceHeight) {
        const size_t yPitch = y.width, crPitch = cr.width, cbPitch = cb.width;
        const size_t rowPairs = sourceHeight / 2, columnPairs = sourceWidth / 2;
        const bool oddWidth = (sourceWidth & 1) != 0;
        const u8* yRows = y.data.data();
        const u8* crRow = cr.data.data();
        const u8* cbRow = cb.data.data();
        u32* outRows = dst->data();

        // Full 2x2 blocks have no edge tests in their inner loop. Snapshot the
        // independent pitches once, and advance pointers through both rows.
        for (size_t row = 0; row < rowPairs; ++row) {
            const u8* y0 = yRows;
            const u8* y1 = yRows + yPitch;
            const u8* crPixel = crRow;
            const u8* cbPixel = cbRow;
            u32* out0 = outRows;
            u32* out1 = outRows + sourceWidth;
            for (size_t column = 0; column < columnPairs; ++column) {
                const int crValue = *crPixel++;
                const int cbValue = *cbPixel++;
                const int r = yuv.r[crValue];
                const int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
                const int b = yuv.b[cbValue];
                out0[0] = rgbFromLuma(y0[0], r, g, b);
                out0[1] = rgbFromLuma(y0[1], r, g, b);
                out1[0] = rgbFromLuma(y1[0], r, g, b);
                out1[1] = rgbFromLuma(y1[1], r, g, b);
                y0 += 2; y1 += 2;
                out0 += 2; out1 += 2;
            }
            if (oddWidth) {
                const int crValue = *crPixel;
                const int cbValue = *cbPixel;
                const int r = yuv.r[crValue];
                const int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
                const int b = yuv.b[cbValue];
                *out0 = rgbFromLuma(*y0, r, g, b);
                *out1 = rgbFromLuma(*y1, r, g, b);
            }
            yRows += yPitch * 2;
            crRow += crPitch; cbRow += cbPitch;
            outRows += sourceWidth * 2;
        }

        // A final odd row uses the same horizontal chroma sharing, without
        // ever forming or reading a nonexistent second luma/output row.
        if (sourceHeight & 1) {
            const u8* y0 = yRows;
            const u8* crPixel = crRow;
            const u8* cbPixel = cbRow;
            u32* out0 = outRows;
            for (size_t column = 0; column < columnPairs; ++column) {
                const int crValue = *crPixel++;
                const int cbValue = *cbPixel++;
                const int r = yuv.r[crValue];
                const int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
                const int b = yuv.b[cbValue];
                out0[0] = rgbFromLuma(y0[0], r, g, b);
                out0[1] = rgbFromLuma(y0[1], r, g, b);
                y0 += 2; out0 += 2;
            }
            if (oddWidth) {
                const int crValue = *crPixel;
                const int cbValue = *cbPixel;
                const int r = yuv.r[crValue];
                const int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
                const int b = yuv.b[cbValue];
                *out0 = rgbFromLuma(*y0, r, g, b);
            }
        }
        return;
    }

    // Build exact floor(tx * sourceWidth / dstW) positions once, then share
    // chroma between adjacent output rows whenever they sample one chroma row.
    // At least 2x vertical downscaling cannot share rows, so keep the original
    // path there. Division avoids overflow in the sourceHeight < 2*dstH test.
    if (dstW <= RGB_MAPPED_COLUMNS && sourceWidth <= 65536 && sourceHeight / dstH < 2) {
        writeRgbMapped(*this, dst, dstW, dstH);
        return;
    }

    // Exact nearest-neighbour stepping without a division per output pixel.
    const size_t xStep = sourceWidth / dstW;
    const size_t xRemainder = sourceWidth % dstW;
    for (size_t ty = 0; ty < dstH; ++ty) {
        size_t sy = ty * sourceHeight / dstH;
        if (sy >= sourceHeight) {
            sy = sourceHeight - 1;
        }
        size_t yRow = sy * y.width;
        size_t cRow = (sy / 2) * cr.width;
        size_t dstRow = ty * dstW;
        size_t sx = 0, xError = 0;
        for (size_t tx = 0; tx < dstW; ++tx) {
            int yv = (int)y.data[yRow + sx];
            size_t ci = cRow + (sx / 2);
            int crValue = cr.data[ci];
            int cbValue = cb.data[(sy / 2) * cb.width + sx / 2];

            int yy = yuv.y[yv];
            int r = yuv.r[crValue];
            int g = (yuv.gc[cbValue] + yuv.gr[crValue]) >> 16;
            int b = yuv.b[cbValue];

            int rr = yuv.clamp[yy + r + 320];
            int gg = yuv.clamp[yy - g + 320];
            int bb = yuv.clamp[yy + b + 320];
            (*dst)[dstRow + tx] = 0xff000000u | ((u32)rr << 16) | ((u32)gg << 8) | (u32)bb;
            sx += xStep;
            xError += xRemainder;
            if (xError >= dstW) {
                xError -= dstW;
                ++sx;
            }
        }
    }
}

static void blockSetConst(std::vector<u8>& d, int di, int dw, int value) {
    u8* dst = &d[(size_t)di];
    for (int y = 0; y < 8; ++y) {
        std::memset(dst + y * dw, value, 8);
    }
}

static void blockSetOverwrite(std::vector<u8>& d, int di, int dw, const int s[64]) {
    u8* dst = &d[(size_t)di];
    for (int y = 0; y < 8; ++y) {
        u8* row = dst + y * dw;
        for (int x = 0; x < 8; ++x) {
            row[x] = (u8)clampU8(s[y * 8 + x]);
        }
    }
}

static void blockSetAddConst(std::vector<u8>& d, int di, int dw, int value) {
    u8* dst = &d[(size_t)di];
    for (int y = 0; y < 8; ++y) {
        u8* row = dst + y * dw;
        for (int x = 0; x < 8; ++x) {
            row[x] = (u8)clampU8((int)row[x] + value);
        }
    }
}

static void blockSetAdd(std::vector<u8>& d, int di, int dw, const int s[64]) {
    u8* dst = &d[(size_t)di];
    for (int y = 0; y < 8; ++y) {
        u8* row = dst + y * dw;
        for (int x = 0; x < 8; ++x) {
            row[x] = (u8)clampU8((int)row[x] + s[y * 8 + x]);
        }
    }
}

template<bool oddH, bool oddV, bool interpolate>
static void processMacroblockRows(const u8* src, u8* dst, int stride, int blockSize) {
    for (int y = 0; y < blockSize; ++y) {
        const u8* source = src + y * stride;
        u8* dest = dst + y * stride;
        if (!oddH && !oddV && !interpolate) {
            std::memcpy(dest, source, (size_t)blockSize);
            continue;
        }
        for (int x = 0; x < blockSize; ++x) {
            int value;
            if (oddH && oddV) {
                value = ((int)source[x] + source[x + 1] + source[x + stride] +
                         source[x + stride + 1] + 2) >> 2;
            } else if (oddH) {
                value = ((int)source[x] + source[x + 1] + 1) >> 1;
            } else if (oddV) {
                value = ((int)source[x] + source[x + stride] + 1) >> 1;
            } else {
                value = source[x];
            }
            // B-frame averaging rounds the motion sample first, then the
            // average with the destination. Combining these changes pixels.
            if (interpolate) value = ((int)dest[x] + value + 1) >> 1;
            dest[x] = (u8)value;
        }
    }
}

template<int blockSize>
static void copyMacroblockRows(const u8* src, u8* dst, int stride) {
    // A constant 8/16-byte length lets the PSP compiler inline each short copy.
    // Motion can leave the source unaligned; memcpy preserves that contract.
    for (int y = 0; y < blockSize; ++y) {
        std::memcpy(dst + y * stride, src + y * stride, blockSize);
    }
}

static void processMacroblock(std::vector<u8>& s, std::vector<u8>& d, int mbRow, int mbCol,
                              int mbWidth, int mbHeight, int motionH, int motionV, int blockSize,
                              bool interpolate) {
    int dw = mbWidth * blockSize;
    int hp = motionH >> 1;
    int vp = motionV >> 1;
    bool oddH = (motionH & 1) != 0;
    bool oddV = (motionV & 1) != 0;

    long long si0 = ((long long)mbRow * blockSize + vp) * dw + (long long)mbCol * blockSize + hp;
    long long di0 = ((long long)mbRow * dw + mbCol) * blockSize;
    long long maxAddress = (long long)dw * ((long long)mbHeight * blockSize - blockSize + 1) - blockSize;
    if (si0 < 0 || si0 > maxAddress || di0 < 0 || di0 > maxAddress) {
        return;
    }

    const u8* src = &s[(size_t)si0];
    u8* dst = &d[(size_t)di0];
    // Select the eight motion modes once for the block. Template constants
    // remove direction and B-frame branches from the pixel loops.
    if (interpolate) {
        if (oddH && oddV) processMacroblockRows<true, true, true>(src, dst, dw, blockSize);
        else if (oddH) processMacroblockRows<true, false, true>(src, dst, dw, blockSize);
        else if (oddV) processMacroblockRows<false, true, true>(src, dst, dw, blockSize);
        else processMacroblockRows<false, false, true>(src, dst, dw, blockSize);
    } else {
        if (oddH && oddV) processMacroblockRows<true, true, false>(src, dst, dw, blockSize);
        else if (oddH) processMacroblockRows<true, false, false>(src, dst, dw, blockSize);
        else if (oddV) processMacroblockRows<false, true, false>(src, dst, dw, blockSize);
        else if (blockSize == 16) copyMacroblockRows<16>(src, dst, dw);
        else if (blockSize == 8) copyMacroblockRows<8>(src, dst, dw);
        else processMacroblockRows<false, false, false>(src, dst, dw, blockSize);
    }
}

static void idct(int block[64]) {
    for (int i = 0; i < 8; ++i) {
        // Sparse MPEG blocks frequently have DC-only columns. This is exactly
        // the full integer transform's result, without its multiply/add work.
        if ((block[8+i] | block[16+i] | block[24+i] | block[32+i] |
             block[40+i] | block[48+i] | block[56+i]) == 0) {
            const int dc = block[i];
            for (int row = 1; row < 8; ++row) block[row * 8 + i] = dc;
            continue;
        }
        int b1 = block[4 * 8 + i];
        int b3 = block[2 * 8 + i] + block[6 * 8 + i];
        int b4 = block[5 * 8 + i] - block[3 * 8 + i];
        int tmp1 = block[8 + i] + block[7 * 8 + i];
        int tmp2 = block[3 * 8 + i] + block[5 * 8 + i];
        int b6 = block[8 + i] - block[7 * 8 + i];
        int b7 = tmp1 + tmp2;
        int m0 = block[i];
        int x4 = ((b6 * 473 - b4 * 196 + 128) >> 8) - b7;
        int x0 = x4 - (((tmp1 - tmp2) * 362 + 128) >> 8);
        int x1 = m0 - b1;
        int x2 = (((block[2 * 8 + i] - block[6 * 8 + i]) * 362 + 128) >> 8) - b3;
        int x3 = m0 + b1;
        int y3 = x1 + x2;
        int y4 = x3 + b3;
        int y5 = x1 - x2;
        int y6 = x3 - b3;
        int y7 = -x0 - ((b4 * 473 + b6 * 196 + 128) >> 8);
        block[i] = b7 + y4;
        block[8 + i] = x4 + y3;
        block[2 * 8 + i] = y5 - x0;
        block[3 * 8 + i] = y6 - y7;
        block[4 * 8 + i] = y6 + y7;
        block[5 * 8 + i] = x0 + y5;
        block[6 * 8 + i] = y3 - x4;
        block[7 * 8 + i] = y4 - b7;
    }

    for (int i = 0; i < 64; i += 8) {
        int b1 = block[4 + i];
        int b3 = block[2 + i] + block[6 + i];
        int b4 = block[5 + i] - block[3 + i];
        int tmp1 = block[1 + i] + block[7 + i];
        int tmp2 = block[3 + i] + block[5 + i];
        int b6 = block[1 + i] - block[7 + i];
        int b7 = tmp1 + tmp2;
        int m0 = block[i];
        int x4 = ((b6 * 473 - b4 * 196 + 128) >> 8) - b7;
        int x0 = x4 - (((tmp1 - tmp2) * 362 + 128) >> 8);
        int x1 = m0 - b1;
        int x2 = (((block[2 + i] - block[6 + i]) * 362 + 128) >> 8) - b3;
        int x3 = m0 + b1;
        int y3 = x1 + x2;
        int y4 = x3 + b3;
        int y5 = x1 - x2;
        int y6 = x3 - b3;
        int y7 = -x0 - ((b4 * 473 + b6 * 196 + 128) >> 8);
        block[i] = (b7 + y4 + 128) >> 8;
        block[1 + i] = (x4 + y3 + 128) >> 8;
        block[2 + i] = (y5 - x0 + 128) >> 8;
        block[3 + i] = (y6 - y7 + 128) >> 8;
        block[4 + i] = (y6 + y7 + 128) >> 8;
        block[5 + i] = (x0 + y5 + 128) >> 8;
        block[6 + i] = (y3 - x4 + 128) >> 8;
        block[7 + i] = (y4 - b7 + 128) >> 8;
    }
}

}
}
