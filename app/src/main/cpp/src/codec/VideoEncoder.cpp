#include "pusher/codec/VideoEncoder.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <chrono>

#include "pusher/common/Log.h"

namespace pusher {

VideoEncoder::~VideoEncoder() {
    stop();
}

namespace {
bool isIdrFrame(const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset + 3 <= size) {
        size_t startCodeLen = 0;
        if (size - offset >= 4 && data[offset] == 0 && data[offset + 1] == 0 &&
            data[offset + 2] == 0 && data[offset + 3] == 1) {
            startCodeLen = 4;
        } else if (data[offset] == 0 && data[offset + 1] == 0 && data[offset + 2] == 1) {
            startCodeLen = 3;
        } else {
            ++offset;
            continue;
        }
        size_t naluStart = offset + startCodeLen;
        if (naluStart < size && (data[naluStart] & 0x1F) == 5) {
            return true;
        }
        offset = naluStart;
    }
    return false;
}

void copyFormatBuffer(AMediaFormat* format, const char* key, std::vector<uint8_t>& out) {
    if (format == nullptr) return;
    void* data = nullptr;
    size_t size = 0;
    if (AMediaFormat_getBuffer(format, key, &data, &size) && size > 0 && data != nullptr) {
        out.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    }
}

void stripAnnexBStartCode(std::vector<uint8_t>& nal) {
    if (nal.size() >= 4 && nal[0] == 0 && nal[1] == 0 && nal[2] == 0 && nal[3] == 1) {
        nal.erase(nal.begin(), nal.begin() + 4);
    } else if (nal.size() >= 3 && nal[0] == 0 && nal[1] == 0 && nal[2] == 1) {
        nal.erase(nal.begin(), nal.begin() + 3);
    }
}
}  // namespace

bool VideoEncoder::start(const VideoConfig& config, Callback callback) {
    stop();
    width_ = config.width;
    height_ = config.height;
    callback_ = std::move(callback);

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, config.width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, config.height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, config.bitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, config.fps);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 2);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F000789);  // COLOR_FormatSurface

    codec_ = AMediaCodec_createEncoderByType("video/avc");
    if (codec_ == nullptr) {
        AMediaFormat_delete(format);
        return false;
    }

    media_status_t status = AMediaCodec_configure(
        codec_, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (status != AMEDIA_OK) {
        LOGE("VideoEncoder: configure failed %d", status);
        AMediaFormat_delete(format);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
        return false;
    }

    status = AMediaCodec_createInputSurface(codec_, &inputSurface_);
    if (status != AMEDIA_OK || inputSurface_ == nullptr) {
        LOGE("VideoEncoder: create input surface failed %d", status);
        AMediaFormat_delete(format);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
        return false;
    }

    status = AMediaCodec_start(codec_);
    if (status != AMEDIA_OK) {
        LOGE("VideoEncoder: start failed %d", status);
        AMediaFormat_delete(format);
        ANativeWindow_release(inputSurface_);
        inputSurface_ = nullptr;
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
        return false;
    }

    extractCodecConfig(codec_);
    AMediaFormat_delete(format);

    running_ = true;
    drainThread_ = std::thread(&VideoEncoder::drainLoop, this);
    for (int i = 0; i < 100 && !spsReady_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

void VideoEncoder::stop() {
    if (!running_.exchange(false)) {
        if (codec_ != nullptr) {
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
            inputSurface_ = nullptr;
        }
        return;
    }
    if (drainThread_.joinable()) {
        drainThread_.join();
    }
    if (codec_ != nullptr) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    inputSurface_ = nullptr;
    sps_.clear();
    pps_.clear();
}

bool VideoEncoder::waitForSps(int timeoutMs) const {
    int waited = 0;
    while (!spsReady_ && waited < timeoutMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waited += 10;
    }
    return spsReady_;
}

void VideoEncoder::extractCodecConfig(AMediaCodec* codec) {
    AMediaFormat* format = AMediaCodec_getOutputFormat(codec);
    if (format == nullptr) return;
    copyFormatBuffer(format, "csd-0", sps_);
    copyFormatBuffer(format, "csd-1", pps_);
    stripAnnexBStartCode(sps_);
    stripAnnexBStartCode(pps_);
    if (!sps_.empty()) {
        spsReady_ = true;
        LOGI("VideoEncoder: SPS=%zu bytes, PPS=%zu bytes", sps_.size(), pps_.size());
    }
    AMediaFormat_delete(format);
}

void VideoEncoder::drainLoop() {
    while (running_) {
        AMediaCodecBufferInfo info{};
        ssize_t index = AMediaCodec_dequeueOutputBuffer(codec_, &info, 10'000);
        if (index >= 0) {
            size_t outSize = 0;
            uint8_t* buffer = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(index), &outSize);
            if (buffer != nullptr && info.size > 0) {
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                    AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(index), false);
                    continue;
                }
                VideoPacket packet;
                packet.data.assign(buffer, buffer + info.size);
                packet.ptsUs = info.presentationTimeUs;
                packet.keyframe = isIdrFrame(buffer, info.size);
                packet.width = width_;
                packet.height = height_;
                if (packet.keyframe) {
                    LOGI("VideoEncoder: keyframe pts=%lld size=%zu",
                         static_cast<long long>(packet.ptsUs), packet.data.size());
                }
                if (callback_) callback_(std::move(packet));
            }
            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(index), false);
        } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            extractCodecConfig(codec_);
        } else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            // 其他 INFO_* 或错误，短暂退让避免空转。
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

}  // namespace pusher
