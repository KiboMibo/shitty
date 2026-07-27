/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "color_spec.h"

#include <algorithm>
#include <cmath>

namespace {
    using stl::StringView;

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

    bool prefixEqual(StringView lhs, StringView rhs) {
        if (lhs.length() != rhs.length()) {
            return false;
        }
        for (size_t i = 0; i < lhs.length(); ++i) {
            u8 a = lhs[i];
            u8 b = rhs[i];
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

    bool parseNumber(StringView text, double& value) {
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
        while (index < text.length() && text[index] >= '0' && text[index] <= '9') {
            mantissa = mantissa * 10.0 + text[index++] - '0';
            digits = true;
        }
        if (index < text.length() && text[index] == '.') {
            ++index;
            double place = 0.1;
            while (index < text.length() && text[index] >= '0' && text[index] <= '9') {
                mantissa += (text[index++] - '0') * place;
                place *= 0.1;
                digits = true;
            }
        }
        int exponent = 0;
        int exponentSign = 1;
        if (index < text.length() && (text[index] == 'e' || text[index] == 'E')) {
            ++index;
            if (index < text.length() && (text[index] == '+' || text[index] == '-')) {
                exponentSign = text[index++] == '-' ? -1 : 1;
            }
            bool exponentDigits = false;
            while (index < text.length() && text[index] >= '0' && text[index] <= '9') {
                exponent = std::min(10000, exponent * 10 + text[index++] - '0');
                exponentDigits = true;
            }
            if (!exponentDigits) {
                return false;
            }
        }
        value = sign * mantissa * std::pow(10.0, exponentSign * exponent);
        return digits && index == text.length() && std::isfinite(value);
    }

    bool parseTriple(StringView spec, StringView prefix, Triple& value) {
        StringView name;
        StringView components;
        if (!spec.split(':', name, components) || !prefixEqual(name, prefix)) {
            return false;
        }
        StringView x;
        StringView tail;
        StringView y;
        StringView z;
        if (!components.split('/', x, tail) || !tail.split('/', y, z) || z.memChr('/') != nullptr) {
            return false;
        }
        return parseNumber(x, value.x) && parseNumber(y, value.y) && parseNumber(z, value.z);
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

    bool convertedColor(StringView spec, Color& color) {
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

bool parseXColor(stl::StringView spec, Color& color) {
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
    const auto component = [&](stl::StringView value, u8& out) {
        if (value.empty() || value.length() > 4) {
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
        const unsigned maximum = (1u << (4 * value.length())) - 1;
        out = (u8)((parsed * 255 + maximum / 2) / maximum);
        return true;
    };
    const auto hashComponent = [&](stl::StringView value, u8& out) {
        if (value.empty() || value.length() > 4) {
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
        if (value.length() < 2) {
            parsed <<= 4;
        } else if (value.length() > 2) {
            parsed >>= 4 * (value.length() - 2);
        }
        out = (u8)parsed;
        return true;
    };

    if (spec.length() >= 4 && spec.length() <= 13 && spec[0] == '#' && (spec.length() - 1) % 3 == 0) {
        const size_t width = (spec.length() - 1) / 3;
        const u8* const components = spec.data() + 1;
        return hashComponent(stl::StringView(components, width), color.red) && hashComponent(stl::StringView(components + width, width), color.green) && hashComponent(stl::StringView(components + 2 * width, width), color.blue);
    }
    if (spec.length() >= 4 && prefixEqual(spec.prefix(4), "rgb:")) {
        StringView red;
        StringView tail;
        StringView green;
        StringView blue;
        const StringView components(spec.data() + 4, spec.length() - 4);
        return components.split('/', red, tail) && tail.split('/', green, blue) && blue.memChr('/') == nullptr && component(red, color.red) && component(green, color.green) && component(blue, color.blue);
    }
    return convertedColor(spec, color);
}
