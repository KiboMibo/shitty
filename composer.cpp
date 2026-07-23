/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"

#include "cell_extra_store.h"

namespace stl {}
using namespace stl;

Composer::~Composer() noexcept {
    delete cellExtras;
}
