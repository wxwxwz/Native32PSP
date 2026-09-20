#include "core/action_vm.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace n32;
namespace n32 { void pspLog(const char*, ...) {} }

// Frozen numeric helpers from the 0125 implementation. Keep these independent
// of the optimized helpers: strtod and printf are the compatibility oracle.
static double oldFloat(const std::string& value) {
    if (value.empty()) return 0.0;
    char* end = 0;
    const double result = std::strtod(value.c_str(), &end);
    return end == value.c_str() ? 0.0 : result;
}
static s64 oldInt(const std::string& value) { return (s64)oldFloat(value); }
static std::string oldNumber(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.15g", value);
    return std::string(buf);
}
static std::string oldInteger(s64 value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", (long long)value);
    return std::string(buf);
}

static u64 randomState = 0x76543210fedcba98ULL;
static u64 randomBits() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 7;
    randomState ^= randomState << 17;
    return randomState;
}
static u64 bits(double value) {
    u64 result;
    static_assert(sizeof(result) == sizeof(value), "binary64 expected");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
static size_t parseCases = 0, numberCases = 0, integerCases = 0;
static void checkParse(const std::string& value) {
    const double expected = oldFloat(value), actual = strToFloat(value);
    if (bits(actual) != bits(expected)) {
        std::fprintf(stderr, "parse mismatch, length=%zu, expected=%016llx actual=%016llx\n",
                     value.size(), (unsigned long long)bits(expected),
                     (unsigned long long)bits(actual));
        std::abort();
    }
    // The pre-existing float -> s64 cast has no defined result for NaN, infinity
    // or out-of-range input. Verify only the domain where that cast is defined.
    if (std::isfinite(expected) && expected >= -9223372036854775808.0 &&
        expected < 9223372036854775808.0) {
        assert(strToInt(value) == oldInt(value));
    }
    ++parseCases;
}
static void checkNumber(double value) {
    const std::string expected = oldNumber(value), actual = numberToString(value);
    if (actual != expected) {
        std::fprintf(stderr, "number mismatch, bits=%016llx expected='%s' actual='%s'\n",
                     (unsigned long long)bits(value), expected.c_str(), actual.c_str());
        std::abort();
    }
    ++numberCases;
}
static void checkInteger(s64 value) {
    const std::string expected = oldInteger(value), actual = intToString(value);
    if (actual != expected) {
        std::fprintf(stderr, "integer mismatch, expected='%s' actual='%s'\n",
                     expected.c_str(), actual.c_str());
        std::abort();
    }
    ++integerCases;
}

static void numericRegression() {
    const char* text[] = {
        "", "0", "-0", "+0", "000000000", "-000000000", "+000000000",
        "0000000000", "-0000000000", "1", "-1", "+1", "000000123",
        "999999999", "-999999999", "+999999999", "1000000000", "-1000000000",
        "2147483647", "-2147483648", "4294967295", "4294967296",
        "9007199254740991", "9007199254740992", "9007199254740993",
        "9223372036854775807", "-9223372036854775808", "9223372036854775808",
        "-9223372036854775809", " 12", "\t-12\r\n", "1 ", "-0 ", "12tail",
        "+", "-", "++1", "--1", "+-1", "abc", ".", ".5", "-.5", "1.",
        "1.25", "-0.0", "1e3", "1E+3", "1e-3", "1e", "1e+", "1e9999",
        "-1e9999", "1e-9999", "-1e-9999", "0x10", "-0x0", "0X1.Ap3",
        "0x1p-1074", "NaN", "-nan", "nan(123)", "nan(0x123)", "INF",
        "-infinity", "+Infinity", "123,45", "0x", "08", "09"
    };
    for (size_t i = 0; i < sizeof(text) / sizeof(text[0]); ++i) checkParse(text[i]);
    checkParse(std::string("12\0tail", 7));
    checkParse(std::string("-0\0tail", 7));
    checkParse(std::string("\0-42", 4));
    checkParse(std::string("123\0", 4));
    checkParse(std::string(300, '0') + "9");
    checkParse(std::string(300, '9'));
    checkParse(std::string(1, (char)0xff) + "12");

    const s64 integers[] = {
        0, 1, -1, 9, -9, 10, -10, 99, -99, 100, -100, 999999999, -999999999,
        1000000000, -1000000000, 2147483647LL, -2147483647LL - 1,
        2147483648LL, -2147483649LL, 4294967295LL, -4294967295LL,
        4294967296LL, -4294967296LL, 5000000000LL, -5000000000LL,
        1000000000000000000LL, -1000000000000000000LL,
        9000000000000000000LL, -9000000000000000000LL,
        9223372036854775807LL, -9223372036854775807LL - 1
    };
    for (size_t i = 0; i < sizeof(integers) / sizeof(integers[0]); ++i) {
        checkInteger(integers[i]);
        checkNumber((double)integers[i]);
        checkParse(oldInteger(integers[i]));
    }
    const s64 chunkEdges[] = {1000000000LL, 2147483648LL, 4294967296LL,
                              10000000000LL, 1000000000000000000LL,
                              4294967295000000000LL};
    for (size_t i = 0; i < sizeof(chunkEdges) / sizeof(chunkEdges[0]); ++i) {
        for (s64 offset = -2; offset <= 2; ++offset) {
            checkInteger(chunkEdges[i] + offset);
            checkInteger(-chunkEdges[i] + offset);
        }
    }
    const double floating[] = {
        0.0, -0.0, 0.5, -0.5, 1.25, -1.25, 999999998.5, -999999998.5,
        1e-4, 1e-5, 1e14, 1e15, 999999999999999.0, 999999999999999.5,
        std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
        std::numeric_limits<double>::denorm_min(),
        -std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(), -std::numeric_limits<double>::quiet_NaN()
    };
    for (size_t i = 0; i < sizeof(floating) / sizeof(floating[0]); ++i) checkNumber(floating[i]);
    const double boundaries[] = {-1000000000.0, -999999999.0, -1.0, -0.0,
                                 0.0, 1.0, 999999999.0, 1000000000.0};
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
        checkNumber(std::nextafter(boundaries[i], std::numeric_limits<double>::infinity()));
        checkNumber(std::nextafter(boundaries[i], -std::numeric_limits<double>::infinity()));
    }

    for (size_t i = 0; i < 100000; ++i) {
        const s64 small = (s64)(randomBits() % 1999999999ULL) - 999999999;
        const std::string plain = oldInteger(small);
        checkParse(plain);
        checkNumber((double)small);
        checkInteger(small);
        switch (i % 8) {
        case 0: checkParse("\t" + plain); break;
        case 1: checkParse(plain + "suffix"); break;
        case 2: checkParse(plain + ".125"); break;
        case 3: checkParse(plain + "e-2"); break;
        case 4: checkParse(plain + std::string("\0extra", 6)); break;
        case 5: checkParse("+" + oldInteger((s64)(randomBits() % 1000000000))); break;
        case 6: checkParse("-000000" + oldInteger((s64)(randomBits() % 1000))); break;
        default: checkParse("0000000000" + oldInteger((s64)(randomBits() % 1000))); break;
        }
        const u64 raw = randomBits();
        s64 integer;
        double real;
        std::memcpy(&integer, &raw, sizeof(integer));
        std::memcpy(&real, &raw, sizeof(real));
        checkInteger(integer);
        checkNumber(real);
        std::string arbitrary;
        const size_t length = (size_t)(randomBits() % 24);
        for (size_t c = 0; c < length; ++c) arbitrary += (char)(randomBits() & 255);
        checkParse(arbitrary);
    }
    std::printf("numeric oracle: parse=%zu format-double=%zu format-integer=%zu PASS\n",
                parseCases, numberCases, integerCases);
}

struct NumericOps {
    double (*real)(const std::string&);
    s64 (*integer)(const std::string&);
    std::string (*formatReal)(double);
    std::string (*formatInteger)(s64);
};
static const NumericOps before = {oldFloat, oldInt, oldNumber, oldInteger};
static const NumericOps after = {strToFloat, strToInt, numberToString, intToString};

struct GridSlot {
    std::string name, x, y, frame, visible;
    bool operator==(const GridSlot& other) const {
        return name == other.name && x == other.x && y == other.y &&
               frame == other.frame && visible == other.visible;
    }
};
struct GridResult {
    std::vector<GridSlot> slots;
    std::string finalRow, finalCol, activeCount;
    size_t visited;
    bool operator==(const GridResult& other) const {
        return slots == other.slots && finalRow == other.finalRow &&
               finalCol == other.finalCol && activeCount == other.activeCount &&
               visited == other.visited;
    }
};

// Synthetic 9x11 map refresh with 22 display slots. It contains the repeated
// string counters, arithmetic, variable-name suffixes and property values of
// that workload, but contains no game resource or hard-coded game script.
static GridResult refreshGrid(const NumericOps& ops, u32 tick) {
    GridResult result;
    result.slots.resize(22);
    result.visited = 0;
    const std::string cameraX = ops.formatInteger((tick * 7) % 320);
    const std::string cameraY = ops.formatInteger((tick * 11) % 480);
    const std::string rowStart = ops.formatInteger(ops.integer(ops.formatReal(ops.real(cameraY) / 32)));
    const std::string colStart = ops.formatInteger(ops.integer(ops.formatReal(ops.real(cameraX) / 32)));
    const std::string rowEnd = ops.formatReal(ops.real(rowStart) + 9);
    const std::string colEnd = ops.formatReal(ops.real(colStart) + 11);
    std::string active = "0", row = rowStart, col;
    for (size_t slot = 0; slot < result.slots.size(); ++slot) {
        result.slots[slot].name = "sfield" + ops.formatInteger((s64)slot);
        result.slots[slot].visible = "0";
    }
    while (ops.integer(ops.formatInteger(ops.real(row) < ops.real(rowEnd))) != 0) {
        col = colStart;
        while (ops.integer(ops.formatInteger(ops.real(col) < ops.real(colEnd))) != 0) {
            ++result.visited;
            const std::string index = ops.formatReal(ops.real(row) * 20 + ops.real(col));
            const s64 tile = ops.integer(index);
            if (tile % 7 == 0) {
                const size_t slot = (size_t)ops.integer(active);
                assert(slot < result.slots.size());
                GridSlot& dst = result.slots[slot];
                dst.x = ops.formatReal(ops.real(col) * 32);
                dst.y = ops.formatReal(ops.real(row) * 32);
                const std::string owner = ops.formatInteger(tile % 5);
                const std::string houses = ops.formatInteger(tile % 7);
                dst.frame = ops.formatReal(ops.real(owner) * 7 + 1 + ops.real(houses));
                dst.visible = "1";
                active = ops.formatReal(ops.real(active) + 1);
            }
            col = ops.formatReal(ops.real(col) + 1);
        }
        row = ops.formatReal(ops.real(row) + 1);
    }
    for (size_t slot = 0; slot < result.slots.size(); ++slot) {
        GridSlot& dst = result.slots[slot];
        if (ops.integer(dst.visible)) {
            dst.x = ops.formatReal(ops.real(dst.x) - ops.real(cameraX));
            dst.y = ops.formatReal(ops.real(dst.y) - ops.real(cameraY));
        }
    }
    result.finalRow = row;
    result.finalCol = col;
    result.activeCount = active;
    return result;
}
static u64 hashText(u64 hash, const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        hash ^= (unsigned char)text[i];
        hash *= 1099511628211ULL;
    }
    return (hash ^ 255) * 1099511628211ULL;
}
static u64 hashGrid(u64 hash, const GridResult& result) {
    for (size_t i = 0; i < result.slots.size(); ++i) {
        const GridSlot& slot = result.slots[i];
        hash = hashText(hash, slot.name);
        hash = hashText(hash, slot.x);
        hash = hashText(hash, slot.y);
        hash = hashText(hash, slot.frame);
        hash = hashText(hash, slot.visible);
    }
    hash = hashText(hash, result.finalRow);
    hash = hashText(hash, result.finalCol);
    return hashText(hash, result.activeCount);
}
static u64 gridRegression() {
    u64 hash = 1469598103934665603ULL;
    for (u32 tick = 0; tick < 257; ++tick) {
        const GridResult expected = refreshGrid(before, tick);
        const GridResult actual = refreshGrid(after, tick);
        assert(expected.visited == 99 && actual.visited == 99);
        assert(expected == actual);
        hash = hashGrid(hash, actual);
    }
    std::printf("99-cell oracle: ticks=257 slots=22 digest=%016llx PASS\n",
                (unsigned long long)hash);
    return hash;
}

static void benchmark(const NumericOps& ops, const char* label, u32 count) {
    // Verify before measuring. Only refreshGrid execution is timed; digest work
    // is outside the timed region. Run separate processes serially for A/B.
    gridRegression();
    for (u32 i = 0; i < 32; ++i) refreshGrid(ops, i);
    u64 hash = 1469598103934665603ULL;
    long long nanoseconds = 0;
    typedef std::chrono::steady_clock Clock;
    for (u32 i = 0; i < count; ++i) {
        const Clock::time_point start = Clock::now();
        const GridResult result = refreshGrid(ops, i);
        nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        hash = hashGrid(hash, result);
    }
    std::printf("99-cell bench %s: ticks=%u elapsed_us=%lld digest=%016llx\n",
                label, count, nanoseconds / 1000, (unsigned long long)hash);
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "--verify";
    if ((mode == "--verify" && argc == 2) || argc == 1) {
        numericRegression();
        gridRegression();
        return 0;
    }
    if ((mode == "--before" || mode == "--after") && argc <= 3) {
        char* end = 0;
        const unsigned long count = argc == 3 ? std::strtoul(argv[2], &end, 10) : 2000;
        if (count == 0 || count > 100000 || (end && *end)) return 2;
        benchmark(mode == "--before" ? before : after, mode.c_str() + 2, (u32)count);
        return 0;
    }
    std::fprintf(stderr, "usage: %s [--verify|--before [ticks]|--after [ticks]]\n", argv[0]);
    return 2;
}
