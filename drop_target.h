/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace stl {
    class ObjPool;
}

namespace plt {
    struct DropTarget;
}

struct Composer;

// The window's drag-and-drop target: accepts text and uri-list drops with
// the copy action and streams the payload into the vterm paste path on the
// platform's transfer fiber.
plt::DropTarget* createDropTarget(stl::ObjPool& owner, Composer& composer);
