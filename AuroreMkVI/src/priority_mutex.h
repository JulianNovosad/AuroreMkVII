#ifndef AURORE_PRIORITY_MUTEX_H
#define AURORE_PRIORITY_MUTEX_H

#include <pthread.h>
#include <stdexcept>
#include <string>
#include <memory>

namespace Aurore {

class PriorityMutex {
private:
    pthread_mutex_t mutex_;
    pthread_mutexattr_t attr_;
    bool initialized_;
    std::string name_;

public:
    explicit PriorityMutex(const std::string& name = "unnamed")
        : initialized_(false), name_(name) {
        pthread_mutexattr_init(&attr_);
        
        int result = pthread_mutexattr_setprotocol(&attr_, PTHREAD_PRIO_INHERIT);
        if (result != 0) {
            pthread_mutexattr_destroy(&attr_);
            throw std::runtime_error("Failed to set priority inheritance protocol: " + std::to_string(result));
        }
        
        result = pthread_mutex_init(&mutex_, &attr_);
        if (result != 0) {
            pthread_mutexattr_destroy(&attr_);
            throw std::runtime_error("Failed to initialize mutex '" + name_ + "': " + std::to_string(result));
        }
        
        initialized_ = true;
    }

    ~PriorityMutex() {
        if (initialized_) {
            pthread_mutex_destroy(&mutex_);
            pthread_mutexattr_destroy(&attr_);
        }
    }

    PriorityMutex(const PriorityMutex&) = delete;
    PriorityMutex& operator=(const PriorityMutex&) = delete;
    PriorityMutex(PriorityMutex&&) = delete;
    PriorityMutex& operator=(PriorityMutex&&) = delete;

    void lock() {
        if (!initialized_) {
            throw std::runtime_error("Mutex not initialized: " + name_);
        }
        int result = pthread_mutex_lock(&mutex_);
        if (result != 0) {
            throw std::runtime_error("Failed to lock mutex '" + name_ + "': " + std::to_string(result));
        }
    }

    bool try_lock() {
        if (!initialized_) {
            throw std::runtime_error("Mutex not initialized: " + name_);
        }
        return pthread_mutex_trylock(&mutex_) == 0;
    }

    void unlock() {
        if (!initialized_) {
            throw std::runtime_error("Mutex not initialized: " + name_);
        }
        int result = pthread_mutex_unlock(&mutex_);
        if (result != 0) {
            throw std::runtime_error("Failed to unlock mutex '" + name_ + "': " + std::to_string(result));
        }
    }

    pthread_mutex_t* native_handle() { return &mutex_; }
    const std::string& name() const { return name_; }
    bool is_initialized() const { return initialized_; }
};

template<typename Mutex>
class UniqueLock {
private:
    Mutex* mutex_;
    bool owned_;
public:
    explicit UniqueLock(Mutex& m) : mutex_(&m), owned_(true) {
        mutex_->lock();
    }
    ~UniqueLock() {
        if (owned_) {
            mutex_->unlock();
        }
    }
    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;
    UniqueLock(UniqueLock&& other) noexcept : mutex_(other.mutex_), owned_(other.owned_) {
        other.owned_ = false;
    }
    UniqueLock& operator=(UniqueLock&& other) noexcept {
        if (this != &other) {
            if (owned_) {
                mutex_->unlock();
            }
            mutex_ = other.mutex_;
            owned_ = other.owned_;
            other.owned_ = false;
        }
        return *this;
    }
    void release() { owned_ = false; }
    Mutex* mutex() const { return mutex_; }
    bool owns_lock() const { return owned_; }
    void lock() { if (!owned_) { mutex_->lock(); owned_ = true; } }
    void unlock() { if (owned_) { mutex_->unlock(); owned_ = false; } }
};

} // namespace Aurore

#endif // AURORE_PRIORITY_MUTEX_H
