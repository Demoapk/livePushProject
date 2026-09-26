#include "pusher/opengl/EglCore.h"

#include "pusher/common/Log.h"

namespace pusher {

// 析构函数：释放 EGL 上下文。
EglCore::~EglCore() {
    release();
}

// 选择可录制 RGBA 配置并创建共享 EGL 上下文。
bool EglCore::init(EGLDisplay display, EGLContext sharedContext) {
    if (display == EGL_NO_DISPLAY) {
        LOGE("EglCore: no display");
        return false;
    }
    display_ = display;

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RECORDABLE_ANDROID, 1,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs) ||
        numConfigs <= 0) {
        LOGE("EglCore: choose config failed");
        return false;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    context_ = eglCreateContext(display_, config_, sharedContext, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("EglCore: create context failed: %d", eglGetError());
        return false;
    }

    presentationTime_ = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(
        eglGetProcAddress("eglPresentationTimeANDROID"));
    return true;
}

// 销毁 EGL 上下文并清空状态。
void EglCore::release() {
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, context_);
    }
    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
    config_ = nullptr;
    presentationTime_ = nullptr;
}

// 用 ANativeWindow 创建 EGL 窗口 Surface。
EGLSurface EglCore::createWindowSurface(ANativeWindow* window) {
    if (window == nullptr) return EGL_NO_SURFACE;
    return eglCreateWindowSurface(display_, config_, window, nullptr);
}

// 销毁指定 EGL Surface。
void EglCore::destroySurface(EGLSurface surface) {
    if (display_ != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface);
    }
}

// 将指定 Surface 设为当前渲染目标。
bool EglCore::makeCurrent(EGLSurface surface) {
    return eglMakeCurrent(display_, surface, surface, context_) == EGL_TRUE;
}

// 交换指定 Surface 的前后缓冲区。
bool EglCore::swapBuffers(EGLSurface surface) {
    return eglSwapBuffers(display_, surface) == EGL_TRUE;
}

// 设置编码器输入帧的显示时间戳。
void EglCore::setPresentationTime(EGLSurface surface, int64_t nsecs) {
    if (presentationTime_ != nullptr) {
        presentationTime_(display_, surface, static_cast<EGLnsecsANDROID>(nsecs));
    }
}

}  // namespace pusher
