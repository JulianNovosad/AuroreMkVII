// Verified headers: [fstream, vector, string, iostream, atomic...]
// Verification timestamp: 2026-01-06 17:08:04
// Standard C++ Library Includes
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono> // For std::chrono::milliseconds

// C System Headers
#include <signal.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h> // For _exit

// Project-specific Headers
#include <string.h> // For C-style string functions (memcpy, memset, etc.)
#include "application.h"
#include "util_logging.h"

// For GPU probing
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <iostream>

// Global variable to store original terminal settings
struct termios original_termios;

// Global flag checked by the main loop and modules.
// Access with memory_order_acquire/release for consistency.
std::atomic<bool> g_running = true;

std::vector<std::string> load_labels(const std::string& path) {
    std::vector<std::string> labels;
    std::ifstream file(path);
    if (!file.is_open()) {
        APP_LOG_ERROR("Failed to open labels file: " + path);
        return labels;
    }
    std::string line;
    while (std::getline(file, line)) {
        labels.push_back(line);
    }
    return labels;
}

// Function to probe GPU availability before initializing any hardware
bool probe_gpu() {
    // Try to get an EGL display
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        std::cerr << "FATAL: Failed to get EGL display. GPU acceleration not available." << std::endl;
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        std::cerr << "FATAL: Failed to initialize EGL. GPU acceleration not available." << std::endl;
        eglTerminate(display);
        return false;
    }

    // Choose a simple config
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &num_configs) || num_configs == 0) {
        std::cerr << "FATAL: Failed to choose EGL config. GPU acceleration not available." << std::endl;
        eglTerminate(display);
        return false;
    }

    // Create a minimal context
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        std::cerr << "FATAL: Failed to create EGL context. GPU acceleration not available." << std::endl;
        eglTerminate(display);
        return false;
    }

    // Create a minimal pbuffer surface
    EGLint pbuffer_attribs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE
    };

    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer_attribs);
    if (surface == EGL_NO_SURFACE) {
        std::cerr << "FATAL: Failed to create pbuffer surface. GPU acceleration not available." << std::endl;
        eglDestroyContext(display, context);
        eglTerminate(display);
        return false;
    }

    // Make context current
    if (!eglMakeCurrent(display, surface, surface, context)) {
        std::cerr << "FATAL: Failed to make context current. GPU acceleration not available." << std::endl;
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        eglTerminate(display);
        return false;
    }

    // Check if we can get OpenGL info
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    
    if (!renderer || !version) {
        std::cerr << "FATAL: Failed to get OpenGL info. GPU acceleration not available." << std::endl;
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        eglTerminate(display);
        return false;
    }

    // Clean up
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);

    std::cout << "[PROBE] GPU available: " << renderer << " - OpenGL ES " << version << std::endl;
    return true;
}

int main(int argc, char** argv) {
    std::cout << "[INIT] Starting main()..." << std::endl;

    // First, probe GPU availability before initializing any hardware
    if (!probe_gpu()) {
        std::cerr << "[FATAL] GPU required but not available. Exiting immediately." << std::endl;
        return 1;
    }
    std::cout << "[INIT] GPU probe successful, continuing with initialization..." << std::endl;

    // Save original terminal settings
    tcgetattr(STDIN_FILENO, &original_termios);

    // Ensure signals are not blocked in the main thread BEFORE any threads are spawned.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGQUIT);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) == -1) {
        APP_LOG_ERROR("Failed to unblock signals: " + std::string(strerror(errno)));
    }

    // Initialize Logger with default values first, so early logs are captured
    ::aurore::logging::Logger::init("run", "logs", nullptr);
    ::aurore::logging::Logger::getInstance().start_writer_thread();
    std::cout << "[INIT] Logger initialized (default config)" << std::endl;

    // Start a hard-kill watchdog thread
    std::cout << "[INIT] Setting up hard-kill watchdog..." << std::endl;
    std::thread hard_kill_watchdog([]() {
        while (g_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // Shutdown requested, wait 10 seconds then force exit
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!g_running.load()) {
                    APP_LOG_ERROR("[WATCHDOG] Graceful shutdown timed out (3s). Forcing termination via _exit(1).");
                    _exit(1);
                }    });
    hard_kill_watchdog.detach();
    std::cout << "[INIT] Hard-kill watchdog started" << std::endl;

    // Initialize Application (this registers signal handlers via constructor)
    std::cout << "[INIT] Creating Application object..." << std::endl;
    Application app(argc, argv);
    std::cout << "[INIT] Application object created successfully" << std::endl;

    // Re-initialize Logger with ConfigLoader after Application object is created
    ::aurore::logging::Logger::getInstance().init_with_config(&app.get_config_loader());
    std::cout << "[INIT] Logger re-initialized with ConfigLoader successfully" << std::endl;

    std::cout << "[INIT] Calling app.run()..." << std::endl;
    return app.run();
}
