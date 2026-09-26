#include "pusher/pipeline/Pipeline.h"

#include <algorithm>

#include "pusher/common/Log.h"

namespace pusher {

Pipeline::Pipeline(VideoConfig videoConfig, AudioConfig audioConfig)
    : videoConfig_(videoConfig),
      audioConfig_(audioConfig),
      muxer_(videoConfig, audioConfig),
      videoQueue_(8),
      audioQueue_(32) {}

Pipeline::~Pipeline() {
    stop();
}

bool Pipeline::start(const std::string& url,
                     const std::vector<uint8_t>& sps,
                     const std::vector<uint8_t>& pps,
                     const std::vector<uint8_t>& asc) {
    stop();
    sps_ = sps;
    pps_ = pps;
    asc_ = asc;
    videoHeaderSent_ = false;
    audioHeaderSent_ = false;
    firstPtsUs_ = -1;
    videoFramesSent_ = 0;
    audioFramesSent_ = 0;

    if (!client_.connect(url)) {
        return false;
    }

    running_ = true;
    thread_ = std::thread(&Pipeline::runLoop, this);
    return true;
}

void Pipeline::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    videoQueue_.finish();
    audioQueue_.finish();
    if (thread_.joinable()) {
        thread_.join();
    }
    client_.close();
    videoQueue_.clear();
    audioQueue_.clear();
}

void Pipeline::onVideoPacket(VideoPacket&& packet) {
    videoQueue_.push(std::move(packet), false);
}

void Pipeline::onAudioPacket(AudioPacket&& packet) {
    audioQueue_.push(std::move(packet), false);
}

void Pipeline::sendVideoHeader() {
    if (videoHeaderSent_) return;
    std::vector<uint8_t> body = muxer_.makeAvcSequenceHeader(sps_, pps_);
    client_.sendVideoTag(0, body.data(), body.size());
    videoHeaderSent_ = true;
}

void Pipeline::sendAudioHeader() {
    if (audioHeaderSent_) return;
    std::vector<uint8_t> body = muxer_.makeAudioSequenceHeader(asc_);
    client_.sendAudioTag(0, body.data(), body.size());
    audioHeaderSent_ = true;
}

void Pipeline::sendVideo(const VideoPacket& packet, uint32_t timestampMs) {
    sendVideoHeader();
    std::vector<uint8_t> body = muxer_.makeVideoBody(packet, sps_, pps_);
    client_.sendVideoTag(timestampMs, body.data(), body.size());
}

void Pipeline::sendAudio(const AudioPacket& packet, uint32_t timestampMs) {
    sendAudioHeader();
    std::vector<uint8_t> body = muxer_.makeAudioBody(packet);
    client_.sendAudioTag(timestampMs, body.data(), body.size());
}

void Pipeline::runLoop() {
    while (running_) {
        VideoPacket videoPeek;
        AudioPacket audioPeek;
        const bool hasVideo = videoQueue_.peek(videoPeek);
        const bool hasAudio = audioQueue_.peek(audioPeek);

        if (!hasVideo && !hasAudio) {
            videoQueue_.waitForItem(500);
            continue;
        }

        if (!hasAudio || (hasVideo && videoPeek.ptsUs <= audioPeek.ptsUs)) {
            VideoPacket video;
            if (videoQueue_.popNoWait(video)) {
                if (firstPtsUs_ < 0) {
                    firstPtsUs_ = video.ptsUs;
                    LOGI("Pipeline: first video pts=%lld us",
                         static_cast<long long>(video.ptsUs));
                }
                uint32_t ts = static_cast<uint32_t>(
                    std::max<int64_t>(0, (video.ptsUs - firstPtsUs_) / 1000));
                sendVideo(video, ts);
                ++videoFramesSent_;
                if (videoFramesSent_ % 30 == 0) {
                    LOGI("Pipeline: video frame=%llu ts=%u pts=%lld",
                         static_cast<unsigned long long>(videoFramesSent_),
                         ts, static_cast<long long>(video.ptsUs));
                }
            }
        } else {
            AudioPacket audio;
            if (audioQueue_.popNoWait(audio)) {
                if (firstPtsUs_ < 0) {
                    firstPtsUs_ = audio.ptsUs;
                    LOGI("Pipeline: first audio pts=%lld us",
                         static_cast<long long>(audio.ptsUs));
                }
                uint32_t ts = static_cast<uint32_t>(
                    std::max<int64_t>(0, (audio.ptsUs - firstPtsUs_) / 1000));
                sendAudio(audio, ts);
                ++audioFramesSent_;
                if (audioFramesSent_ % 30 == 0) {
                    LOGI("Pipeline: audio frame=%llu ts=%u pts=%lld",
                         static_cast<unsigned long long>(audioFramesSent_),
                         ts, static_cast<long long>(audio.ptsUs));
                }
            }
        }
    }
}

}  // namespace pusher
