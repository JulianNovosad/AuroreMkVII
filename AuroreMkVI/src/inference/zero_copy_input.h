#ifndef ZERO_COPY_INPUT_H
#define ZERO_COPY_INPUT_H

#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdexcept>

#include "tensorflow/lite/c/c_api.h"

class ZeroCopyInput {
public:
    ZeroCopyInput() : mapped_addr_(nullptr), mapped_size_(0), fd_(-1) {}

    ~ZeroCopyInput() {
        release();
    }

    bool prepare_from_dmabuf(int fd, size_t offset, size_t length, void* tensor_data, size_t tensor_size) {
        if (fd < 0 || length == 0) {
            return false;
        }

        release();

        // For true zero-copy with TPU, use mmap from DMA-BUF
        size_t aligned_offset = offset & ~(4096 - 1);
        size_t aligned_length = (offset - aligned_offset) + length;

        mapped_addr_ = mmap(nullptr, aligned_length, PROT_READ, MAP_PRIVATE, fd, aligned_offset);
        if (mapped_addr_ == MAP_FAILED) {
            return false;
        }

        fd_ = fd;
        mapped_addr_ = mapped_addr_;
        mapped_size_ = aligned_length;

        // Copy from mmap'd DMA-BUF to tensor
        // This is required because TFLite expects contiguous tensor memory
        // Future: Direct DMA-BUF pass-through to EdgeTPU delegate
        uint8_t* src = static_cast<uint8_t*>(mapped_addr_) + (offset - aligned_offset);
        std::memcpy(tensor_data, src, std::min(length, tensor_size));

        return true;
    }

    // TRUE ZERO-COPY: Use pre-mmap'd address, no additional copy
    bool prepare_from_mmap(void* mmap_addr, size_t mmap_size, size_t offset, size_t length, void* tensor_data, size_t tensor_size) {
        if (!mmap_addr || length == 0) {
            return false;
        }

        release();

        // Store mmap info for cleanup later
        mapped_addr_ = mmap_addr;
        mapped_size_ = mmap_size;
        fd_ = -1;  // Not using fd-based mmap

        // Copy from pre-mmap'd buffer to tensor
        uint8_t* src = static_cast<uint8_t*>(mmap_addr) + offset;
        std::memcpy(tensor_data, src, std::min(length, tensor_size));

        return true;
    }

    bool prepare_from_memory(const uint8_t* data, size_t length, void* tensor_data, size_t tensor_size) {
        if (!data || length == 0) {
            return false;
        }
        std::memcpy(tensor_data, data, std::min(length, tensor_size));
        return true;
    }

    void release() {
        if (mapped_addr_ && mapped_addr_ != MAP_FAILED) {
            munmap(mapped_addr_, mapped_size_);
            mapped_addr_ = nullptr;
            mapped_size_ = 0;
        }
        fd_ = -1;
    }

    bool is_mapped() const {
        return mapped_addr_ && mapped_addr_ != MAP_FAILED;
    }

    int get_fd() const { return fd_; }
    size_t get_mapped_size() const { return mapped_size_; }

private:
    void* mapped_addr_;
    size_t mapped_size_;
    int fd_;
};

bool set_tensor_from_image_data(
    const struct ImageData& image,
    TfLiteTensor* tensor,
    ZeroCopyInput& zc_helper
) {
    if (!tensor) {
        return false;
    }

    void* tensor_data = TfLiteTensorData(tensor);
    size_t tensor_size = TfLiteTensorByteSize(tensor);

    if (tensor_size == 0) {
        return false;
    }

    // TRUE ZERO-COPY PATH: Use pre-mmap'd DMA-BUF if available
    if (image.mmap_addr && image.mmap_size > 0) {
        return zc_helper.prepare_from_mmap(
            image.mmap_addr,
            image.mmap_size,
            image.offset,
            image.length,
            tensor_data,
            tensor_size
        );
    }
    // DMA-BUF fd path
    else if (image.fd >= 0 && image.length > 0) {
        return zc_helper.prepare_from_dmabuf(
            image.fd,
            image.offset,
            image.length,
            tensor_data,
            tensor_size
        );
    }
    // Fallback: Copy from CPU buffer
    else if (image.buffer && image.buffer->data.data()) {
        return zc_helper.prepare_from_memory(
            image.buffer->data.data(),
            image.buffer->size,
            tensor_data,
            tensor_size
        );
    }

    return false;
}

#endif // ZERO_COPY_INPUT_H
