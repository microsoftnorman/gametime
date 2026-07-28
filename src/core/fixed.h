#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eh {

using fx = int32_t;
constexpr int FX_SHIFT = 12;
constexpr fx FX_ONE = 4096;

constexpr fx fx_from_int(int value) {
    return static_cast<fx>(static_cast<int64_t>(value) * FX_ONE);
}

constexpr fx fx_from_float(float value) {
    return static_cast<fx>(value * static_cast<float>(FX_ONE));
}

constexpr float fx_to_float(fx value) {
    return static_cast<float>(value) / static_cast<float>(FX_ONE);
}

constexpr fx fx_mul(fx lhs, fx rhs) {
    return static_cast<fx>((static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs)) >> FX_SHIFT);
}

constexpr fx fx_div(fx lhs, fx rhs) {
    return static_cast<fx>((static_cast<int64_t>(lhs) * FX_ONE) / rhs);
}

inline fx fx_abs(fx value) { return value < 0 ? -value : value; }

using angle_t = uint16_t;

namespace detail {

inline const std::array<fx, 1024> &sin_table() {
    static const std::array<fx, 1024> table = [] {
        std::array<fx, 1024> values{};
        constexpr double TAU = 6.283185307179586476925286766559;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double radians =
                TAU * static_cast<double>(i) / static_cast<double>(values.size());
            values[i] = static_cast<fx>(std::lround(std::sin(radians) * FX_ONE));
        }

        values[0] = 0;
        values[256] = FX_ONE;
        values[512] = 0;
        values[768] = -FX_ONE;
        return values;
    }();
    return table;
}

} // namespace detail

inline fx fx_sin(angle_t angle) {
    return detail::sin_table()[static_cast<std::size_t>(angle) >> 6];
}

inline fx fx_cos(angle_t angle) {
    return fx_sin(static_cast<angle_t>(angle + static_cast<angle_t>(16384)));
}

inline angle_t angle_from_deg(double degrees) {
    double wrapped = std::fmod(degrees, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    const auto units = static_cast<uint32_t>(std::lround(wrapped * 65536.0 / 360.0));
    return static_cast<angle_t>(units);
}

} // namespace eh
