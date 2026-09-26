#pragma once

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "pusher/opengl/EglCore.h"
#include "pusher/opengl/GlProgram.h"

namespace pusher {

/**
 * 相机 OES 纹理渲染器。
 * 在 GLSurfaceView 提供的 EGL 上下文上工作，推流时使用共享 EGL 上下文
 * 把同一帧绘制到编码器输入 Surface。
 */
class GlRenderer {
public:
    bool init();
    GLuint createOesTexture();
    void renderFrame(const float* transformMatrix, int64_t timestampNs);
    void setBeautyEnabled(bool enabled);
    bool captureFrame(uint8_t* out, int width, int height);

    void setEncoderWindow(ANativeWindow* window, int width, int height);
    void clearEncoder();
    void releaseEncoderNow();
    void release();

private:
    void drawQuad(const float* transformMatrix);

    GlProgram program_;
    GLuint oesTexture_ = 0;
    GLuint vbo_ = 0;
    GLint aPosition_ = -1;
    GLint aTexCoord_ = -1;
    GLint uTexMatrix_ = -1;
    GLint uTexture_ = -1;

    GlProgram beautyProgram_;
    GLint bPosition_ = -1;
    GLint bTexCoord_ = -1;
    GLint bTexMatrix_ = -1;
    GLint bTexture_ = -1;
    GLint bTexelSize_ = -1;
    std::atomic<bool> beautyEnabled_{false};

    EglCore encoderEgl_;
    EGLSurface encoderSurface_ = EGL_NO_SURFACE;
    ANativeWindow* pendingWindow_ = nullptr;
    int encoderWidth_ = 0;
    int encoderHeight_ = 0;
    bool encoderActive_ = false;
    bool clearRequested_ = false;
    std::mutex encoderMutex_;
};

}  // namespace pusher
