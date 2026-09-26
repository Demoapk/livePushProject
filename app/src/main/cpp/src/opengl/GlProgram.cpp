#include "pusher/opengl/GlProgram.h"

#include "pusher/common/Log.h"

namespace pusher {

// 析构函数：释放 GL program 和 shader。
GlProgram::~GlProgram() {
    release();
}

namespace {
// 编译一个 shader，失败返回 0。
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGE("compile shader failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
}  // namespace

// 编译并链接 GL program。
bool GlProgram::build(const char* vertexSource, const char* fragmentSource) {
    vertexShader_ = compileShader(GL_VERTEX_SHADER, vertexSource);
    fragmentShader_ = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader_ == 0 || fragmentShader_ == 0) {
        release();
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader_);
    glAttachShader(program_, fragmentShader_);
    glLinkProgram(program_);

    GLint status = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        char log[512] = {0};
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        LOGE("link program failed: %s", log);
        release();
        return false;
    }

    glDeleteShader(vertexShader_);
    glDeleteShader(fragmentShader_);
    vertexShader_ = 0;
    fragmentShader_ = 0;
    return true;
}

// 启用当前 GL program。
void GlProgram::use() const {
    glUseProgram(program_);
}

// 释放 program 和未删除的 shader。
void GlProgram::release() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vertexShader_ != 0) {
        glDeleteShader(vertexShader_);
        vertexShader_ = 0;
    }
    if (fragmentShader_ != 0) {
        glDeleteShader(fragmentShader_);
        fragmentShader_ = 0;
    }
}

// 获取 attribute 位置。
GLint GlProgram::attribute(const char* name) const {
    return glGetAttribLocation(program_, name);
}

// 获取 uniform 位置。
GLint GlProgram::uniform(const char* name) const {
    return glGetUniformLocation(program_, name);
}

}  // namespace pusher
