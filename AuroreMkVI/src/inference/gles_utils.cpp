#include "gles_utils.h"
#include "util_logging.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

#ifndef EGL_KHR_image
#define EGL_KHR_image 1
#endif
#ifndef EGL_KHR_image_base
#define EGL_KHR_image_base 1
#endif
#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 1
#endif

#include <dlfcn.h>

namespace aurore {
namespace gpu {

static thread_local bool tls_is_current = false;

GLContext::GLContext()
    : egl_display_(EGL_NO_DISPLAY), egl_context_(EGL_NO_CONTEXT), egl_surface_(EGL_NO_SURFACE),
      egl_config_{}, initialized_(false), software_mode_(false), sw_buffer_(nullptr) {
}

GLContext::~GLContext() {
    destroy();
}

bool GLContext::init() {
    if (initialized_) {
        return true;
    }

    const char* display_env = getenv("DISPLAY");
    bool has_display = display_env && strlen(display_env) > 0;

    if (!has_display) {
        APP_LOG_INFO("GLES: No DISPLAY, using software fallback");

        egl_display_ = (EGLDisplay)1;
        egl_context_ = (EGLContext)1;
        egl_surface_ = (EGLSurface)1;

        initialized_ = true;
        software_mode_ = true;
        APP_LOG_INFO("GLES: Software rendering mode (no GPU)");
        return true;
    }

    egl_display_ = eglGetDisplay((EGLNativeDisplayType)EGL_DEFAULT_DISPLAY);
    if (egl_display_ == EGL_NO_DISPLAY) {
        APP_LOG_ERROR("GLES: Failed to get EGL display");
        return false;
    }
    if (egl_display_ == EGL_NO_DISPLAY) {
        APP_LOG_ERROR("GLES: Failed to get EGL display");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(egl_display_, &major, &minor)) {
        APP_LOG_ERROR("GLES: Failed to initialize EGL");
        return false;
    }

    APP_LOG_INFO("GLES: EGL version " + std::to_string(major) + "." + std::to_string(minor));

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint num_configs;
    if (!eglChooseConfig(egl_display_, config_attribs, &egl_config_, 1, &num_configs) || num_configs == 0) {
        APP_LOG_ERROR("GLES: Failed to choose EGL config");
        return false;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, context_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) {
        APP_LOG_ERROR("GLES: Failed to create EGL context");
        return false;
    }

    EGLint pbuffer_attribs[] = {
        EGL_WIDTH, 64,
        EGL_HEIGHT, 64,
        EGL_NONE
    };

    egl_surface_ = eglCreatePbufferSurface(egl_display_, egl_config_, pbuffer_attribs);
    if (egl_surface_ == EGL_NO_SURFACE) {
        APP_LOG_WARNING("GLES: Failed to create pbuffer surface, using context without surface");
        egl_surface_ = EGL_NO_SURFACE;
    }

    if (!make_current()) {
        APP_LOG_ERROR("GLES: Failed to make context current");
        return false;
    }

    APP_LOG_INFO("GLES: OpenGL ES version: " + std::string((char*)glGetString(GL_VERSION)));
    APP_LOG_INFO("GLES: Renderer: " + std::string((char*)glGetString(GL_RENDERER)));
    APP_LOG_INFO("GLES: Vendor: " + std::string((char*)glGetString(GL_VENDOR)));

    initialized_ = true;
    return true;
}

void GLContext::destroy() {
    if (initialized_) {
        done_current();
        if (sw_buffer_) {
            free(sw_buffer_);
            sw_buffer_ = nullptr;
        }
        if (!software_mode_) {
            if (egl_surface_ != EGL_NO_SURFACE) {
                eglDestroySurface(egl_display_, egl_surface_);
            }
            if (egl_context_ != EGL_NO_CONTEXT) {
                eglDestroyContext(egl_display_, egl_context_);
            }
            eglTerminate(egl_display_);
        }
        initialized_ = false;
        software_mode_ = false;
    }
}

bool GLContext::make_current() {
    if (software_mode_) {
        if (!sw_buffer_) {
            sw_buffer_ = (unsigned char*)calloc(64 * 64 * 4, 1);
        }
        tls_is_current = true;
        return true;
    }

    EGLBoolean result;
    if (egl_surface_ != EGL_NO_SURFACE) {
        result = eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);
    } else {
        result = eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context_);
    }
    if (result == EGL_TRUE) {
        tls_is_current = true;
    }
    return result == EGL_TRUE;
}

void GLContext::done_current() {
    if (tls_is_current) {
        if (!software_mode_) {
            eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        tls_is_current = false;
    }
}

GLContext& GLContext::get_instance() {
    static GLContext instance;
    return instance;
}

GLTexturePool::GLTexturePool()
    : max_textures_(0)
{
}

GLTexturePool::~GLTexturePool() {
    destroy();
}

bool GLTexturePool::init(int max_textures) {
    max_textures_ = max_textures;
    pool_.resize(max_textures);
    in_use_.resize(max_textures, false);

    for (int i = 0; i < max_textures; ++i) {
        glGenTextures(1, &pool_[i].id);
        if (pool_[i].id == 0) {
            APP_LOG_ERROR("GLES: Failed to generate texture " + std::to_string(i));
            return false;
        }
    }

    APP_LOG_INFO("GLES: Created texture pool with " + std::to_string(max_textures) + " textures");
    return true;
}

void GLTexturePool::destroy() {
    for (auto& tex : pool_) {
        if (tex.id != 0) {
            glDeleteTextures(1, &tex.id);
            tex.id = 0;
        }
    }
    pool_.clear();
    in_use_.clear();
}

GLTexture GLTexturePool::create_texture(int width, int height, int format) {
    GLTexture tex;
    tex.width = width;
    tex.height = height;
    tex.format = format;

    for (int i = 0; i < max_textures_; ++i) {
        if (!in_use_[i]) {
            tex.id = pool_[i].id;
            in_use_[i] = true;
            break;
        }
    }

    if (tex.id == 0) {
        glGenTextures(1, &tex.id);
    }

    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return tex;
}

bool GLTexturePool::import_dmabuf(GLTexture& tex, int fd, int width, int height, int stride, int offset) {
    (void)fd;
    (void)width;
    (void)height;
    (void)stride;
    (void)offset;

    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tex.width, tex.height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void GLTexturePool::release_texture(GLTexture& tex) {
    for (int i = 0; i < max_textures_; ++i) {
        if (pool_[i].id == tex.id) {
            in_use_[i] = false;
            break;
        }
    }
    tex.id = 0;
}

bool check_gl_error(const std::string& operation) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        APP_LOG_ERROR("GLES: Error after " + operation + ": 0x" + std::to_string(error));
        return false;
    }
    return true;
}

std::string load_shader_source(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        APP_LOG_ERROR("GLES: Failed to open shader file: " + filepath);
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint compile_shader(GLenum type, const std::string& source) {
    if (source.empty()) {
        return 0;
    }

    GLuint shader = glCreateShader(type);
    const char* source_cstr = source.c_str();
    glShaderSource(shader, 1, &source_cstr, nullptr);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            std::vector<char> info_log(info_len);
            glGetShaderInfoLog(shader, info_len, nullptr, info_log.data());
            APP_LOG_ERROR("GLES: Shader compilation failed: " + std::string(info_log.data()));
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint link_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            std::vector<char> info_log(info_len);
            glGetProgramInfoLog(program, info_len, nullptr, info_log.data());
            APP_LOG_ERROR("GLES: Program linking failed: " + std::string(info_log.data()));
        }
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

} // namespace gpu
} // namespace aurore
