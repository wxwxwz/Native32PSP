#ifndef NATIVE32_MPEG_PLAYBACK_BUDGET_H
#define NATIVE32_MPEG_PLAYBACK_BUDGET_H
namespace n32 { namespace mpeg {
// Keep overload history across cheap/skipped ticks instead of immediately
// re-enabling expensive B pictures. Leave 11 ms for MP2 and PSP presentation.
class PlaybackBudget {
public:
    PlaybackBudget() : debt(0) {}
    bool reduceWork() const { return debt != 0; }
    void observe(unsigned micros) {
        const unsigned budget = 22000, limit = 100000;
        if (micros > budget) {
            const unsigned excess = micros - budget;
            debt = excess >= limit - debt ? limit : debt + excess;
        } else {
            const unsigned credit = budget - micros;
            debt = credit >= debt ? 0 : debt - credit;
        }
    }
private:
    unsigned debt;
};
} }
#endif
