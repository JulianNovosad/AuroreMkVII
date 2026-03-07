#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <vector>

struct Framebuffer {
    uint32_t fb_id;
    uint32_t handle;
    uint32_t pitch;
    uint32_t size;
    void* map;
};

static int drm_fd = -1;
static uint32_t connector_id = 0;
static uint32_t crtc_id = 0;

bool init_drm() {
    drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        std::cerr << "Failed to open /dev/dri/card0" << std::endl;
        return false;
    }

    drmModeRes* resources = drmModeGetResources(drm_fd);
    if (!resources) {
        std::cerr << "Failed to get DRM resources" << std::endl;
        return false;
    }

    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector* conn = drmModeGetConnector(drm_fd, resources->connectors[i]);
        if (!conn) continue;

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            connector_id = conn->connector_id;
            crtc_id = resources->crtcs[0];
            std::cout << "Found HDMI connector: " << connector_id << std::endl;
            drmModeFreeConnector(conn);
            break;
        }
        drmModeFreeConnector(conn);
    }
    drmModeFreeResources(resources);

    if (connector_id == 0) {
        std::cerr << "No HDMI connector found" << std::endl;
        return false;
    }

    return true;
}

void draw_text_8x8(uint32_t* screen, int screen_w, int screen_h, int x, int y, const uint8_t* font, uint32_t color) {
    for (int py = 0; py < 8; py++) {
        for (int px = 0; px < 8; px++) {
            int sx = x + px;
            int sy = y + py;
            if (sx >= 0 && sx < (int)screen_w && sy >= 0 && sy < (int)screen_h) {
                if (font[py] & (1 << px)) {
                    screen[sy * screen_w + sx] = color;
                }
            }
        }
    }
}

void draw_char(uint32_t* screen, int screen_w, int screen_h, char c, int x, int y, uint32_t color) {
    static const uint8_t font[][8] = {
        {0x3c,0x66,0x6e,0x7e,0x76,0x66,0x3c,0x00}, // 0
        {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00}, // 1
        {0x3c,0x66,0x06,0x0c,0x18,0x30,0x7e,0x00}, // 2
        {0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00}, // 3
        {0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x1e,0x00}, // 4
        {0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00}, // 5
        {0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0x00}, // 6
        {0x7e,0x66,0x06,0x0c,0x18,0x18,0x18,0x00}, // 7
        {0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00}, // 8
        {0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0x00}, // 9
        {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0x00}, // A
        {0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00}, // B
        {0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00}, // C
        {0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00}, // D
        {0x7e,0x60,0x60,0x78,0x60,0x60,0x7e,0x00}, // E
        {0x7e,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // F
        {0x3c,0x66,0x60,0x6e,0x66,0x66,0x3e,0x00}, // G
        {0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0x00}, // H
        {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // I
        {0x1e,0x0c,0x0c,0x0c,0x0c,0xcc,0x78,0x00}, // J
        {0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0x00}, // K
        {0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x00}, // L
        {0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0x00}, // M
        {0xc6,0xe6,0xf6,0xde,0xce,0xc6,0xc6,0x00}, // N
        {0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // O
        {0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0x00}, // P
        {0x3c,0x66,0x66,0x66,0x66,0x3c,0x0e,0x00}, // Q
        {0x7c,0x66,0x66,0x7c,0x78,0x6c,0x66,0x00}, // R
        {0x3c,0x66,0x30,0x18,0x0c,0x66,0x3c,0x00}, // S
        {0x7e,0x5a,0x18,0x18,0x18,0x18,0x18,0x00}, // T
        {0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // U
        {0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0x00}, // V
        {0xc6,0xc6,0xc6,0xd6,0xfe,0xee,0xc6,0x00}, // W
        {0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0x00}, // X
        {0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x00}, // Y
        {0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0x00}, // Z
    };

    if (c >= '0' && c <= '9') {
        draw_text_8x8(screen, screen_w, screen_h, x, y, font[c - '0'], color);
    } else if (c >= 'A' && c <= 'Z') {
        draw_text_8x8(screen, screen_w, screen_h, x, y, font[10 + c - 'A'], color);
    }
}

int main() {
    std::cout << "=== DRM Text Display Test ===" << std::endl;

    if (!init_drm()) {
        std::cerr << "DRM init failed" << std::endl;
        return 1;
    }

    drmModeModeInfo mode = {0};
    mode.hdisplay = 1280;
    mode.vdisplay = 720;

    if (drmModeSetCrtc(drm_fd, crtc_id, -1, 0, 0, &connector_id, 1, &mode)) {
        std::cerr << "Failed to set CRTC" << std::endl;
        return 1;
    }

    int width = 1280;
    int height = 720;
    int size = width * height * 4;

    struct drm_mode_create_dumb create_req = {0};
    create_req.width = width;
    create_req.height = height;
    create_req.bpp = 32;
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_req)) {
        std::cerr << "Failed to create dumb buffer" << std::endl;
        return 1;
    }

    struct drm_mode_map_dumb mreq = {0};
    mreq.handle = create_req.handle;
    drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    void* screen = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);

    memset(screen, 0, size);

    uint32_t green = 0xFF00FF00;
    uint32_t white = 0xFFFFFFFF;
    uint32_t yellow = 0xFF00FFFF;

    int y = 50;
    const char* numbers = "0123456789";
    for (int i = 0; numbers[i]; i++) {
        draw_char((uint32_t*)screen, width, height, numbers[i], 100 + i * 30, y, yellow);
    }

    y += 40;
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; alphabet[i]; i++) {
        draw_char((uint32_t*)screen, width, height, alphabet[i], 50 + i * 22, y, green);
    }

    y += 50;
    const char* msg = "HELLO WORLD 12345";
    for (int i = 0; msg[i]; i++) {
        char c = msg[i];
        if (c == ' ') continue;
        if (c >= '0' && c <= '9') {
            draw_char((uint32_t*)screen, width, height, c, 100 + i * 25, y, yellow);
        } else if (c >= 'A' && c <= 'Z') {
            draw_char((uint32_t*)screen, width, height, c, 100 + i * 25, y, white);
        }
    }

    uint32_t fb_id;
    drmModeAddFB(drm_fd, width, height, 24, 32, width * 4, mreq.handle, &fb_id);
    drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0, &connector_id, 1, &mode);

    std::cout << "Displaying text for 10 seconds..." << std::endl;
    std::cout << "Row 1: Numbers 0-9 (RED)" << std::endl;
    std::cout << "Row 2: Alphabet A-Z (GREEN)" << std::endl;
    std::cout << "Row 3: HELLO WORLD 12345 (WHITE/YELLOW)" << std::endl;
    std::cout << "If you see this text on your HDMI display, text rendering works!" << std::endl;

    sleep(10);

    drmModeRmFB(drm_fd, fb_id);
    munmap(screen, size);
    close(drm_fd);

    std::cout << "Test complete." << std::endl;
    return 0;
}
