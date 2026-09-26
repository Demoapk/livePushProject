#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "pusher/common/Types.h"

struct AMediaCodec;

namespace pusher {

class AudioEncoder {
public:
    using Callback = std::function<void(AudioPacket&&)>;

    ~AudioEncoder();

    bool start(const AudioConfig& config, Callback callback);
    void stop();
    void feed(const uint8_t* pcm, size_t size, int64_t ptsUs);

    const std::vector<uint8_t>& audioSpecificConfig() const { return asc_; }
    bool waitForAsc(int timeoutMs) const;

private:
    void drainLoop();
    void extractAsc();

    AMediaCodec* codec_ = nullptr;
    Callback callback_;
    std::thread drainThread_;
    std::atomic<bool> running_{false};
    std::vector<uint8_t> asc_;
    std::atomic<bool> ascReady_{false};
    uint64_t feeds_ = 0;
    int sampleRate_ = 0;
    int channels_ = 0;
};

}  // namespace pusher
