#pragma once

#include <cstdint>
#include <vector>

namespace pusher {

struct VideoConfig {
    int width = 1280;
    int height = 720;
    int fps = 30;
    int bitrate = 2'500'000;
};

struct AudioConfig {
    int sampleRate = 44100;
    int channels = 1;
    int bitrate = 96'000;
};

struct VideoPacket {
    std::vector<uint8_t> data;
    int64_t ptsUs = 0;
    bool keyframe = false;
    int width = 0;
    int height = 0;
};

struct AudioPacket {
    std::vector<uint8_t> data;
    int64_t ptsUs = 0;
};

}  // namespace pusher
