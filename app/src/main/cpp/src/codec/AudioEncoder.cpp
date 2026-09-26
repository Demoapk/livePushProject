#include "pusher/codec/AudioEncoder.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "pusher/common/Log.h"

namespace pusher {

// 析构函数：停止编码器。
AudioEncoder::~AudioEncoder() {
    stop();
}

// 配置并启动 AAC 硬编码器。
bool AudioEncoder::start(const AudioConfig& config, Callback callback) {
    stop();
    sampleRate_ = config.sampleRate;
    channels_ = config.channels;
    callback_ = std::move(callback);

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, config.sampleRate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, config.channels);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, config.bitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 4096);

    codec_ = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (codec_ == nullptr) {
        AMediaFormat_delete(format);
        return false;
    }

    media_status_t status = AMediaCodec_configure(
        codec_, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (status != AMEDIA_OK) {
        AMediaFormat_delete(format);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
        return false;
    }

    status = AMediaCodec_start(codec_);
    if (status != AMEDIA_OK) {
        AMediaFormat_delete(format);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
        return false;
    }

    extractAsc();
    AMediaFormat_delete(format);

    running_ = true;
    drainThread_ = std::thread(&AudioEncoder::drainLoop, this);
    for (int i = 0; i < 100 && !ascReady_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// 提取 AudioSpecificConfig。
void AudioEncoder::extractAsc() {
    if (codec_ == nullptr) return;
    AMediaFormat* outFormat = AMediaCodec_getOutputFormat(codec_);
    if (outFormat != nullptr) {
        void* data = nullptr;
        size_t size = 0;
        if (AMediaFormat_getBuffer(outFormat, "csd-0", &data, &size) && size > 0) {
            asc_.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
            ascReady_ = true;
            LOGI("AudioEncoder: ASC=%zu bytes, sampleRate=%d, channels=%d",
                 asc_.size(), sampleRate_, channels_);
        }
        AMediaFormat_delete(outFormat);
    }
}

// 停止编码线程并释放 MediaCodec。
void AudioEncoder::stop() {
    if (!running_.exchange(false)) {
        if (codec_ != nullptr) {
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
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
    asc_.clear();
    feeds_ = 0;
}

// 等待 AudioSpecificConfig 可用，超时返回 false。
bool AudioEncoder::waitForAsc(int timeoutMs) const {
    int waited = 0;
    while (!ascReady_ && waited < timeoutMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waited += 10;
    }
    return ascReady_;
}

// 把 PCM 帧送入编码器输入队列。
void AudioEncoder::feed(const uint8_t* pcm, size_t size, int64_t ptsUs) {
    if (codec_ == nullptr || !running_ || pcm == nullptr || size == 0) return;

    ssize_t index = AMediaCodec_dequeueInputBuffer(codec_, 5'000);
    if (index < 0) {
        return;
    }
    size_t bufferSize = 0;
    uint8_t* buffer = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(index), &bufferSize);
    if (buffer == nullptr || bufferSize < size) {
        AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(index), 0, 0, ptsUs, 0);
        return;
    }

    memcpy(buffer, pcm, size);
    AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(index), 0, size, ptsUs, 0);
    ++feeds_;
    if (feeds_ % 30 == 0) {
        LOGI("AudioEncoder: feed frame=%llu pts=%lld",
             static_cast<unsigned long long>(feeds_),
             static_cast<long long>(ptsUs));
    }
}

// 循环读取编码器输出，把 AAC 帧回调给 Pipeline。
void AudioEncoder::drainLoop() {
    while (running_) {
        AMediaCodecBufferInfo info{};
        ssize_t index = AMediaCodec_dequeueOutputBuffer(codec_, &info, 10'000);
        if (index >= 0) {
            size_t outSize = 0;
            uint8_t* buffer = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(index), &outSize);
            if (buffer != nullptr && info.size > 0) {
                if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) == 0) {
                    AudioPacket packet;
                    packet.data.assign(buffer, buffer + info.size);
                    packet.ptsUs = info.presentationTimeUs;
                    if (callback_) callback_(std::move(packet));
                }
            }
            AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(index), false);
        } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            extractAsc();
        } else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

}  // namespace pusher
