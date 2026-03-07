#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>
#include <iomanip>

namespace RetestValidation {

struct RemediationItem {
    int finding_id;
    std::string severity;
    std::string description;
    std::string file;
    std::string remediation;
    std::string status;
    std::string verified_by;
    std::string verification_date;
};

struct TestResult {
    std::string test_name;
    bool passed;
    double duration_ms;
    std::string output;
};

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

bool file_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

std::vector<std::string> find_testExecutables(const std::string& build_dir) {
    std::vector<std::string> tests;
    std::string src_dir = build_dir + "/src";
    
    DIR* d = opendir(src_dir.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.find("_test") != std::string::npos && 
                name.find("evidence") == std::string::npos &&
                name.find("retest") == std::string::npos &&
                name[name.length()-1] != '~') {
                tests.push_back(src_dir + "/" + name);
            }
        }
        closedir(d);
    }
    return tests;
}

std::vector<RemediationItem> load_remediation_items() {
    std::vector<RemediationItem> items;
    
    RemediationItem r1;
    r1.finding_id = 1;
    r1.severity = "CRITICAL";
    r1.description = "Priority inheritance for mutexes";
    r1.file = "src/lockfree_queue.h";
    r1.remediation = "Use pthread_mutexattr_setprotocol with PTHREAD_PRIO_INHERIT";
    r1.status = "PENDING";
    r1.verified_by = "";
    r1.verification_date = "";
    items.push_back(r1);
    
    RemediationItem r2;
    r2.finding_id = 2;
    r2.severity = "HIGH";
    r2.description = "Memory ordering for atomics";
    r2.file = "src/camera/camera_capture.cpp";
    r2.remediation = "Add memory_order_acquire/release semantics";
    r2.status = "PENDING";
    r2.verified_by = "";
    r2.verification_date = "";
    items.push_back(r2);
    
    RemediationItem r3;
    r3.finding_id = 3;
    r3.severity = "HIGH";
    r3.description = "memcpy size validation";
    r3.file = "src/application.cpp";
    r3.remediation = "Add size parameter validation before memcpy";
    r3.status = "PENDING";
    r3.verified_by = "";
    r3.verification_date = "";
    items.push_back(r3);
    
    RemediationItem r4;
    r4.finding_id = 5;
    r4.severity = "CRITICAL";
    r4.description = "Manual fire override protection";
    r4.file = "src/logic.cpp";
    r4.remediation = "Implement 2-step confirmation with timeout and audit logging";
    r4.status = "PENDING";
    r4.verified_by = "";
    r4.verification_date = "";
    items.push_back(r4);
    
    RemediationItem r5;
    r5.finding_id = 6;
    r5.severity = "HIGH";
    r5.description = "Target override timeout";
    r5.file = "src/logic.cpp";
    r5.remediation = "Add timeout protection to target override";
    r5.status = "PENDING";
    r5.verified_by = "";
    r5.verification_date = "";
    items.push_back(r5);
    
    RemediationItem r6;
    r6.finding_id = 7;
    r6.severity = "HIGH";
    r6.description = "Fire bypass verification";
    r6.file = "src/logic.cpp";
    r6.remediation = "Implement hardware switch verification for fire bypass";
    r6.status = "PENDING";
    r6.verified_by = "";
    r6.verification_date = "";
    items.push_back(r6);
    
    RemediationItem r7;
    r7.finding_id = 8;
    r7.severity = "HIGH";
    r7.description = "Target lock bypass verification";
    r7.file = "src/logic.cpp";
    r7.remediation = "Add hardware switch verification for target lock bypass";
    r7.status = "PENDING";
    r7.verified_by = "";
    r7.verification_date = "";
    items.push_back(r7);
    
    return items;
}

std::vector<TestResult> run_safety_tests(const std::string& build_dir) {
    std::vector<TestResult> results;
    
    std::vector<std::string> tests = find_testExecutables(build_dir);
    
    for (const auto& test : tests) {
        TestResult r;
        r.test_name = test.substr(test.find_last_of("/") + 1);
        r.passed = false;
        r.duration_ms = 0;
        
        if (file_exists(test)) {
            std::string cmd = test + " 2>&1";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buffer[4096];
                std::string output;
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    output += buffer;
                }
                pclose(pipe);
                
                r.output = output;
                r.passed = output.find("FAIL") == std::string::npos;
            }
        } else {
            r.output = "Test executable not found";
        }
        
        results.push_back(r);
    }
    
    return results;
}

void print_remediation_status(const std::vector<RemediationItem>& items) {
    std::cout << "\n=== REMEDIATION STATUS ===\n\n";
    
    int pending = 0, in_progress = 0, verified = 0;
    for (const auto& r : items) {
        if (r.status == "PENDING") pending++;
        else if (r.status == "IN_PROGRESS") in_progress++;
        else if (r.status == "VERIFIED") verified++;
    }
    
    std::cout << "  Total items: " << items.size() << "\n";
    std::cout << "  Pending: " << pending << "\n";
    std::cout << "  In Progress: " << in_progress << "\n";
    std::cout << "  Verified: " << verified << "\n\n";
    
    std::cout << "  By Severity:\n";
    int critical_pending = 0, high_pending = 0, medium_pending = 0, low_pending = 0;
    for (const auto& r : items) {
        if (r.status != "VERIFIED") {
            if (r.severity == "CRITICAL") critical_pending++;
            else if (r.severity == "HIGH") high_pending++;
            else if (r.severity == "MEDIUM") medium_pending++;
            else if (r.severity == "LOW") low_pending++;
        }
    }
    
    std::cout << "    CRITICAL pending: " << critical_pending << "\n";
    std::cout << "    HIGH pending: " << high_pending << "\n";
    std::cout << "    MEDIUM pending: " << medium_pending << "\n";
    std::cout << "    LOW pending: " << low_pending << "\n";
    
    bool pass = critical_pending == 0;
    std::cout << "\n  [CRITICAL_REMEDIATION] " << (pass ? "ALL CRITICAL ITEMS REMEDIATED" : "CRITICAL ITEMS REMAIN") << "\n";
}

void print_test_results(const std::vector<TestResult>& results) {
    std::cout << "\n=== SAFETY TEST RESULTS ===\n\n";
    
    int passed = 0, failed = 0;
    for (const auto& r : results) {
        std::cout << "  " << r.test_name << ": " << (r.passed ? "PASS" : "FAIL") << "\n";
        if (r.passed) passed++; else failed++;
    }
    
    std::cout << "\n  Total: " << results.size() << "\n";
    std::cout << "  Passed: " << passed << "\n";
    std::cout << "  Failed: " << failed << "\n";
    
    double pass_rate = results.empty() ? 0 : (static_cast<double>(passed) / results.size() * 100);
    std::cout << "  Pass rate: " << std::fixed << std::setprecision(1) << pass_rate << "%\n";
    
    bool pass = pass_rate >= 90.0;
    std::cout << "  [TEST_COVERAGE] " << (pass ? "PASS" : "FAIL") << "\n";
}

void generate_validation_report(const std::vector<RemediationItem>& items,
                                const std::vector<TestResult>& results,
                                const std::string& output_path) {
    std::ofstream report(output_path);
    if (!report.is_open()) {
        std::cerr << "Failed to create validation report\n";
        return;
    }
    
    report << "# Aurore Mk VI Retest and Validation Report\n";
    report << "---\n\n";
    
    time_t now = time(nullptr);
    report << "**Generated:** " << ctime(&now);
    report << "**Track ID:** safety_audit_20260202\n\n";
    
    report << "## Remediation Summary\n\n";
    report << "| ID | Severity | Status | File |\n";
    report << "|----|----------|--------|------|\n";
    for (const auto& r : items) {
        report << "| " << r.finding_id << " | " << r.severity 
               << " | " << r.status << " | " << r.file << " |\n";
    }
    report << "\n";
    
    report << "## Test Results\n\n";
    report << "| Test | Status |\n";
    report << "|------|--------|\n";
    for (const auto& r : results) {
        report << "| " << r.test_name << " | " << (r.passed ? "PASS" : "FAIL") << " |\n";
    }
    report << "\n";
    
    int pending = 0, verified = 0;
    for (const auto& r : items) {
        if (r.status == "VERIFIED") verified++;
        else pending++;
    }
    
    report << "## Validation Status\n\n";
    report << "- Pending remediations: " << pending << "\n";
    report << "- Verified remediations: " << verified << "\n";
    report << "- Test pass rate: " << (results.empty() ? 0 : 
              (100.0 * std::count_if(results.begin(), results.end(), 
                  [](const TestResult& r){ return r.passed; }) / results.size())) << "%\n\n";
    
    report << "---\n";
    report << "*Validation completed by Aurore Mk VI Safety Audit Framework*\n";
    
    report.close();
    std::cout << "  Validation report: " << output_path << "\n";
}

void print_summary() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║        RETEST AND VALIDATION - PHASE 12 SUMMARY                   ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Remediation:        Track status of all findings                  ║\n";
    std::cout << "║ Test Validation:    Run all safety tests to verify fixes         ║\n";
    std::cout << "║ Regression:         Ensure no new issues introduced              ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Status: IN PROGRESS                                               ║\n";
    std::cout << "║ Next: Phase 13 (Sign-off and Closure)                            ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════════╝\n";
}

}

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       AURORE MK VI - RETEST AND VALIDATION TOOL                   ║\n";
    std::cout << "║            Phase 12: Retest and Validation                        ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════════╝\n\n";
    
    std::string build_dir = "/home/pi/Aurore/build";
    std::string output_dir = "/home/pi/Aurore/evidence";
    
    if (argc > 1) build_dir = argv[1];
    if (argc > 2) output_dir = argv[2];
    
    auto remediations = RetestValidation::load_remediation_items();
    std::cout << "  Loaded " << remediations.size() << " remediation items\n";
    
    RetestValidation::print_remediation_status(remediations);
    
    auto test_results = RetestValidation::run_safety_tests(build_dir);
    std::cout << "\n  Ran " << test_results.size() << " safety tests\n";
    RetestValidation::print_test_results(test_results);
    
    system(("mkdir -p " + output_dir).c_str());
    RetestValidation::generate_validation_report(remediations, test_results, 
                                                  output_dir + "/validation_report.md");
    
    RetestValidation::print_summary();
    
    std::cout << "\n  Validation complete.\n\n";
    
    return 0;
}
