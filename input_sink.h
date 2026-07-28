/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <plt/input.h>

#include <std/lib/node.h>

using InputKey = plt::InputKey;
using InputAction = plt::InputAction;
using KeyInput = plt::KeyInput;
using TextInput = plt::TextInput;
using PointerButton = plt::PointerButton;
using PointerMotionInput = plt::PointerMotionInput;
using PointerButtonInput = plt::PointerButtonInput;
using ScrollInput = plt::ScrollInput;
using plt::InputAlt;
using plt::InputAltGraph;
using plt::InputCapsLock;
using plt::InputControl;
using plt::InputNumLock;
using plt::InputShift;
using plt::InputSuper;

struct InputSink: stl::IntrusiveNode {
    virtual bool key(const KeyInput& input) = 0;
    virtual bool text(const TextInput& input) = 0;
    virtual bool pointerMotion(const PointerMotionInput& input) = 0;
    virtual bool pointerButton(const PointerButtonInput& input) = 0;
    virtual bool scroll(const ScrollInput& input) = 0;
    virtual void focus(bool focused) = 0;
    virtual void pointerPresence(bool present) = 0;
    virtual void flush() = 0;

    ~InputSink() noexcept;
};
