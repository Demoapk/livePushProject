#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "pusher/common/PacketQueue.h"
#include "pusher/common/Types.h"
#include "pusher/output/FlvMuxer.h"
#include "pusher/output/RtmpClient.h"

namespace pusher {

/**
 * 推流 Pipeline：接收编码后的音视频包，按时间戳归并、封装 FLV 并写入 RTMP。
 */
class Pipeline {
public:
    Pipeline(VideoConfig videoConfig, AudioConfig audioConfig);
    ~Pipeline();

    bool start(const std::string& url,
               const std::vector<uint8_t>& sps,
               const std::vector<uint8_t>& pps,
               const std::vector<uint8_t>& asc);
    void stop();

    void onVideoPacket(VideoPacket&& packet);
    void onAudioPacket(AudioPacket&& packet);

private:
    void runLoop();
    void sendVideoHeader();
    void sendAudioHeader();
    void sendVideo(const VideoPacket& packet, uint32_t timestampMs);
    void sendAudio(const AudioPacket& packet, uint32_t timestampMs);

    VideoConfig videoConfig_;
    AudioConfig audioConfig_;
    FlvMuxer muxer_;
    RtmpClient client_;

    PacketQueue<VideoPacket> videoQueue_;
    PacketQueue<AudioPacket> audioQueue_;

    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    std::vector<uint8_t> asc_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    bool videoHeaderSent_ = false;
    bool audioHeaderSent_ = false;
    int64_t firstPtsUs_ = -1;
    uint64_t videoFramesSent_ = 0;
    uint64_t audioFramesSent_ = 0;
};

}  // namespace pusher
