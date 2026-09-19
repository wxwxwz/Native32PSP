#ifndef N32_MP3_SOFTWARE_H
#define N32_MP3_SOFTWARE_H
#include <psptypes.h>
#include <vector>
#include <memory>
namespace n32 {
class SoftwareMp3Stream {
public:
    SoftwareMp3Stream(const std::vector<u8>& input,u32 outputRate);
    ~SoftwareMp3Stream();
    bool readBlock(std::vector<s16>* output);
    void rewind();
    size_t retainedBytes() const;
private:
    struct State;
    std::unique_ptr<State> state;
};
std::vector<s16> decodeSoftwareMp3(const std::vector<u8>& input, u32 outputRate);
}
#endif
