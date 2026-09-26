#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/native_window.h>
#include <cstdint>

namespace pusher {

class EglCore {
public:
    EglCore() = default;
    ~EglCore();

    bool init(EGLDisplay display, EGLContext sharedContext);
    void release();

    EGLSurface createWindowSurface(ANativeWindow* window);
    void destroySurface(EGLSurface surface);
    bool makeCurrent(EGLSurface surface);
    bool swapBuffers(EGLSurface surface);
    void setPresentationTime(EGLSurface surface, int64_t nsecs);

    EGLDisplay display() const { return display_; }
    EGLContext context() const { return context_; }

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig config_ = nullptr;
    PFNEGLPRESENTATIONTIMEANDROIDPROC presentationTime_ = nullptr;
};

}  // namespace pusher
