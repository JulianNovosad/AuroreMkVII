# Aurore MkVII Dependency Documentation

## Software Bill of Materials (SBOM)

This document lists all third-party dependencies for AuroreMkVII, including versions, licenses, and security status.

---

## Direct Dependencies

| Name | Minimum Version | License | Usage | Security Status |
|------|-----------------|---------|-------|-----------------|
| **OpenCV** | 4.9.0 | Apache 2.0 / BSD 3-Clause | Image processing (ORB, KCF tracker, CLAHE, homography) | ✅ Secure (CVE-2024-2167 fixed) |
| **libcamera** | 0.2.0 | LGPL-2.1 | RAW10 frame acquisition from Sony IMX708 | ✅ No known CVEs |
| **libwebp** | 1.3.2 | BSD 3-Clause | WebP image handling (transitive via OpenCV) | ✅ Secure (CVE-2023-4863 fixed) |
| **glibc** | 2.39 | LGPL-2.1 / GPL-2.0 | System C library | ⚠️ Update required (CVE-2024-2961) |
| **pthread** | system | LGPL-2.1 | Real-time thread scheduling | ✅ No known CVEs |
| **librt** | system | LGPL-2.1 | clock_nanosleep, POSIX timers | ✅ No known CVEs |
| **GoogleTest** | system | BSD 3-Clause | Unit testing framework | ✅ No known CVEs |

---

## Security Advisories

### CVE-2023-4863 (CRITICAL) - libwebp Heap Buffer Overflow

**Status:** ✅ **MITIGATED**

- **CVSS Score:** 9.6 (Critical)
- **Affected Versions:** libwebp < 1.3.2
- **Fixed Version:** 1.3.2+
- **Description:** Heap buffer overflow in WebP image decoder allows remote code execution via crafted WebP images
- **Exploit Available:** Yes (public PoC)
- **Mitigation:** CMakeLists.txt enforces minimum version 1.3.2 with build-time check

**Verification:**
```bash
# Check installed version
pkg-config --modversion libwebp

# Should output: 1.3.2 or higher
```

---

### CVE-2024-2167 (HIGH) - OpenCV Heap Buffer Overflow

**Status:** ✅ **MITIGATED**

- **CVSS Score:** 7.5 (High)
- **Affected Versions:** OpenCV < 4.9.0
- **Fixed Version:** 4.9.0+
- **Description:** Heap-based buffer overflow in image decoding pipeline
- **Mitigation:** CMakeLists.txt enforces minimum version 4.9.0

**Verification:**
```bash
# Check installed version
pkg-config --modversion opencv4

# Should output: 4.9.0 or higher
```

**Note:** Ubuntu 24.04 ships with OpenCV 4.6.0. Users must either:
1. Build OpenCV 4.9.0+ from source
2. Use a PPA with newer OpenCV (e.g., `ppa:deadsy/libopencv`)
3. Accept the risk for development/testing only (NOT for deployment)

---

### CVE-2024-2961 (HIGH) - glibc iconv Buffer Overflow

**Status:** ⚠️ **REQUIRES SYSTEM UPDATE**

- **CVSS Score:** 7.4 (High)
- **Affected Versions:** glibc < 2.39-1ubuntu3
- **Fixed Version:** 2.39-1ubuntu3+
- **Description:** Buffer overflow in iconv_string processing
- **Mitigation:** System update required

**Action Required:**
```bash
# Update system packages
sudo apt update && sudo apt upgrade

# Verify glibc version
ldd --version

# Should show: 2.39-1ubuntu3 or higher
```

---

## Transitive Dependencies

OpenCV 4.9.0+ pulls in the following transitive dependencies:

| Package | Version | License | Notes |
|---------|---------|---------|-------|
| libavcodec | 60.x | LGPL-2.1+ / GPL-2.0+ | FFmpeg (video decoding) |
| libavformat | 60.x | LGPL-2.1+ / GPL-2.0+ | FFmpeg (container formats) |
| libavutil | 58.x | LGPL-2.1+ / GPL-2.0+ | FFmpeg (utility functions) |
| libpng | 1.6.x | zlib | PNG image handling |
| libjpeg | 8.x | IJG | JPEG image handling |
| libtiff | 4.x | MIT-like | TIFF image handling |
| libopenjp2 | 2.x | BSD 2-Clause | JPEG 2000 support |
| libprotobuf | 3.x | BSD 3-Clause | Protocol Buffers (DNN models) |

---

## License Compliance

### LGPL-2.1 Dependencies

The following dependencies are licensed under LGPL-2.1:
- libcamera
- glibc
- FFmpeg components (if used)

**Compliance Requirements:**
1. **Dynamic Linking:** AuroreMkVII must dynamically link these libraries (default behavior)
2. **Source Disclosure:** Modifications to LGPL libraries must be made available
3. **Reverse Engineering:** Users must be allowed to reverse engineer for debugging

**Current Status:** ✅ **COMPLIANT** - All LGPL dependencies are dynamically linked via pkg-config.

### Attribution

AuroreMkVII includes the following third-party software:
- OpenCV (https://opencv.org) - Apache 2.0 / BSD 3-Clause
- libcamera (https://libcamera.org) - LGPL-2.1
- libwebp (https://developers.google.com/speed/webp) - BSD 3-Clause

---

## Build-Time Version Checks

CMakeLists.txt includes automatic security version checks:

```cmake
# OpenCV >= 4.9.0
pkg_check_modules(OPENCV REQUIRED opencv4>=4.9.0)

# libwebp >= 1.3.2 (explicit check with fatal error)
pkg_check_modules(WEBP libwebp)
if(WEBP_FOUND)
    if(WEBP_VERSION VERSION_LESS 1.3.2)
        message(FATAL_ERROR "libwebp ${WEBP_VERSION} is vulnerable to CVE-2023-4863")
    endif()
endif()
```

---

## Installation Requirements

### Ubuntu 24.04 LTS (Target Platform)

```bash
# System update (includes glibc fix)
sudo apt update && sudo apt upgrade

# Install dependencies
sudo apt install \
    libopencv-dev (>= 4.9.0 from PPA or source) \
    libcamera-dev \
    libwebp-dev \
    cmake \
    g++ \
    pkg-config

# Optional: Build OpenCV 4.9.0 from source
# See docs/build-opencv.md for instructions
```

### Cross-Compilation (Raspberry Pi 5)

```bash
# On build host
sudo apt install \
    g++-aarch64-linux-gnu \
    libopencv-dev:arm64 \
    libcamera-dev:arm64

# Or build in Docker container (recommended)
./scripts/docker-build-rpi.sh
```

---

## Dependency Update Policy

1. **Security Patches:** Applied within 7 days of disclosure
2. **Minor Updates:** Applied monthly during maintenance window
3. **Major Updates:** Evaluated for compatibility before upgrade

**Responsible Disclosure:** Report security vulnerabilities to [project maintainer contact].

---

## SBOM Export

For supply chain compliance, generate CycloneDX SBOM:

```bash
# Install cyclonedx-cmake
pip install cyclonedx-bom

# Generate SBOM
cd build-native
cyclonedx-cmake -o sbom.json
```

---

**Last Updated:** 2026-03-07  
**Maintainer:** AuroreMkVII Security Team
