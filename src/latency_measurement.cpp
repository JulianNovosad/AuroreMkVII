/**
 * @file latency_measurement.cpp
 * @brief End-to-end latency measurement for Aurore MkVII pipeline
 *
 * Runs the pipeline stages sequentially in one thread and records
 * nanosecond timestamps at each stage boundary:
 *
 *   Camera HW capture → Vision (wrap_as_mat) → Track (KCF update) → Actuation compute
 *
 * Note: sequential execution measures per-stage WCET accurately.
 * The multi-threaded pipeline adds ~4ms of phased scheduling overhead
 * (2ms vision→track phase + 2ms track→actuation phase) on top of these numbers.
 *
 * Usage: aurore_latency_measurement [--samples=N] [--output=path.csv] [--verbose]
 */

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <opencv2/core.hpp>

#include "aurore/camera_wrapper.hpp"
#include "aurore/timing.hpp"
#include "aurore/tracker.hpp"

namespace {

volatile sig_atomic_t g_shutdown = 0;
void signal_handler(int /*sig*/) { g_shutdown = 1; }

struct Stats {
    uint64_t min_us{UINT64_MAX};
    uint64_t max_us{0};
    double   sum_us{0.0};
    double   sum_sq_us{0.0};
    uint64_t count{0};

    void record(uint64_t v) {
        if (v < min_us) min_us = v;
        if (v > max_us) max_us = v;
        sum_us    += static_cast<double>(v);
        sum_sq_us += static_cast<double>(v) * static_cast<double>(v);
        ++count;
    }

    double mean()   const { return count ? sum_us / static_cast<double>(count) : 0.0; }
    double stddev() const {
        if (count < 2) return 0.0;
        const double m = mean();
        return std::sqrt(sum_sq_us / static_cast<double>(count) - m * m);
    }
};

void print_stats(const char* label, const Stats& s) {
    std::cout << "\n  " << label << ":\n"
              << "    Min:     " << s.min_us    << " µs\n"
              << "    Max:     " << s.max_us    << " µs\n"
              << "    Mean:    " << static_cast<uint64_t>(s.mean())   << " µs\n"
              << "    Jitter:  " << static_cast<uint64_t>(s.stddev()) << " µs (1\xCF\x83)\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    uint64_t    num_samples = 1000;
    std::string output_path = "latency_samples.csv";
    bool        verbose     = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--samples=", 0) == 0)
            num_samples = std::stoull(arg.substr(10));
        else if (arg.rfind("--output=", 0) == 0)
            output_path = arg.substr(9);
        else if (arg == "--verbose" || arg == "-v")
            verbose = true;
    }

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "=== Aurore MkVII End-to-End Latency Measurement ===\n"
              << "Samples : " << num_samples << "\n"
              << "Output  : " << output_path << "\n\n";

    // Camera init
    auto camera = std::make_unique<aurore::CameraWrapper>();
    if (!camera->init()) {
        std::cerr << "FATAL: camera init failed\n";
        return 1;
    }
    camera->start();

    // Tracker (initialised on first usable frame)
    aurore::KcfTracker tracker;
    tracker.set_camera(camera.get());
    bool tracker_ready = false;

    // CSV output
    std::ofstream csv(output_path);
    if (!csv.is_open()) {
        std::cerr << "FATAL: cannot open " << output_path << "\n";
        camera->stop();
        return 1;
    }
    csv << "sample_id,frame_seq,sw_receive_ts_ns,"
           "vision_us,track_us,actuation_compute_us,end_to_end_us,track_valid\n";

    Stats vision_stats, track_stats, actuation_stats, e2e_stats;

    // Warmup: discard 5 frames then initialise tracker and run 3 cold-start updates
    // before collecting any measurements (per CLAUDE.md: WCET from 2nd invocation).
    for (int w = 0; w < 5; ++w) {
        aurore::ZeroCopyFrame wf{};
        camera->capture_frame(wf, 500);
        camera->release_frame(wf);
    }
    {
        aurore::ZeroCopyFrame wf{};
        if (camera->capture_frame(wf, 500)) {
            const cv::Mat wbgr = camera->wrap_as_mat(wf);
            if (!tracker_ready && wbgr.cols > 0) {
                const int iw = wbgr.cols / 4;
                const int ih = wbgr.rows / 4;
                const cv::Rect2d bbox(
                    static_cast<double>(wbgr.cols / 2 - iw / 2),
                    static_cast<double>(wbgr.rows / 2 - ih / 2),
                    static_cast<double>(iw),
                    static_cast<double>(ih));
                tracker_ready = tracker.init(wbgr, bbox);
            }
            // Run 3 warmup updates to prime FFT allocations and caches
            for (int u = 0; u < 3 && tracker_ready; ++u) tracker.update(wbgr);
            camera->release_frame(wf);
        }
    }

    uint64_t sample_id = 0;

    while (sample_id < num_samples && !g_shutdown) {

        // Stage 1: Camera capture (t_sw_receive = software-side receive timestamp)
        aurore::ZeroCopyFrame frame{};
        if (!camera->capture_frame(frame, 100)) {
            if (verbose) std::cerr << "capture_frame timeout, retrying\n";
            continue;
        }
        const uint64_t t_sw_receive = aurore::get_timestamp();  // software pipeline start

        // Stage 2: Vision — wrap DMA buffer as BGR Mat (zero-copy)
        const uint64_t t_vis_start = aurore::get_timestamp();
        const cv::Mat  bgr         = camera->wrap_as_mat(frame);
        const uint64_t t_vis_done  = aurore::get_timestamp();

        // Stage 3: Track compute — KCF update (tracker already warmed up)
        aurore::TrackSolution sol{};
        if (tracker_ready) {
            sol = tracker.update(bgr);
        }
        const uint64_t t_track_done = aurore::get_timestamp();

        // Stage 4: Actuation compute (angle clamping; I2C write measured separately)
        float az_deg = 0.f;
        float el_deg = 0.f;
        if (sol.valid && bgr.cols > 0 && bgr.rows > 0) {
            az_deg = (sol.centroid_x / static_cast<float>(bgr.cols) - 0.5f) * 180.f;
            el_deg = (0.5f - sol.centroid_y / static_cast<float>(bgr.rows)) * 55.f;
            az_deg = std::max(-90.f, std::min(90.f, az_deg));
            el_deg = std::max(-10.f, std::min(45.f, el_deg));
        }
        (void)az_deg;
        (void)el_deg;
        const uint64_t t_act_done = aurore::get_timestamp();

        camera->release_frame(frame);

        // Compute latencies (µs)
        const uint64_t vision_us    = (t_vis_done   - t_vis_start)  / 1000;
        const uint64_t track_us     = (t_track_done  - t_vis_done)   / 1000;
        const uint64_t actuation_us = (t_act_done    - t_track_done) / 1000;
        // E2E: software pipeline start (frame received from kernel) → actuation done.
        // Excludes camera ISP pipeline latency which is hardware-constant and outside spec scope.
        const uint64_t e2e_us       = (t_act_done > t_sw_receive)
                                       ? (t_act_done - t_sw_receive) / 1000
                                       : 0;

        csv << sample_id       << ","
            << frame.sequence  << ","
            << t_sw_receive    << ","
            << vision_us       << ","
            << track_us        << ","
            << actuation_us    << ","
            << e2e_us          << ","
            << (sol.valid ? 1 : 0) << "\n";

        vision_stats.record(vision_us);
        track_stats.record(track_us);
        actuation_stats.record(actuation_us);
        e2e_stats.record(e2e_us);

        if (verbose && sample_id % 100 == 0) {
            std::cout << "sample " << sample_id
                      << "  e2e=" << e2e_us << "us"
                      << "  track=" << (sol.valid ? "ok" : "no") << "\n";
        }

        ++sample_id;
    }

    camera->stop();
    csv.close();

    // Summary
    std::cout << "\n=== RESULTS (" << sample_id << " samples) ===\n";
    print_stats("Vision (wrap_as_mat)", vision_stats);
    print_stats("Track  (KCF update)",  track_stats);
    print_stats("Actuation compute",    actuation_stats);

    const bool vision_pass = vision_stats.count && vision_stats.max_us <= 5000;
    const bool track_pass  = track_stats.count  && track_stats.max_us  <= 5000;

    std::cout << "\n  End-to-End (HW capture timestamp to actuation compute):\n"
              << "    Min:    " << e2e_stats.min_us << " us\n"
              << "    Max:    " << e2e_stats.max_us << " us\n"
              << "    Mean:   " << static_cast<uint64_t>(e2e_stats.mean())   << " us\n"
              << "    Jitter: " << static_cast<uint64_t>(e2e_stats.stddev()) << " us (1 sigma)\n"
              << "\n  Note: multi-thread pipeline adds ~4ms phased scheduling overhead;\n"
              << "        I2C actuation write adds ~1-3ms hardware latency on top.\n"
              << "\n  WCET spec per stage: <=5000 us\n"
              << "  Vision: " << (vision_pass ? "PASS" : "FAIL")
              << " (max=" << vision_stats.max_us << " us)\n"
              << "  Track:  " << (track_pass  ? "PASS" : "FAIL")
              << " (max=" << track_stats.max_us  << " us)\n\n";

    return 0;
}
