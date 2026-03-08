#include "eternal/render/Shaders.hpp"

extern "C" {
#include <wlr/util/log.h>
}

#include <cstdlib>
#include <vector>

namespace eternal::Shaders {

GLuint compileShader(std::string_view source, GLenum type) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        wlr_log(WLR_ERROR, "glCreateShader failed");
        return 0;
    }

    const char* src_ptr = source.data();
    auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &src_ptr, &length);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        std::vector<char> log(log_len + 1, '\0');
        glGetShaderInfoLog(shader, log_len, nullptr, log.data());

        const char* type_str = (type == GL_VERTEX_SHADER)   ? "vertex"
                             : (type == GL_FRAGMENT_SHADER) ? "fragment"
                                                            : "unknown";
        wlr_log(WLR_ERROR, "Failed to compile %s shader:\n%s",
                type_str, log.data());
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint linkProgram(GLuint vert, GLuint frag) {
    if (vert == 0 || frag == 0) return 0;

    GLuint program = glCreateProgram();
    if (program == 0) {
        wlr_log(WLR_ERROR, "glCreateProgram failed");
        return 0;
    }

    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        std::vector<char> log(log_len + 1, '\0');
        glGetProgramInfoLog(program, log_len, nullptr, log.data());

        wlr_log(WLR_ERROR, "Failed to link shader program:\n%s", log.data());
        glDeleteProgram(program);
        return 0;
    }

    // Shaders can be detached after linking.
    glDetachShader(program, vert);
    glDetachShader(program, frag);

    return program;
}

GLuint buildProgram(std::string_view vert_src, std::string_view frag_src) {
    GLuint vert = compileShader(vert_src, GL_VERTEX_SHADER);
    if (vert == 0) return 0;

    GLuint frag = compileShader(frag_src, GL_FRAGMENT_SHADER);
    if (frag == 0) {
        glDeleteShader(vert);
        return 0;
    }

    GLuint prog = linkProgram(vert, frag);

    // Always clean up individual shader objects after linking.
    glDeleteShader(vert);
    glDeleteShader(frag);

    return prog;
}

} // namespace eternal::Shaders
