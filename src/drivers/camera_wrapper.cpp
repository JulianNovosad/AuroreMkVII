/**
 * @file camera_wrapper.cpp
 * @brief Camera capture implementation
 *
 * Supports three capture modes selected by the AURORE_CAM_MODE env var:
 *   (unset)   → libcamera (real IMX708 on RPi 5), fallback to test pattern
 *   "test"    → test pattern generator (development, no hardware)
 *   "webcam"  → OpenCV webcam capture (USB webcam)
 *
 * On AURORE_LAPTOP_BUILD (test / unit-test builds), libcamera is absent;
 * only the test-pattern and webcam paths are compiled.
 *
 * The real libcamera path:
 *   1. init_libcamera()    — configure stream, allocate DMA buffers, queue requests
 *   2. capture_libcamera() — wait on requestCompleted, copy data, requeue
 *   3. cleanup_libcamera() — stop camera, unmap DMA buffers, release resources
 */

#include "aurore/camera_wrapper.hpp"
#include "aurore/timing.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <system_error>

#include <sys/mman.h>

// OpenCV headers
#include <opencv2/opencv.hpp>

// ARM NEON headers for SIMD optimization
#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>
#endif

// VideoCore VII GPU acceleration headers (Raspberry Pi 5 only)
// Guarded by AURORE_USE_GPU compile-time flag
#ifdef AURORE_USE_GPU
#include <bcm_host.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <sys/stat.h>
#endif

// libcamera headers — only available on non-laptop (hardware) builds
#ifndef AURORE_LAPTOP_BUILD
#include <libcamera/libcamera.h>
#endif

// Security headers for frame authentication
#include "aurore/security.hpp"

namespace aurore {

/**
 * @brief Internal implementation (pimpl pattern)
 *
 * Supports libcamera (real hardware), test pattern, and OpenCV webcam.
 * The libcamera fields and methods are conditionally compiled.
 */
struct CameraWrapper::Impl {
    int width  = 0;
    int height = 0;
    int fps    = 0;
    int lc_stride = 0;  // actual stride from libcamera (may differ from computed)
    uint64_t frame_counter = 0;

    // --- Capture mode flags (set by configure_stream) ---
    bool use_libcamera    = false;
    bool use_test_pattern = false;
    bool use_webcam       = false;

    // --- Persistent BGR output buffer (avoids mmap/page-fault churn per frame) ---
    // Allocated once in wrap_as_mat on first call; reused on all subsequent calls.
    // Eliminates ~975 MCL_FUTURE page-lock faults × 25µs = ~25ms per frame.
    cv::Mat bgr_scratch;

    // --- Staging buffer for non-cacheable DMA reads ---
    // DMA buffers are non-cacheable, causing ~25ms per read due to memory controller delays.
    // First copy DMA to cached staging, then process from cached memory (~1ms copy vs ~25ms slow reads).
    std::vector<uint8_t> raw_staging;

    // --- OpenCV webcam state ---
    cv::VideoCapture webcam_cap;
    int webcam_id = 0;

    // --- Test pattern state ---
    cv::Point2f target_pos;
    cv::Point2f target_velocity;
    float target_size = 30.0f;

    // --- GPU acceleration state (VideoCore VII) ---
    // Guarded by AURORE_USE_GPU compile-time flag
#ifdef AURORE_USE_GPU
    bool gpu_available = false;
    bool gpu_initialized = false;
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLContext egl_context = EGL_NO_CONTEXT;
    GLuint shader_program = 0;
    GLuint texture_id = 0;
    GLuint vbo_id = 0;

    /**
     * @brief Check if VideoCore VII GPU is available
     *
     * Checks for:
     * - /dev/fb0 (framebuffer device)
     * - EGL display availability
     * - OpenGL ES 2.0 support
     *
     * @return true if GPU acceleration is available
     */
    bool check_gpu_availability() {
        // Check framebuffer device
        struct stat st;
        if (stat("/dev/fb0", &st) != 0) {
            std::cout << "[camera] GPU: /dev/fb0 not found\n";
            return false;
        }

        // Initialize BCM host (required for VideoCore access)
        if (bcm_host_init() != 0) {
            std::cout << "[camera] GPU: bcm_host_init failed\n";
            return false;
        }

        // Get EGL display
        egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (egl_display == EGL_NO_DISPLAY) {
            std::cout << "[camera] GPU: eglGetDisplay failed\n";
            bcm_host_deinit();
            return false;
        }

        // Initialize EGL
        EGLint major, minor;
        if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
            std::cout << "[camera] GPU: eglInitialize failed\n";
            bcm_host_deinit();
            return false;
        }

        // Check for OpenGL ES 2.0 support
        const char* extensions = eglQueryString(egl_display, EGL_EXTENSIONS);
        if (!extensions || !strstr(extensions, "OpenGL_ES")) {
            std::cout << "[camera] GPU: OpenGL ES not supported\n";
            eglTerminate(egl_display);
            bcm_host_deinit();
            return false;
        }

        std::cout << "[camera] GPU: VideoCore VII available (EGL "
                  << major << "." << minor << ")\n";
        return true;
    }

    /**
     * @brief Initialize GPU acceleration for RAW10→BGR888 conversion
     *
     * Sets up:
     * - EGL context and surface
     * - OpenGL ES 2.0 shader program for color conversion
     * - Texture and buffer objects
     *
     * @return true if GPU initialization successful
     */
    bool init_gpu_acceleration() {
        if (!gpu_available) {
            return false;
        }
        if (gpu_initialized) {
            return true;
        }

        // Setup EGL config
        EGLint config_attrs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };

        EGLConfig config;
        EGLint num_configs;
        if (eglChooseConfig(egl_display, config_attrs, &config, 1, &num_configs) != EGL_TRUE) {
            std::cerr << "[camera] GPU: eglChooseConfig failed\n";
            return false;
        }

        // Create EGL surface (pbuffer for off-screen rendering)
        EGLint surface_attrs[] = {
            EGL_WIDTH, width,
            EGL_HEIGHT, height,
            EGL_NONE
        };
        egl_surface = eglCreatePbufferSurface(egl_display, config, surface_attrs);
        if (egl_surface == EGL_NO_SURFACE) {
            std::cerr << "[camera] GPU: eglCreatePbufferSurface failed\n";
            return false;
        }

        // Create EGL context
        EGLint context_attrs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attrs);
        if (egl_context == EGL_NO_CONTEXT) {
            std::cerr << "[camera] GPU: eglCreateContext failed\n";
            eglDestroySurface(egl_display, egl_surface);
            return false;
        }

        // Make context current
        if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) != EGL_TRUE) {
            std::cerr << "[camera] GPU: eglMakeCurrent failed\n";
            eglDestroyContext(egl_display, egl_context);
            eglDestroySurface(egl_display, egl_surface);
            return false;
        }

        // Create shader program for RAW10→BGR888 conversion
        const char* vertex_shader_src = R"(
            attribute vec4 a_position;
            attribute vec2 a_texCoord;
            varying vec2 v_texCoord;
            void main() {
                gl_Position = a_position;
                v_texCoord = a_texCoord;
            }
        )";

        const char* fragment_shader_src = R"(
            precision highp float;
            varying vec2 v_texCoord;
            uniform sampler2D u_texture;
            uniform vec2 u_texelSize;  // 1.0/width, 1.0/height
            void main() {
                vec2 pos = v_texCoord;
                // Bayer BGGR pattern: determine which color at this pixel
                // BGGR: (even,even)=B, (odd,even)=G, (even,odd)=G, (odd,odd)=R
                float x = pos.x * u_texelSize.x;
                float y = pos.y * u_texelSize.y;
                bool evenX = mod(floor(x), 2.0) < 0.5;
                bool evenY = mod(floor(y), 2.0) < 0.5;

                float r, g, b;

                if (!evenX && !evenY) {
                    // Red pixel (odd, odd)
                    r = texture2D(u_texture, pos).r;
                    g = (texture2D(u_texture, pos + vec2(-u_texelSize.x, 0.0)).r +
                         texture2D(u_texture, pos + vec2(u_texelSize.x, 0.0)).r +
                         texture2D(u_texture, pos + vec2(0.0, -u_texelSize.y)).r +
                         texture2D(u_texture, pos + vec2(0.0, u_texelSize.y)).r) * 0.25;
                    b = (texture2D(u_texture, pos + vec2(-u_texelSize.x, -u_texelSize.y)).r +
                         texture2D(u_texture, pos + vec2(u_texelSize.x, -u_texelSize.y)).r +
                         texture2D(u_texture, pos + vec2(-u_texelSize.x, u_texelSize.y)).r +
                         texture2D(u_texture, pos + vec2(u_texelSize.x, u_texelSize.y)).r) * 0.25;
                } else if (evenX && evenY) {
                    // Blue pixel (even, even)
                    b = texture2D(u_texture, pos).r;
                    g = (texture2D(u_texture, pos + vec2(u_texelSize.x, 0.0)).r +
                         texture2D(u_texture, pos + vec2(-u_texelSize.x, 0.0)).r +
                         texture2D(u_texture, pos + vec2(0.0, u_texelSize.y)).r +
                         texture2D(u_texture, pos + vec2(0.0, -u_texelSize.y)).r) * 0.25;
                    r = (texture2D(u_texture, pos + vec2(u_texelSize.x, u_texelSize.y)).r +
                         texture2D(u_texture, pos + vec2(-u_texelSize.x, -u_texelSize.y)).r +
                         texture2D(u_texture, pos + vec2(u_texelSize.x, -u_texelSize.y)).r +
                         texture2D(u_texture, pos + vec2(-u_texelSize.x, u_texelSize.y)).r) * 0.25;
                } else {
                    // Green pixel
                    g = texture2D(u_texture, pos).r;
                    if (evenX) {  // (odd, even) = top/bottom green
                        r = (texture2D(u_texture, pos + vec2(-u_texelSize.x, 0.0)).r +
                             texture2D(u_texture, pos + vec2(u_texelSize.x, 0.0)).r) * 0.5;
                        b = (texture2D(u_texture, pos + vec2(0.0, -u_texelSize.y)).r +
                             texture2D(u_texture, pos + vec2(0.0, u_texelSize.y)).r) * 0.5;
                    } else {  // (even, odd) = left/right green
                        r = (texture2D(u_texture, pos + vec2(0.0, -u_texelSize.y)).r +
                             texture2D(u_texture, pos + vec2(0.0, u_texelSize.y)).r) * 0.5;
                        b = (texture2D(u_texture, pos + vec2(-u_texelSize.x, 0.0)).r +
                             texture2D(u_texture, pos + vec2(u_texelSize.x, 0.0)).r) * 0.5;
                    }
                }

                // Output BGR for OpenCV
                gl_FragColor = vec4(b, g, r, 1.0);
            }
        )";

        // Compile and link shader program
        auto compile_shader = [](GLenum type, const char* src) -> GLuint {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);
            GLint status;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (status == GL_FALSE) {
                char log[512];
                glGetShaderInfoLog(shader, 512, nullptr, log);
                std::cerr << "[camera] GPU: Shader compile failed: " << log << "\n";
                return 0;
            }
            return shader;
        };

        GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
        if (vs == 0 || fs == 0) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            eglDestroyContext(egl_display, egl_context);
            eglDestroySurface(egl_display, egl_surface);
            return false;
        }

        gpu_program = glCreateProgram();
        glAttachShader(gpu_program, vs);
        glAttachShader(gpu_program, fs);
        glLinkProgram(gpu_program);
        GLint link_status;
        glGetProgramiv(gpu_program, GL_LINK_STATUS, &link_status);
        if (link_status == GL_FALSE) {
            char log[512];
            glGetProgramInfoLog(gpu_program, 512, nullptr, log);
            std::cerr << "[camera] GPU: Program link failed: " << log << "\n";
            glDeleteShader(vs);
            glDeleteShader(fs);
            glDeleteProgram(gpu_program);
            eglDestroyContext(egl_display, egl_context);
            eglDestroySurface(egl_display, egl_surface);
            return false;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);

        glUseProgram(gpu_program);
        glGenTextures(1, &gpu_texture);
        glBindTexture(GL_TEXTURE_2D, gpu_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        gpu_initialized = true;
        std::cout << "[camera] GPU: VideoCore VII acceleration initialized\n";
        return true;
    }

    /**
     * @brief Cleanup GPU resources
     */
    void cleanup_gpu() {
        if (!gpu_initialized) {
            return;
        }

        if (egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display, egl_context);
            egl_context = EGL_NO_CONTEXT;
        }
        if (egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display, egl_surface);
            egl_surface = EGL_NO_SURFACE;
        }
        if (egl_display != EGL_NO_DISPLAY) {
            eglTerminate(egl_display);
            egl_display = EGL_NO_DISPLAY;
        }

        bcm_host_deinit();
        gpu_initialized = false;
        gpu_available = false;
    }

    /**
     * @brief Convert RAW10 to BGR888 using GPU (VideoCore VII)
     *
     * Uses OpenGL ES 2.0 shader to perform parallel color conversion.
     * Expected performance: < 0.5ms for 1536×864 frame on RPi 5.
     *
     * @param raw Raw RAW10 frame data
     * @param bgr Output BGR888 frame
     * @return true if GPU conversion successful
     */
    bool convert_raw10_to_bgr_gpu(const cv::Mat& raw, cv::Mat& bgr) {
        if (!gpu_initialized) {
            return false;
        }

        // Make context current (may be needed if called from different thread)
        if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) != EGL_TRUE) {
            return false;
        }

        // GPU-based RAW10→BGR888 conversion using VideoCore VII
        // Upload RAW data as luminance texture
        int width = raw.cols;
        int height = raw.rows;

        // Create FBO for offscreen rendering
        static GLuint fbo = 0;
        static GLuint rbo = 0;
        if (fbo == 0) {
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glGenRenderbuffers(1, &rbo);
            glBindRenderbuffer(GL_RENDERBUFFER, rbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "[camera] GPU: FBO incomplete\n";
                return false;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // Upload as luminance texture (1 channel)
        glBindTexture(GL_TEXTURE_2D, gpu_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, raw.data);

        // Set up viewport and FBO
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
        glUseProgram(gpu_program);

        // Set texture uniform
        GLint tex_loc = glGetUniformLocation(gpu_program, "u_texture");
        glUniform1i(tex_loc, 0);

        // Set texel size uniform
        GLint texel_loc = glGetUniformLocation(gpu_program, "u_texelSize");
        glUniform2f(texel_loc, 1.0f/width, 1.0f/height);

        // Render fullscreen quad
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        // Simple quad vertices (two triangles)
        const float vertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
        };

        GLuint vbo, vao;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        GLint pos_attr = glGetAttribLocation(gpu_program, "a_position");
        GLint tex_attr = glGetAttribLocation(gpu_program, "a_texCoord");
        glEnableVertexAttribArray(pos_attr);
        glEnableVertexAttribArray(tex_attr);
        glVertexAttribPointer(pos_attr, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        glVertexAttribPointer(tex_attr, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Read back BGR data
        bgr.create(height, width, CV_8UC3);
        glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, bgr.data);

        // Cleanup
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return true;
    }
#endif  // AURORE_USE_GPU

#ifndef AURORE_LAPTOP_BUILD
    // =========================================================================
    // libcamera state (hardware builds only)
    // =========================================================================
    std::unique_ptr<libcamera::CameraManager>        lc_cm;
    std::shared_ptr<libcamera::Camera>               lc_camera;
    std::unique_ptr<libcamera::FrameBufferAllocator> lc_allocator;
    libcamera::Stream*                               lc_stream = nullptr;
    std::vector<std::unique_ptr<libcamera::Request>> lc_requests;

    // DMA buffer mapping: FrameBuffer* → {mmap ptr, size}
    struct MappedBuf { void* data; size_t size; };
    std::unordered_map<const libcamera::FrameBuffer*, MappedBuf> lc_mapped;

    // Completed-request synchronisation
    std::mutex              lc_mutex;
    std::condition_variable lc_cv;
    std::queue<libcamera::Request*> lc_completed;
    bool lc_stopped = false;
#endif  // !AURORE_LAPTOP_BUILD

    // =========================================================================
    // Mode selection
    // =========================================================================

    bool configure_stream(const CameraConfig& config) {
        width         = config.width;
        height        = config.height;
        fps           = config.fps;
        frame_counter = 0;

        use_libcamera    = false;
        use_test_pattern = false;
        use_webcam       = false;
        webcam_id        = 0;

#ifdef AURORE_USE_GPU
        gpu_available = false;
        gpu_initialized = false;
#endif

        const char* cam_mode = std::getenv("AURORE_CAM_MODE");
        if (cam_mode) {
            const std::string mode(cam_mode);
            if (mode == "webcam" || mode == "webcam0") {
                use_webcam = true;
            } else if (mode == "test" || mode == "pattern") {
                use_test_pattern = true;
            } else if (mode == "libcamera" || mode == "camera") {
                use_libcamera = true;
            } else {
                std::fprintf(stderr, "FATAL: Unknown AURORE_CAM_MODE: %s\n", cam_mode);
                return false;
            }
        } else {
#ifndef AURORE_LAPTOP_BUILD
            // Default on hardware builds: try libcamera, fallback on failure
            use_libcamera = true;
#else
            use_test_pattern = true;
#endif
        }

#ifdef AURORE_USE_GPU
        // Check GPU availability if enabled in config
        if (config.enable_hw_accel) {
            gpu_available = check_gpu_availability();
            if (gpu_available) {
                std::cout << "[camera] VideoCore VII GPU acceleration available\n";
            } else {
                std::cout << "[camera] GPU acceleration unavailable - using NEON/CPU fallback\n";
            }
        }
#endif

        return true;
    }

    // =========================================================================
    // Init / cleanup dispatch
    // =========================================================================

    bool init_camera(const CameraConfig& config) {
#ifndef AURORE_LAPTOP_BUILD
        if (use_libcamera) {
            if (init_libcamera(config)) {
                return true;
            }
            std::fprintf(stderr, "FATAL: libcamera init failed. No camera found or hardware error.\n");
            return false;
        }
#else
        (void)config;
#endif

        if (use_webcam) {
            webcam_cap.open(webcam_id, cv::CAP_V4L2);
            if (!webcam_cap.isOpened()) {
                webcam_cap.open(webcam_id);
            }
            if (webcam_cap.isOpened()) {
                webcam_cap.set(cv::CAP_PROP_FRAME_WIDTH,  width);
                webcam_cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
                webcam_cap.set(cv::CAP_PROP_FPS,          fps);
                std::cout << "[camera] Webcam opened: " << width << "x" << height
                          << " @ " << webcam_cap.get(cv::CAP_PROP_FPS) << " FPS\n";
                return true;
            }
            std::fprintf(stderr, "FATAL: Webcam unavailable (ID: %d)\n", webcam_id);
            return false;
        }

        if (use_test_pattern) {
            std::cout << "[camera] Test pattern generator ("
                      << width << "x" << height << ")\n";
            target_pos      = cv::Point2f(static_cast<float>(width)  / 2.0f,
                                          static_cast<float>(height) / 2.0f);
            target_velocity = cv::Point2f(2.0f, 1.5f);
            target_size     = 30.0f;
        }
        return true;
    }

    void cleanup() {
#ifndef AURORE_LAPTOP_BUILD
        if (use_libcamera) {
            cleanup_libcamera();
        }
#endif

#ifdef AURORE_USE_GPU
        if (gpu_initialized) {
            cleanup_gpu();
        }
#endif

        if (webcam_cap.isOpened()) {
            webcam_cap.release();
        }
    }

#ifndef AURORE_LAPTOP_BUILD
    // =========================================================================
    // libcamera implementation (hardware builds only)
    // =========================================================================

    /**
     * @brief Initialise libcamera: configure stream, allocate DMA buffers, queue requests.
     */
    bool init_libcamera(const CameraConfig& config) {
        lc_cm = std::make_unique<libcamera::CameraManager>();
        if (lc_cm->start() != 0) {
            std::cerr << "[camera] CameraManager start failed\n";
            lc_cm.reset();
            return false;
        }

        if (lc_cm->cameras().empty()) {
            std::cerr << "[camera] No cameras found\n";
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        lc_camera = lc_cm->cameras()[0];
        if (lc_camera->acquire() != 0) {
            std::cerr << "[camera] Camera acquire failed\n";
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        auto cfg = lc_camera->generateConfiguration({libcamera::StreamRole::Raw});
        if (!cfg) {
            std::cerr << "[camera] generateConfiguration failed\n";
            lc_camera->release();
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        auto& scfg        = cfg->at(0);
        scfg.size.width   = static_cast<unsigned int>(config.width);
        scfg.size.height  = static_cast<unsigned int>(config.height);
        scfg.pixelFormat  = libcamera::formats::BGR888;
        scfg.bufferCount  = static_cast<unsigned int>(config.buffer_count);

        if (cfg->validate() == libcamera::CameraConfiguration::Invalid) {
            std::cerr << "[camera] Configuration invalid\n";
            lc_camera->release();
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        if (lc_camera->configure(cfg.get()) != 0) {
            std::cerr << "[camera] configure() failed\n";
            lc_camera->release();
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        lc_stream = scfg.stream();
        lc_stride = static_cast<int>(scfg.stride);  // actual stride after negotiation

        lc_allocator = std::make_unique<libcamera::FrameBufferAllocator>(lc_camera);
        if (lc_allocator->allocate(lc_stream) < 0) {
            std::cerr << "[camera] Buffer allocation failed\n";
            lc_camera->release();
            lc_camera.reset();
            lc_cm->stop();
            lc_cm.reset();
            return false;
        }

        for (const auto& fb : lc_allocator->buffers(lc_stream)) {
            const auto& plane = fb->planes()[0];
            const off_t off   = (plane.offset != libcamera::FrameBuffer::Plane::kInvalidOffset)
                                ? static_cast<off_t>(plane.offset) : 0;
            void* mapped = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED,
                                plane.fd.get(), off);
            if (mapped == MAP_FAILED) {
                std::cerr << "[camera] mmap failed: " << strerror(errno) << "\n";
                continue;
            }
            lc_mapped[fb.get()] = {mapped, plane.length};

            auto req = lc_camera->createRequest();
            if (req && req->addBuffer(lc_stream, fb.get()) == 0) {
                lc_requests.push_back(std::move(req));
            }
        }

        if (lc_requests.empty()) {
            std::cerr << "[camera] No capture requests created\n";
            cleanup_libcamera();
            return false;
        }

        lc_camera->requestCompleted.connect(this, &Impl::on_request_completed);

        libcamera::ControlList controls(lc_camera->controls());
        controls.set(libcamera::controls::ExposureTime,
                     static_cast<int32_t>(config.exposure_us));
        controls.set(libcamera::controls::AnalogueGain, config.gain);

        // Enforce the configured frame rate. Without this the ISP picks its own
        // default (typically 30fps), causing VISION_LATENCY_EXCEEDED at 120Hz.
        if (config.fps > 0) {
            const int64_t frame_us = 1000000LL / static_cast<int64_t>(config.fps);
            const std::array<int64_t, 2> dur = {frame_us, frame_us};
            controls.set(libcamera::controls::FrameDurationLimits,
                         libcamera::Span<const int64_t, 2>(dur));
            std::cout << "[camera] FrameDurationLimits: " << frame_us << "us ("
                      << config.fps << "fps)\n";
        }

        if (lc_camera->start(&controls) != 0) {
            std::cerr << "[camera] camera->start() failed\n";
            cleanup_libcamera();
            return false;
        }

        for (auto& req : lc_requests) {
            lc_camera->queueRequest(req.get());
        }

        std::cout << "[camera] libcamera: "
                  << scfg.size.width << "x" << scfg.size.height
                  << " " << scfg.pixelFormat.toString()
                  << " stride=" << scfg.stride << "\n";
        return true;
    }

    /** @brief Signal slot — called from CameraManager's event thread. */
    void on_request_completed(libcamera::Request* req) {
        if (req->status() == libcamera::Request::RequestCancelled) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(lc_mutex);
            lc_completed.push(req);
        }
        lc_cv.notify_one();
    }

    /**
     * @brief Blocking libcamera frame capture (zero-copy).
     *
     * Returns a descriptor pointing directly to the DMA buffer.
     * Consumer MUST call release_frame() when done.
     */
    bool capture_libcamera(ZeroCopyFrame& frame, int timeout_ms) {
        std::unique_lock<std::mutex> lock(lc_mutex);
        const bool got = lc_cv.wait_for(
            lock, std::chrono::milliseconds(timeout_ms),
            [this] { return !lc_completed.empty() || lc_stopped; });

        if (!got || lc_completed.empty()) {
            frame.valid = false;
            snprintf(frame.error, sizeof(frame.error), "%s", "libcamera timeout");
            return false;
        }

        libcamera::Request* req = lc_completed.front();
        lc_completed.pop();
        lock.unlock();

        const auto& bufs = req->buffers();
        const auto  it   = bufs.find(lc_stream);
        if (it == bufs.end()) {
            std::fprintf(stderr, "FATAL: libcamera request has no buffer for our stream!\n");
            return false;
        }

        libcamera::FrameBuffer* fb = it->second;
        const auto& meta           = fb->metadata();

        const auto mit = lc_mapped.find(fb);
        if (mit == lc_mapped.end()) {
            std::fprintf(stderr, "FATAL: libcamera buffer not found in DMA map!\n");
            return false;
        }

        frame.sequence      = meta.sequence;
        frame.timestamp_ns  = static_cast<TimestampNs>(meta.timestamp);
        frame.width         = width;
        frame.height        = height;
        frame.format        = PixelFormat::BGR888;
        frame.plane_data[0] = mit->second.data;
        frame.plane_size[0] = mit->second.size;
        // Use actual stride from libcamera (format may differ from SGRBG10_CSI2P, e.g. PISP_COMP1)
        frame.stride[0]     = lc_stride;

        frame.request_ptr   = req;
        frame.valid         = true;
        frame.error[0]      = 0;  // DMA buffer

        // Compute frame authentication (SHA256 + HMAC) - ICD-001 / AM7-L2-SEC-001
        // Note: This is synchronous for correctness; async version available via AsyncFrameAuthenticator
        authenticate_frame(frame);

        frame_counter++;
        if (!frame.validate(width, height)) {
            std::fprintf(stderr, "FATAL: libcamera frame failed validation (geometry/corruption)!\n");
            return false;
        }
        return true;
    }

#endif  // !AURORE_LAPTOP_BUILD

    // =========================================================================
    // Common methods (all builds)
    // =========================================================================

    void release_frame(ZeroCopyFrame& frame) {
        if (!frame.valid) return;

#ifndef AURORE_LAPTOP_BUILD
        if (frame.request_ptr && use_libcamera) {
            libcamera::Request* req = static_cast<libcamera::Request*>(frame.request_ptr);
            req->reuse(libcamera::Request::ReuseBuffers);
            lc_camera->queueRequest(req);
            frame.request_ptr = nullptr;
            return;
        }
#endif

        // Free aligned memory (allocated with aligned_alloc)
        if (frame.error[0] == 1 && frame.plane_data[0] != nullptr) {
            free(frame.plane_data[0]);
            frame.plane_data[0] = nullptr;
            frame.error[0] = 0;
        }
    }

#ifndef AURORE_LAPTOP_BUILD
    /** @brief Stop and release all libcamera resources. */
    void cleanup_libcamera() {
        {
            std::lock_guard<std::mutex> lock(lc_mutex);
            lc_stopped = true;
        }
        lc_cv.notify_all();

        if (lc_camera) {
            lc_camera->stop();
            lc_requests.clear();

            for (auto& [fb, buf] : lc_mapped) {
                munmap(buf.data, buf.size);
            }
            lc_mapped.clear();

            if (lc_allocator && lc_stream) {
                lc_allocator->free(lc_stream);
            }
            lc_allocator.reset();
            lc_stream = nullptr;

            lc_camera->release();
            lc_camera.reset();
        }

        if (lc_cm) {
            lc_cm->stop();
            lc_cm.reset();
        }
    }
#endif  // !AURORE_LAPTOP_BUILD

    // =========================================================================
    // Test pattern
    // =========================================================================

    cv::Mat generate_test_pattern() {
        cv::Mat frame(height, width, CV_8UC3, cv::Scalar(128, 128, 128));

        for (int x = 0; x < width; x += 100) {
            cv::line(frame, cv::Point(x, 0), cv::Point(x, height),
                     cv::Scalar(100, 100, 100), 1);
        }
        for (int y = 0; y < height; y += 100) {
            cv::line(frame, cv::Point(0, y), cv::Point(width, y),
                     cv::Scalar(100, 100, 100), 1);
        }

        target_pos += target_velocity;
        if (target_pos.x < target_size ||
            target_pos.x > static_cast<float>(width) - target_size) {
            target_velocity.x = -target_velocity.x;
        }
        if (target_pos.y < target_size ||
            target_pos.y > static_cast<float>(height) - target_size) {
            target_velocity.y = -target_velocity.y;
        }

        cv::circle(frame, target_pos, static_cast<int>(target_size),
                   cv::Scalar(0, 0, 255), -1);

        const int cx = width / 2, cy = height / 2;
        cv::line(frame, cv::Point(cx - 20, cy), cv::Point(cx + 20, cy),
                 cv::Scalar(0, 255, 0), 2);
        cv::line(frame, cv::Point(cx, cy - 20), cv::Point(cx, cy + 20),
                 cv::Scalar(0, 255, 0), 2);

        cv::putText(frame, "Frame: " + std::to_string(frame_counter),
                    cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                    0.7, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, "Test Pattern Mode",
                    cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX,
                    0.7, cv::Scalar(255, 255, 0), 2);

        return frame;
    }

    // =========================================================================
    // Unified capture dispatch
    // =========================================================================

    bool capture_frame_stub(ZeroCopyFrame& frame, int timeout_ms = 20) {
#ifndef AURORE_LAPTOP_BUILD
        if (use_libcamera) {
            return capture_libcamera(frame, timeout_ms);
        }
#else
        (void)timeout_ms;
#endif

        cv::Mat bgr_frame;
        if (use_webcam && webcam_cap.isOpened()) {
            webcam_cap >> bgr_frame;
            if (bgr_frame.empty()) {
                frame.valid = false;
                snprintf(frame.error, sizeof(frame.error),
                         "%s", "Webcam capture failed");
                return false;
            }
        } else {
            bgr_frame = generate_test_pattern();
        }

        frame.sequence      = frame_counter++;
        frame.timestamp_ns  = get_timestamp(ClockId::MonotonicRaw);
        frame.width         = width;
        frame.height        = height;
        frame.format        = PixelFormat::BGR888;
        frame.valid         = !bgr_frame.empty();

        const size_t sz = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
        // 64-byte aligned allocation for SIMD optimization
        auto* frame_data = static_cast<uint8_t*>(aligned_alloc(64, sz));
        if (!frame_data) {
            std::fprintf(stderr, "FATAL: aligned_alloc failed for frame data (%zu bytes)\n", sz);
            return false;
        }
        std::memcpy(frame_data, bgr_frame.data, sz);

        // Runtime alignment check (debug assertion)
        if ((reinterpret_cast<uintptr_t>(frame_data) & 0x3Fu) != 0) {
            std::fprintf(stderr, "FATAL: frame data not 64-byte aligned at %p\n", frame_data);
            return false;
        }

        frame.plane_data[0] = frame_data;
        frame.plane_size[0] = sz;
        frame.stride[0]     = width * 3;
        frame.error[0]      = 1;  // Mark heap-allocated

        snprintf(frame.error + 1, sizeof(frame.error) - 1,
                 "%s", "Development mode - BGR capture");

        // Compute frame authentication (SHA256 + HMAC) - ICD-001 / AM7-L2-SEC-001
        authenticate_frame(frame);

        if (!frame.validate(width, height)) {
            std::fprintf(stderr, "FATAL: Captured frame failed validation (geometry/corruption)!\n");
            return false;
        }
        return true;
    }
};

// =============================================================================
// CameraWrapper public interface
// =============================================================================

CameraWrapper::CameraWrapper(const CameraConfig& config)
    : impl_(std::make_unique<Impl>())
    , config_(config)
    , running_(false)
    , frame_count_(0)
    , error_count_(0) {

    if (!config_.validate()) {
        throw CameraException("Invalid camera configuration");
    }
}

CameraWrapper::~CameraWrapper() {
    stop();
    impl_->cleanup();
}

bool CameraWrapper::init() {
    try {
        // configure_stream must run before init_camera to set mode flags
        impl_->configure_stream(config_);
        if (!impl_->init_camera(config_)) {
            impl_->cleanup();
            throw CameraException("Camera initialization failed");
        }
        return true;
    }
    catch (const CameraException& e) {
        impl_->cleanup();
        throw;
    }
}

bool CameraWrapper::start() {
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }
    running_.store(true, std::memory_order_release);
    return true;
}

void CameraWrapper::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    running_.store(false, std::memory_order_release);

#ifndef AURORE_LAPTOP_BUILD
    // Signal libcamera shutdown so capture_libcamera() unblocks
    if (impl_ && impl_->use_libcamera && impl_->lc_camera) {
        {
            std::lock_guard<std::mutex> lock(impl_->lc_mutex);
            impl_->lc_stopped = true;
        }
        impl_->lc_cv.notify_all();
        impl_->lc_camera->stop();
    }
#endif
}

bool CameraWrapper::capture_frame(ZeroCopyFrame& frame, int timeout_ms) {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }
    return impl_->capture_frame_stub(frame, timeout_ms);
}

bool CameraWrapper::try_capture_frame(ZeroCopyFrame& frame) {
    return capture_frame(frame, 0);
}

void CameraWrapper::release_frame(ZeroCopyFrame& frame) {
    if (impl_) {
        impl_->release_frame(frame);
    }
}

cv::Mat CameraWrapper::wrap_as_mat(const ZeroCopyFrame& frame,
                                    PixelFormat target_format) {
    if (!frame.validate(config_.width, config_.height)) {
        std::fprintf(stderr, "FATAL: wrap_as_mat: Frame validation failed (geometry/contract violation)\n");
        return cv::Mat();
    }
    if (!frame.is_valid()) {
        std::fprintf(stderr, "FATAL: wrap_as_mat: Frame is not valid!\n");
        return cv::Mat();
    }

    if (frame.format == PixelFormat::BGR888 && target_format == PixelFormat::BGR888) {
        // ISP configured for BGR888: DMA bytes are already B,G,R — no conversion needed.
        // Zero-copy: return Mat header over DMA buffer (AM7-L3-VIS-001).
        // Caller MUST NOT use this Mat after camera->release_frame().
        return cv::Mat(frame.height, frame.width, CV_8UC3,
                       frame.plane_data[0], static_cast<size_t>(frame.stride[0]));
    }

    // Hardware: RAW10 → greyscale BGR888
    // Supports both SGRBG10_CSI2P (5 bytes/4 pixels) and PISP_COMP1 (1 byte/pixel, stride=width).
    // NEON SIMD path: ~1ms for 1536×864 on RPi 5 once the output buffer is warm.
    if (frame.format == PixelFormat::RAW10 && target_format == PixelFormat::BGR888) {
#ifdef AURORE_DEBUG_TIMING
        const uint64_t tw0 = aurore::get_timestamp();
#endif
        // Reuse persistent scratch buffer to avoid mmap/page-fault churn with MCL_FUTURE.
        impl_->bgr_scratch.create(frame.height, frame.width, CV_8UC3);
#ifdef AURORE_DEBUG_TIMING
        const uint64_t tw1 = aurore::get_timestamp();
#endif
        const uint8_t* raw = static_cast<const uint8_t*>(frame.plane_data[0]);
        const int stride   = frame.stride[0];

#ifdef AURORE_USE_GPU
        // Try GPU acceleration first (VideoCore VII)
        // Expected performance: < 0.5ms for 1536×864
        if (impl_->gpu_initialized) {
            // GPU path: convert using OpenGL ES shader (VideoCore VII)
            if (impl_->convert_raw10_to_bgr_gpu(impl_->bgr_scratch, impl_->bgr_scratch)) {
                return impl_->bgr_scratch;
            }
            // Fall through to NEON/CPU path if GPU fails
        }
#endif

        // Two format paths based on stride:
        //   stride == width       → PISP_COMP1 (1 byte/pixel, 8-bit compressed Bayer)
        //   stride == width*10/8  → SGRBG10_CSI2P (packed 10-bit, 5 bytes per 4 pixels)
        //
        // PISP_COMP1 uses sequential 1-byte reads per pixel which allows the ARM
        // hardware prefetcher to work on the Non-Cacheable DMA buffer, reducing
        // decode time from ~25ms to ~1ms.
        const bool is_pisp_comp1 = (stride == frame.width);

#if defined(__aarch64__) || defined(__arm__)
        if (is_pisp_comp1) {
            // Stage DMA to cached memory first (~1ms copy from non-cacheable DMA).
            const size_t needed = static_cast<size_t>(frame.height) * static_cast<size_t>(stride);
            impl_->raw_staging.resize(needed);
            std::memcpy(impl_->raw_staging.data(), raw, needed);

            // Edge-aware demosaic using OpenCV
            cv::Mat bayer_mat(frame.height, frame.width, CV_8UC1, impl_->raw_staging.data());
            cv::cvtColor(bayer_mat, impl_->bgr_scratch, cv::COLOR_BayerBG2BGR_EA);
        } else {
            // SGRBG10_CSI2P: 5 bytes per 4 pixels (packed 10-bit).
            static const uint8_t kPerm[8] = {0, 1, 2, 3, 5, 6, 7, 0};
            const uint8x8_t perm = vld1_u8(kPerm);

            impl_->raw_staging.resize(static_cast<size_t>(frame.height) * static_cast<size_t>(stride));
            std::memcpy(impl_->raw_staging.data(), raw, static_cast<size_t>(frame.height) * static_cast<size_t>(stride));
            const uint8_t* staged = impl_->raw_staging.data();

            for (int row = 0; row < frame.height; ++row) {
                const uint8_t* line = staged + row * stride;
                uint8_t* out = impl_->bgr_scratch.ptr<uint8_t>(row);
                int col = 0;

                for (; col <= frame.width - 8; col += 8) {
                    uint8x8_t g = vld1_u8(line);
                    const uint8_t p7 = line[8];
                    line += 10;

                    uint8x8_t px = vtbl1_u8(g, perm);
                    px = vset_lane_u8(p7, px, 7);

                    uint8x8x3_t bgr;
                    bgr.val[0] = px;
                    bgr.val[1] = px;
                    bgr.val[2] = px;
                    vst3_u8(out, bgr);
                    out += 24;
                }

                for (; col < frame.width; col += 4) {
                    const uint8_t g0 = line[0];
                    const uint8_t g1 = line[1];
                    const uint8_t g2 = line[2];
                    const uint8_t g3 = line[3];
                    line += 5;

                    if (col     < frame.width) { out[0]=g0; out[1]=g0; out[2]=g0; out += 3; }
                    if (col + 1 < frame.width) { out[0]=g1; out[1]=g1; out[2]=g1; out += 3; }
                    if (col + 2 < frame.width) { out[0]=g2; out[1]=g2; out[2]=g2; out += 3; }
                    if (col + 3 < frame.width) { out[0]=g3; out[1]=g3; out[2]=g3; out += 3; }
                }
            }
        }
#else
        if (is_pisp_comp1) {
            impl_->raw_staging.resize(static_cast<size_t>(frame.height) * static_cast<size_t>(stride));
            std::memcpy(impl_->raw_staging.data(), raw, static_cast<size_t>(frame.height) * static_cast<size_t>(stride));
            const uint8_t* staged = impl_->raw_staging.data();
            for (int row = 0; row < frame.height; ++row) {
                const uint8_t* src = staged + row * stride;
                uint8_t* out = impl_->bgr_scratch.ptr<uint8_t>(row);
                for (int col = 0; col < frame.width; ++col) {
                    const uint8_t v = src[col];
                    *out++ = v; *out++ = v; *out++ = v;
                }
            }
        } else {
            impl_->raw_staging.resize(static_cast<size_t>(frame.height) * static_cast<size_t>(stride));
            std::memcpy(impl_->raw_staging.data(), raw, static_cast<size_t>(frame.height) * static_cast<size_t>(stride));
            const uint8_t* staged = impl_->raw_staging.data();
            for (int row = 0; row < frame.height; ++row) {
                const uint8_t* line = staged + row * stride;
                uint8_t* out = impl_->bgr_scratch.ptr<uint8_t>(row);
                for (int col = 0; col < frame.width; col += 4) {
                    const uint8_t g0 = line[0];
                    const uint8_t g1 = line[1];
                    const uint8_t g2 = line[2];
                    const uint8_t g3 = line[3];
                    line += 5;

                    if (col     < frame.width) { out[0]=g0; out[1]=g0; out[2]=g0; out += 3; }
                    if (col + 1 < frame.width) { out[0]=g1; out[1]=g1; out[2]=g1; out += 3; }
                    if (col + 2 < frame.width) { out[0]=g2; out[1]=g2; out[2]=g2; out += 3; }
                    if (col + 3 < frame.width) { out[0]=g3; out[1]=g3; out[2]=g3; out += 3; }
                }
            }
        }
#endif
#ifdef AURORE_DEBUG_TIMING
        const uint64_t tw2 = aurore::get_timestamp();
        static uint32_t wrap_call_count = 0;
        if (++wrap_call_count <= 10 || (tw2 - tw0) > 5000000ULL) {
            std::fprintf(stderr, "[wrap_as_mat #%u] alloc=%uus neon=%uus total=%uus\n",
                wrap_call_count,
                static_cast<unsigned>((tw1 - tw0) / 1000),
                static_cast<unsigned>((tw2 - tw1) / 1000),
                static_cast<unsigned>((tw2 - tw0) / 1000));
        }
#endif
        return impl_->bgr_scratch;
    }

    (void)target_format;
    return cv::Mat();
}

bool CameraWrapper::set_exposure(int exposure_us) {
    (void)exposure_us;
    return impl_ != nullptr;
}

bool CameraWrapper::set_gain(float gain) {
    (void)gain;
    return impl_ != nullptr;
}

// =============================================================================
// =============================================================================
// FrameBufferAllocator (stub — used by tests; real allocation in init_libcamera)
// =============================================================================

bool FrameBufferAllocator::allocate(int width, int height, PixelFormat format, int count) {
    width_  = width;
    height_ = height;
    format_ = format;
    count_  = count;

    switch (format) {
        case PixelFormat::RAW10:
            stride_[0]     = width * 2;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * static_cast<size_t>(height);
            break;
        case PixelFormat::BGR888:
        case PixelFormat::RGB888:
            stride_[0]     = width * 3;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * static_cast<size_t>(height);
            break;
        case PixelFormat::NV12:
            stride_[0]     = width;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * static_cast<size_t>(height);
            stride_[1]     = width;
            plane_size_[1] = static_cast<size_t>(stride_[1]) * static_cast<size_t>(height) / 2u;
            break;
        case PixelFormat::YUV420:
            stride_[0]     = width;
            plane_size_[0] = static_cast<size_t>(stride_[0]) * static_cast<size_t>(height);
            stride_[1]     = width / 2;
            plane_size_[1] = static_cast<size_t>(stride_[1]) * static_cast<size_t>(height) / 2u;
            stride_[2]     = width / 2;
            plane_size_[2] = static_cast<size_t>(stride_[2]) * static_cast<size_t>(height) / 2u;
            break;
    }

    buffers_.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        buffers_[static_cast<size_t>(i)].fd   = -1;
        buffers_[static_cast<size_t>(i)].data = nullptr;
        buffers_[static_cast<size_t>(i)].size = plane_size_[0];
    }
    return true;
}

void FrameBufferAllocator::free() {
    for (auto& buffer : buffers_) {
        if (buffer.data) { munmap(buffer.data, buffer.size); }
        if (buffer.fd >= 0) { close(buffer.fd); }
    }
    buffers_.clear();
}

void* FrameBufferAllocator::get_data(int index, int /*plane*/) {
    if (index < 0 || index >= count_) { return nullptr; }
    return buffers_[static_cast<size_t>(index)].data;
}

}  // namespace aurore
