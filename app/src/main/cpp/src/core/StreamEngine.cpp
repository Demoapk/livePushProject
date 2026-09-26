#include "pusher/core/StreamEngine.h"

#include "pusher/codec/AudioEncoder.h"
#include "pusher/codec/VideoEncoder.h"
#include "pusher/common/Log.h"
#include "pusher/opengl/GlRenderer.h"
#include "pusher/pipeline/Pipeline.h"

namespace pusher {

// 析构函数：统一释放渲染、编码和推流资源。
StreamEngine::~StreamEngine() {
    release();
}

StreamEngine::StreamEngine() = default;

// 在 GL 线程创建或重建渲染器。
bool StreamEngine::initGl() {
    if (renderer_) {
        renderer_->release();
        renderer_.reset();
    }
    renderer_ = std::make_unique<GlRenderer>();
    return renderer_->init();
}

// 创建相机使用的 OES 外部纹理。
int StreamEngine::createOesTexture() {
    if (!renderer_) return -1;
    return static_cast<int>(renderer_->createOesTexture());
}

// 每帧渲染预览；推流时再渲染到编码器输入 Surface。
void StreamEngine::renderFrame(const float* transformMatrix, int64_t timestampNs) {
    if (renderer_) {
        renderer_->renderFrame(transformMatrix, timestampNs);
    }
}

// 开启或关闭美颜滤镜。
void StreamEngine::setBeautyEnabled(bool enabled) {
    if (renderer_) {
        renderer_->setBeautyEnabled(enabled);
    }
}

// 读取当前预览帧像素。
bool StreamEngine::captureFrame(uint8_t* out, int width, int height) {
    if (!renderer_) return false;
    return renderer_->captureFrame(out, width, height);
}

// 创建音视频编码器和 RTMP Pipeline，等编码配置就绪后连接服务器。
int StreamEngine::startStream(const char* url,
                              const VideoConfig& video,
                              const AudioConfig& audio) {
    if (url == nullptr || renderer_ == nullptr) {
        return -1;
    }
    stopStream();

    pipeline_ = std::make_unique<Pipeline>(video, audio);

    videoEncoder_ = std::make_unique<VideoEncoder>();
    bool videoOk = videoEncoder_->start(video, [this](VideoPacket&& packet) {
        if (pipeline_) pipeline_->onVideoPacket(std::move(packet));
    });

    audioEncoder_ = std::make_unique<AudioEncoder>();
    bool audioOk = audioEncoder_->start(audio, [this](AudioPacket&& packet) {
        if (pipeline_) pipeline_->onAudioPacket(std::move(packet));
    });

    if (!videoOk || !audioOk) {
        LOGE("StreamEngine: encoder start failed video=%d audio=%d", videoOk, audioOk);
        stopStream();
        return -2;
    }

    // 先让 GL 把相机帧渲染进编码器，等 SPS/PPS 与 AudioSpecificConfig 就绪后再连接，
    // 否则 FLV sequence header 里的 SPS/PPS 会是空的，服务器无法解析视频帧。
    renderer_->setEncoderWindow(videoEncoder_->inputSurface(), video.width, video.height);
    videoEncoder_->waitForSps(3000);
    audioEncoder_->waitForAsc(1000);

    LOGI("StreamEngine: video=%dx%d fps=%d bitrate=%d sps=%zu pps=%zu",
         video.width, video.height, video.fps, video.bitrate,
         videoEncoder_->sps().size(), videoEncoder_->pps().size());
    LOGI("StreamEngine: audio=%dHz ch=%d bitrate=%d asc=%zu",
         audio.sampleRate, audio.channels, audio.bitrate,
         audioEncoder_->audioSpecificConfig().size());

    bool connected = pipeline_->start(
        url, videoEncoder_->sps(), videoEncoder_->pps(), audioEncoder_->audioSpecificConfig());
    if (!connected) {
        LOGE("StreamEngine: pipeline start failed");
        stopStream();
        return -3;
    }

    return 0;
}

// 按顺序释放编码器窗口、音视频编码器和 Pipeline。
void StreamEngine::stopStream() {
    if (renderer_) {
        renderer_->releaseEncoderNow();
    }
    if (videoEncoder_) {
        videoEncoder_->stop();
        videoEncoder_.reset();
    }
    if (audioEncoder_) {
        audioEncoder_->stop();
        audioEncoder_.reset();
    }
    if (pipeline_) {
        pipeline_->stop();
        pipeline_.reset();
    }
}

// 把麦克风 PCM 数据送入 AAC 编码器。
void StreamEngine::onAudioPcm(const uint8_t* pcm, size_t size, int64_t timestampNs) {
    if (audioEncoder_) {
        audioEncoder_->feed(pcm, size, timestampNs / 1000);
    }
}

// 释放所有 native 资源。
void StreamEngine::release() {
    stopStream();
    if (renderer_) {
        renderer_->release();
        renderer_.reset();
    }
}

}  // namespace pusher
