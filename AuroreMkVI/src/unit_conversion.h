#ifndef AURORE_UNIT_CONVERSION_H
#define AURORE_UNIT_CONVERSION_H

#include <cmath>

namespace Aurore {
namespace units {

constexpr double KELVIN_OFFSET = 273.15;

constexpr double MM_TO_M = 0.001;
constexpr double CM_TO_M = 0.01;
constexpr double KM_TO_M = 1000.0;

constexpr double IN_TO_M = 0.0254;
constexpr double FT_TO_M = 0.3048;
constexpr double YD_TO_M = 0.9144;

constexpr double MS_TO_KMH = 3.6;
constexpr double MS_TO_MPH = 2.23694;
constexpr double MS_TO_FPS = 3.28084;

constexpr double G_TO_KG = 0.001;
constexpr double LB_TO_KG = 0.453592;

constexpr double PA_TO_KPA = 0.001;
constexpr double PA_TO_HPA = 0.01;
constexpr double PA_TO_ATM = 9.86923e-6;

constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / M_PI;

inline double celsius_to_kelvin(float celsius) {
    return static_cast<double>(celsius) + KELVIN_OFFSET;
}

inline double mm_to_m(float mm) {
    return static_cast<double>(mm) * MM_TO_M;
}

inline double cm_to_m(float cm) {
    return static_cast<double>(cm) * CM_TO_M;
}

inline double km_to_m(float km) {
    return static_cast<double>(km) * KM_TO_M;
}

inline double in_to_m(float inches) {
    return static_cast<double>(inches) * IN_TO_M;
}

inline double ft_to_m(float feet) {
    return static_cast<double>(feet) * FT_TO_M;
}

inline double g_to_kg(float grams) {
    return static_cast<double>(grams) * G_TO_KG;
}

inline double lb_to_kg(float pounds) {
    return static_cast<double>(pounds) * LB_TO_KG;
}

inline double deg_to_rad(float degrees) {
    return static_cast<double>(degrees) * DEG_TO_RAD;
}

inline double rad_to_deg(float radians) {
    return static_cast<double>(radians) * RAD_TO_DEG;
}

} // namespace units
} // namespace Aurore

#endif // AURORE_UNIT_CONVERSION_H
