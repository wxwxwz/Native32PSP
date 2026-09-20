#ifndef NATIVE32_FRAME_CLOCK_H
#define NATIVE32_FRAME_CLOCK_H
#include <psptypes.h>

namespace n32 {
// A fixed 30 Hz timeline independent of vblank count. At most two ticks are
// run per pass, preventing a long load/stall from causing an unbounded catch-up.
// Hysteresis prevents a cheap skipped tick from immediately restoring full
// rendering. Only rendered samples can confirm a change; never skip >2 presentation opportunities.
class RenderCadence {
public:
    RenderCadence() { reset(); }
    void reset() { interval = 1; phase = 0; renderedCost = skippedCost = 0;
        haveRendered = haveSkipped = false; pressure = recovery = 0;
        pressureDebt = cycleCost = cycleSamples = 0; }
    bool next() { bool draw = phase == 0; phase = (phase + 1) % interval; return draw; }
    void observe(bool rendered, u32 micros) {
        // Ignore pathological load times when estimating steady-state cost.
        if (micros > 200000) micros = 200000;
        // Normally a draw spans at most three opportunities with two ticks
        // each. Bound partial cycles even if a caller suppresses more draws.
        if (cycleSamples == 8) {
            cycleCost = cycleSamples = pressureDebt = pressure = 0;
        }
        cycleCost += micros;
        ++cycleSamples;
        u32& average = rendered ? renderedCost : skippedCost;
        bool& known = rendered ? haveRendered : haveSkipped;
        average = known ? (average * 7 + micros) / 8 : micros;
        known = true;
        if (!rendered) return;
        // Count fresh over-budget cycles, not repeated echoes of one spike in
        // the EMA. Cheap cycles repay work, but do not erase recurring overload
        // unless they repay all of it. Include every skipped catch-up tick.
        const u32 budget = cycleSamples * 30000;
        if (cycleCost > budget) {
            const u32 excess = cycleCost - budget, limit = 4800000;
            pressureDebt = excess >= limit - pressureDebt ? limit : pressureDebt + excess;
            if (pressure < 3) ++pressure;
        } else {
            const u32 credit = budget - cycleCost;
            pressureDebt = credit >= pressureDebt ? 0 : pressureDebt - credit;
            if (!pressureDebt) pressure = 0;
        }
        cycleCost = cycleSamples = 0;
        u32 faster = interval > 1 ?
            (renderedCost + (interval - 2) * skippedCost) / (interval - 1) : renderedCost;
        recovery = faster < 24000 ? recovery + 1 : 0;
        if (pressure >= 3 && interval < 3) {
            ++interval; phase = 1; pressure = recovery = pressureDebt = 0;
        } else if (recovery >= 30 && interval > 1) {
            --interval; phase = 0; pressure = recovery = pressureDebt = 0;
        }
        // Saturate counters during sustained overload/idle.
        if (pressure > 3) pressure = 3;
        if (recovery > 30) recovery = 30;
    }
    unsigned period() const { return interval; }
private:
    unsigned interval, phase, pressure, recovery;
    u32 renderedCost, skippedCost, pressureDebt, cycleCost, cycleSamples;
    bool haveRendered, haveSkipped;
};

struct TimingWindow {
    u64 total=0;u32 count=0,peak=0;
    void add(u32 us){total+=us;++count;if(us>peak)peak=us;}
    u32 average()const{return count?(u32)(total/count):0;}
    void reset(){total=0;count=peak=0;}
};

class FrameClock {
public:
    FrameClock() { reset(); }
    void reset() { started = false; phase = 0; previous = 0; droppedTicks = 0; }
    unsigned due(u32 now) {
        if (!started) {
            started = true;
            previous = now;
            return 1;
        }
        u32 elapsed = now - previous; // Also handles the PSP's 32-bit clock wrap.
        previous = now;
        u64 accumulated = phase + (u64)elapsed * 30;
        u32 ticks = (u32)(accumulated / 1000000);
        phase = (u32)(accumulated % 1000000);
        if (ticks > 2) {
            droppedTicks += ticks - 2;
            ticks = 2;
        }
        return ticks;
    }
    u32 untilNext(u32 now) const {
        if(!started)return 0;
        u64 accumulated=(u64)phase+(u64)(u32)(now-previous)*30;
        return accumulated>=1000000 ? 0 : (u32)((1000000-accumulated+29)/30);
    }
    u32 droppedTicks;
private:
    bool started;
    u32 previous;
    u32 phase;
};
}
#endif
