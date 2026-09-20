#ifndef NATIVE32_MPEG_PLAYBACK_BUDGET_H
#define NATIVE32_MPEG_PLAYBACK_BUDGET_H
namespace n32 { namespace mpeg {
// Keep overload history and at most one tick of unused video time. An I/P
// burst must not discard both following B pictures just because an earlier
// cheap tick's credit was lost. Leave 11 ms for MP2 and PSP presentation.
class PlaybackBudget {
public:
    PlaybackBudget() : balance(0) {}
    bool reduceWork() const { return balance > 0; }
    void observe(unsigned micros) {
        const unsigned budget = 22000;
        const int limit = 100000, minimum = -22000;
        if (micros > budget) {
            const unsigned excess = micros - budget;
            // Compare before conversion/addition, including unsigned timer
            // extremes; the signed sum is bounded by limit on this branch.
            balance = excess >= (unsigned)(limit - balance) ? limit : balance + (int)excess;
        } else {
            balance -= (int)(budget - micros);
            if (balance < minimum) balance = minimum;
        }
    }
private:
    int balance;
};
} }
#endif
