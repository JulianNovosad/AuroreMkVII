#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <map>
#include <set>
#include <sstream>

struct HardwareFinding {
    std::string severity;
    std::string category;
    std::string file;
    int line;
    std::string description;
    std::string recommendation;
};

void check_race_conditions(const std::string& src_dir, std::vector<HardwareFinding>& findings) {
    std::vector<std::string> files = {
        "src/camera/camera_capture.cpp", "src/camera/camera_capture.h",
        "src/image_processor.cpp", "src/image_processor.h",
        "src/inference/inference.cpp", "src/inference/inference.h",
        "src/logic.cpp", "src/logic.h",
        "src/lockfree_queue.h", "src/buffer_pool.h"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        std::set<std::string> shared_vars;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Detect atomic variable declarations
            std::regex atomic_decl(R"(std::atomic<(\w+)>\s+(\w+))");
            std::smatch match;
            if (std::regex_search(line, match, atomic_decl)) {
                shared_vars.insert(match[2]);
            }
            
            // Detect non-atomic shared variable access
            std::regex shared_access(R"((\w+)\.(load|store|fetch))");
            if (std::regex_search(line, match, shared_access)) {
                std::string var = match[1];
                if (shared_vars.count(var) == 0) {
                    // Check if it's an atomic being accessed non-atomically
                    HardwareFinding f;
                    f.severity = "HIGH";
                    f.category = "Race Condition";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Atomic variable access without proper memory ordering";
                    f.recommendation = "Specify memory_order_acquire/release/consume";
                    findings.push_back(f);
                }
            }
            
            // Check for missing memory barriers
            std::regex memory_order(R"(memory_order)");
            if (line.find("atomic") != std::string::npos && 
                line.find("memory_order") == std::string::npos &&
                (line.find(".load(") != std::string::npos || 
                 line.find(".store(") != std::string::npos)) {
                HardwareFinding f;
                f.severity = "MEDIUM";
                f.category = "Memory Ordering";
                f.file = file;
                f.line = line_num;
                f.description = "Atomic operation without explicit memory ordering";
                f.recommendation = "Specify memory_order for correctness guarantees";
                findings.push_back(f);
            }
            
            // Check for potential deadlock (nested locks)
            std::regex lock_guard(R"(lock_guard<)");
            if (std::regex_search(line, lock_guard)) {
                // This is simplified - real analysis would track lock ordering
            }
        }
    }
}

void check_dma_ownership(const std::string& src_dir, std::vector<HardwareFinding>& findings) {
    std::vector<std::string> files = {
        "src/camera/camera_capture.cpp", "src/camera/camera_capture.h",
        "src/image_processor.cpp", "src/inference/inference.cpp"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for DMA buffer operations
            std::vector<std::pair<std::regex, std::string>> dma_patterns = {
                {std::regex(R"(mmap\()"), "DMA buffer mapping"},
                {std::regex(R"(munmap\()"), "DMA buffer unmapping"},
                {std::regex(R"(fd\s*=\s*\d+)"), "File descriptor usage"},
                {std::regex(R"(DMA|BUF|dma|buf)"), "DMA-related operation"}
            };
            
            for (const auto& [pattern, desc] : dma_patterns) {
                if (std::regex_search(line, pattern)) {
                    // Check for proper pairing
                    HardwareFinding f;
                    f.severity = "LOW";
                    f.category = "DMA Ownership";
                    f.file = file;
                    f.line = line_num;
                    f.description = "DMA operation: " + desc;
                    f.recommendation = "Verify ownership transfer correctness";
                    findings.push_back(f);
                }
            }
            
            // Check for use-after-free risk
            if (line.find("release()") != std::string::npos ||
                line.find("reset()") != std::string::npos) {
                if (line.find("shared_ptr") != std::string::npos ||
                    line.find("unique_ptr") != std::string::npos) {
                    HardwareFinding f;
                    f.severity = "MEDIUM";
                    f.category = "DMA Ownership";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Smart pointer release - verify DMA buffer not in use";
                    f.recommendation = "Add synchronization to prevent use-after-free";
                    findings.push_back(f);
                }
            }
        }
    }
}

void check_cache_coherency(const std::string& src_dir, std::vector<HardwareFinding>& findings) {
    std::vector<std::string> files = {
        "src/camera/camera_capture.cpp", "src/image_processor.cpp",
        "src/inference/inference.cpp"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for cache control operations
            std::regex cache_ops(R"(cache|flush|invalidate|CPU_CLEAN|CACHE_CLEAN)");
            if (std::regex_search(line, cache_ops)) {
                HardwareFinding f;
                f.severity = "LOW";
                f.category = "Cache Coherency";
                f.file = file;
                f.line = line_num;
                f.description = "Cache operation detected";
                f.recommendation = "Verify correct cache flush/invalidate ordering";
                findings.push_back(f);
            }
            
            // Check for memory barriers
            std::regex barrier(R"(memory_barrier|__sync_synchronize|std::memory_order)");
            if (std::regex_search(line, barrier)) {
                HardwareFinding f;
                f.severity = "LOW";
                f.category = "Memory Barrier";
                f.file = file;
                f.line = line_num;
                f.description = "Memory barrier usage detected";
                f.recommendation = "Verify barrier placement is correct";
                findings.push_back(f);
            }
            
            // Check for buffer recycling without cache invalidation
            if (line.find("recycle") != std::string::npos ||
                line.find("pool") != std::string::npos) {
                if (line.find("cache") == std::string::npos &&
                    line.find("flush") == std::string::npos) {
                    HardwareFinding f;
                    f.severity = "MEDIUM";
                    f.category = "Cache Coherency";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Buffer recycling - verify cache invalidation";
                    f.recommendation = "Add cache flush before buffer reuse";
                    findings.push_back(f);
                }
            }
        }
    }
}

void check_hardware_timeouts(const std::string& src_dir, std::vector<HardwareFinding>& findings) {
    std::vector<std::string> files = {
        "src/camera/camera_capture.cpp", "src/inference/inference.cpp",
        "src/servo/pca9685_controller.cpp", "src/logic.cpp"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for timeout configurations
            std::regex timeout_config(R"(timeout|Timeout|TIMEOUT)");
            if (std::regex_search(line, timeout_config)) {
                HardwareFinding f;
                f.severity = "LOW";
                f.category = "Hardware Timeout";
                f.file = file;
                f.line = line_num;
                f.description = "Timeout configuration";
                f.recommendation = "Verify timeout values are appropriate";
                findings.push_back(f);
            }
            
            // Check for hardware error handling
            if (line.find("error") != std::string::npos ||
                line.find("ERROR") != std::string::npos) {
                if (line.find("return") != std::string::npos ||
                    line.find("throw") != std::string::npos) {
                    HardwareFinding f;
                    f.severity = "LOW";
                    f.category = "Error Handling";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Hardware error handling path";
                    f.recommendation = "Verify graceful degradation on errors";
                    findings.push_back(f);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::string src_dir = "/home/pi/Aurore";
    if (argc > 1) src_dir = argv[1];
    
    std::cout << "========================================\n";
    std::cout << "Hardware-Software Interface Audit\n";
    std::cout << "========================================\n\n";
    
    std::vector<HardwareFinding> findings;
    
    std::cout << "Checking for race conditions...\n";
    check_race_conditions(src_dir, findings);
    
    std::cout << "Checking DMA ownership...\n";
    check_dma_ownership(src_dir, findings);
    
    std::cout << "Checking cache coherency...\n";
    check_cache_coherency(src_dir, findings);
    
    std::cout << "Checking hardware timeouts...\n";
    check_hardware_timeouts(src_dir, findings);
    
    // Print findings
    std::cout << "\n========================================\n";
    std::cout << "HARDWARE INTERFACE FINDINGS\n";
    std::cout << "========================================\n\n";
    
    int critical = 0, high = 0, medium = 0, low = 0;
    
    for (const auto& f : findings) {
        if (f.severity == "CRITICAL") critical++;
        else if (f.severity == "HIGH") high++;
        else if (f.severity == "MEDIUM") medium++;
        else low++;
        
        std::cout << "[" << f.severity << "] " << f.category << "\n";
        std::cout << "  File: " << f.file << ":" << f.line << "\n";
        std::cout << "  " << f.description << "\n";
        std::cout << "  Recommendation: " << f.recommendation << "\n\n";
    }
    
    // Summary
    std::cout << "========================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "========================================\n";
    std::cout << "  CRITICAL: " << critical << "\n";
    std::cout << "  HIGH:     " << high << "\n";
    std::cout << "  MEDIUM:   " << medium << "\n";
    std::cout << "  LOW:      " << low << "\n";
    std::cout << "  TOTAL:    " << findings.size() << "\n";
    
    if (critical > 0 || high > 0) {
        std::cout << "\nRECOMMENDATION: Address CRITICAL and HIGH severity findings.\n";
        return 1;
    }
    
    std::cout << "\nSTATUS: Hardware interface acceptable.\n";
    return 0;
}
