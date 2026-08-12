/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "brand.h"
#include "ui_imgui.h"

#include "color.h"
#include "imtty_icon_data.h"

using namespace stl;

namespace {
    struct ImttyBrand final: public Brand {
        StringView displayName() const override;
        StringView executableName() const override;
        StringView identifier() const override;
        StringView fontSizeEnvironment() const override;
        StringView versionEnvironment() const override;
        StringView iconData() const override;
        Color accentColor() const override;
        double accentTint() const override;
        Ui* createUi(ObjPool& owner, Composer& composer) const override;
    };

    static Brand* createBrand();
}

StringView ImttyBrand::displayName() const {
    return StringView(u8"Imtty");
}

StringView ImttyBrand::executableName() const {
    return StringView(u8"it");
}

StringView ImttyBrand::identifier() const {
    return StringView(u8"imtty");
}

StringView ImttyBrand::fontSizeEnvironment() const {
    return StringView(u8"IMTTY_FONT_SIZE");
}

StringView ImttyBrand::versionEnvironment() const {
    return StringView(u8"IMTTY_VERSION");
}

StringView ImttyBrand::iconData() const {
    return StringView((const u8*)(imttyIcon.data), imttyIcon.size);
}

Color ImttyBrand::accentColor() const {
    // The ImGui-blue of the logo glaze.
    return {0x42, 0x96, 0xfa};
}

double ImttyBrand::accentTint() const {
    return 25.0;
}

Ui* ImttyBrand::createUi(ObjPool& owner, Composer& composer) const {
    return createImguiUi(owner, composer);
}

namespace {
    static Brand* createBrand() {
        static ImttyBrand brand;
        return &brand;
    }
}

int main(int argc, char* argv[]) {
    return runMain(*createBrand(), argc, argv);
}
