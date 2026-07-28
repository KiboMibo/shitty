/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "window.h"

#include "clipboard.h"
#include "composer.h"
#include "desktop_actions.h"
#include "input_sink.h"
#include "listener.h"
#include "options.h"
#include "small_obj_allocator.h"
#include "test_mode.h"
#include "vk_renderer.h"

#include <plt/platform.h>
#include <plt/window.h>

#include <std/alg/minmax.h>
#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/lib/list.h>
#include <std/mem/obj_pool.h>
#include <std/sys/throw.h>

#include <cmath>
#include <cerrno>
#include <new>
#include <spawn.h>

using namespace stl;

extern char** environ;

namespace {
    constexpr int testRelease = 0;
    constexpr int testPress = 1;
    constexpr int testRepeat = 2;
    constexpr int testModShift = 0x0001;
    constexpr int testModControl = 0x0002;
    constexpr int testModAlt = 0x0004;
    constexpr int testModSuper = 0x0008;
    constexpr int testModCapsLock = 0x0010;
    constexpr int testModNumLock = 0x0020;

    struct TestInputTranslator {
        static InputKey key(int key);
        static InputAction action(int action, bool& valid);
        static u16 modifiers(int modifiers, bool rightAlt);
        static u32 baseCodepoint(int key);
        static void sendKey(Composer& composer, int key, int action, int modifiers);
        static void sendText(Composer& composer, u32 codepoint, int modifiers);
        static void contentScale(Composer& composer, float xScale, float yScale);
    };

    struct NativeWindowImpl final: public Window, public Clipboard, public DesktopActions, public plt::WindowEvents, public plt::InputSink {
        explicit NativeWindowImpl(Composer& composer);
        ~NativeWindowImpl();

        void initialize() override;
        void show() override;
        void activate() override;
        void requestClose() override;
        bool requestFrame() override;
        void cancelFrame() override;

        void setTitle(StringView title) override;
        void requestAttention() override;
        void requestRedraw() override;
        void restore() override;
        void iconify() override;
        void move(i32 x, i32 y) override;
        void focus() override;
        void setMaximized(bool maximized) override;
        void setFullscreen(bool fullscreen) override;
        void resizePixels(u32 width, u32 height) override;
        WindowInfo info() override;
        Clipboard* clipboard() override;
        DesktopActions* desktopActions() override;

        Renderer* createRender() override;
        TestModeInput* testApi() override;

        void readPrimary(Output* output) override;
        void readClipboard(Output* output) override;
        void writePrimary(StringView content) override;
        void writeClipboard(StringView content) override;

        void openUri(StringView uri) override;
        void pointerIcon(PointerIcon icon) override;

        void close() override;
        void resized(const plt::WindowInfo& info) override;
        void redraw() override;
        void frame() override;

        void key(const plt::KeyInput& input) override;
        void text(const plt::TextInput& input) override;
        void pointerMotion(const plt::PointerMotionInput& input) override;
        void pointerButton(const plt::PointerButtonInput& input) override;
        void scroll(const plt::ScrollInput& input) override;
        void focus(bool focused) override;
        void pointerPresence(bool present) override;
        void flush() override;

        void publish(stl::IntrusiveList& listeners, void* argument = nullptr);
        void publishWindow(const ::WindowEvents& events);
        void cancelClipboardReads();

        Composer& composer;
        plt::Window* native = nullptr;
        Buffer uriBuffer;
        IntrusiveList clipboardReads;
        bool initialized = false;
        bool callbacksActive = false;
    };

    struct ClipboardOutput final: public plt::ClipboardRead, public IntrusiveNode {
        ClipboardOutput(NativeWindowImpl* window, Output* output);

        void operator delete(ClipboardOutput* read, std::destroying_delete_t) noexcept;

        bool data(StringView chunk) override;
        void done(bool success) override;
        void cancel();
        void complete(bool success);

        NativeWindowImpl* window;
        Output* output;
    };

    struct HeadlessWindowImpl final: public Window, public TestModeInput {
        explicit HeadlessWindowImpl(Composer& composer);

        void initialize() override;
        void show() override;
        void activate() override;
        void requestClose() override;
        bool requestFrame() override;
        void cancelFrame() override;

        void setTitle(StringView title) override;
        void requestAttention() override;
        void requestRedraw() override;
        void restore() override;
        void iconify() override;
        void move(i32 x, i32 y) override;
        void focus() override;
        void setMaximized(bool maximized) override;
        void setFullscreen(bool fullscreen) override;
        void resizePixels(u32 width, u32 height) override;
        WindowInfo info() override;
        Clipboard* clipboard() override;
        DesktopActions* desktopActions() override;

        Renderer* createRender() override;
        TestModeInput* testApi() override;

        void testKeyEvent(int key, int scancode, int action, int modifiers) override;
        void testTextInput(unsigned codepoint, int modifiers) override;
        void testContentScale(float xScale, float yScale) override;
        void testClipboard(Clipboard* clipboard) override;
        void testDesktopActions(DesktopActions* actions) override;

        Composer& composer;
        Clipboard* clipboard_ = nullptr;
        DesktopActions* desktopActions_ = nullptr;
    };
}

NativeWindowImpl::NativeWindowImpl(Composer& composer_)
    : composer(composer_)
{
}

NativeWindowImpl::~NativeWindowImpl() {
    cancelClipboardReads();
}

void NativeWindowImpl::initialize() {
    if (initialized) {
        return;
    }
    initialized = true;
    native = composer.platform->createWindow(
        *composer.pool,
        {
            .appId = StringView(u8"shitty"),
            .title = StringView(opts.title),
            .width = (u32)(max(320, (int)(opts.nCols) * opts.fontsize / 2)),
            .height = (u32)(max(200, (int)(opts.nRows) * opts.fontsize)),
            .input = this,
            .events = this,
        }
    );
    const plt::WindowInfo current = native->info();
    if (isfinite(current.contentScale) && current.contentScale > 0.0f) {
        composer.setContentScale(current.contentScale);
    }
}

void NativeWindowImpl::show() {
    const u32 border = 2u * opts.border;
    const u32 width = border + (u32)(opts.nCols) * composer.glyphWidth;
    const u32 height = border + (u32)(opts.nRows) * composer.glyphHeight;
    native->setMinimumSize(border + composer.glyphWidth, border + composer.glyphHeight);
    native->setResizeUnit(composer.glyphWidth, composer.glyphHeight, border, border);
    native->resize(width, height);
    native->show();
    resized(native->info());
}

void NativeWindowImpl::activate() {
    callbacksActive = true;
}

void NativeWindowImpl::requestClose() {
    native->requestClose();
}

bool NativeWindowImpl::requestFrame() {
    return native->requestFrame();
}

void NativeWindowImpl::cancelFrame() {
    native->cancelFrame();
}

void NativeWindowImpl::setTitle(StringView title) {
    native->setTitle(title);
}

void NativeWindowImpl::requestAttention() {
    native->requestAttention();
}

void NativeWindowImpl::requestRedraw() {
    native->requestRedraw();
}

void NativeWindowImpl::restore() {
    native->restore();
}

void NativeWindowImpl::iconify() {
    native->iconify();
}

void NativeWindowImpl::move(i32 x, i32 y) {
    native->move(x, y);
}

void NativeWindowImpl::focus() {
    native->focus();
}

void NativeWindowImpl::setMaximized(bool maximized) {
    native->setMaximized(maximized);
}

void NativeWindowImpl::setFullscreen(bool fullscreen) {
    native->setFullscreen(fullscreen);
}

void NativeWindowImpl::resizePixels(u32 width, u32 height) {
    native->resize(width, height);
}

WindowInfo NativeWindowImpl::info() {
    const plt::WindowInfo source = native->info();
    return {
        .x = source.x,
        .y = source.y,
        .screenPixelWidth = source.screenPixelWidth,
        .screenPixelHeight = source.screenPixelHeight,
        .iconified = source.iconified,
        .maximized = source.maximized,
        .fullscreen = source.fullscreen,
    };
}

Clipboard* NativeWindowImpl::clipboard() {
    return this;
}

DesktopActions* NativeWindowImpl::desktopActions() {
    return this;
}

Renderer* NativeWindowImpl::createRender() {
    const plt::RenderContext context = native->renderContext();
    return Renderer::create(composer, context);
}

TestModeInput* NativeWindowImpl::testApi() {
    return nullptr;
}

void NativeWindowImpl::readPrimary(Output* output) {
    ClipboardOutput* const read = composer.smallObjects->make<ClipboardOutput>(this, output);
    clipboardReads.pushBack(read);
    native->readPrimary(*read);
}

void NativeWindowImpl::readClipboard(Output* output) {
    ClipboardOutput* const read = composer.smallObjects->make<ClipboardOutput>(this, output);
    clipboardReads.pushBack(read);
    native->readClipboard(*read);
}

void NativeWindowImpl::writePrimary(StringView content) {
    native->writePrimary(content);
}

void NativeWindowImpl::writeClipboard(StringView content) {
    native->writeClipboard(content);
}

void NativeWindowImpl::openUri(StringView uri) {
    uriBuffer.reset();
    uriBuffer.append(uri.data(), uri.length());
    char* const arguments[] = {
        (char*)("xdg-open"),
        uriBuffer.cStr(),
        nullptr,
    };
    pid_t pid = -1;
    posix_spawnp(&pid, arguments[0], nullptr, nullptr, arguments, environ);
}

void NativeWindowImpl::pointerIcon(PointerIcon icon) {
    native->pointerIcon(icon == PointerIcon::Link ? plt::PointerIcon::Link : plt::PointerIcon::Text);
}

void NativeWindowImpl::publish(IntrusiveList& listeners, void* argument) {
    for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen(argument);
    }
}

void NativeWindowImpl::publishWindow(const ::WindowEvents& events) {
    publish(composer.windowEventListeners, (void*)(&events));
}

void NativeWindowImpl::cancelClipboardReads() {
    while (!clipboardReads.empty()) {
        static_cast<ClipboardOutput*>(clipboardReads.mutFront())->cancel();
    }
}

ClipboardOutput::ClipboardOutput(NativeWindowImpl* window_, Output* output_)
    : window(window_)
    , output(output_)
{
}

void ClipboardOutput::operator delete(ClipboardOutput* read, std::destroying_delete_t) noexcept {
    SmallObjAllocator* const allocator = read->window->composer.smallObjects;
    allocator->release(read);
}

bool ClipboardOutput::data(StringView chunk) {
    output->write(chunk.data(), chunk.length());
    return true;
}

void ClipboardOutput::done(bool success) {
    complete(success);
}

void ClipboardOutput::cancel() {
    window->native->cancelClipboardRead(*this);
    complete(false);
}

void ClipboardOutput::complete(bool success) {
    unlink();
    Output* const completed = output;
    output = nullptr;
    if (success) {
        completed->finish();
    }
    delete completed;
    delete this;
}

void NativeWindowImpl::close() {
    if (callbacksActive) {
        publishWindow({.close = true});
    }
    composer.platform->stop();
}

void NativeWindowImpl::resized(const plt::WindowInfo& current) {
    if (isfinite(current.contentScale) && current.contentScale > 0.0f) {
        composer.setContentScale(current.contentScale);
    }
    composer.resize((u16)(min(current.width, (u32)(UINT16_MAX))), (u16)(min(current.height, (u32)(UINT16_MAX))));
    if (callbacksActive) {
        publishWindow({.resized = true});
    }
}

void NativeWindowImpl::redraw() {
    if (callbacksActive) {
        publishWindow({.redraw = true});
    }
}

void NativeWindowImpl::frame() {
    if (callbacksActive) {
        publish(composer.frameReadyListeners);
    }
}

void NativeWindowImpl::key(const plt::KeyInput& input) {
    if (callbacksActive) {
        composer.input->key(input);
    }
}

void NativeWindowImpl::text(const plt::TextInput& input) {
    if (callbacksActive) {
        composer.input->text(input);
    }
}

void NativeWindowImpl::pointerMotion(const plt::PointerMotionInput& input) {
    if (callbacksActive) {
        composer.input->pointerMotion(input);
    }
}

void NativeWindowImpl::pointerButton(const plt::PointerButtonInput& input) {
    if (callbacksActive) {
        composer.input->pointerButton(input);
    }
}

void NativeWindowImpl::scroll(const plt::ScrollInput& input) {
    if (callbacksActive) {
        composer.input->scroll(input);
    }
}

void NativeWindowImpl::focus(bool focused) {
    if (callbacksActive) {
        composer.input->focus(focused);
    }
}

void NativeWindowImpl::pointerPresence(bool present) {
    if (callbacksActive) {
        composer.input->pointerPresence(present);
    }
}

void NativeWindowImpl::flush() {
    if (callbacksActive) {
        composer.input->flush();
    }
}

HeadlessWindowImpl::HeadlessWindowImpl(Composer& composer_)
    : composer(composer_)
{
}

void HeadlessWindowImpl::initialize() {
}

void HeadlessWindowImpl::show() {
}

void HeadlessWindowImpl::activate() {
}

void HeadlessWindowImpl::requestClose() {
}

bool HeadlessWindowImpl::requestFrame() {
    return false;
}

void HeadlessWindowImpl::cancelFrame() {
}

void HeadlessWindowImpl::setTitle(StringView) {
}

void HeadlessWindowImpl::requestAttention() {
}

void HeadlessWindowImpl::requestRedraw() {
}

void HeadlessWindowImpl::restore() {
}

void HeadlessWindowImpl::iconify() {
}

void HeadlessWindowImpl::move(i32, i32) {
}

void HeadlessWindowImpl::focus() {
}

void HeadlessWindowImpl::setMaximized(bool) {
}

void HeadlessWindowImpl::setFullscreen(bool) {
}

void HeadlessWindowImpl::resizePixels(u32 width, u32 height) {
    composer.resize((u16)(min(width, (u32)(UINT16_MAX))), (u16)(min(height, (u32)(UINT16_MAX))));
}

WindowInfo HeadlessWindowImpl::info() {
    return {
        .screenPixelWidth = composer.pixelWidth,
        .screenPixelHeight = composer.pixelHeight,
    };
}

Clipboard* HeadlessWindowImpl::clipboard() {
    return clipboard_;
}

DesktopActions* HeadlessWindowImpl::desktopActions() {
    return desktopActions_;
}

Renderer* HeadlessWindowImpl::createRender() {
    Errno(ENOTSUP).raise(StringView(u8"headless window has no renderer"));
}

TestModeInput* HeadlessWindowImpl::testApi() {
    return this;
}

void HeadlessWindowImpl::testKeyEvent(int key, int, int action, int modifiers) {
    TestInputTranslator::sendKey(composer, key, action, modifiers);
}

void HeadlessWindowImpl::testTextInput(unsigned codepoint, int modifiers) {
    TestInputTranslator::sendText(composer, codepoint, modifiers);
}

void HeadlessWindowImpl::testContentScale(float xScale, float yScale) {
    TestInputTranslator::contentScale(composer, xScale, yScale);
}

void HeadlessWindowImpl::testClipboard(Clipboard* clipboard) {
    clipboard_ = clipboard;
}

void HeadlessWindowImpl::testDesktopActions(DesktopActions* actions) {
    desktopActions_ = actions;
}

InputKey TestInputTranslator::key(int key) {
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || key == ' ' || (key >= '\'' && key <= '/') || key == ';' || key == '=' || (key >= '[' && key <= '`')) {
        return InputKey::Printable;
    }
    if (key >= 290 && key <= 309) {
        return (InputKey)((u8)(InputKey::F1) + key - 290);
    }
    if (key >= 320 && key <= 329) {
        return (InputKey)((u8)(InputKey::Keypad0) + key - 320);
    }
    switch (key) {
        case 256:
            return InputKey::Escape;
        case 257:
            return InputKey::Enter;
        case 258:
            return InputKey::Tab;
        case 259:
            return InputKey::Backspace;
        case 260:
            return InputKey::Insert;
        case 261:
            return InputKey::Delete;
        case 262:
            return InputKey::Right;
        case 263:
            return InputKey::Left;
        case 264:
            return InputKey::Down;
        case 265:
            return InputKey::Up;
        case 266:
            return InputKey::PageUp;
        case 267:
            return InputKey::PageDown;
        case 268:
            return InputKey::Home;
        case 269:
            return InputKey::End;
        case 280:
            return InputKey::CapsLock;
        case 281:
            return InputKey::ScrollLock;
        case 282:
            return InputKey::NumLock;
        case 283:
            return InputKey::PrintScreen;
        case 284:
            return InputKey::Pause;
        case 330:
            return InputKey::KeypadDecimal;
        case 331:
            return InputKey::KeypadDivide;
        case 332:
            return InputKey::KeypadMultiply;
        case 333:
            return InputKey::KeypadSubtract;
        case 334:
            return InputKey::KeypadAdd;
        case 335:
            return InputKey::KeypadEnter;
        case 336:
            return InputKey::KeypadEqual;
        case 340:
            return InputKey::LeftShift;
        case 341:
            return InputKey::LeftControl;
        case 342:
            return InputKey::LeftAlt;
        case 343:
            return InputKey::LeftSuper;
        case 344:
            return InputKey::RightShift;
        case 345:
            return InputKey::RightControl;
        case 346:
            return InputKey::RightAlt;
        case 347:
            return InputKey::RightSuper;
        case 348:
            return InputKey::Menu;
        default:
            return InputKey::Unknown;
    }
}

InputAction TestInputTranslator::action(int action, bool& valid) {
    valid = true;
    switch (action) {
        case testPress:
            return InputAction::Press;
        case testRepeat:
            return InputAction::Repeat;
        case testRelease:
            return InputAction::Release;
        default:
            valid = false;
            return InputAction::Release;
    }
}

u16 TestInputTranslator::modifiers(int modifiers, bool rightAlt) {
    u16 result = 0;
    if (modifiers & testModShift) {
        result |= InputShift;
    }
    if (modifiers & testModControl) {
        result |= InputControl;
    }
    if (modifiers & testModAlt) {
        result |= rightAlt ? InputAltGraph : InputAlt;
    }
    if (modifiers & testModSuper) {
        result |= InputSuper;
    }
    if (modifiers & testModCapsLock) {
        result |= InputCapsLock;
    }
    if (modifiers & testModNumLock) {
        result |= InputNumLock;
    }
    return result;
}

u32 TestInputTranslator::baseCodepoint(int key) {
    if (key >= 'A' && key <= 'Z') {
        return key - 'A' + 'a';
    }
    return TestInputTranslator::key(key) == InputKey::Printable ? (u32)(key) : 0;
}

void TestInputTranslator::sendKey(Composer& composer, int keyCode, int actionCode, int rawModifiers) {
    bool valid;
    const InputAction inputAction = action(actionCode, valid);
    const InputKey inputKey = key(keyCode);
    if (!valid || inputKey == InputKey::Unknown) {
        return;
    }
    const bool rightAlt = inputKey == InputKey::RightAlt;
    const u32 codepoint = baseCodepoint(keyCode);
    composer.input->key({
        .key = inputKey,
        .action = inputAction,
        .modifiers = modifiers(rawModifiers, rightAlt),
        .layoutCodepoint = codepoint,
        .baseCodepoint = codepoint,
    });
}

void TestInputTranslator::sendText(Composer& composer, u32 codepoint, int rawModifiers) {
    if (codepoint != 0) {
        composer.input->text({codepoint, modifiers(rawModifiers, false)});
    }
}

void TestInputTranslator::contentScale(Composer& composer, float xScale, float yScale) {
    const float scale = max(xScale, yScale);
    if (isfinite(scale) && scale > 0.0f) {
        composer.setContentScale(scale);
    }
}

Window* Window::create(Composer& composer) {
    return composer.pool->make<NativeWindowImpl>(composer);
}

Window* Window::createHeadless(Composer& composer) {
    return composer.pool->make<HeadlessWindowImpl>(composer);
}
