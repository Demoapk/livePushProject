#include "pusher/opengl/GlRenderer.h"

#include <GLES2/gl2ext.h>

#include "pusher/common/Log.h"

namespace pusher {

namespace {

const char* kVertexShader = R"(
attribute vec4 aPosition;
attribute vec4 aTexCoord;
uniform mat4 uTexMatrix;
varying vec2 vTexCoord;
void main() {
    gl_Position = aPosition;
    vTexCoord = (uTexMatrix * aTexCoord).xy;
}
)";

const char* kFragmentShader = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
varying vec2 vTexCoord;
uniform samplerExternalOES uTexture;
void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

const char* kBeautyFragmentShader = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
varying vec2 vTexCoord;
uniform samplerExternalOES uTexture;
uniform vec2 uTexelSize;
void main() {
    vec3 c = texture2D(uTexture, vTexCoord).rgb;
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            vec2 off = vec2(float(dx), float(dy)) * uTexelSize;
            vec3 t = texture2D(uTexture, vTexCoord + off).rgb;
            float sp = float(dx * dx + dy * dy);
            float w = exp(-sp / 8.0);
            vec3 d = t - c;
            w *= exp(-dot(d, d) * 20.0);
            sum += t * w;
            wsum += w;
        }
    }
    vec3 blurred = sum / wsum;
    vec3 outColor = blurred + (c - blurred) * 0.75;
    gl_FragColor = vec4(outColor, 1.0);
}
)";

constexpr float kQuadVertices[] = {
    // x, y, u, v
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
};

}  // namespace

bool GlRenderer::init() {
    if (!program_.build(kVertexShader, kFragmentShader)) {
        return false;
    }

    aPosition_ = program_.attribute("aPosition");
    aTexCoord_ = program_.attribute("aTexCoord");
    uTexMatrix_ = program_.uniform("uTexMatrix");
    uTexture_ = program_.uniform("uTexture");

    if (beautyProgram_.build(kVertexShader, kBeautyFragmentShader)) {
        bPosition_ = beautyProgram_.attribute("aPosition");
        bTexCoord_ = beautyProgram_.attribute("aTexCoord");
        bTexMatrix_ = beautyProgram_.uniform("uTexMatrix");
        bTexture_ = beautyProgram_.uniform("uTexture");
        bTexelSize_ = beautyProgram_.uniform("uTexelSize");
    }

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOGE("GlRenderer: no current EGL context");
        return false;
    }
    if (!encoderEgl_.init(display, context)) {
        LOGE("GlRenderer: create shared EGL context failed");
        return false;
    }

    return true;
}

GLuint GlRenderer::createOesTexture() {
    glGenTextures(1, &oesTexture_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexture_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    return oesTexture_;
}

void GlRenderer::setEncoderWindow(ANativeWindow* window, int width, int height) {
    std::lock_guard<std::mutex> lock(encoderMutex_);
    pendingWindow_ = window;
    encoderWidth_ = width;
    encoderHeight_ = height;
    if (window == nullptr) {
        clearRequested_ = true;
    }
}

void GlRenderer::clearEncoder() {
    setEncoderWindow(nullptr, 0, 0);
}

void GlRenderer::setBeautyEnabled(bool enabled) {
    beautyEnabled_ = enabled;
}

void GlRenderer::releaseEncoderNow() {
    std::lock_guard<std::mutex> lock(encoderMutex_);
    if (encoderSurface_ != EGL_NO_SURFACE) {
        encoderEgl_.destroySurface(encoderSurface_);
        encoderSurface_ = EGL_NO_SURFACE;
    }
    encoderActive_ = false;
    pendingWindow_ = nullptr;
    clearRequested_ = false;
}

void GlRenderer::drawQuad(const float* transformMatrix) {
    if (oesTexture_ == 0) return;

    const bool beauty = beautyEnabled_ && bPosition_ >= 0;
    GlProgram& prog = beauty ? beautyProgram_ : program_;
    const GLint posLoc = beauty ? bPosition_ : aPosition_;
    const GLint texLoc = beauty ? bTexCoord_ : aTexCoord_;
    const GLint matrixLoc = beauty ? bTexMatrix_ : uTexMatrix_;
    const GLint textureLoc = beauty ? bTexture_ : uTexture_;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    prog.use();
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    glEnableVertexAttribArray(static_cast<GLuint>(posLoc));
    glVertexAttribPointer(static_cast<GLuint>(posLoc), 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), reinterpret_cast<void*>(0));

    glEnableVertexAttribArray(static_cast<GLuint>(texLoc));
    glVertexAttribPointer(static_cast<GLuint>(texLoc), 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexture_);
    glUniform1i(textureLoc, 0);

    static const float kIdentity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    glUniformMatrix4fv(matrixLoc, 1, GL_FALSE, transformMatrix != nullptr ? transformMatrix : kIdentity);

    if (beauty) {
        glUniform2f(bTexelSize_, 1.0f / static_cast<float>(encoderWidth_ > 0 ? encoderWidth_ : 1280),
                    1.0f / static_cast<float>(encoderHeight_ > 0 ? encoderHeight_ : 720));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(static_cast<GLuint>(posLoc));
    glDisableVertexAttribArray(static_cast<GLuint>(texLoc));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
}

void GlRenderer::renderFrame(const float* transformMatrix, int64_t timestampNs) {
    EGLDisplay previewDisplay = eglGetCurrentDisplay();
    EGLContext previewContext = eglGetCurrentContext();
    EGLSurface previewSurface = eglGetCurrentSurface(EGL_DRAW);
    if (previewDisplay == EGL_NO_DISPLAY || previewContext == EGL_NO_CONTEXT) {
        return;
    }

    GLint previewViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, previewViewport);

    // 处理编码器窗口/销毁请求。
    {
        std::lock_guard<std::mutex> lock(encoderMutex_);
        if (clearRequested_) {
            if (encoderSurface_ != EGL_NO_SURFACE) {
                encoderEgl_.destroySurface(encoderSurface_);
                encoderSurface_ = EGL_NO_SURFACE;
            }
            encoderActive_ = false;
            clearRequested_ = false;
            pendingWindow_ = nullptr;
        } else if (pendingWindow_ != nullptr) {
            if (encoderSurface_ != EGL_NO_SURFACE) {
                encoderEgl_.destroySurface(encoderSurface_);
            }
            encoderSurface_ = encoderEgl_.createWindowSurface(pendingWindow_);
            pendingWindow_ = nullptr;
            encoderActive_ = encoderSurface_ != EGL_NO_SURFACE;
        }
    }

    drawQuad(transformMatrix);

    {
        std::lock_guard<std::mutex> lock(encoderMutex_);
        if (encoderActive_ && encoderSurface_ != EGL_NO_SURFACE) {
            encoderEgl_.makeCurrent(encoderSurface_);
            glViewport(0, 0, encoderWidth_, encoderHeight_);
            drawQuad(transformMatrix);
            encoderEgl_.setPresentationTime(encoderSurface_, timestampNs);
            encoderEgl_.swapBuffers(encoderSurface_);
            eglMakeCurrent(previewDisplay, previewSurface, previewSurface, previewContext);
            glViewport(previewViewport[0], previewViewport[1], previewViewport[2], previewViewport[3]);
        }
    }
}

void GlRenderer::release() {
    std::lock_guard<std::mutex> lock(encoderMutex_);
    if (encoderSurface_ != EGL_NO_SURFACE) {
        encoderEgl_.destroySurface(encoderSurface_);
        encoderSurface_ = EGL_NO_SURFACE;
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (oesTexture_ != 0) {
        glDeleteTextures(1, &oesTexture_);
        oesTexture_ = 0;
    }
    encoderEgl_.release();
    program_.release();
    beautyProgram_.release();
}

}  // namespace pusher
