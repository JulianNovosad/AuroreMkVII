// C++ Standard Library Headers (alphabetical)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// C Standard Library Headers (alphabetical)
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>

// Third-party Libraries
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/property_ids.h>
#include <opencv2/opencv.hpp>

// Project Specific Headers
#include "application.h"
#include "camera_capture.h"
#include "timing.h"
#include "util_logging.h"
#include "pipeline_trace.h"
#include "safe_memcpy.h"
#include "fault_injection.h"
#include "atomic_ordering.h"

// Helper to convert libcamera PixelFormat to a string for logging
#ifdef DEBUG_MODE
static std::string pixelFormatToString(const libcamera::PixelFormat& format) {
    std::stringstream ss;
    ss << "'" << std::hex << std::setfill('0') << std::setw(8) << format.fourcc() << "'";
    return ss.str();
}
#endif

// Optimization from Coral/Library codebase
static void setup_thread_affinity() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset); // Dedicate Core 2 to Camera/ISP
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    APP_LOG_INFO("CameraCapture: Thread affinity set to Core 2");
}

bool CameraCapture::process_frame_buffer(const libcamera::FrameBuffer* fb,
                                 const libcamera::StreamConfiguration& cfg,
                                 std::shared_ptr<BufferPool<uint8_t>> buffer_pool,
                                 std::shared_ptr<ObjectPool<ImageData>> image_data_pool,
                                 ImageQueue& queue,
                                 const char* stream_name,
                                 [[maybe_unused]] unsigned int target_width,
                                 [[maybe_unused]] unsigned int target_height,
                                 std::chrono::steady_clock::time_point capture_time,
                                 uint64_t t_capture_raw_ms,
                                 const libcamera::PixelFormat& actual_format,
                                 long long frame_id,
                                 [[maybe_unused]] long long exposure_ms,
                                 std::atomic<int64_t>* main_stream_drop_counter,
                                 std::atomic<int64_t>* tpu_stream_drop_counter,
                                 std::atomic<int64_t>* frames_produced_counter) // Mapped_buffers parameter removed
{
    (void)main_stream_drop_counter; // Silence unused parameter warning
    (void)tpu_stream_drop_counter;  // Silence unused parameter warning
    (void)frames_produced_counter;  // Silence unused parameter warning
    auto process_start = std::chrono::steady_clock::now();
    
    size_t total_length = 0;
    std::vector<int> fds;
    std::vector<size_t> offsets;
    std::vector<size_t> lengths;

    for (const auto& plane : fb->planes()) {
        total_length += plane.length;
        fds.push_back(plane.fd.get());
        offsets.push_back(plane.offset);
        lengths.push_back(plane.length);
    }

    if (fds.empty()) {
        APP_LOG_ERROR(std::string(stream_name) + " No planes found in framebuffer.");
        return false;
    }

    // Allocate ImageData object
    auto img_data = image_data_pool->acquire();
    if (!img_data) {
        APP_LOG_ERROR(std::string(stream_name) + " Image data pool exhaust.");
        return false;
    }

    // Populate ImageData with DMA-BUF details
    img_data->width = cfg.size.width;
    img_data->height = cfg.size.height;
    img_data->stride = cfg.stride;
    img_data->format = actual_format;
    img_data->frame_id = static_cast<int>(frame_id);
    img_data->capture_time = capture_time;
    img_data->t_capture_raw_ms = t_capture_raw_ms;
    img_data->length = total_length; // Total length of all planes combined
    
    // For now, assuming single FD for the primary plane, as `ImageData` has single `fd` and `offset`.
    // This will need refinement for true multi-plane DMA-BUF handling.
    img_data->fd = fds[0];
    img_data->offset = offsets[0];

    // Population of telemetry fields
    img_data->cam_exposure_ms = static_cast<float>(exposure_ms);
    auto now = std::chrono::steady_clock::now();
    img_data->cam_isp_latency_ms = std::chrono::duration<float, std::chrono::milliseconds::period>(now - capture_time).count();
    img_data->cam_buffer_usage_percent = -1.0f; // Placeholder

    // Acquire a PooledBuffer<uint8_t> as a metadata container, NOT for pixel data.
    auto pooled_buffer = buffer_pool->acquire();
    if (!pooled_buffer) {
        APP_LOG_ERROR(std::string(stream_name) + " Failed to acquire metadata buffer from pool.");
        image_data_pool->release(img_data);
        return false;
    }

    // Ensure buffer is large enough for pixel data
    if (pooled_buffer->data.size() < total_length) {
        pooled_buffer->data.resize(total_length);
    }

    // Copy actual pixel data from libcamera FrameBuffer to pooled_buffer
    // The framebuffer planes contain the actual image data
    size_t copied = 0;
    for (const auto& plane : fb->planes()) {
        if (copied >= total_length) break;

        // Use mmap to access DMA-BUF data from CPU
        if (plane.fd.isValid()) {
            void* map_addr = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED, plane.fd.get(), plane.offset);
            if (map_addr != MAP_FAILED) {
                size_t to_copy = std::min<size_t>(plane.length, total_length - copied);
                std::memcpy(pooled_buffer->data.data() + copied, map_addr, to_copy);
                copied += to_copy;
                munmap(map_addr, plane.length);
            }
        }
    }

    if (copied == 0) {
        APP_LOG_WARNING(std::string(stream_name) + " Failed to copy any pixel data from framebuffer");
    }

    if (copied == 0) {
        APP_LOG_WARNING(std::string(stream_name) + " Failed to copy any pixel data from framebuffer");
    }

    // Populate metadata into the pooled_buffer
    pooled_buffer->cam_exposure_ms = img_data->cam_exposure_ms;
    pooled_buffer->cam_isp_latency_ms = img_data->cam_isp_latency_ms;
    pooled_buffer->frame_id = img_data->frame_id;
    pooled_buffer->t_capture_raw_ms = img_data->t_capture_raw_ms;
    pooled_buffer->size = copied; // Store actual copied size

    img_data->buffer = std::move(pooled_buffer); // Transfer ownership of metadata container
    
    img_data->ingest_start_time = process_start;
    img_data->ingest_end_time = std::chrono::steady_clock::now();
    
    if (this->app_ref_) {
        this->app_ref_->inc_cam_to_viz_produced();
    }

    if (!queue.push(img_data)) {
        if (this->app_ref_) {
            this->app_ref_->inc_cam_to_viz_dropped();
        }
        img_data->buffer.reset(); // Release metadata buffer
        image_data_pool->release(img_data); 
        return false;
    }
    
    return true;
}

CameraCapture::CameraCapture(unsigned int main_width, unsigned int main_height,
                             unsigned int tpu_width, unsigned int tpu_height,
                             unsigned int tpu_fps,
                             unsigned int target_tpu_width, unsigned int target_tpu_height,
                             std::shared_ptr<BufferPool<uint8_t>> image_buffer_pool,
                             std::shared_ptr<ObjectPool<ImageData>> image_data_pool,
                             ImageQueue& image_processor_input_queue,
                             std::chrono::seconds watchdog_timeout)
    : width_(main_width), height_(main_height),
      tpu_width_(tpu_width), tpu_height_(tpu_height),
      tpu_fps_(tpu_fps),
      target_tpu_width_(target_tpu_width), target_tpu_height_(target_tpu_height),
      image_processor_input_queue_(image_processor_input_queue),
      image_buffer_pool_(image_buffer_pool),
      image_data_pool_(image_data_pool),
      watchdog_timeout_(watchdog_timeout),
      frame_count_(0),
      main_output_queues_() { // Initialize as empty vector
    APP_LOG_INFO("CameraCapture constructor called.");
    
    camera_manager_ = std::make_unique<libcamera::CameraManager>();
    int ret = camera_manager_->start();
    if (ret) {
        APP_LOG_ERROR("Failed to start CameraManager: " + std::to_string(ret));
        camera_manager_.reset();
    }
}

CameraCapture::~CameraCapture() {
    stop();
}

bool CameraCapture::acquire_camera() {
    if (!camera_manager_) {
        APP_LOG_ERROR("Acquire: FAILURE - CameraManager is null.");
        return false;
    }
    
    auto cameras = camera_manager_->cameras();
    if (cameras.empty()) {
        APP_LOG_ERROR("Acquire: FAILURE - No cameras found.");
        return false;
    }

    for (const auto& cam : cameras) {
        if (cam->id().find("imx708") != std::string::npos) {
            camera_ = cam;
            APP_LOG_INFO("Explicitly selected IMX708 camera: " + camera_->id());
            break;
        }
    }
    
    // If no IMX708 camera found, use the first available camera
    if (!camera_) {
        camera_ = cameras[0];
        APP_LOG_INFO("IMX708 camera not found, using default camera: " + camera_->id());
    }

    if (!camera_) {
        APP_LOG_ERROR("Acquire: FAILURE - Null camera pointer.");
        return false;
    }

    APP_LOG_INFO("Selected Camera ID: " + camera_->id());
    
    int ret = camera_->acquire();
    if (ret) {
        APP_LOG_ERROR("Acquire: FAILURE - Failed to acquire camera: " + std::to_string(ret));
        camera_.reset();
        return false;
    }
    
    APP_LOG_INFO("Camera acquired successfully.");
    return true;
}

bool CameraCapture::start() {
    APP_LOG_INFO("CameraCapture::start() called.");
    // REMEDIATION 2026-02-02: Use memory_order_acquire for atomic loads
    if (Aurore::atomic_load_acquire(running_)) return false;
    APP_LOG_INFO("About to acquire camera...");
    if (!acquire_camera()) {
        APP_LOG_ERROR("Failed to acquire camera!");
        return false;
    }
    APP_LOG_INFO("Camera acquired successfully, about to setup camera...");
    if (!setup_camera()) {
        APP_LOG_ERROR("Failed to setup camera!");
        camera_->release();
        camera_.reset();
        return false;
    }
    APP_LOG_INFO("Camera setup completed successfully");
    
    camera_->requestCompleted.connect(this, &CameraCapture::request_complete_callback);

    libcamera::ControlList controls_to_set;
    // [REQ-007] Hard FPS Lock & Exposure
    int64_t frame_duration_us = 1000000 / tpu_fps_;
    controls_to_set.set(libcamera::controls::FrameDurationLimits, libcamera::Span<const int64_t, 2>({frame_duration_us, frame_duration_us}));
    
    // FIX: Higher exposure for brighter image
    controls_to_set.set(libcamera::controls::AeEnable, false);
    controls_to_set.set(libcamera::controls::ExposureTime, int64_t(25000));  // 25ms
    controls_to_set.set(libcamera::controls::AnalogueGain, 3.0f);  // 3x gain
    
    // NOTE: Removed ScalerCrop control - it affects ALL streams at the ISP level.
    // The TPU stream is already configured to output 320x320 via tpuCfg.size (lines 400-405).
    // ScalerCrop was incorrectly cropping the main stream too, causing 320x320 display output.
    // Main stream now gets full sensor FOV, TPU stream gets ISP-cropped 320x320 via stream config.
    
    if (camera_->start(&controls_to_set)) {
        APP_LOG_ERROR("Failed to start camera.");
        camera_->requestCompleted.disconnect(this, &CameraCapture::request_complete_callback);
        camera_->release();
        camera_.reset();
        return false;
    }

    // Restore FrameDuration logging
    const libcamera::ControlList &properties = camera_->properties();
    auto frame_duration = properties.get(libcamera::controls::FrameDuration);
    if (frame_duration) {
        APP_LOG_INFO("Actual FrameDuration: " + std::to_string(*frame_duration) + " us (" + std::to_string(1000000.0 / *frame_duration) + " FPS)");
    }

    // REMEDIATION 2026-02-02: Use memory_order_release for atomic stores
    Aurore::atomic_store_release(running_, true);
    Aurore::atomic_store_release(processing_running_, true);
    request_processor_thread_ = std::thread(&CameraCapture::request_processor_thread_func, this);
    
    frame_count_ = 0;
    last_frame_time_ = std::chrono::steady_clock::now();
    first_frame_time_ = last_frame_time_;
    
    for (auto& req_ptr : requests_) {
        if (camera_->queueRequest(req_ptr.get())) { 
            APP_LOG_ERROR("Failed to queue initial request.");
            // REMEDIATION 2026-02-02: Use memory_order_release for atomic store
            Aurore::atomic_store_release(running_, false); 
            stop(); 
            return false;
        }
    }
    return true;
}

void CameraCapture::stop() {
    // REMEDIATION 2026-02-02: Use memory_order_release for atomic store
    if (!running_.exchange(false)) return;
    APP_LOG_INFO("Stopping CameraCapture...");
    Aurore::atomic_store_release(processing_running_, false);

    if (camera_) {
        camera_->requestCompleted.disconnect(this, &CameraCapture::request_complete_callback);
        camera_->stop();
        camera_->release();
        APP_LOG_INFO("Libcamera released.");
    }
    
    request_queue_cond_var_.notify_one();
    // Use timed join to prevent indefinite blocking
    if (request_processor_thread_.joinable()) {
        std::promise<bool> promise;
        std::future<bool> future = promise.get_future();
        
        std::thread timer_thread([this, &promise]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (request_processor_thread_.joinable()) {
                APP_LOG_WARNING("CameraCapture request processor thread did not join within timeout");
                promise.set_value(false);
            } else {
                promise.set_value(true);
            }
        });
        
        if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout) {
            timer_thread.join();
        } else {
            future.get();
            if (request_processor_thread_.joinable()) {
                request_processor_thread_.join();
            }
        }
        if (timer_thread.joinable()) {
            timer_thread.join();
        }
    }

    if (allocator_) {
        if (video_stream_) allocator_->free(video_stream_);
        if (tpu_stream_) allocator_->free(tpu_stream_);
        allocator_.reset();
    }
    
    // PERSISTENT MAPPING CLEANUP (No longer needed for DMA-BUF based zero-copy in CameraCapture)
    mapped_buffers_.clear(); // Clear the map, but no munmap is needed for fds.
    
    requests_.clear();
    camera_.reset();
    APP_LOG_INFO("CameraCapture stopped.");
}

bool CameraCapture::setup_camera() {
    if (!camera_) {
        APP_LOG_ERROR("Setup camera: camera is null!");
        return false;
    }
    APP_LOG_INFO("Setup camera: starting configuration process...");
    
    // MANDATE: Exactly two streams for PiSP BCM2712_C0
    // Requesting processed formats (YUV/RGB) engages the ISP hardware.
    std::vector<libcamera::StreamRole> roles = {
        libcamera::StreamRole::VideoRecording, // Main pipeline (Encoder/Overlays)
        libcamera::StreamRole::Viewfinder      // TPU pipeline (Inference)
    };
    
    APP_LOG_INFO("Setup camera: generating configuration...");
    std::unique_ptr<libcamera::CameraConfiguration> config = camera_->generateConfiguration(roles);
    if (!config || config->size() != 2) {
        APP_LOG_ERROR("Failed to generate configuration for exactly two streams.");
        return false;
    }

    // --- PHASE II: SENSOR TUNING ---
    // Request full sensor resolution for maximum flexibility with ISP cropping
    libcamera::SensorConfiguration sensorConfig;
    sensorConfig.bitDepth = 10;
    sensorConfig.outputSize = {4608, 2592};
    config->sensorConfig = sensorConfig;

    // Main Stream (Index 0): ISP processed BGR for direct display compatibility
    libcamera::StreamConfiguration& mainCfg = config->at(0);
    mainCfg.pixelFormat = libcamera::formats::BGR888;
    mainCfg.size.width = width_;
    mainCfg.size.height = height_;
    mainCfg.bufferCount = 10;

    // TPU Stream (Index 1): ISP processed RGB888 for Coral TPU requirements
    // ISP-LEVEL CROPPING: 320x320 output from ISP hardware (cropped from full sensor)
    libcamera::StreamConfiguration& tpuCfg = config->at(1);
    tpuCfg.pixelFormat = libcamera::formats::RGB888; 
    // Request 320x320 - ISP crops from full sensor (4608x2592) to this size
    tpuCfg.size.width = 320; 
    tpuCfg.size.height = 320; 
    tpuCfg.bufferCount = 32;
    
    // VALIDATION AUDIT
    libcamera::CameraConfiguration::Status validation = config->validate();
    if (validation == libcamera::CameraConfiguration::Invalid) {
        APP_LOG_ERROR("Camera configuration is INVALID and cannot be adjusted.");
        return false;
    }
    if (validation == libcamera::CameraConfiguration::Adjusted) {
        APP_LOG_WARNING("Camera configuration was ADJUSTED by libcamera to fit hardware constraints.");
    }

    APP_LOG_INFO("Setup camera: about to configure camera...");
    if (camera_->configure(config.get())) {
        APP_LOG_ERROR("Failed to call camera_->configure(). Hardware rejected processed stream configuration.");
        return false;
    }
    APP_LOG_INFO("Setup camera: camera configured successfully");

    video_stream_ = mainCfg.stream();
    tpu_stream_ = tpuCfg.stream();
    actual_pixel_format_ = mainCfg.pixelFormat;
    actual_size_ = mainCfg.size;
    actual_stride_ = mainCfg.stride;

    allocator_ = std::make_unique<libcamera::FrameBufferAllocator>(camera_);
    // allocator_->setMemType(libcamera::V4L2_MEMORY_DMABUF); // Removed: setMemType does not exist
    if (allocator_->allocate(video_stream_) < 0) {
        APP_LOG_ERROR("Failed to allocate buffers for Main (YUV) stream.");
        return false;
    }
    if (allocator_->allocate(tpu_stream_) < 0) {
        APP_LOG_ERROR("Failed to allocate buffers for TPU (RGB) stream.");
        return false;
    }

    const std::vector<std::unique_ptr<libcamera::FrameBuffer>>& video_buffers = allocator_->buffers(video_stream_);
    const std::vector<std::unique_ptr<libcamera::FrameBuffer>>& tpu_buffers = allocator_->buffers(tpu_stream_);
    
    // PERSISTENT MAPPING for DMA-BUF is different now.
    // We will populate mapped_buffers_ with fd/offset directly, no mmap here.
    mapped_buffers_.clear(); // Clear the map (now using MappedBufferInfo)
    for (const auto& buffer : video_buffers) {
        size_t total_length = 0;
        int fd = -1;
        size_t offset = 0;
        for (const auto& plane : buffer->planes()) {
            total_length += plane.length;
            if (fd == -1) { // Take the first FD and offset. Needs more robust handling for multi-plane.
                fd = plane.fd.get();
                offset = plane.offset;
            }
        }
        if (fd != -1) {
            mapped_buffers_[buffer.get()] = {nullptr, total_length, fd, offset}; // addr is nullptr as we don't mmap here.
        }
    }
    for (const auto& buffer : tpu_buffers) {
        size_t total_length = 0;
        int fd = -1;
        size_t offset = 0;
        for (const auto& plane : buffer->planes()) {
            total_length += plane.length;
            if (fd == -1) { // Take the first FD and offset.
                fd = plane.fd.get();
                offset = plane.offset;
            }
        }
        if (fd != -1) {
            mapped_buffers_[buffer.get()] = {nullptr, total_length, fd, offset}; // addr is nullptr.
        }
    }

    size_t min_buffers = std::min(video_buffers.size(), tpu_buffers.size());
    
    requests_.clear();
    for (unsigned int i = 0; i < min_buffers; ++i) {
        std::unique_ptr<libcamera::Request> request = camera_->createRequest();
        if (!request) return false;
        if (request->addBuffer(video_stream_, video_buffers[i].get()) != 0) return false;
        if (request->addBuffer(tpu_stream_, tpu_buffers[i].get()) != 0) return false;
        requests_.push_back(std::move(request));
    }
    
    APP_LOG_INFO("ISP Engaged. Camera configured with " + std::to_string(requests_.size()) + " dual-buffered YUV/RGB requests.");
    return true;
}

void CameraCapture::request_complete_callback(libcamera::Request* request) {
    // REMEDIATION 2026-02-02: Use memory_order_acquire for atomic loads
    if (!Aurore::atomic_load_acquire(running_) || !Aurore::atomic_load_acquire(processing_running_)) {
        if (request->status() != libcamera::Request::RequestCancelled) {
            request->reuse(libcamera::Request::ReuseBuffers);
            if (camera_) camera_->queueRequest(request);
        }
        return;
    }
    if (request->status() != libcamera::Request::RequestComplete) {
        APP_LOG_ERROR("Request failed status: " + std::to_string(request->status()));
        request->reuse(libcamera::Request::ReuseBuffers); 
        if (camera_) camera_->queueRequest(request);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(request_queue_mutex_);
        request_queue_.push(request);
        request_queue_cond_var_.notify_one();
    }
}



void CameraCapture::request_processor_thread_func() {
    setup_thread_affinity(); // Apply affinity

    APP_LOG_INFO("CameraCapture: Request processor thread started.");
    // REMEDIATION 2026-02-02: Use memory_order_acquire for atomic loads
    while (Aurore::atomic_load_acquire(processing_running_) && g_running.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(request_queue_mutex_);
        if (!request_queue_cond_var_.wait_for(lock, std::chrono::microseconds(1000), [this] {  // Reduced to 1ms for higher frame rates
            return !request_queue_.empty() || !Aurore::atomic_load_acquire(processing_running_) || !g_running.load();
        })) continue;

        if ((!Aurore::atomic_load_acquire(processing_running_) || !g_running.load()) && request_queue_.empty()) break;
        if (request_queue_.empty()) continue;
        
        libcamera::Request* request = request_queue_.front();
        request_queue_.pop();
        lock.unlock();

        if (!request) continue;

        auto now_mon = std::chrono::steady_clock::now();
        auto now_sys = std::chrono::system_clock::now();
        long long epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch()).count();
        
        // [REQ-009] Authoritative Monotonic Raw Clock (Unified across all modules)
        uint64_t t_capture_ms = get_time_raw_ms();
        auto capture_time = now_mon; 
        
        long long frame_id = request->sequence();
        // Validate frame_id to prevent invalid values from propagating
        if (frame_id < 0) {
            static std::atomic<int> invalid_frame_counter{0};
            int counter = invalid_frame_counter.fetch_add(1);
            if (counter % 100 == 0) { // Log every 100th invalid frame to avoid log spam
                APP_LOG_WARNING("CameraCapture: Invalid frame_id " + std::to_string(frame_id) + " detected, using sequence counter instead");
            }
            // Use global frame counter as fallback
            frame_id = ImageData::global_frame_counter.fetch_add(1);
        }

        // --- INSERT FAULT INJECTION HERE ---
        if (aurore::faultinjection::inject_frame_drop()) {
            APP_LOG_WARNING("CameraCapture: Injected frame drop for frame_id " + std::to_string(request->sequence()));
            if (app_ref_) {
                app_ref_->inc_cam_to_viz_dropped(); // Assuming visual pipeline is affected
                app_ref_->inc_cam_to_tpu_proc_dropped(); // Assuming TPU pipeline is affected
            }
            request->reuse(libcamera::Request::ReuseBuffers);
            // REMEDIATION 2026-02-02: Use memory_order_acquire for atomic load
            if (Aurore::atomic_load_acquire(running_) && camera_) camera_->queueRequest(request);
            continue; // Skip further processing for this frame
        }
        // --- END FAULT INJECTION ---

        // [TRACE] MIPI Interrupt Stage Entry
        uint64_t mipi_timestamp_ns = 0;
        for (auto const& [stream, buffer] : request->buffers()) {
            mipi_timestamp_ns = buffer->metadata().timestamp;
            break;
        }
        if (mipi_timestamp_ns > 0) {
            aurore::trace::trace_stage_data(aurore::trace::TraceStage::MIPI_INTERRUPT, 
                                       static_cast<uint32_t>(frame_id), 
                                       &mipi_timestamp_ns, sizeof(mipi_timestamp_ns));
        }
        
        // [TRACE] LIBCAMERA_CAPTURE Stage Entry
        aurore::trace::trace_stage_enter(aurore::trace::TraceStage::LIBCAMERA_CAPTURE, 
                                         static_cast<uint32_t>(frame_id));

        long long exposure_ms = 0;
        auto md_exposure_us = request->metadata().get(libcamera::controls::ExposureTime);
        if (md_exposure_us) exposure_ms = *md_exposure_us / 1000;

        // Log CameraCapture telemetry to unified CSV
        {
            ::aurore::logging::CsvLogEntry cam_entry;
            ::aurore::logging::copy_to_array(cam_entry.module, "CameraCapture");
            copy_to_array(cam_entry.event, "frame_captured");
            cam_entry.produced_ts_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            cam_entry.call_ts_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            cam_entry.cam_frame_id = static_cast<int>(frame_id);
            cam_entry.cam_exposure_ms = static_cast<float>(exposure_ms);
            
            // ISP Latency: Time from sensor capture to now
            uint64_t sensor_ts_ns = 0;
            for (auto const& [stream, buffer] : request->buffers()) {
                sensor_ts_ns = buffer->metadata().timestamp;
                break;
            }
            if (sensor_ts_ns > 0) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
                cam_entry.cam_isp_latency_ms = static_cast<float>(now_ns - sensor_ts_ns) / 1000000.0f;
            } else {
                cam_entry.cam_isp_latency_ms = 0.0f;
            }
            
            if (image_buffer_pool_) {
                auto stats = image_buffer_pool_->get_buffer_stats();
                if (stats.second > 0) {
                    cam_entry.cam_buffer_usage_percent = (static_cast<float>(stats.second - stats.first) / static_cast<float>(stats.second)) * 100.0f;
                }
            }
            
            ::aurore::logging::Logger::getInstance().log_csv(cam_entry);
        }
        
        // Extract buffers
        libcamera::Request::BufferMap captured_buffers = request->buffers();
        
        // Log SENSOR FPS frequently
                    if (captured_buffers.empty()) {
                        APP_LOG_WARNING("CameraCapture: No buffers in completed request.");
                        if (app_ref_) {
                            app_ref_->inc_cam_to_viz_dropped(); // Changed from produced to dropped
                            app_ref_->inc_cam_to_viz_dropped();
                            app_ref_->inc_cam_to_tpu_proc_dropped(); // Changed from produced to dropped
                            app_ref_->inc_cam_to_tpu_proc_dropped();
                        }
                        
                        request->reuse(libcamera::Request::ReuseBuffers); 
                        // REMEDIATION 2026-02-02: Use memory_order_acquire for atomic load
                        if (Aurore::atomic_load_acquire(running_) && camera_) camera_->queueRequest(request);
                    } else {
                        // The `mapped_buffers_` parameter is removed from process_frame_buffer
            // Extract sensor timestamp for latency calculation
            uint64_t sensor_ts_ns = 0;
            for (auto const& [stream, buffer] : captured_buffers) {
                sensor_ts_ns = buffer->metadata().timestamp;
                break;
            }

            if (captured_buffers.count(video_stream_)) {
                const libcamera::FrameBuffer* video_fb = captured_buffers.at(video_stream_);
                if (video_fb) {
                    // ISP MANDATE: Distribute to ALL registered consumers (Visualization, Encoder, etc.)
                    // Now iterating through the stored vector of ImageQueue pointers
                    for (auto* queue_ptr : main_output_queues_) {
                        if (queue_ptr) { // Check for nullptr in case any queue was not properly set
                            if (!this->process_frame_buffer(video_fb, video_stream_->configuration(), image_buffer_pool_, image_data_pool_, *queue_ptr, "Main Video Stream", width_, height_, capture_time, t_capture_ms, video_stream_->configuration().pixelFormat, frame_id, exposure_ms, &main_stream_drop_count_, &tpu_stream_drop_count_, nullptr)) { // mapped_buffers_ parameter removed
                                // Drop handled inside process_frame_buffer
                            }
                        } else {
                            APP_LOG_ERROR("CameraCapture: Null pointer in main_output_queues_ vector.");
                        }
                    }
                } else {
                    APP_LOG_ERROR("CameraCapture: Null video frame buffer.");
                    if (app_ref_) {
                        app_ref_->inc_cam_to_viz_dropped(); // Changed from produced to dropped
                        app_ref_->inc_cam_to_viz_dropped();
                    }
                }
            }

            if (captured_buffers.count(tpu_stream_)) {
                const libcamera::FrameBuffer* tpu_fb = captured_buffers.at(tpu_stream_);
                if (tpu_fb) {
                    // If direct inference queue is set (TPU disabled), pass frame directly to inference
                    if (direct_inference_queue_) {
                        // Pass the frame directly to the inference queue without processing
                        if (this->process_frame_for_direct_inference(tpu_fb, tpu_stream_->configuration(), capture_time, t_capture_ms, frame_id, exposure_ms, sensor_ts_ns)) {
                            fps_measurement_frames_++;
                            if (fps_measurement_frames_ > skip_initial_measurements_) {
                                auto interval_us = std::chrono::duration_cast<std::chrono::microseconds>(now_mon - last_frame_time_).count();
                                static double rolling_avg_us = 8333.33;
                                rolling_avg_us = rolling_avg_us * 0.95 + (double)interval_us * 0.05;
                                frame_rate_.store(static_cast<int>(1000000.0 / rolling_avg_us));
                            }
                            last_frame_time_ = now_mon;
                            last_frame_timestamp_.store(epoch_ms); // Telemetry Epoch MS
                        }
                    } else {
                        // Original processing for TPU-enabled case
                        if (this->process_tpu_processed_frame_buffer(tpu_fb, tpu_stream_->configuration(), capture_time, t_capture_ms, frame_id, exposure_ms, sensor_ts_ns)) {
                            fps_measurement_frames_++;
                            if (fps_measurement_frames_ > skip_initial_measurements_) {
                                auto interval_us = std::chrono::duration_cast<std::chrono::microseconds>(now_mon - last_frame_time_).count();
                                static double rolling_avg_us = 8333.33;
                                rolling_avg_us = rolling_avg_us * 0.95 + (double)interval_us * 0.05;
                                frame_rate_.store(static_cast<int>(1000000.0 / rolling_avg_us));
                            }
                            last_frame_time_ = now_mon;
                            last_frame_timestamp_.store(epoch_ms); // Telemetry Epoch MS
                        }
                    }
                } else {
                    APP_LOG_ERROR("CameraCapture: Null TPU frame buffer.");
                    if (app_ref_) {
                        app_ref_->inc_cam_to_tpu_proc_dropped(); // Changed from produced to dropped
                        app_ref_->inc_cam_to_tpu_proc_dropped();
                    }
                }
            } else {
                APP_LOG_WARNING("CameraCapture: TPU stream buffer missing.");
                if (app_ref_) {
                    app_ref_->inc_cam_to_tpu_proc_dropped(); // Changed from produced to dropped
                    app_ref_->inc_cam_to_tpu_proc_dropped();
                }
            }

            // [TRACE] LIBCAMERA_CAPTURE Stage Exit
            uint64_t capture_end_ns = get_time_raw_ns();
            uint64_t capture_latency_ns = capture_end_ns - (mipi_timestamp_ns > 0 ? mipi_timestamp_ns : capture_end_ns);
            aurore::trace::trace_stage_exit(aurore::trace::TraceStage::LIBCAMERA_CAPTURE, 
                                             static_cast<uint32_t>(frame_id), 
                                             capture_latency_ns);

            // REUSE AFTER PROCESSING
            request->reuse(libcamera::Request::ReuseBuffers); 
            // REMEDIATION 2026-02-02: Use memory_order_acquire for atomic load
            if (Aurore::atomic_load_acquire(running_) && camera_) camera_->queueRequest(request);
        }
    }
}

bool CameraCapture::init_video_encoder() { return true; }

bool CameraCapture::process_tpu_processed_frame_buffer(const libcamera::FrameBuffer* fb,
                                                 const libcamera::StreamConfiguration& cfg,
                                                 std::chrono::steady_clock::time_point capture_time,
                                                 uint64_t t_capture_raw_ms,
                                                 long long frame_id,
                                                 long long exposure_ms,
                                                 uint64_t sensor_ts_ns) { // Mapped_buffers parameter removed
    if (fb->planes().empty()) {
        APP_LOG_ERROR("TPU processed: No planes.");
        return false;
    }

    const libcamera::FrameBuffer::Plane& plane = fb->planes()[0];
    int fd = plane.fd.get();
    size_t length = plane.length;

    // No longer using pooled_buffer for storing actual pixel data directly via memcpy.
    // Acquire a PooledBuffer<uint8_t> as a metadata container, NOT for pixel data.
    auto pooled_buffer = image_buffer_pool_->acquire();
    if (!pooled_buffer) {
        APP_LOG_ERROR("TPU processed: Failed to acquire metadata buffer from pool.");
        if (app_ref_) {
            app_ref_->inc_cam_to_tpu_proc_produced();
            app_ref_->inc_cam_to_tpu_proc_dropped();
        }
        return false;
    }

    ImageData* image_data = image_data_pool_->acquire();
    if (!image_data) {
        APP_LOG_ERROR("TPU processed: ImageData pool exhaustion.");
        if (app_ref_) {
            app_ref_->inc_cam_to_tpu_proc_produced();
            app_ref_->inc_cam_to_tpu_proc_dropped();
        }
        pooled_buffer.reset(); // Release metadata buffer
        return false;
    }

    *image_data = ImageData(capture_time, (int)frame_id);
    image_data->width = cfg.size.width;
    image_data->height = cfg.size.height;
    image_data->stride = cfg.stride;
    image_data->format = cfg.pixelFormat; // libcamera::formats::RGB888
    image_data->length = length;
    image_data->fd = fd;
    image_data->offset = plane.offset;
    
    // Populate metadata into the pooled_buffer
    pooled_buffer->cam_exposure_ms = static_cast<float>(exposure_ms);
    pooled_buffer->frame_id = (int)frame_id;
    pooled_buffer->t_capture_raw_ms = t_capture_raw_ms;
    pooled_buffer->size = length; // Indicate the size this buffer refers to.
    
    image_data->buffer = std::move(pooled_buffer); // Transfer ownership of metadata container
    image_data->capture_time = capture_time;
    image_data->t_capture_raw_ms = t_capture_raw_ms;

    // Population of telemetry fields for TPU stream
    image_data->cam_exposure_ms = static_cast<float>(exposure_ms);
    
    if (sensor_ts_ns > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        image_data->cam_isp_latency_ms = static_cast<float>(now_ns - sensor_ts_ns) / 1000000.0f;
    } else {
        auto now = std::chrono::steady_clock::now();
        image_data->cam_isp_latency_ms = std::chrono::duration<float, std::milli>(now - capture_time).count();
    }

    if (image_buffer_pool_) {
        auto stats = image_buffer_pool_->get_buffer_stats();
        if (stats.second > 0) {
            image_data->cam_buffer_usage_percent = (static_cast<float>(stats.second - stats.first) / static_cast<float>(stats.second)) * 100.0f;
        }
    }

    if (this->app_ref_) {
        this->app_ref_->inc_cam_to_tpu_proc_produced();
    }

    // Accounting Fix: Move increment_produced inside the lock-protected push block
    if (!image_processor_input_queue_.push(image_data)) {
        // CRITICAL: Release IMMEDIATELY if push fails to prevent pool exhaustion
        if (this->app_ref_) {
            app_ref_->inc_cam_to_tpu_proc_dropped();
        }
        image_data->buffer.reset();
        image_data_pool_->release(image_data);
        return false;
    }
    
    auto process_end = std::chrono::steady_clock::now();
    auto push_time = std::chrono::duration_cast<std::chrono::microseconds>(process_end - capture_time).count();
    long long current_push_avg = avg_capture_time_us_.load();
    avg_capture_time_us_.store(current_push_avg == 0 ? push_time : (long long)(current_push_avg * 0.8 + push_time * 0.2));
    
    return true;
}
bool CameraCapture::process_frame_for_direct_inference(const libcamera::FrameBuffer* fb,
                                                 const libcamera::StreamConfiguration& cfg,
                                                 std::chrono::steady_clock::time_point capture_time,
                                                 uint64_t t_capture_raw_ms,
                                                 long long frame_id,
                                                 long long exposure_ms,
                                                 uint64_t sensor_ts_ns) {
    if (fb->planes().empty()) {
        APP_LOG_ERROR("Direct inference: No planes.");
        return false;
    }

    const libcamera::FrameBuffer::Plane& plane = fb->planes()[0];
    int fd = plane.fd.get();
    size_t length = plane.length;

    // Acquire a PooledBuffer<uint8_t> as a metadata container, NOT for pixel data.
    auto pooled_buffer = image_buffer_pool_->acquire();
    if (!pooled_buffer) {
        APP_LOG_ERROR("Direct inference: Failed to acquire metadata buffer from pool.");
        if (app_ref_) {
            app_ref_->inc_cam_to_tpu_proc_produced();
            app_ref_->inc_cam_to_tpu_proc_dropped();
        }
        return false;
    }

    ImageData* image_data = image_data_pool_->acquire();
    if (!image_data) {
        APP_LOG_ERROR("Direct inference: ImageData pool exhaustion.");
        if (app_ref_) {
            app_ref_->inc_cam_to_tpu_proc_produced();
            app_ref_->inc_cam_to_tpu_proc_dropped();
        }
        pooled_buffer.reset(); // Release metadata buffer
        return false;
    }

    *image_data = ImageData(capture_time, (int)frame_id);
    image_data->width = cfg.size.width;
    image_data->height = cfg.size.height;
    image_data->stride = cfg.stride;
    image_data->format = cfg.pixelFormat; // libcamera::formats::RGB888
    image_data->length = length;
    image_data->fd = fd;
    image_data->offset = plane.offset;

    // Populate metadata into the pooled_buffer
    pooled_buffer->cam_exposure_ms = static_cast<float>(exposure_ms);
    pooled_buffer->frame_id = (int)frame_id;
    pooled_buffer->t_capture_raw_ms = t_capture_raw_ms;
    pooled_buffer->size = length; // Indicate the size this buffer refers to.

    image_data->buffer = std::move(pooled_buffer); // Transfer ownership of metadata container
    image_data->capture_time = capture_time;
    image_data->t_capture_raw_ms = t_capture_raw_ms;

    // Population of telemetry fields for TPU stream
    image_data->cam_exposure_ms = static_cast<float>(exposure_ms);

    if (sensor_ts_ns > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        image_data->cam_isp_latency_ms = static_cast<float>(now_ns - sensor_ts_ns) / 1000000.0f;
    } else {
        auto now = std::chrono::steady_clock::now();
        image_data->cam_isp_latency_ms = std::chrono::duration<float, std::milli>(now - capture_time).count();
    }

    if (image_buffer_pool_) {
        auto stats = image_buffer_pool_->get_buffer_stats();
        if (stats.second > 0) {
            image_data->cam_buffer_usage_percent = (static_cast<float>(stats.second - stats.first) / static_cast<float>(stats.second)) * 100.0f;
        }
    }

    if (this->app_ref_) {
        this->app_ref_->inc_cam_to_tpu_proc_produced();
    }

    // Push directly to the inference queue (bypassing ImageProcessor)
    if (!direct_inference_queue_->push(image_data)) {
        // CRITICAL: Release IMMEDIATELY if push fails to prevent pool exhaustion
        if (this->app_ref_) {
            app_ref_->inc_cam_to_tpu_proc_dropped();
        }
        image_data->buffer.reset();
        image_data_pool_->release(image_data);
        return false;
    }

    auto process_end = std::chrono::steady_clock::now();
    auto push_time = std::chrono::duration_cast<std::chrono::microseconds>(process_end - capture_time).count();
    long long current_push_avg = avg_capture_time_us_.load();
    avg_capture_time_us_.store(current_push_avg == 0 ? push_time : (long long)(current_push_avg * 0.8 + push_time * 0.2));

    return true;
}
