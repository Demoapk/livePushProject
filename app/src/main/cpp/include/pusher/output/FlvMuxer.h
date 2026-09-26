#pragma once

#include <cstdint>
#include <vector>

#include "pusher/common/Types.h"

namespace pusher {

/**
 * FLV tag 封装：产出不带 11 字节 FLV tag header 的 tag body，
 * 由 RtmpClient 包装成 RTMP audio/video/data message。
 */
class FlvMuxer {
public:
    FlvMuxer(VideoConfig videoConfig, AudioConfig audioConfig)
        : video_(videoConfig), audio_(audioConfig) {}

    std::vector<uint8_t> makeAvcSequenceHeader(
        const std::vector<uint8_t>& sps,
        const std::vector<uint8_t>& pps) const;

    std::vector<uint8_t> makeVideoBody(
        const VideoPacket& packet,
        const std::vector<uint8_t>& sps,
        const std::vector<uint8_t>& pps) const;

    std::vector<uint8_t> makeAudioSequenceHeader(
        const std::vector<uint8_t>& asc) const;

    std::vector<uint8_t> makeAudioBody(const AudioPacket& packet) const;

private:
    VideoConfig video_;
    AudioConfig audio_;
};

}  // namespace pusher
