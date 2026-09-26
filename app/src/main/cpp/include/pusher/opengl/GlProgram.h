#pragma once

#include <GLES2/gl2.h>
#include <string>

namespace pusher {

class GlProgram {
public:
    ~GlProgram();

    bool build(const char* vertexSource, const char* fragmentSource);
    void use() const;
    void release();

    GLint attribute(const char* name) const;
    GLint uniform(const char* name) const;

private:
    GLuint program_ = 0;
    GLuint vertexShader_ = 0;
    GLuint fragmentShader_ = 0;
};

}  // namespace pusher
