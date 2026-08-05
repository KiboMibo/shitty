/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "brand.h"

#include "color.h"
#include "pretty_icon_data.h"

using namespace stl;

namespace {
    struct PrettyBrand final: public Brand {
        StringView displayName() const override;
        StringView executableName() const override;
        StringView identifier() const override;
        StringView fontSizeEnvironment() const override;
        StringView versionEnvironment() const override;
        StringView iconData() const override;
        Color accentColor() const override;
        double accentTint() const override;
    };

    static Brand* createBrand();
}

StringView PrettyBrand::displayName() const {
    return StringView(u8"Pretty");
}

StringView PrettyBrand::executableName() const {
    return StringView(u8"pt");
}

StringView PrettyBrand::identifier() const {
    return StringView(u8"pretty");
}

StringView PrettyBrand::fontSizeEnvironment() const {
    return StringView(u8"PRETTY_FONT_SIZE");
}

StringView PrettyBrand::versionEnvironment() const {
    return StringView(u8"PRETTY_VERSION");
}

StringView PrettyBrand::iconData() const {
    return StringView((const u8*)(prettyIcon.data), prettyIcon.size);
}

Color PrettyBrand::accentColor() const {
    // The magenta of the logo glaze.
    return {0xd9, 0x46, 0xef};
}

double PrettyBrand::accentTint() const {
    return 25.0;
}

namespace {
    static Brand* createBrand() {
        static PrettyBrand brand;
        return &brand;
    }
}

int main(int argc, char* argv[]) {
    return runMain(*createBrand(), argc, argv);
}
