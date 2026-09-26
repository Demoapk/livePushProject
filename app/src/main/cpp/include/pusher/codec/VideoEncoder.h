#pragma once

#include <android/native_window.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "pusher/common/Types.h"

struct AMediaCodec;

namespace pusher {

class VideoEncoder {
public:
    using Callback = std::function<void(VideoPacket&&)>;

    ~VideoEncoder();

    bool start(const VideoConfig& config, Callback callback);
    void stop();

    ANativeWindow* inputSurface() const { return inputSurface_; }
    const std::vector<uint8_t>& sps() const { return sps_; }
    const std::vector<uint8_t>& pps() const { return pps_; }
    bool waitForSps(int timeoutMs) const;

private:
    void drainLoop();
    void extractCodecConfig(AMediaCodec* codec);

    AMediaCodec* codec_ = nullptr;
    ANativeWindow* inputSurface_ = nullptr;
    Callback callback_;
    std::thread drainThread_;
    std::atomic<bool> running_{false};
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    std::atomic<bool> spsReady_{false};
    int width_ = 0;
    int height_ = 0;
};

}  // namespace pusher
