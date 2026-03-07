#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <map>
#include <set>
#include <sstream>

struct SecurityFinding {
    std::string severity;  // CRITICAL, HIGH, MEDIUM, LOW
    std::string category;
    std::string file;
    int line;
    std::string description;
    std::string recommendation;
};

void check_buffer_operations(const std::string& src_dir, std::vector<SecurityFinding>& findings) {
    std::vector<std::string> files = {
        "src/application.cpp", "src/camera/camera_capture.cpp",
        "src/image_processor.cpp", "src/inference/inference.cpp",
        "src/logic.cpp", "src/buffer_pool.h"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Skip include statements (both #include <...> and #include "...")
            if (line.find("#include") != std::string::npos) continue;
            
            // Check if line is a comment (starts with // after whitespace)
            size_t first_non_space = line.find_first_not_of(" \t");
            if (first_non_space != std::string::npos && 
                line.size() > first_non_space + 1 &&
                line[first_non_space] == '/' && 
                line[first_non_space + 1] == '/') continue;
            
            // Check for memcpy without size validation
            // Also skip SAFE_MEMCPY which has explicit size validation
            if (line.find("memcpy") != std::string::npos && 
                line.find("sizeof") == std::string::npos &&
                line.find("SAFE_MEMCPY") == std::string::npos) {
                SecurityFinding f;
                f.severity = "CRITICAL";
                f.category = "Buffer Overflow";
                f.file = file;
                f.line = line_num;
                f.description = "memcpy without explicit size validation";
                f.recommendation = "Add explicit size parameter validated against destination buffer";
                findings.push_back(f);
            }
            
            // Check for sprintf/sprintf
            if (line.find("sprintf(") != std::string::npos) {
                SecurityFinding f;
                f.severity = "HIGH";
                f.category = "Buffer Overflow";
                f.file = file;
                f.line = line_num;
                f.description = "Use of sprintf (no bounds checking)";
                f.recommendation = "Use snprintf or std::format";
                findings.push_back(f);
            }
            
            // Check for strcpy
            if (line.find("strcpy(") != std::string::npos) {
                SecurityFinding f;
                f.severity = "HIGH";
                f.category = "Buffer Overflow";
                f.file = file;
                f.line = line_num;
                f.description = "Use of strcpy (no bounds checking)";
                f.recommendation = "Use strncpy or std::string";
                findings.push_back(f);
            }
            
            // Check for unsafe array indexing
            std::regex unsafe_index(R"(\[\s*\w+\s*\])");
            if (std::regex_search(line, unsafe_index) && line.find("size_t") == std::string::npos) {
                if (line.find("if") == std::string::npos && line.find("<") == std::string::npos) {
                    SecurityFinding f;
                    f.severity = "MEDIUM";
                    f.category = "Array Bounds";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Array access without explicit bounds check";
                    f.recommendation = "Add bounds validation before array access";
                    findings.push_back(f);
                }
            }
        }
    }
}

void check_integer_operations(const std::string& src_dir, std::vector<SecurityFinding>& findings) {
    std::vector<std::string> files = {
        "src/logic.cpp", "src/geometry_verification.cpp",
        "src/ballistic_safety_test.cpp"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for potential integer overflow in multiplication
            if (line.find("*") != std::string::npos && 
                (line.find("width") != std::string::npos || 
                 line.find("height") != std::string::npos ||
                 line.find("size") != std::string::npos)) {
                
                if (line.find("int") != std::string::npos || line.find("int32") != std::string::npos) {
                    SecurityFinding f;
                    f.severity = "MEDIUM";
                    f.category = "Integer Overflow";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Potential integer overflow in multiplication";
                    f.recommendation = "Use 64-bit intermediate or check for overflow";
                    findings.push_back(f);
                }
            }
            
            // Check for signed/unsigned conversion issues
            if (line.find("-") != std::string::npos && line.find("size_t") != std::string::npos) {
                SecurityFinding f;
                f.severity = "MEDIUM";
                f.category = "Integer Overflow";
                f.file = file;
                f.line = line_num;
                f.description = "Potential underflow with unsigned type";
                f.recommendation = "Ensure proper handling of signed/unsigned conversion";
                findings.push_back(f);
            }
        }
    }
}

void check_secrets_management(const std::string& src_dir, std::vector<SecurityFinding>& findings) {
    std::vector<std::string> files = {
        "src/application.cpp", "src/config_loader.cpp",
        "src/logic.cpp"
    };
    
    std::regex secrets_pattern(R"((password|secret|token|key|api_key|credential)[\w\s]*=[\s]*["'][^"']+["'])",
                               std::regex::icase);
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            if (std::regex_search(line, secrets_pattern)) {
                SecurityFinding f;
                f.severity = "CRITICAL";
                f.category = "Secrets Management";
                f.file = file;
                f.line = line_num;
                f.description = "Potential hardcoded secret detected";
                f.recommendation = "Move secrets to environment variables or secure configuration";
                findings.push_back(f);
            }
        }
    }
}

void check_input_validation(const std::string& src_dir, std::vector<SecurityFinding>& findings) {
    std::vector<std::string> files = {
        "src/config_loader.cpp", "src/application.cpp",
        "src/camera/camera_capture.cpp"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for gets() usage
            if (line.find("gets(") != std::string::npos) {
                SecurityFinding f;
                f.severity = "CRITICAL";
                f.category = "Input Validation";
                f.file = file;
                f.line = line_num;
                f.description = "Use of gets() - buffer overflow vulnerability";
                f.recommendation = "Use fgets() or std::getline()";
                findings.push_back(f);
            }
            
            // Check for system() calls
            if (line.find("system(") != std::string::npos) {
                SecurityFinding f;
                f.severity = "HIGH";
                f.category = "Code Injection";
                f.file = file;
                f.line = line_num;
                f.description = "Use of system() with potential command injection";
                f.recommendation = "Validate input or use safer alternatives";
                findings.push_back(f);
            }
            
            // Check for popen() usage
            if (line.find("popen(") != std::string::npos) {
                SecurityFinding f;
                f.severity = "HIGH";
                f.category = "Code Injection";
                f.file = file;
                f.line = line_num;
                f.description = "Use of popen() with potential command injection";
                f.recommendation = "Validate input or use safer alternatives";
                findings.push_back(f);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::string src_dir = "/home/pi/Aurore";
    if (argc > 1) src_dir = argv[1];
    
    std::cout << "========================================\n";
    std::cout << "Security Audit - Buffer Overflow, Integer Overflow, Secrets\n";
    std::cout << "========================================\n\n";
    
    std::vector<SecurityFinding> findings;
    
    // Run all checks
    std::cout << "Checking buffer operations...\n";
    check_buffer_operations(src_dir, findings);
    
    std::cout << "Checking integer operations...\n";
    check_integer_operations(src_dir, findings);
    
    std::cout << "Checking secrets management...\n";
    check_secrets_management(src_dir, findings);
    
    std::cout << "Checking input validation...\n";
    check_input_validation(src_dir, findings);
    
    // Print findings
    std::cout << "\n========================================\n";
    std::cout << "SECURITY FINDINGS\n";
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
        std::cout << "\nRECOMMENDATION: Address CRITICAL and HIGH severity findings before deployment.\n";
        return 1;
    }
    
    std::cout << "\nSTATUS: No critical security vulnerabilities detected.\n";
    return 0;
}
