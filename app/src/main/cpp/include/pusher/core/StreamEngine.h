#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "pusher/common/Types.h"

namespace pusher {

class AudioEncoder;
class GlRenderer;
class Pipeline;
class VideoEncoder;

/**
 * 推流引擎门面：组合 OpenGL 渲染、H.264/AAC 编码和 RTMP Pipeline。
 */
class StreamEngine {
public:
    StreamEngine();
    ~StreamEngine();

    bool initGl();
    int createOesTexture();
    void renderFrame(const float* transformMatrix, int64_t timestampNs);
    void setBeautyEnabled(bool enabled);
    bool captureFrame(uint8_t* out, int width, int height);

    int startStream(const char* url, const VideoConfig& video, const AudioConfig& audio);
    void stopStream();

    void onAudioPcm(const uint8_t* pcm, size_t size, int64_t timestampNs);
    void release();

private:
    std::unique_ptr<GlRenderer> renderer_;
    std::unique_ptr<VideoEncoder> videoEncoder_;
    std::unique_ptr<AudioEncoder> audioEncoder_;
    std::unique_ptr<Pipeline> pipeline_;
};

}  // namespace pusher
