/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "color.h"

#include "hex.h"

#include <std/ios/out_zc.h>
#include <std/str/view.h>

using namespace stl;

template <>
void stl::output<ZeroCopyOutput, ::Color>(ZeroCopyOutput& output, ::Color color) {
    output << StringView(u8"rgb:") << Hex{(u64)(color.red) * 0x101, 4} << StringView(u8"/") << Hex{(u64)(color.green) * 0x101, 4} << StringView(u8"/") << Hex{(u64)(color.blue) * 0x101, 4};
}
