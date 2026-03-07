#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <cmath>
#include <limits>
#include <sstream>
#include <set>

struct NumericalFinding {
    std::string severity;
    std::string category;
    std::string file;
    int line;
    std::string description;
    std::string recommendation;
};

void check_floating_point_precision(const std::string& src_dir, std::vector<NumericalFinding>& findings) {
    std::vector<std::string> files = {
        "src/logic.cpp", "src/logic.h", "src/geometry_verification.cpp"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for double operations in single precision context
            if (line.find("float") != std::string::npos) {
                // Check for operations that might need double precision
                if (line.find("sqrt(") != std::string::npos ||
                    line.find("sin(") != std::string::npos ||
                    line.find("cos(") != std::string::npos ||
                    line.find("atan2(") != std::string::npos) {
                    if (line.find("double") == std::string::npos) {
                        NumericalFinding f;
                        f.severity = "MEDIUM";
                        f.category = "Floating-Point Precision";
                        f.file = file;
                        f.line = line_num;
                        f.description = "Trigonometric or sqrt operation on float - precision loss possible";
                        f.recommendation = "Consider using double for intermediate calculations";
                        findings.push_back(f);
                    }
                }
            }
            
            // Check for very small denominators (division by near-zero)
            std::regex near_zero(R"(/\s*(0\.0*[1-9]|1e-[0-9]+))");
            if (std::regex_search(line, near_zero)) {
                NumericalFinding f;
                f.severity = "HIGH";
                f.category = "Numerical Stability";
                f.file = file;
                f.line = line_num;
                f.description = "Division by very small value - potential overflow";
                f.recommendation = "Add bounds check before division";
                findings.push_back(f);
            }
            
            // Check for catastrophic cancellation patterns
            if ((line.find("-") != std::string::npos || line.find("+") != std::string::npos) &&
                line.find("sqrt") != std::string::npos) {
                if (line.find("double") == std::string::npos) {
                    NumericalFinding f;
                    f.severity = "MEDIUM";
                    f.category = "Catastrophic Cancellation";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Subtraction near sqrt result - precision loss";
                    f.recommendation = "Use algebraic reformulation or double precision";
                    findings.push_back(f);
                }
            }
        }
    }
}

void check_gimbal_lock(const std::string& src_dir, std::vector<NumericalFinding>& findings) {
    std::vector<std::string> files = {
        "src/logic.cpp", "src/logic.h", "src/orientation_sensor.cpp"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for Euler angle conversions
            if (line.find("euler") != std::string::npos || line.find("Euler") != std::string::npos) {
                NumericalFinding f;
                f.severity = "MEDIUM";
                f.category = "Gimbal Lock";
                f.file = file;
                f.line = line_num;
                f.description = "Euler angle usage - potential gimbal lock at 90° pitch";
                f.recommendation = "Verify quaternion usage or document gimbal lock handling";
                findings.push_back(f);
            }
            
            // Check for rotation matrix construction
            if (line.find("rotation_matrix") != std::string::npos || 
                line.find("rotationMatrix") != std::string::npos) {
                NumericalFinding f;
                f.severity = "LOW";
                f.category = "Gimbal Lock";
                f.file = file;
                f.line = line_num;
                f.description = "Rotation matrix detected - verify gimbal lock prevention";
                f.recommendation = "Ensure proper axis ordering and singularity handling";
                findings.push_back(f);
            }
            
            // Check for pitch angle usage
            if (line.find("pitch") != std::string::npos) {
                NumericalFinding f;
                f.severity = "LOW";
                f.category = "Gimbal Lock";
                f.file = file;
                f.line = line_num;
                f.line = line_num;
                f.description = "Pitch angle detected - verify pitch ±90° handling";
                f.recommendation = "Check for gimbal lock at extreme pitch angles";
                findings.push_back(f);
            }
        }
    }
}

void check_unit_consistency(const std::string& src_dir, std::vector<NumericalFinding>& findings) {
    std::vector<std::string> files = {
        "src/logic.cpp", "src/logic.h", "src/config_loader.cpp"
    };
    
    // Track reported issues to avoid duplicates
    std::set<std::string> reported;
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for mixed unit operations
            std::vector<std::pair<std::string, std::string>> unit_pairs = {
                {"m", "cm"}, {"m", "mm"}, {"mm", "cm"},
                {"deg", "rad"}, {"rad", "deg"},
                {"ms", "s"}, {"s", "ms"}
            };
            
            for (const auto& [u1, u2] : unit_pairs) {
                if (line.find(u1) != std::string::npos && line.find(u2) != std::string::npos) {
                    NumericalFinding f;
                    f.severity = "MEDIUM";
                    f.category = "Unit Consistency";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Mixed units " + u1 + " and " + u2 + " in same expression";
                    f.recommendation = "Standardize to single unit system";
                    findings.push_back(f);
                }
            }
            
            // Check for hardcoded constants without units
            std::regex magic_number(R"(=\s*[0-9]+\.[0-9]+)");
            if (std::regex_search(line, magic_number)) {
                if (line.find("//") == std::string::npos && 
                    line.find("const float") == std::string::npos &&
                    line.find("#define") == std::string::npos) {
                    NumericalFinding f;
                    f.severity = "LOW";
                    f.category = "Unit Documentation";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Magic number without unit documentation";
                    f.recommendation = "Add comment with unit specification";
                    findings.push_back(f);
                }
            }
        }
    }
}

void check_vector_normalization(const std::string& src_dir, std::vector<NumericalFinding>& findings) {
    std::vector<std::string> files = {
        "src/logic.cpp", "src/logic.h", "src/geometry_verification.cpp"
    };
    
    for (const auto& file : files) {
        std::ifstream ifs(src_dir + "/" + file);
        if (!ifs.is_open()) continue;
        
        std::string line;
        int line_num = 0;
        
        while (std::getline(ifs, line)) {
            line_num++;
            
            // Check for magnitude calculation without zero check
            if (line.find("magnitude()") != std::string::npos ||
                line.find(".length()") != std::string::npos) {
                if (line.find("if") == std::string::npos && line.find("> 0") == std::string::npos) {
                    NumericalFinding f;
                    f.severity = "HIGH";
                    f.category = "Numerical Stability";
                    f.file = file;
                    f.line = line_num;
                    f.description = "Division by magnitude without zero check";
                    f.recommendation = "Add check for zero magnitude before division";
                    findings.push_back(f);
                }
            }
            
            // Check for normalize() without magnitude check
            if (line.find("normalize()") != std::string::npos ||
                line.find("normalized()") != std::string::npos) {
                NumericalFinding f;
                f.severity = "MEDIUM";
                f.category = "Numerical Stability";
                f.file = file;
                f.line = line_num;
                f.description = "Vector normalization - verify zero-vector handling";
                f.recommendation = "Check for zero vector before normalization";
                findings.push_back(f);
            }
            
            // Check for std::isfinite usage
            if (line.find("isfinite") == std::string::npos && 
                line.find("isinf") == std::string::npos &&
                (line.find("sqrt") != std::string::npos ||
                 line.find("log") != std::string::npos ||
                 line.find("division") != std::string::npos)) {
                NumericalFinding f;
                f.severity = "LOW";
                f.category = "NaN/Inf Handling";
                f.file = file;
                f.line = line_num;
                f.description = "Numerical operation without NaN/Inf validation";
                f.recommendation = "Consider adding std::isfinite checks";
                findings.push_back(f);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::string src_dir = "/home/pi/Aurore";
    if (argc > 1) src_dir = argv[1];
    
    std::cout << "========================================\n";
    std::cout << "Numerical Stability Analysis\n";
    std::cout << "========================================\n\n";
    
    std::vector<NumericalFinding> findings;
    
    std::cout << "Checking floating-point precision...\n";
    check_floating_point_precision(src_dir, findings);
    
    std::cout << "Checking gimbal lock risks...\n";
    check_gimbal_lock(src_dir, findings);
    
    std::cout << "Checking unit consistency...\n";
    check_unit_consistency(src_dir, findings);
    
    std::cout << "Checking vector normalization...\n";
    check_vector_normalization(src_dir, findings);
    
    // Print findings
    std::cout << "\n========================================\n";
    std::cout << "NUMERICAL FINDINGS\n";
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
    
    std::cout << "\nSTATUS: Numerical stability acceptable.\n";
    return 0;
}
