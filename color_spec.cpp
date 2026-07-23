/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "color_spec.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace {
    struct Triple {
        double x;
        double y;
        double z;
    };

    constexpr double whiteX = 0.95047;
    constexpr double whiteY = 1.0;
    constexpr double whiteZ = 1.08883;
    constexpr double chromaScale = 7.50725;
    constexpr double bestRedU = 0.7127;
    constexpr double bestRedV = 0.4931;
    constexpr double pi = 3.14159265358979323846;

    bool prefixEqual(std::string_view lhs, std::string_view rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }
        for (size_t i = 0; i < lhs.size(); ++i) {
            unsigned char a = (unsigned char)lhs[i];
            unsigned char b = (unsigned char)rhs[i];
            if (a >= 'A' && a <= 'Z') {
                a = (unsigned char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (unsigned char)(b - 'A' + 'a');
            }
            if (a != b) {
                return false;
            }
        }
        return true;
    }

    bool parseNumber(std::string_view text, double& value) {
        if (text.empty()) {
            return false;
        }
        size_t index = 0;
        double sign = 1.0;
        if (text[index] == '+' || text[index] == '-') {
            sign = text[index++] == '-' ? -1.0 : 1.0;
        }
        double mantissa = 0.0;
        bool digits = false;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            mantissa = mantissa * 10.0 + text[index++] - '0';
            digits = true;
        }
        if (index < text.size() && text[index] == '.') {
            ++index;
            double place = 0.1;
            while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
                mantissa += (text[index++] - '0') * place;
                place *= 0.1;
                digits = true;
            }
        }
        int exponent = 0;
        int exponentSign = 1;
        if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
            ++index;
            if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
                exponentSign = text[index++] == '-' ? -1 : 1;
            }
            bool exponentDigits = false;
            while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
                exponent = std::min(10000, exponent * 10 + text[index++] - '0');
                exponentDigits = true;
            }
            if (!exponentDigits) {
                return false;
            }
        }
        value = sign * mantissa * std::pow(10.0, exponentSign * exponent);
        return digits && index == text.size() && std::isfinite(value);
    }

    bool parseTriple(const std::string& spec, std::string_view prefix, Triple& value) {
        const size_t colon = spec.find(':');
        if (colon == std::string::npos || !prefixEqual(std::string_view(spec).substr(0, colon), prefix)) {
            return false;
        }
        const size_t first = spec.find('/', colon + 1);
        const size_t second = first == std::string::npos ? std::string::npos : spec.find('/', first + 1);
        if (first == std::string::npos || second == std::string::npos || spec.find('/', second + 1) != std::string::npos) {
            return false;
        }
        return parseNumber(std::string_view(spec).substr(colon + 1, first - colon - 1), value.x) && parseNumber(std::string_view(spec).substr(first + 1, second - first - 1), value.y) && parseNumber(std::string_view(spec).substr(second + 1), value.z);
    }

    Triple whiteUvY() {
        const double divisor = whiteX + 15.0 * whiteY + 3.0 * whiteZ;
        return {4.0 * whiteX / divisor, 9.0 * whiteY / divisor, 1.0};
    }

    Triple uvYToXyz(Triple value) {
        double divisor = 6.0 * value.x - 16.0 * value.y + 12.0;
        if (divisor == 0.0) {
            value = whiteUvY();
            divisor = 6.0 * value.x - 16.0 * value.y + 12.0;
        }
        const double x = 9.0 * value.x / divisor;
        const double y = 4.0 * value.y / divisor;
        const double z = 1.0 - x - y;
        if (y == 0.0) {
            return {x, value.z, z};
        }
        return {x * value.z / y, value.z, z * value.z / y};
    }

    Triple xyzToUvY(Triple value) {
        const double divisor = value.x + 15.0 * value.y + 3.0 * value.z;
        if (divisor == 0.0) {
            Triple result = whiteUvY();
            result.z = value.y;
            return result;
        }
        return {4.0 * value.x / divisor, 9.0 * value.y / divisor, value.y};
    }

    double valueToY(double value) {
        if (value < 7.99953624) {
            return value / 903.29;
        }
        const double scaled = (value + 16.0) / 116.0;
        return scaled * scaled * scaled;
    }

    double yToValue(double value) {
        return value < 0.008856 ? value * 903.29 : std::cbrt(value) * 116.0 - 16.0;
    }

    double hueOffset() {
        const Triple white = whiteUvY();
        return std::atan((bestRedV - white.y) / (bestRedU - white.x)) * 180.0 / pi;
    }

    Triple tekHvcToXyz(Triple value) {
        if (value.y == 0.0 || value.y == 100.0) {
            Triple neutral = whiteUvY();
            neutral.z = value.y == 0.0 ? 0.0 : 1.0;
            return uvYToXyz(neutral);
        }
        const double hue = (value.x + hueOffset()) * pi / 180.0;
        Triple uvY = whiteUvY();
        uvY.x += std::cos(hue) * value.z / (value.y * chromaScale);
        uvY.y += std::sin(hue) * value.z / (value.y * chromaScale);
        uvY.z = valueToY(value.y);
        return uvYToXyz(uvY);
    }

    Triple xyzToTekHvc(Triple value) {
        const Triple uvY = xyzToUvY(value);
        const Triple white = whiteUvY();
        const double u = uvY.x - white.x;
        const double v = uvY.y - white.y;
        const double lightness = yToValue(value.y);
        double hue = std::atan2(v, u) * 180.0 / pi - hueOffset();
        while (hue < 0.0) {
            hue += 360.0;
        }
        while (hue >= 360.0) {
            hue -= 360.0;
        }
        return {
            hue,
            lightness,
            lightness * chromaScale * std::sqrt(u * u + v * v),
        };
    }

    Triple xyzToLinearRgb(Triple value) {
        return {
            3.2404542 * value.x - 1.5371385 * value.y - 0.4985314 * value.z,
            -0.9692660 * value.x + 1.8760108 * value.y + 0.0415560 * value.z,
            0.0556434 * value.x - 0.2040259 * value.y + 1.0572252 * value.z,
        };
    }

    bool inGamut(Triple value) {
        constexpr double epsilon = 0.000001;
        return value.x >= -epsilon && value.x <= 1.0 + epsilon && value.y >= -epsilon && value.y <= 1.0 + epsilon && value.z >= -epsilon && value.z <= 1.0 + epsilon;
    }

    Triple gamutMap(Triple xyz) {
        Triple rgb = xyzToLinearRgb(xyz);
        if (inGamut(rgb)) {
            return rgb;
        }
        Triple hvc = xyzToTekHvc(xyz);
        double lower = 0.0;
        double upper = hvc.z;
        for (unsigned i = 0; i < 32; ++i) {
            hvc.z = (lower + upper) * 0.5;
            const Triple candidate = xyzToLinearRgb(tekHvcToXyz(hvc));
            if (inGamut(candidate)) {
                lower = hvc.z;
                rgb = candidate;
            } else {
                upper = hvc.z;
            }
        }
        return rgb;
    }

    u8 encodeSrgb(double value) {
        value = std::clamp(value, 0.0, 1.0);
        value = value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
        return (u8)std::lround(value * 255.0);
    }

    bool convertedColor(const std::string& spec, Color& color) {
        Triple value;
        Triple xyz;
        if (parseTriple(spec, "rgbi", value)) {
            if (value.x < 0.0 || value.x > 1.0 || value.y < 0.0 || value.y > 1.0 || value.z < 0.0 || value.z > 1.0) {
                return false;
            }
            color = {encodeSrgb(value.x), encodeSrgb(value.y), encodeSrgb(value.z)};
            return true;
        }
        if (parseTriple(spec, "CIEXYZ", value)) {
            if (value.y < 0.0 || value.y > 1.0) {
                return false;
            }
            xyz = value;
        } else if (parseTriple(spec, "CIEuvY", value)) {
            if (value.z < 0.0 || value.z > 1.0) {
                return false;
            }
            xyz = uvYToXyz(value);
        } else if (parseTriple(spec, "CIExyY", value)) {
            if (value.x < 0.0 || value.x > 1.0 || value.y < 0.0 || value.y > 1.0 || value.z < 0.0 || value.z > 1.0) {
                return false;
            }
            if (value.y == 0.0) {
                xyz = {};
            } else {
                xyz = {
                    value.x * value.z / value.y,
                    value.z,
                    (1.0 - value.x - value.y) * value.z / value.y,
                };
            }
        } else if (parseTriple(spec, "CIELab", value)) {
            if (value.x < 0.0 || value.x > 100.0) {
                return false;
            }
            double lightness = (value.x + 16.0) / 116.0;
            xyz.y = lightness * lightness * lightness;
            if (xyz.y < 0.008856) {
                lightness = value.x / 9.03292;
                xyz = {
                    whiteX * (value.y / 3893.5 + lightness),
                    lightness,
                    whiteZ * (lightness - value.z / 1557.4),
                };
            } else {
                const double x = lightness + value.y / 5.0;
                const double z = lightness - value.z / 2.0;
                xyz.x = whiteX * x * x * x;
                xyz.z = whiteZ * z * z * z;
            }
        } else if (parseTriple(spec, "CIELuv", value)) {
            if (value.x < 0.0 || value.x > 100.0) {
                return false;
            }
            Triple uvY = whiteUvY();
            uvY.z = valueToY(value.x);
            if (value.x != 0.0) {
                const double scale = 13.0 * (value.x / 100.0);
                uvY.x += value.y / scale;
                uvY.y += value.z / scale;
            }
            xyz = uvYToXyz(uvY);
        } else if (parseTriple(spec, "TekHVC", value)) {
            if (value.y < 0.0 || value.y > 100.0 || value.z < 0.0) {
                return false;
            }
            value.x = std::fmod(value.x, 360.0);
            if (value.x < 0.0) {
                value.x += 360.0;
            }
            xyz = tekHvcToXyz(value);
        } else {
            return false;
        }

        if (!std::isfinite(xyz.x) || !std::isfinite(xyz.y) || !std::isfinite(xyz.z) || xyz.y < 0.0 || xyz.y > 1.0) {
            return false;
        }
        const Triple rgb = gamutMap(xyz);
        if (!std::isfinite(rgb.x) || !std::isfinite(rgb.y) || !std::isfinite(rgb.z)) {
            return false;
        }
        color = {encodeSrgb(rgb.x), encodeSrgb(rgb.y), encodeSrgb(rgb.z)};
        return true;
    }
}

bool parseXColor(const std::string& spec, Color& color) {
    const auto hexDigit = [](unsigned char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };
    const auto component = [&](const std::string& value, u8& out) {
        if (value.empty() || value.size() > 4) {
            return false;
        }
        unsigned parsed = 0;
        for (unsigned char ch : value) {
            const int digit = hexDigit(ch);
            if (digit < 0) {
                return false;
            }
            parsed = parsed * 16 + (unsigned)digit;
        }
        const unsigned maximum = (1u << (4 * value.size())) - 1;
        out = (u8)((parsed * 255 + maximum / 2) / maximum);
        return true;
    };
    const auto hashComponent = [&](const std::string& value, u8& out) {
        if (value.empty() || value.size() > 4) {
            return false;
        }
        unsigned parsed = 0;
        for (unsigned char ch : value) {
            const int digit = hexDigit(ch);
            if (digit < 0) {
                return false;
            }
            parsed = parsed * 16 + (unsigned)digit;
        }
        if (value.size() < 2) {
            parsed <<= 4;
        } else if (value.size() > 2) {
            parsed >>= 4 * (value.size() - 2);
        }
        out = (u8)parsed;
        return true;
    };

    if (spec.size() >= 4 && spec.size() <= 13 && spec[0] == '#' && (spec.size() - 1) % 3 == 0) {
        const size_t width = (spec.size() - 1) / 3;
        return hashComponent(spec.substr(1, width), color.red) && hashComponent(spec.substr(1 + width, width), color.green) && hashComponent(spec.substr(1 + 2 * width, width), color.blue);
    }
    if (spec.size() >= 4 && prefixEqual(std::string_view(spec).substr(0, 4), "rgb:")) {
        const size_t first = spec.find('/', 4);
        const size_t second = first == std::string::npos ? std::string::npos : spec.find('/', first + 1);
        return first != std::string::npos && second != std::string::npos && spec.find('/', second + 1) == std::string::npos && component(spec.substr(4, first - 4), color.red) && component(spec.substr(first + 1, second - first - 1), color.green) && component(spec.substr(second + 1), color.blue);
    }
    return convertedColor(spec, color);
}
