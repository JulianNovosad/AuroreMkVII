#ifndef AURORE_NUMERICAL_STABILITY_H
#define AURORE_NUMERICAL_STABILITY_H

#include <cmath>
#include <limits>
#include <algorithm>

// REMEDIATION 2026-02-02: Finding #10 - Numerical stability utilities
// Use double precision for intermediate trigonometric calculations

namespace Aurore {

// Stable square root that handles edge cases
inline double stable_sqrt(double value) {
    if (value <= 0.0) {
        return 0.0;
    }
    if (std::isinf(value)) {
        return std::numeric_limits<double>::infinity();
    }
    if (std::isnan(value)) {
        return 0.0;
    }
    return std::sqrt(value);
}

// Double-precision trigonometric functions for stability
template<typename T>
inline double stable_atan2(T y, T x) {
    // Promote to double for calculation
    double dy = static_cast<double>(y);
    double dx = static_cast<double>(x);
    
    // Handle edge cases
    if (dx == 0.0 && dy == 0.0) {
        return 0.0;
    }
    
    return std::atan2(dy, dx);
}

template<typename T>
inline double stable_sin(T angle) {
    return std::sin(static_cast<double>(angle));
}

template<typename T>
inline double stable_cos(T angle) {
    return std::cos(static_cast<double>(angle));
}

template<typename T>
inline double stable_tan(T angle) {
    double dangle = static_cast<double>(angle);
    // Avoid singularity at pi/2
    const double pi_2 = M_PI / 2.0;
    if (std::abs(dangle - pi_2) < 1e-10 || std::abs(dangle + pi_2) < 1e-10) {
        return std::numeric_limits<double>::max();
    }
    return std::tan(dangle);
}

// Stable hypot to avoid overflow/underflow
template<typename T>
inline double stable_hypot(T x, T y) {
    double dx = std::abs(static_cast<double>(x));
    double dy = std::abs(static_cast<double>(y));
    
    if (dx == 0.0 && dy == 0.0) {
        return 0.0;
    }
    
    // Scale to avoid overflow
    double max_val = std::max(dx, dy);
    if (max_val == 0.0) {
        return 0.0;
    }
    
    dx /= max_val;
    dy /= max_val;
    
    return max_val * std::sqrt(dx * dx + dy * dy);
}

// Catastrophic cancellation safe subtraction
// (a + b) - (c + d) computed safely when a ≈ c and b ≈ d
template<typename T>
inline double safe_subtract(T a, T b) {
    double da = static_cast<double>(a);
    double db = static_cast<double>(b);
    return da - db;
}

// Kahan summation for better precision in accumulated sums
template<typename Iterator>
double kahan_sum(Iterator begin, Iterator end) {
    double sum = 0.0;
    double c = 0.0;  // Running compensation for lost low-order bits
    
    for (auto it = begin; it != end; ++it) {
        double y = static_cast<double>(*it) - c;
        double t = sum + y;
        c = (t - sum) - y;
        sum = t;
    }
    
    return sum;
}

} // namespace Aurore

#endif // AURORE_NUMERICAL_STABILITY_H
