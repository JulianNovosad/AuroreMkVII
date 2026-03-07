#ifndef GLES_UTILS_H
#define GLES_UTILS_H

#include <GLES3/gl31.h>
#include <EGL/egl.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace aurore {
namespace gpu {

struct GLTexture {
    GLuint id;
    int width;
    int height;
    int format;

    GLTexture() : id(0), width(0), height(0), format(0) {}
};

class GLTexturePool {
public:
    GLTexturePool();
    ~GLTexturePool();

    bool init(int max_textures);
    void destroy();

    GLTexture create_texture(int width, int height, int format);
    bool import_dmabuf(GLTexture& tex, int fd, int width, int height, int stride, int offset);
    void release_texture(GLTexture& tex);

    size_t get_pool_size() const { return pool_.size(); }

private:
    std::vector<GLTexture> pool_;
    std::vector<bool> in_use_;
    int max_textures_;
};

class GLContext {
public:
    GLContext();
    ~GLContext();

    bool init();
    void destroy();

    EGLDisplay get_egl_display() const { return egl_display_; }
    EGLContext get_egl_context() const { return egl_context_; }
    EGLSurface get_egl_surface() const { return egl_surface_; }

    bool make_current();
    void done_current();

    bool is_software_mode() const { return software_mode_; }

    static GLContext& get_instance();

private:
    EGLDisplay egl_display_;
    EGLContext egl_context_;
    EGLSurface egl_surface_;
    EGLConfig egl_config_;
    bool initialized_;
    bool software_mode_;
    unsigned char* sw_buffer_;
};

bool check_gl_error(const std::string& operation);

std::string load_shader_source(const std::string& filepath);

GLuint compile_shader(GLenum type, const std::string& source);
GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

} // namespace gpu
} // namespace aurore

#endif // GLES_UTILS_H
