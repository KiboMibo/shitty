/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "test_mode.h"

#include "cell_extra_store.h"
#include "clipboard.h"
#include "composer.h"
#include "desktop_actions.h"
#include "grapheme.h"
#include "font_pack.h"
#include "hex.h"
#include "input_handler.h"
#include "keyboard.h"
#include "listener.h"
#include "options.h"
#include "mouse_protocol.h"
#include "mouse_frontend.h"
#include "pty.h"
#include "pty_output.h"
#include "render_reference.h"
#include "startup.h"
#include "test_input.h"
#include "utf8.h"
#include "render.h"
#include "vterm.h"
#include "vterm_test.h"
#include "vterm_trace.h"

#include <plt/platform_headless.h>

#include <std/dbg/assert.h>
#include <std/ios/output.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/sys/throw.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <functional>
#include <map>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

using namespace stl;
using namespace plt;

namespace {
    extern "C" int openpty(int*, int*, char*, const termios*, const winsize*);

    struct TestPty final: public Pty, public Listener {
        TestPty(Composer& composer, int fd);

        ssize_t read(u8* buffer, size_t size);
        ssize_t write(const u8* buffer, size_t size) override;
        void outputReady() override;
        void onListen(void*) override;

        bool flushOutput();
        void setReadHandler(std::function<ssize_t(u8*, size_t)> handler);
        void setWriteHandler(std::function<ssize_t(const u8*, size_t)> handler);
        std::string takeReadData();
        std::string takeWriteData();

        void applySize();

        Composer& composer_;
        int fd_;
        std::function<ssize_t(u8*, size_t)> onRead;
        std::function<ssize_t(const u8*, size_t)> onWrite;
        std::string readData;
        std::string writeData;
    };

    struct TestUtf8Decoder {
        TestUtf8Decoder();

        std::vector<u32> push(const std::string& input);
        std::vector<u32> flush();
        void reset();

        std::vector<u32> output;
        Utf8Decoder decoder;
    };

    struct FailFontChange final: public Listener {
        void arm();
        void onListen(void*) override;

        bool armed = false;
    };
}

TestPty::TestPty(Composer& composer, int fd)
    : composer_(composer)
    , fd_(fd)
    , onRead([this](u8* buffer, size_t size) {
        return ::read(fd_, buffer, size);
    })
    , onWrite([this](const u8* buffer, size_t size) {
        return ::write(fd_, buffer, size);
    }) {
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("test PTY nonblocking setup failed");
    }
}

ssize_t TestPty::read(u8* buffer, size_t size) {
    const ssize_t count = onRead(buffer, size);
    if (count > 0) {
        readData.append((const char*)(buffer), (size_t)(count));
    }
    return count;
}

ssize_t TestPty::write(const u8* buffer, size_t size) {
    const ssize_t count = onWrite(buffer, size);
    if (count > 0) {
        writeData.append((const char*)(buffer), (size_t)(count));
    }
    return count;
}

void TestPty::outputReady() {
}

void TestPty::onListen(void*) {
    applySize();
}

bool TestPty::flushOutput() {
    return !composer_.ptyOutputs->flush();
}

void TestPty::applySize() {
    winsize size{};
    size.ws_col = composer_.columns;
    size.ws_row = composer_.rows;
    size.ws_xpixel = composer_.columns * composer_.glyphWidth;
    size.ws_ypixel = composer_.rows * composer_.glyphHeight;
    if (ioctl(fd_, TIOCSWINSZ, &size) < 0) {
        throw std::runtime_error("test PTY resize failed");
    }
}

void TestPty::setReadHandler(std::function<ssize_t(u8*, size_t)> handler) {
    onRead = std::move(handler);
}

void TestPty::setWriteHandler(std::function<ssize_t(const u8*, size_t)> handler) {
    onWrite = std::move(handler);
}

std::string TestPty::takeReadData() {
    std::string result;
    result.swap(readData);
    return result;
}

std::string TestPty::takeWriteData() {
    std::string result;
    result.swap(writeData);
    return result;
}

TestUtf8Decoder::TestUtf8Decoder() {
}

std::vector<u32> TestUtf8Decoder::push(const std::string& input) {
    for (const unsigned char ch : input) {
        if (ch < 0x80) {
            if (decoder.checkPrematureEOS()) {
                output.push_back(decoder.getUnicode());
            }
            output.push_back(ch);
        } else {
            for (int completed = decoder.pushByte(ch); completed > 0; --completed) {
                output.push_back(decoder.getUnicode());
            }
        }
    }
    std::vector<u32> result;
    result.swap(output);
    return result;
}

std::vector<u32> TestUtf8Decoder::flush() {
    if (decoder.checkPrematureEOS()) {
        output.push_back(decoder.getUnicode());
    }
    std::vector<u32> result;
    result.swap(output);
    return result;
}

void TestUtf8Decoder::reset() {
    output.clear();
    decoder.reset();
}

void FailFontChange::arm() {
    armed = true;
}

void FailFontChange::onListen(void*) {
    if (!armed) {
        return;
    }
    armed = false;
    Errno(EIO).raise(StringView(u8"injected font replacement failure"));
}

namespace {

    void writeAll(int fd, StringView data) {
        size_t offset = 0;
        while (offset < data.length()) {
            const ssize_t count = write(fd, data.data() + offset, data.length() - offset);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("test control write failed");
            }
            offset += (size_t)(count);
        }
    }

    void writeAll(int fd, const std::string& data) {
        writeAll(fd, StringView((const u8*)(data.data()), data.size()));
    }

    void writeAll(int fd, const char* data) {
        writeAll(fd, StringView(data));
    }

    std::string toString(const StringBuilder& builder) {
        return std::string((const char*)(builder.data()), builder.used());
    }

    bool readLine(int fd, std::string& buffered, std::string& line) {
        while (true) {
            const size_t newline = buffered.find('\n');
            if (newline != std::string::npos) {
                line = buffered.substr(0, newline);
                buffered.erase(0, newline + 1);
                return true;
            }

            char chunk[4096];
            const ssize_t count = read(fd, chunk, sizeof(chunk));
            if (count == 0) {
                return false;
            }
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("test control read failed");
            }
            buffered.append(chunk, (size_t)(count));
        }
    }

    u8 hexDigit(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        throw std::runtime_error("invalid hex input");
    }

    std::string decodeHex(const std::string& input) {
        if (input.size() % 2) {
            throw std::runtime_error("odd-length hex input");
        }
        std::string output;
        output.reserve(input.size() / 2);
        for (size_t k = 0; k < input.size(); k += 2) {
            output.push_back((char)((hexDigit(input[k]) << 4) | hexDigit(input[k + 1])));
        }
        return output;
    }

    u8 hexDigit(u8 ch) {
        if (ch >= u8'0' && ch <= u8'9') {
            return ch - u8'0';
        }
        if (ch >= u8'a' && ch <= u8'f') {
            return ch - u8'a' + 10;
        }
        if (ch >= u8'A' && ch <= u8'F') {
            return ch - u8'A' + 10;
        }
        Errno(EINVAL).raise(StringView(u8"invalid hex input"));
    }

    Buffer decodeHex(StringView input) {
        if (input.length() % 2) {
            Errno(EINVAL).raise(StringView(u8"odd-length hex input"));
        }
        Buffer output(input.length() / 2);
        for (size_t index = 0; index < input.length(); index += 2) {
            const u8 byte = (hexDigit(input[index]) << 4) | hexDigit(input[index + 1]);
            output.append(&byte, 1);
        }
        return output;
    }

    std::string encodeHex(StringView input) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string output;
        output.reserve(input.length() * 2);
        for (const u8 ch : input) {
            output.push_back(digits[ch >> 4]);
            output.push_back(digits[ch & 15]);
        }
        return output;
    }

    std::string encodeHex(const std::string& input) {
        return encodeHex(StringView((const u8*)(input.data()), input.size()));
    }

    void appendHex(StringBuilder& output, StringView input) {
        for (const u8 byte : input) {
            output << Hex{byte, 2};
        }
    }

    struct TestClipboard final: public Clipboard {
        void readPrimary(Output* output) override;
        void readClipboard(Output* output) override;
        void writePrimary(StringView content) override;
        void writeClipboard(StringView content) override;
        void read(Output* output, const Buffer& content);

        Buffer primary;
        Buffer system;
        u64 generation = 0;
        size_t readChunk = 0;
    };

    struct TestDesktopActions final: public DesktopActions {
        void openUri(StringView uri) override;
        void pointerIcon(::PointerIcon icon) override;

        Buffer openedUri;
        ::PointerIcon icon = ::PointerIcon::Text;
        u64 openCount = 0;
    };

    struct TestTerminal {
        TestTerminal(Composer& composer, Vterm& terminal, TestApi& testApi, TestPty& pty, ReferenceRenderer& renderer, plt::WindowHeadless& window);

        void feedPtyOutput(const u8* data, size_t size);
        void feedPtyOutput(const std::vector<std::string>& chunks);
        void update();
        void redraw();
        void preedit(StringView text, i32 cursorBegin, i32 cursorEnd);
        void resize(u16 width, u16 height);
        int writePty(InputKey key, VtModifier modifiers = VtModifier::none, bool userInput = false);
        int writePty(u8 byte, VtModifier modifiers = VtModifier::none, bool userInput = false);
        int writePty(const char* text, bool userInput = false);
        int writePty(const u8* data, size_t size, bool userInput = false);
        int writeKittyKey(InputKey key, u16 modifiers, VtermKeyEventType event);
        int writeKittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, VtermKeyEventType event);
        bool readPty(bool flushOutput = true);
        void drainPty();
        bool servicePty(bool readable, bool writable);
        bool flushPtyOutput();
        MouseTrackingState getMouseTrackingState();
        u8 getKittyKeyboardFlags();
        bool getScreenReverseVideo();
        u8 getLedState();
        bool getReverseWrapMode();
        bool getNationalReplacementMode();
        bool getAnsiMode(u32 mode);
        bool getPrivateMode(u32 mode);
        bool getTabStop(u16 column);
        void setWrapped(u16 row);
        u8 getRowSemantic(i32 row);
        u8 getSemanticClick();
        bool cursorIsAtPrompt();
        bool getPendingWrap();
        TerminalCursor::Style getCursorStyle();
        TerminalPen getPenState();
        RectangleOrigin getRectangleOrigin();
        size_t getHyperlinkCount();
        std::string getHyperlink(int x, int y);
        bool mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY);
        void setLocatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons = 0);
        void reportLocatorButton(u8 button, bool pressed);
        void mouseWheelUp(u16 count = 1);
        void mouseWheelDown(u16 count = 1);
        void pageUp();
        void pageDown();
        void selectStart(int x, int y, bool cycle);
        void selectExtend(int x, int y, bool cycle);
        void selectUpdate(int x, int y);
        bool selectFinish(std::string& selection);
        void selectRectangularModeToggle();
        void pasteSelection(const std::string& selection);
        void setHasFocus(bool focused);
        bool expireSynchronizedOutput(bool force = false);
        bool advanceAnimation(bool force = false);
        bool advanceSelectionAutoscroll();
        Buffer allText() const;

        Composer& composer;
        Vterm& terminal;
        TestApi& testApi;
        TestPty& pty;
        ReferenceRenderer& renderer;
        plt::WindowHeadless& window;
        u8 ptyInputBuffer[64 * 1024];
        bool consumePty(bool drain, bool flushOutput);
        bool present();
    };

    template <typename Cell>
    unsigned cellUnderline(const Cell& cell) {
        return cell.underline;
    }

    template <>
    unsigned cellUnderline(const TerminalCell& cell) {
        return cell.underlined();
    }

    template <typename Cell>
    unsigned cellFlags(const Cell& cell, u8 lineAttribute) {
        return (cell.dwidth << 0) | (cell.dwidth_cont << 1) | (cell.bold << 2) | (cell.italic << 3) | (cellUnderline(cell) << 4) | (cell.inverse << 5) | (cell.wrap << 6) | (cell.faint << 7) | (cell.blink << 8) | (cell.conceal << 9) | (cell.strike << 10) | (cell.overline << 11) | (cell.underline_style << 12) | ((cell.protected_char != 0) << 15) | (lineAttribute << 16) | (cell.drawn << 18);
    }

    unsigned cellFlags(const TerminalCell& cell) {
        return cellFlags(cell, 0);
    }

}

void TestClipboard::readPrimary(Output* output) {
    read(output, primary);
}

void TestClipboard::readClipboard(Output* output) {
    read(output, system);
}

void TestClipboard::read(Output* output, const Buffer& content) {
    const size_t chunk = readChunk == 0 ? content.used() : readChunk;
    size_t offset = 0;
    while (offset != content.used()) {
        const size_t remaining = content.used() - offset;
        const size_t size = remaining < chunk ? remaining : chunk;
        output->write((const u8*)(content.data()) + offset, size);
        offset += size;
    }
    output->finish();
    delete output;
}

void TestClipboard::writePrimary(StringView content) {
    primary.reset();
    primary.append(content.data(), content.length());
    ++generation;
}

void TestClipboard::writeClipboard(StringView content) {
    system.reset();
    system.append(content.data(), content.length());
}

void TestDesktopActions::openUri(StringView uri) {
    openedUri.reset();
    openedUri.append(uri.data(), uri.length());
    ++openCount;
}

void TestDesktopActions::pointerIcon(::PointerIcon icon_) {
    icon = icon_;
}

TestTerminal::TestTerminal(Composer& composer_, Vterm& terminal, TestApi& testApi, TestPty& pty, ReferenceRenderer& renderer_, plt::WindowHeadless& window_)
    : composer(composer_)
    , terminal(terminal)
    , testApi(testApi)
    , pty(pty)
    , renderer(renderer_)
    , window(window_) {
}

bool TestTerminal::present() {
    while (window.framePending()) {
        if (!window.dispatchFrame()) {
            return false;
        }
    }
    return true;
}

void TestTerminal::feedPtyOutput(const u8* data, size_t size) {
    renderer.resetUpdateStats();
    terminal.feedPty(StringView(data, size));
    update();
}

void TestTerminal::feedPtyOutput(const std::vector<std::string>& chunks) {
    renderer.resetUpdateStats();
    for (const std::string& chunk : chunks) {
        terminal.feedPty(StringView((const u8*)(chunk.data()), chunk.size()));
    }
    update();
}

void TestTerminal::update() {
    flushPtyOutput();
    window.requestFrame();
    present();
    flushPtyOutput();
}

void TestTerminal::redraw() {
    terminal.expose();
    update();
}

void TestTerminal::preedit(StringView text, i32 cursorBegin, i32 cursorEnd) {
    terminal.preedit(text, cursorBegin, cursorEnd);
    update();
}

void TestTerminal::resize(u16 width, u16 height) {
    window.requestResize(width, height);
    update();
}

Buffer TestTerminal::allText() const {
    Buffer output((renderer.historyRows() + renderer.rows()) * ((size_t)(renderer.columns()) * 4 + 1));
    const auto appendCodepoint = [&](u32 codepoint) {
        Utf8Encoder::pushUnicode(codepoint, [&](u8 byte) {
            output.append(&byte, 1);
        });
    };
    for (i32 row = -(i32)(renderer.historyRows()); row < renderer.rows(); ++row) {
        size_t contentEnd = output.used();
        for (u16 column = 0; column < renderer.columns(); ++column) {
            const VtermTestCell value = testApi.logicalCell(row, column);
            const TerminalCell& cell = value.cell;
            if (cell.dwidth_cont) {
                continue;
            }
            if (value.graphemeSize != 0) {
                for (size_t index = 0; index < value.graphemeSize; ++index) {
                    appendCodepoint(value.grapheme[index]);
                }
            } else {
                appendCodepoint(cell.uc_pt == 0 ? ' ' : cell.uc_pt);
            }
            if (cell.drawn || (cell.uc_pt != 0 && cell.uc_pt != ' ') || value.graphemeSize != 0) {
                contentEnd = output.used();
            }
        }
        output.seekAbsolute(contentEnd);
        const u8 separator = 0;
        output.append(&separator, 1);
    }
    return output;
}

int TestTerminal::writePty(InputKey key, VtModifier modifiers, bool) {
    testApi.key(key, modifiers);
    update();
    return 1;
}

int TestTerminal::writePty(u8 byte, VtModifier modifiers, bool) {
    testApi.character(byte, modifiers);
    update();
    return 1;
}

int TestTerminal::writePty(const char* text, bool userInput) {
    return writePty((const u8*)(text), strlen(text), userInput);
}

int TestTerminal::writePty(const u8* data, size_t size, bool userInput) {
    terminal.sendBytes(StringView(data, size), userInput);
    update();
    return size;
}

int TestTerminal::writeKittyKey(InputKey key, u16 modifiers, VtermKeyEventType keyEvent) {
    testApi.kittyKey(key, modifiers, keyEvent);
    update();
    return 1;
}

int TestTerminal::writeKittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, VtermKeyEventType keyEvent) {
    testApi.kittyKey(key, shiftedKey, baseLayoutKey, modifiers, keyEvent);
    update();
    return 1;
}

bool TestTerminal::flushPtyOutput() {
    return pty.flushOutput();
}

bool TestTerminal::readPty(bool flushOutput) {
    return consumePty(false, flushOutput);
}

void TestTerminal::drainPty() {
    consumePty(true, true);
}

bool TestTerminal::consumePty(bool drain, bool flushOutput) {
    bool finished = false;
    while (true) {
        ssize_t count;
        do {
            count = pty.read(ptyInputBuffer, sizeof(ptyInputBuffer));
        } while (count < 0 && errno == EINTR);
        if (count > 0) {
            terminal.feedPty(StringView(ptyInputBuffer, count));
            if (drain) {
                continue;
            }
        } else if (count == 0 || (count < 0 && errno == EIO)) {
            finished = true;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            finished = true;
        }
        break;
    }
    if (flushOutput) {
        flushPtyOutput();
    }
    window.requestFrame();
    present();
    if (flushOutput) {
        flushPtyOutput();
    }
    return finished;
}

bool TestTerminal::servicePty(bool readable, bool writable) {
    if (writable) {
        flushPtyOutput();
    }
    return readable && readPty(!writable);
}

MouseTrackingState TestTerminal::getMouseTrackingState() {
    return testApi.inspect().mouse;
}

u8 TestTerminal::getKittyKeyboardFlags() {
    return testApi.inspect().kittyKeyboardFlags;
}

bool TestTerminal::getScreenReverseVideo() {
    return testApi.inspect().screenReverseVideo;
}

u8 TestTerminal::getLedState() {
    return testApi.inspect().ledState;
}

bool TestTerminal::getReverseWrapMode() {
    return testApi.inspect().reverseWrapMode;
}

bool TestTerminal::getNationalReplacementMode() {
    return testApi.inspect().nationalReplacementMode;
}

bool TestTerminal::getAnsiMode(u32 mode) {
    return testApi.ansiMode(mode);
}

bool TestTerminal::getPrivateMode(u32 mode) {
    return testApi.privateMode(mode);
}

bool TestTerminal::getTabStop(u16 column) {
    return testApi.tabStop(column);
}

void TestTerminal::setWrapped(u16 row) {
    testApi.setWrapped(row);
    update();
}

u8 TestTerminal::getRowSemantic(i32 row) {
    return testApi.rowSemantic(row);
}

u8 TestTerminal::getSemanticClick() {
    return testApi.semanticClick();
}

bool TestTerminal::cursorIsAtPrompt() {
    return testApi.cursorIsAtPrompt();
}

bool TestTerminal::getPendingWrap() {
    return testApi.inspect().pendingWrap;
}

TerminalCursor::Style TestTerminal::getCursorStyle() {
    return testApi.inspect().cursorStyle;
}

TerminalPen TestTerminal::getPenState() {
    return testApi.inspect().pen;
}

RectangleOrigin TestTerminal::getRectangleOrigin() {
    return testApi.inspect().rectangleOrigin;
}

size_t TestTerminal::getHyperlinkCount() {
    return testApi.inspect().hyperlinkCount;
}

std::string TestTerminal::getHyperlink(int x, int y) {
    const StringView result = testApi.hyperlinkAt(x, y);
    return std::string((const char*)(result.data()), result.length());
}

bool TestTerminal::mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY) {
    const bool result = testApi.mouseHighlightRelease(endX, endY, mouseX, mouseY);
    update();
    return result;
}

void TestTerminal::setLocatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons) {
    testApi.locatorPosition(column, row, pixelX, pixelY, buttons);
    update();
}

void TestTerminal::reportLocatorButton(u8 button, bool pressed) {
    testApi.locatorButton(button, pressed);
    update();
}

void TestTerminal::mouseWheelUp(u16 count) {
    testApi.scrollUp(count);
    update();
}

void TestTerminal::mouseWheelDown(u16 count) {
    testApi.scrollDown(count);
    update();
}

void TestTerminal::pageUp() {
    testApi.pageUp();
    update();
}

void TestTerminal::pageDown() {
    testApi.pageDown();
    update();
}

void TestTerminal::selectStart(int x, int y, bool cycle) {
    testApi.selectionStart(x, y, cycle);
    update();
}

void TestTerminal::selectExtend(int x, int y, bool cycle) {
    testApi.selectionExtend(x, y, cycle);
    update();
}

void TestTerminal::selectUpdate(int x, int y) {
    testApi.selectionUpdate(x, y);
    update();
}

bool TestTerminal::selectFinish(std::string& selection) {
    const VtermTextResult result = testApi.selectionFinish();
    selection.assign((const char*)(result.text.data()), result.text.length());
    update();
    return result.status;
}

void TestTerminal::selectRectangularModeToggle() {
    testApi.selectionRectangular();
    update();
}

void TestTerminal::pasteSelection(const std::string& selection) {
    testApi.paste(StringView((const u8*)(selection.data()), selection.size()));
    update();
}

void TestTerminal::setHasFocus(bool focused) {
    composer.input->focus(focused);
    update();
}

bool TestTerminal::expireSynchronizedOutput(bool force) {
    const bool result = terminal.expireSynchronizedOutput(force);
    update();
    return result;
}

bool TestTerminal::advanceAnimation(bool force) {
    const bool result = terminal.advanceAnimation(force);
    update();
    return result;
}

bool TestTerminal::advanceSelectionAutoscroll() {
    const bool result = testApi.advanceSelectionAutoscroll();
    update();
    return result;
}

namespace {

    std::string drainInput(int fd) {
        std::string output;
        char chunk[4096];
        while (true) {
            const ssize_t count = read(fd, chunk, sizeof(chunk));
            if (count > 0) {
                output.append(chunk, (size_t)(count));
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (count == 0) {
                break;
            }
            throw std::runtime_error("test PTY read failed");
        }
        return output;
    }

    InputKey parseKey(const std::string& name) {
        static const std::map<std::string, InputKey> keys = {
            {"SPACE", InputKey::Space},
            {"RETURN", InputKey::Enter},
            {"BACKSPACE", InputKey::Backspace},
            {"TAB", InputKey::Tab},
            {"UP", InputKey::Up},
            {"DOWN", InputKey::Down},
            {"LEFT", InputKey::Left},
            {"RIGHT", InputKey::Right},
            {"INSERT", InputKey::Insert},
            {"DELETE", InputKey::Delete},
            {"HOME", InputKey::Home},
            {"END", InputKey::End},
            {"PAGE_UP", InputKey::PageUp},
            {"PAGE_DOWN", InputKey::PageDown},
            {"CLEAR", InputKey::Clear},
            {"F1", InputKey::F1},
            {"F2", InputKey::F2},
            {"F3", InputKey::F3},
            {"F4", InputKey::F4},
            {"F5", InputKey::F5},
            {"F6", InputKey::F6},
            {"F7", InputKey::F7},
            {"F8", InputKey::F8},
            {"F9", InputKey::F9},
            {"F10", InputKey::F10},
            {"F11", InputKey::F11},
            {"F12", InputKey::F12},
            {"F13", InputKey::F13},
            {"F14", InputKey::F14},
            {"F15", InputKey::F15},
            {"F16", InputKey::F16},
            {"F17", InputKey::F17},
            {"F18", InputKey::F18},
            {"F19", InputKey::F19},
            {"F20", InputKey::F20},
            {"F21", InputKey::F21},
            {"F22", InputKey::F22},
            {"F23", InputKey::F23},
            {"F24", InputKey::F24},
            {"F25", InputKey::F25},
            {"F26", InputKey::F26},
            {"F27", InputKey::F27},
            {"F28", InputKey::F28},
            {"F29", InputKey::F29},
            {"F30", InputKey::F30},
            {"F31", InputKey::F31},
            {"F32", InputKey::F32},
            {"F33", InputKey::F33},
            {"F34", InputKey::F34},
            {"F35", InputKey::F35},
            {"KP_F1", InputKey::KeypadF1},
            {"KP_F2", InputKey::KeypadF2},
            {"KP_F3", InputKey::KeypadF3},
            {"KP_F4", InputKey::KeypadF4},
            {"KP_PLUS", InputKey::KeypadAdd},
            {"KP_MINUS", InputKey::KeypadSubtract},
            {"KP_STAR", InputKey::KeypadMultiply},
            {"KP_SLASH", InputKey::KeypadDivide},
            {"KP_COMMA", InputKey::KeypadSeparator},
            {"KP_DOT", InputKey::KeypadDecimal},
            {"KP_EQUAL", InputKey::KeypadEqual},
            {"KP_TAB", InputKey::KeypadTab},
            {"KP_SPACE", InputKey::KeypadSpace},
            {"KP_ENTER", InputKey::KeypadEnter},
            {"KP_LEFT", InputKey::KeypadLeft},
            {"KP_RIGHT", InputKey::KeypadRight},
            {"KP_UP", InputKey::KeypadUp},
            {"KP_DOWN", InputKey::KeypadDown},
            {"KP_HOME", InputKey::KeypadHome},
            {"KP_END", InputKey::KeypadEnd},
            {"KP_PAGE_UP", InputKey::KeypadPageUp},
            {"KP_PAGE_DOWN", InputKey::KeypadPageDown},
            {"KP_INSERT", InputKey::KeypadInsert},
            {"KP_DELETE", InputKey::KeypadDelete},
            {"KP_BEGIN", InputKey::KeypadBegin},
            {"KP_0", InputKey::Keypad0},
            {"KP_1", InputKey::Keypad1},
            {"KP_2", InputKey::Keypad2},
            {"KP_3", InputKey::Keypad3},
            {"KP_4", InputKey::Keypad4},
            {"KP_5", InputKey::Keypad5},
            {"KP_6", InputKey::Keypad6},
            {"KP_7", InputKey::Keypad7},
            {"KP_8", InputKey::Keypad8},
            {"KP_9", InputKey::Keypad9},
            {"CAPS_LOCK", InputKey::CapsLock},
            {"SCROLL_LOCK", InputKey::ScrollLock},
            {"NUM_LOCK", InputKey::NumLock},
            {"PRINT", InputKey::PrintScreen},
            {"PAUSE", InputKey::Pause},
            {"MENU", InputKey::Menu},
            {"LEFT_SHIFT", InputKey::LeftShift},
            {"LEFT_CONTROL", InputKey::LeftControl},
            {"LEFT_ALT", InputKey::LeftAlt},
            {"LEFT_SUPER", InputKey::LeftSuper},
            {"RIGHT_SHIFT", InputKey::RightShift},
            {"RIGHT_CONTROL", InputKey::RightControl},
            {"RIGHT_ALT", InputKey::RightAlt},
            {"RIGHT_SUPER", InputKey::RightSuper},
            {"MEDIA_PLAY", InputKey::MediaPlay},
            {"MEDIA_PAUSE", InputKey::MediaPause},
            {"MEDIA_PLAY_PAUSE", InputKey::MediaPlayPause},
            {"MEDIA_REVERSE", InputKey::MediaReverse},
            {"MEDIA_STOP", InputKey::MediaStop},
            {"MEDIA_FAST_FORWARD", InputKey::MediaFastForward},
            {"MEDIA_REWIND", InputKey::MediaRewind},
            {"MEDIA_TRACK_NEXT", InputKey::MediaTrackNext},
            {"MEDIA_TRACK_PREVIOUS", InputKey::MediaTrackPrevious},
            {"MEDIA_RECORD", InputKey::MediaRecord},
            {"VOLUME_DOWN", InputKey::VolumeDown},
            {"VOLUME_UP", InputKey::VolumeUp},
            {"VOLUME_MUTE", InputKey::VolumeMute},
        };
        const auto found = keys.find(name);
        if (found == keys.end()) {
            throw std::runtime_error("unknown key");
        }
        return found->second;
    }
}

int runTestMode(Composer& composer, TestInput& input, plt::WindowEvents& events, plt::FrameCallback& frame, int controlFd, int argc, char* argv[]) {
    int io[2];
    if (openpty(&io[0], &io[1], nullptr, nullptr, nullptr) < 0) {
        throw std::runtime_error("test openpty failed");
    }
    // Terminal-side descriptors must not leak into spawned children: a child
    // holding the control socket or the pty pair alive wedges the harness
    // when st_test itself dies.
    if (fcntl(controlFd, F_SETFD, FD_CLOEXEC) < 0
        || fcntl(io[0], F_SETFD, FD_CLOEXEC) < 0
        || fcntl(io[1], F_SETFD, FD_CLOEXEC) < 0) {
        throw std::runtime_error("test FD_CLOEXEC setup failed");
    }
    termios childTtyAttrs;
    if (tcgetattr(io[1], &childTtyAttrs) < 0) {
        close(io[0]);
        close(io[1]);
        throw std::runtime_error("test tcgetattr failed");
    }
    termios ttyAttrs = childTtyAttrs;
    cfmakeraw(&ttyAttrs);
    if (tcsetattr(io[1], TCSANOW, &ttyAttrs) < 0) {
        close(io[0]);
        close(io[1]);
        throw std::runtime_error("test tcsetattr failed");
    }
    const int flags = fcntl(io[1], F_GETFL, 0);
    if (flags < 0 || fcntl(io[1], F_SETFL, flags | O_NONBLOCK) < 0) {
        close(io[0]);
        close(io[1]);
        throw std::runtime_error("test socket setup failed");
    }

    {
        unsigned glyphWidth = 1;
        unsigned glyphHeight = 1;
        if (const char* geometry = std::getenv("SHITTY_TEST_GLYPH")) {
            std::istringstream input(geometry);
            char separator = 0;
            if (!(input >> glyphWidth >> separator >> glyphHeight) || separator != 'x' || !glyphWidth || !glyphHeight || input.peek() != EOF) {
                throw std::runtime_error("invalid test glyph geometry");
            }
        }
        composer.setGlyphSize(glyphWidth, glyphHeight);
    }
    const u16 width = 2 * opts.border + opts.nCols * composer.glyphWidth;
    const u16 height = 2 * opts.border + opts.nRows * composer.glyphHeight;
    composer.platform = plt::createHeadlessPlatform(*composer.pool);
    composer.window = composer.platform->createWindow(
        *composer.pool,
        {
            .title = StringView(opts.title),
            .width = width,
            .height = height,
            .input = composer.input,
            .events = &events,
            .frame = &frame,
        }
    );
    auto& window = static_cast<plt::WindowHeadless&>(*composer.window);
    window.requestFrame();
    window.dispatchFrame();
    TestPty terminalPty(composer, io[0]);
    composer.pty = &terminalPty;
    terminalPty.applySize();
    composer.resizedListeners.pushBack(&terminalPty);
    composer.ptyOutputs = PtyOutputQueue::create(composer.pool, composer.smallObjects, terminalPty);
    composer.ptyOutput = composer.ptyOutputs->append();
    TestClipboard clipboard;
    TestDesktopActions desktopActions;
    composer.clipboard = &clipboard;
    composer.desktopActions = &desktopActions;
    composer.rendererPool = ObjPool::fromMemory();
    composer.renderer = Renderer::create(composer, *composer.rendererPool, window.renderContext());
    auto& renderer = static_cast<ReferenceRenderer&>(*composer.renderer);
    VtermTrace& vtermTrace = *VtermTrace::create(composer);
    Vterm& vterm = *Vterm::create(composer, &vtermTrace);
    composer.vterm = &vterm;
    TestApi& testApi = *vterm.testApi();
    renderer.attach(testApi);
    TestTerminal terminal(composer, vterm, testApi, terminalPty, renderer, window);
    FailFontChange failFontChange;
    composer.fontChangedListeners.pushFront(&failFontChange);
    pid_t childPid = -1;
    int childExitStatus = -1;

    struct ScriptedPtyRead {
        std::string data;
        int error = 0;
        bool eof = false;
    };

    std::deque<ScriptedPtyRead> scriptedPtyReads;

    struct ScriptedPtyWrite {
        size_t count = 0;
        int error = 0;
    };

    std::deque<ScriptedPtyWrite> scriptedPtyWrites;
    std::string writtenPtyData;
    TestUtf8Decoder testUtf8Decoder;
    const auto installScriptedPtyReader = [&]() {
        terminalPty.setReadHandler([&scriptedPtyReads](u8* buffer, size_t size) {
            if (scriptedPtyReads.empty()) {
                errno = EAGAIN;
                return (ssize_t)(-1);
            }
            auto& item = scriptedPtyReads.front();
            if (item.eof) {
                scriptedPtyReads.pop_front();
                return (ssize_t)(0);
            }
            if (item.error) {
                errno = item.error;
                scriptedPtyReads.pop_front();
                return (ssize_t)(-1);
            }
            const size_t count = std::min(size, item.data.size());
            std::copy_n(item.data.data(), count, buffer);
            item.data.erase(0, count);
            if (item.data.empty()) {
                scriptedPtyReads.pop_front();
            }
            return (ssize_t)(count);
        });
    };
    terminal.redraw();
    writeAll(controlFd, "READY\n");

    const auto pumpChild = [&]() {
        terminal.flushPtyOutput();
        pollfd source{io[0], POLLIN, 0};
        if (poll(&source, 1, 0) > 0 && (source.revents & POLLIN)) {
            terminal.readPty();
        }
        terminal.flushPtyOutput();
        int status = 0;
        if (childPid > 0 && waitpid(childPid, &status, WNOHANG) == childPid) {
            childPid = -1;
            if (WIFEXITED(status)) {
                childExitStatus = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                childExitStatus = 128 + WTERMSIG(status);
            } else {
                childExitStatus = 255;
            }
            // By reap time every child write has completed, but a PTY read is
            // not required to consume every buffered write.  Drain through
            // EAGAIN before publishing the exit status and final screen.
            terminal.drainPty();
        }
    };

    std::string buffered;
    std::string line;
    while (readLine(controlFd, buffered, line)) {
        try {
            if (line.compare(0, 6, "WRITE ") == 0) {
                const std::string input = decodeHex(line.substr(6));
                terminal.feedPtyOutput((const u8*)input.data(), input.size());
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 13, "WRITE_CHUNKS ") == 0) {
                std::istringstream args(line.substr(13));
                std::vector<std::string> chunks;
                std::string encoded;
                while (args >> encoded) {
                    chunks.push_back(decodeHex(encoded));
                }
                if (chunks.empty()) {
                    throw std::runtime_error("empty PTY chunk list");
                }
                terminal.feedPtyOutput(chunks);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 15, "MEASURE_WIDTHS ") == 0) {
                std::istringstream args(line.substr(15));
                std::string encoded;
                std::string input;
                size_t count = 0;
                while (args >> encoded) {
                    input += "\x1b"
                             "c";
                    input += decodeHex(encoded);
                    input += "\x1b[6n";
                    ++count;
                }
                if (!count) {
                    throw std::runtime_error("empty width measurement");
                }
                // Reports are taken from the in-process write capture (see
                // READ_INPUT): the kernel pty path is asynchronous and the
                // reports would double-report through a later READ_INPUT.
                drainInput(io[1]);
                terminalPty.takeWriteData();
                terminal.feedPtyOutput((const u8*)input.data(), input.size());
                for (int attempt = 0; attempt < 1000 && !terminal.flushPtyOutput(); ++attempt) {
                    drainInput(io[1]);
                }
                drainInput(io[1]);
                writeAll(controlFd, "OK " + encodeHex(terminalPty.takeWriteData()) + "\n");
            } else if (line == "OPTIONS") {
                const auto packedColor = [](Color color) {
                    return ((u32)(color.red) << 16) | ((u32)(color.green) << 8) | color.blue;
                };
                writeAll(controlFd, "OK fontsize=" + std::to_string(opts.fontsize) + " border=" + std::to_string(opts.border) + " columns=" + std::to_string(opts.nCols) + " rows=" + std::to_string(opts.nRows) + " save_lines=" + std::to_string(opts.saveLines) + " fg=" + std::to_string(packedColor(opts.fg)) + " bg=" + std::to_string(packedColor(opts.bg)) + " cr=" + std::to_string(packedColor(opts.cr)) + " alt_scroll=" + std::to_string(opts.altScrollMode) + " bold_colors=" + std::to_string(opts.boldColors) + " auto_copy=" + std::to_string(opts.autoCopyMode) + " allow_osc52_read=" + std::to_string(opts.allowOsc52Read) + " allow_window_ops=" + std::to_string(opts.allowWindowOps) + "\n");
            } else if (line == "ARGV") {
                std::string arguments;
                for (int index = 0; index < argc; ++index) {
                    if (index) {
                        arguments.push_back('\0');
                    }
                    arguments += argv[index];
                }
                writeAll(controlFd, "OK " + encodeHex(arguments) + "\n");
            } else if (line == "LAUNCH_COMMAND") {
                const LaunchCommand command = buildLaunchCommand(argc, argv, opts.shell, opts.login);
                std::string encoded = command.executable;
                for (const auto& argument : command.arguments) {
                    encoded.push_back('\0');
                    encoded += argument;
                }
                writeAll(controlFd, "OK " + encodeHex(encoded) + "\n");
            } else if (line.compare(0, 10, "FONT_LOAD ") == 0) {
                const std::string request = decodeHex(line.substr(10));
                const size_t first = request.find('\0');
                if (first == std::string::npos) {
                    throw std::runtime_error("invalid font load request");
                }
                ObjPool::Ref fontPool = ObjPool::fromMemory();
                const StringView fontname((const u8*)(request.data()), first);
                const StringView dwfontname((const u8*)(request.data() + first + 1), request.size() - first - 1);
                Fontpack* fonts = Fontpack::create(composer, *fontPool, fontname, dwfontname, opts.fontsize);
                writeAll(controlFd, "OK " + std::to_string(fonts->getPx()) + " " + std::to_string(fonts->getPy()) + " " + std::to_string(fonts->hasBold()) + " " + std::to_string(fonts->hasItalic()) + " " + std::to_string(fonts->hasBoldItalic()) + " " + std::to_string(fonts->hasDoubleWidth()) + "\n");
            } else if (line.compare(0, 13, "RENDER_IMAGE ") == 0) {
                const std::string request = decodeHex(line.substr(13));
                const size_t first = request.find('\0');
                if (first == std::string::npos) {
                    throw std::runtime_error("invalid render image request");
                }
                ObjPool::Ref renderPool = ObjPool::fromMemory();
                Composer& renderComposer = *renderPool->make<Composer>(renderPool.mutPtr());
                const StringView fontname((const u8*)(request.data()), first);
                const StringView dwfontname((const u8*)(request.data() + first + 1), request.size() - first - 1);
                Fontpack* fonts = Fontpack::create(renderComposer, *renderPool, fontname, dwfontname, opts.fontsize);
                renderComposer.fonts = fonts;
                renderComposer.setCellExtras(composer.cellExtras);
                renderComposer.setGlyphSize(fonts->getPx(), fonts->getPy());
                const u16 imageWidth = 2 * opts.border + renderer.columns() * fonts->getPx();
                const u16 imageHeight = 2 * opts.border + renderer.rows() * fonts->getPy();
                renderComposer.resize(imageWidth, imageHeight);
                renderComposer.platform = plt::createHeadlessPlatform(*renderPool);
                const TerminalUpdate imageUpdate = renderer.renderUpdate();

                struct ImageFrame final: plt::FrameCallback {
                    bool frame(const plt::WindowInfo&) override {
                        return renderer->update(*update);
                    }

                    Renderer* renderer = nullptr;
                    const TerminalUpdate* update = nullptr;
                } imageFrame;

                renderComposer.window = renderComposer.platform->createWindow(
                    *renderPool,
                    {
                        .width = imageWidth,
                        .height = imageHeight,
                        .frame = &imageFrame,
                    }
                );
                auto& imageWindow = static_cast<plt::WindowHeadless&>(*renderComposer.window);
                imageFrame.renderer = Renderer::create(renderComposer, *renderPool, imageWindow.renderContext());
                imageFrame.update = &imageUpdate;
                imageWindow.requestFrame();
                if (!imageWindow.dispatchFrame()) {
                    throw std::runtime_error("reference image presentation failed");
                }
                const plt::HeadlessFrame image = imageWindow.presentedFrame();
                const std::string pixels((const char*)(image.pixels), image.length);
                writeAll(controlFd, "OK " + std::to_string(image.width) + " " + std::to_string(image.height) + " " + encodeHex(pixels) + "\n");
            } else if (line.compare(0, 16, "GRAPHEME_BREAKS ") == 0) {
                std::istringstream args(line.substr(16));
                std::string token;
                std::string boundaries;
                GraphemeBreaker breaker;
                while (args >> token) {
                    size_t consumed = 0;
                    const unsigned long value = std::stoul(token, &consumed, 16);
                    if (consumed != token.size() || value > 0x10ffff) {
                        throw std::runtime_error("invalid codepoint");
                    }
                    boundaries += breaker.breakBefore(value) ? '1' : '0';
                }
                if (boundaries.empty()) {
                    throw std::runtime_error("empty grapheme sequence");
                }
                writeAll(controlFd, "OK " + boundaries + "\n");
            } else if (line.compare(0, 8, "PREEDIT ") == 0) {
                std::istringstream args(line.substr(8));
                std::string encoded;
                i32 begin = -1;
                i32 end = -1;
                args >> encoded >> begin >> end;
                const std::string decoded = encoded == "-" ? std::string() : decodeHex(encoded);
                terminal.preedit(StringView((const u8*)(decoded.data()), decoded.size()), begin, end);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 6, "INPUT ") == 0) {
                const std::string input = decodeHex(line.substr(6));
                const std::u8string bytes(input.begin(), input.end());
                terminal.writePty(bytes.data(), bytes.size(), false);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 6, "SPAWN ") == 0) {
                if (childPid > 0) {
                    throw std::runtime_error("child already running");
                }
                if (tcsetattr(io[1], TCSANOW, &childTtyAttrs) < 0) {
                    throw std::runtime_error("test child tcsetattr failed");
                }
                const std::string encoded = decodeHex(line.substr(6));
                std::vector<std::string> arguments;
                size_t start = 0;
                while (start < encoded.size()) {
                    const size_t end = encoded.find('\0', start);
                    arguments.push_back(encoded.substr(start, end == std::string::npos ? std::string::npos : end - start));
                    if (end == std::string::npos) {
                        break;
                    }
                    start = end + 1;
                }
                if (arguments.empty() || arguments[0].empty()) {
                    throw std::runtime_error("empty child command");
                }
                const char* ttyPath = ttyname(io[1]);
                if (!ttyPath) {
                    throw std::runtime_error("test child tty has no path");
                }
                const std::string childTtyPath = ttyPath;
                childExitStatus = -1;
                childPid = fork();
                if (childPid < 0) {
                    throw std::runtime_error("test fork failed");
                }
                if (childPid == 0) {
                    setsid();
                    close(io[1]);
                    const int childTty = open(childTtyPath.c_str(), O_RDWR);
                    if (childTty < 0) {
                        _exit(126);
                    }
                    ioctl(childTty, TIOCSCTTY, 0);
                    dup2(childTty, STDIN_FILENO);
                    dup2(childTty, STDOUT_FILENO);
                    dup2(childTty, STDERR_FILENO);
                    close(io[0]);
                    if (childTty > STDERR_FILENO) {
                        close(childTty);
                    }
                    configureTerminalChildEnvironment();
                    std::vector<char*> argv;
                    for (auto& argument : arguments) {
                        argv.push_back(argument.data());
                    }
                    argv.push_back(nullptr);
                    execvp(argv[0], argv.data());
                    _exit(127);
                }
                writeAll(controlFd, "OK\n");
            } else if (line == "PUMP") {
                pumpChild();
                writeAll(controlFd, "OK\n");
            } else if (line == "READ_PTY") {
                writeAll(controlFd, "OK " + std::to_string(terminal.readPty()) + "\n");
            } else if (line == "READ_CHILD_OUTPUT") {
                writeAll(controlFd, "OK " + encodeHex(terminalPty.takeReadData()) + "\n");
            } else if (line.compare(0, 16, "PTY_READ_SCRIPT ") == 0) {
                scriptedPtyReads.clear();
                std::istringstream args(line.substr(16));
                std::string token;
                while (args >> token) {
                    if (token == "z") {
                        scriptedPtyReads.push_back({"", 0, true});
                    } else if (token.size() > 1 && token[0] == 'd') {
                        scriptedPtyReads.push_back({decodeHex(token.substr(1)), 0, false});
                    } else if (token.size() > 1 && token[0] == 'e') {
                        size_t consumed = 0;
                        const int error = std::stoi(token.substr(1), &consumed);
                        if (consumed != token.size() - 1 || error <= 0) {
                            throw std::runtime_error("invalid PTY errno");
                        }
                        scriptedPtyReads.push_back({"", error, false});
                    } else {
                        throw std::runtime_error("invalid PTY read script");
                    }
                }
                if (scriptedPtyReads.empty()) {
                    throw std::runtime_error("empty PTY read script");
                }
                installScriptedPtyReader();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 16, "PTY_READ_REPEAT ") == 0) {
                std::istringstream args(line.substr(16));
                unsigned byte;
                size_t count;
                int eof;
                if (!(args >> byte >> count >> eof) || byte > 255 || count == 0 || count > 64 * 1024 * 1024 || eof < 0 || eof > 1) {
                    throw std::runtime_error("invalid repeated PTY input");
                }
                scriptedPtyReads.clear();
                scriptedPtyReads.push_back({std::string(count, (char)(byte)), 0, false});
                if (eof) {
                    scriptedPtyReads.push_back({"", 0, true});
                }
                installScriptedPtyReader();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 17, "PTY_WRITE_SCRIPT ") == 0) {
                scriptedPtyWrites.clear();
                writtenPtyData.clear();
                std::istringstream args(line.substr(17));
                std::string token;
                while (args >> token) {
                    size_t consumed = 0;
                    if (token.size() > 1 && token[0] == 'n') {
                        const unsigned long count = std::stoul(token.substr(1), &consumed);
                        if (consumed != token.size() - 1 || count == 0) {
                            throw std::runtime_error("invalid PTY write count");
                        }
                        scriptedPtyWrites.push_back({count, 0});
                    } else if (token.size() > 1 && token[0] == 'e') {
                        const int error = std::stoi(token.substr(1), &consumed);
                        if (consumed != token.size() - 1 || error <= 0) {
                            throw std::runtime_error("invalid PTY write errno");
                        }
                        scriptedPtyWrites.push_back({0, error});
                    } else {
                        throw std::runtime_error("invalid PTY write script");
                    }
                }
                if (scriptedPtyWrites.empty()) {
                    throw std::runtime_error("empty PTY write script");
                }
                terminalPty.setWriteHandler([&scriptedPtyWrites, &writtenPtyData](const u8* buffer, size_t size) {
                    if (scriptedPtyWrites.empty()) {
                        errno = EAGAIN;
                        return (ssize_t)(-1);
                    }
                    const auto item = scriptedPtyWrites.front();
                    scriptedPtyWrites.pop_front();
                    if (item.error) {
                        errno = item.error;
                        return (ssize_t)(-1);
                    }
                    const size_t count = std::min(size, item.count);
                    writtenPtyData.append((const char*)(buffer), count);
                    return (ssize_t)(count);
                });
                writeAll(controlFd, "OK\n");
            } else if (line == "WAIT_READ_PTY") {
                pollfd source{io[0], POLLIN, 0};
                int ready = 0;
                do {
                    ready = poll(&source, 1, 1000);
                } while (ready < 0 && errno == EINTR);
                if (ready <= 0 || !(source.revents & POLLIN)) {
                    throw std::runtime_error("PTY input timeout");
                }
                terminal.readPty();
                writeAll(controlFd, "OK\n");
            } else if (line == "FAIL_NEXT_PRESENT") {
                window.failNextPresentation();
                writeAll(controlFd, "OK\n");
            } else if (line == "FAIL_NEXT_FONT_CHANGE") {
                failFontChange.arm();
                writeAll(controlFd, "OK\n");
            } else if (line == "PRESENT") {
                terminal.redraw();
                writeAll(controlFd, "OK\n");
            } else if (line == "GPU_ATTRIBUTE_MASKS") {
                TerminalCell cell{};
                cell.dwidth = true;
                const u32 doubleWidth = Renderer::cellAttributes(cell);
                cell.dwidth = false;
                cell.dwidth_cont = true;
                const u32 continuation = Renderer::cellAttributes(cell);
                writeAll(controlFd, "OK " + std::to_string(doubleWidth) + " " + std::to_string(continuation) + "\n");
            } else if (line == "POLL_CHILD") {
                pumpChild();
                writeAll(controlFd, "OK " + std::to_string(childPid > 0) + " " + std::to_string(childExitStatus) + " " + encodeHex(renderer.screenText()) + "\n");
            } else if (line == "CHILD_STATUS") {
                writeAll(controlFd, "OK " + std::to_string(childPid > 0) + " " + std::to_string(childExitStatus) + "\n");
            } else if (line == "PAGE_UP") {
                terminal.pageUp();
                writeAll(controlFd, "OK\n");
            } else if (line == "PAGE_DOWN") {
                terminal.pageDown();
                writeAll(controlFd, "OK\n");
            } else if (line == "WHEEL_UP") {
                terminal.mouseWheelUp();
                writeAll(controlFd, "OK\n");
            } else if (line == "WHEEL_DOWN") {
                terminal.mouseWheelDown();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 7, "SCROLL ") == 0) {
                std::istringstream args(line.substr(7));
                double x;
                double y;
                unsigned modifiers;
                int pixelX;
                int pixelY;
                if (!(args >> x >> y >> modifiers >> pixelX >> pixelY) || modifiers > 7) {
                    throw std::runtime_error("invalid scroll event");
                }
                composer.input->scroll({x, y, pixelX, pixelY, (u16)(modifiers)});
                terminal.update();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 8, "POINTER ") == 0) {
                std::istringstream args(line.substr(8));
                double x, y, scaleX, scaleY;
                unsigned modifiers;
                if (!(args >> x >> y >> modifiers >> scaleX >> scaleY) || modifiers > 7) {
                    throw std::runtime_error("invalid pointer event");
                }
                const int pixelX = mouseFramebufferCoordinate(x, scaleX);
                const int pixelY = mouseFramebufferCoordinate(y, scaleY);
                composer.input->pointerMotion({pixelX, pixelY, (u16)(modifiers)});
                terminal.update();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 7, "BUTTON ") == 0) {
                std::istringstream args(line.substr(7));
                int button;
                unsigned pressed, modifiers;
                double x, y, time, scaleX, scaleY;
                if (!(args >> button >> pressed >> x >> y >> modifiers >> time >> scaleX >> scaleY) || button < 0 || button > 7 || pressed > 1 || modifiers > 7) {
                    throw std::runtime_error("invalid button event");
                }
                const int pixelX = mouseFramebufferCoordinate(x, scaleX);
                const int pixelY = mouseFramebufferCoordinate(y, scaleY);
                const u64 clipboardGeneration = clipboard.generation;
                composer.input->pointerButton({(PointerButton)(button), pressed != 0, pixelX, pixelY, (u16)(modifiers), time});
                terminal.update();
                std::string selection;
                if (clipboard.generation != clipboardGeneration) {
                    const StringView content(clipboard.primary);
                    selection.assign((const char*)(content.data()), content.length());
                }
                writeAll(controlFd, "OK " + encodeHex(selection) + "\n");
            } else if (line.compare(0, 7, "RESIZE ") == 0) {
                std::istringstream args(line.substr(7));
                unsigned columns;
                unsigned rows;
                if (!(args >> columns >> rows) || !columns || !rows) {
                    throw std::runtime_error("invalid resize");
                }
                terminal.resize(2 * opts.border + columns * composer.glyphWidth, 2 * opts.border + rows * composer.glyphHeight);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 14, "RESIZE_PIXELS ") == 0) {
                std::istringstream args(line.substr(14));
                unsigned pixelWidth;
                unsigned pixelHeight;
                if (!(args >> pixelWidth >> pixelHeight) || pixelWidth <= 2 * opts.border || pixelHeight <= 2 * opts.border) {
                    throw std::runtime_error("invalid pixel resize");
                }
                terminal.resize(pixelWidth, pixelHeight);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 12, "WINDOW_INFO ") == 0) {
                std::istringstream args(line.substr(12));
                i64 x;
                i64 y;
                u64 pixelWidth;
                u64 pixelHeight;
                u64 screenWidth;
                u64 screenHeight;
                unsigned iconified;
                unsigned maximized;
                unsigned fullscreen;
                if (!(args >> x >> y >> pixelWidth >> pixelHeight >> screenWidth >> screenHeight >> iconified >> maximized >> fullscreen) || x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX || pixelWidth > UINT16_MAX || pixelHeight > UINT16_MAX || screenWidth > UINT32_MAX || screenHeight > UINT32_MAX || iconified > 1 || maximized > 1 || fullscreen > 1) {
                    throw std::runtime_error("invalid window info");
                }
                plt::WindowInfo info = window.info();
                info.x = x;
                info.y = y;
                info.width = pixelWidth;
                info.height = pixelHeight;
                info.screenPixelWidth = screenWidth;
                info.screenPixelHeight = screenHeight;
                info.iconified = iconified;
                info.maximized = maximized;
                info.fullscreen = fullscreen;
                window.configure(info);
                terminal.update();
                writeAll(controlFd, "OK\n");
            } else if (line == "WINSIZE") {
                winsize size{};
                if (ioctl(io[0], TIOCGWINSZ, &size) < 0) {
                    throw std::runtime_error("test TIOCGWINSZ failed");
                }
                writeAll(controlFd, "OK " + std::to_string(size.ws_col) + " " + std::to_string(size.ws_row) + "\n");
            } else if (line == "WINSIZE_FULL") {
                winsize size{};
                if (ioctl(io[0], TIOCGWINSZ, &size) < 0) {
                    throw std::runtime_error("test TIOCGWINSZ failed");
                }
                writeAll(controlFd, "OK " + std::to_string(size.ws_col) + " " + std::to_string(size.ws_row) + " " + std::to_string(size.ws_xpixel) + " " + std::to_string(size.ws_ypixel) + "\n");
            } else if (line == "FONT_STATE") {
                StringBuilder output;
                output << StringView(u8"OK ") << composer.fontSize << StringView(u8" ") << composer.glyphWidth << StringView(u8" ") << composer.glyphHeight << StringView(u8" ") << composer.pixelWidth << StringView(u8" ") << composer.pixelHeight << StringView(u8" ") << composer.columns << StringView(u8" ") << composer.rows << StringView(u8" ") << (unsigned)(composer.contentScale * 1000.0f + 0.5f) << StringView(u8" ") << opts.border << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line == "LAST_UPDATE") {
                writeAll(controlFd, renderer.lastUpdate());
            } else if (line == "LAST_UPDATE_ROWS") {
                writeAll(controlFd, renderer.lastUpdateRows());
            } else if (line.compare(0, 15, "FRONTEND_SCALE ") == 0) {
                unsigned xNumerator = 0;
                unsigned xDenominator = 0;
                unsigned yNumerator = 0;
                unsigned yDenominator = 0;
                char trailing = 0;
                if (sscanf(line.c_str() + 15, "%u %u %u %u %c", &xNumerator, &xDenominator, &yNumerator, &yDenominator, &trailing) != 4 || xNumerator == 0 || xDenominator == 0 || yNumerator == 0 || yDenominator == 0 || xNumerator > 10000 || xDenominator > 10000 || yNumerator > 10000 || yDenominator > 10000) {
                    Errno(EINVAL).raise(StringView(u8"invalid frontend scale"));
                }
                plt::WindowInfo info = window.info();
                info.contentScale = std::max((float)(xNumerator) / xDenominator, (float)(yNumerator) / yDenominator);
                window.configure(info);
                terminal.update();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 4, "KEY ") == 0) {
                std::istringstream args(line.substr(4));
                std::string name;
                unsigned modifiers;
                if (!(args >> name >> modifiers) || modifiers > 7) {
                    throw std::runtime_error("invalid key");
                }
                terminal.writePty(parseKey(name), (VtModifier)(modifiers), true);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 5, "CHAR ") == 0) {
                std::istringstream args(line.substr(5));
                unsigned character;
                unsigned modifiers;
                if (!(args >> character >> modifiers) || character > 255 || modifiers > 7) {
                    throw std::runtime_error("invalid char");
                }
                terminal.writePty((u8)(character), (VtModifier)(modifiers), true);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 18, "CONTROL_CHARACTER ") == 0) {
                std::istringstream args(line.substr(18));
                int key;
                unsigned shifted;
                u8 character = 0;
                if (!(args >> key >> shifted) || shifted > 1 || !controlCharacter(key, shifted, character)) {
                    throw std::runtime_error("invalid control character");
                }
                writeAll(controlFd, "OK " + std::to_string(character) + "\n");
            } else if (line.compare(0, 17, "FRONTEND_CONTROL ") == 0) {
                std::istringstream args(line.substr(17));
                int key;
                unsigned shifted;
                unsigned alt;
                u8 character = 0;
                if (!(args >> key >> shifted >> alt) || shifted > 1 || alt > 1 || !controlCharacter(key, shifted, character)) {
                    throw std::runtime_error("invalid frontend control");
                }
                VtModifier modifiers = VtModifier::control;
                if (shifted) {
                    modifiers = modifiers | VtModifier::shift;
                }
                if (alt) {
                    modifiers = modifiers | VtModifier::alt;
                }
                terminal.writePty(character, modifiers, true);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 19, "FRONTEND_KEY_EVENT ") == 0) {
                std::istringstream args(line.substr(19));
                int key;
                int scancode;
                int action;
                int modifiers;
                if (!(args >> key >> scancode >> action >> modifiers) || action < 0 || action > 2 || modifiers < 0) {
                    throw std::runtime_error("invalid frontend key event");
                }
                input.key(key, scancode, action, modifiers);
                terminal.update();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 20, "FRONTEND_TEXT_EVENT ") == 0) {
                std::istringstream args(line.substr(20));
                unsigned codepoint;
                int modifiers;
                if (!(args >> codepoint >> modifiers) || codepoint > 0x10ffff || modifiers < 0) {
                    throw std::runtime_error("invalid frontend text event");
                }
                input.text(codepoint, modifiers);
                terminal.update();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 10, "KITTY_KEY ") == 0) {
                std::istringstream args(line.substr(10));
                u32 key;
                u32 shifted;
                u32 base;
                unsigned modifiers;
                unsigned event;
                if (!(args >> key >> shifted >> base >> modifiers >> event) || event < 1 || event > 3) {
                    throw std::runtime_error("invalid kitty key");
                }
                terminal.writeKittyKey(key, shifted, base, modifiers, (VtermKeyEventType)(event));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 14, "KITTY_SPECIAL ") == 0) {
                std::istringstream args(line.substr(14));
                std::string name;
                unsigned modifiers;
                unsigned event;
                if (!(args >> name >> modifiers >> event) || event < 1 || event > 3) {
                    throw std::runtime_error("invalid kitty special key");
                }
                terminal.writeKittyKey(parseKey(name), modifiers, (VtermKeyEventType)(event));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 6, "PASTE ") == 0) {
                terminal.pasteSelection(decodeHex(line.substr(6)));
                writeAll(controlFd, "OK\n");
            } else if (line == "PASTE_CLIPBOARD 0" || line == "PASTE_CLIPBOARD 1") {
                writeAll(controlFd, testApi.pasteClipboard(line.back() == '1') ? "OK 1\n" : "OK 0\n");
            } else if (line.compare(0, 6, "FOCUS ") == 0) {
                terminal.setHasFocus(line.substr(6) == "1");
                writeAll(controlFd, "OK\n");
            } else if (line == "POINTER_PRESENCE 0" || line == "POINTER_PRESENCE 1") {
                composer.input->pointerPresence(line.back() == '1');
                terminal.update();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 18, "HIGHLIGHT_RELEASE ") == 0) {
                std::istringstream args(line.substr(18));
                unsigned endX, endY, mouseX, mouseY;
                if (!(args >> endX >> endY >> mouseX >> mouseY)) {
                    throw std::runtime_error("invalid highlight release");
                }
                terminal.mouseHighlightRelease(endX, endY, mouseX, mouseY);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 17, "LOCATOR_POSITION ") == 0) {
                std::istringstream args(line.substr(17));
                unsigned column, row, pixelX, pixelY, buttons;
                if (!(args >> column >> row >> pixelX >> pixelY >> buttons)) {
                    throw std::runtime_error("invalid locator position");
                }
                terminal.setLocatorPosition(column, row, pixelX, pixelY, buttons);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 15, "LOCATOR_BUTTON ") == 0) {
                std::istringstream args(line.substr(15));
                unsigned button, pressed;
                if (!(args >> button >> pressed)) {
                    throw std::runtime_error("invalid locator button");
                }
                terminal.reportLocatorButton(button, pressed != 0);
                writeAll(controlFd, "OK\n");
            } else if (line == "SYNC_TIMEOUT") {
                terminal.expireSynchronizedOutput(true);
                writeAll(controlFd, "OK\n");
            } else if (line == "BLINK_TICK") {
                if (terminal.advanceAnimation(true)) {
                    terminal.redraw();
                }
                writeAll(controlFd, "OK\n");
            } else if (line == "SELECTION_AUTOSCROLL_TICK") {
                terminal.advanceSelectionAutoscroll();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 13, "SELECT_START ") == 0 || line.compare(0, 14, "SELECT_EXTEND ") == 0 || line.compare(0, 14, "SELECT_UPDATE ") == 0) {
                const bool start = line.compare(0, 13, "SELECT_START ") == 0;
                const bool extend = line.compare(0, 14, "SELECT_EXTEND ") == 0;
                std::istringstream args(line.substr(start ? 13 : 14));
                int column;
                int row;
                if (!(args >> column >> row)) {
                    throw std::runtime_error("invalid selection point");
                }
                unsigned cycle = 0;
                if ((start || extend) && args >> cycle && cycle > 1) {
                    throw std::runtime_error("invalid selection cycle");
                }
                if (start) {
                    terminal.selectStart(opts.border + column * composer.glyphWidth, opts.border + row * composer.glyphHeight, cycle != 0);
                } else if (extend) {
                    terminal.selectExtend(opts.border + column * composer.glyphWidth, opts.border + row * composer.glyphHeight, cycle != 0);
                } else {
                    terminal.selectUpdate(opts.border + column * composer.glyphWidth, opts.border + row * composer.glyphHeight);
                }
                writeAll(controlFd, "OK\n");
            } else if (line == "SELECT_RECTANGULAR") {
                terminal.selectRectangularModeToggle();
                writeAll(controlFd, "OK\n");
            } else if (line == "SELECT_FINISH") {
                std::string selection;
                terminal.selectFinish(selection);
                writeAll(controlFd, "OK " + encodeHex(selection) + "\n");
            } else if (line.compare(0, 10, "HYPERLINK ") == 0) {
                std::istringstream args(line.substr(10));
                int column;
                int row;
                if (!(args >> column >> row)) {
                    throw std::runtime_error("invalid hyperlink point");
                }
                writeAll(controlFd, "OK " + encodeHex(terminal.getHyperlink(opts.border + column, opts.border + row)) + "\n");
            } else if (line == "HYPERLINK_COUNT") {
                writeAll(controlFd, "OK " + std::to_string(terminal.getHyperlinkCount()) + "\n");
            } else if (line == "DESKTOP_STATE") {
                StringBuilder output;
                output << StringView(u8"OK ") << (unsigned)(desktopActions.icon) << StringView(u8" ") << desktopActions.openCount << StringView(u8" ") << renderer.hoveredHyperlink() << StringView(u8" ") << renderer.hoveredLinkBegin() << StringView(u8" ") << renderer.hoveredLinkEnd() << StringView(u8" ");
                if (desktopActions.openedUri.empty()) {
                    output << StringView(u8"-");
                } else {
                    appendHex(output, StringView(desktopActions.openedUri));
                }
                output << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line == "READ_ACTIONS") {
                writeAll(controlFd, "OK " + encodeHex(vtermTrace.drainActions()) + "\n");
            } else if (line == "STATE") {
                const auto& mouse = terminal.getMouseTrackingState();
                writeAll(controlFd, "OK " + std::to_string((unsigned)(mouse.mode)) + " " + std::to_string((unsigned)(mouse.enc)) + " " + std::to_string(mouse.focusEventMode) + " " + std::to_string(terminal.getKittyKeyboardFlags()) + "\n");
            } else if (line == "PROTOCOL_STATE") {
                writeAll(controlFd, "OK " + std::to_string(terminal.getScreenReverseVideo()) + " " + std::to_string(terminal.getLedState()) + " " + std::to_string(terminal.getReverseWrapMode()) + " " + std::to_string(terminal.getNationalReplacementMode()) + " 0\n");
            } else if (line == "CURSOR_STATE") {
                writeAll(controlFd, "OK " + std::to_string(terminal.getPrivateMode(25)) + " " + std::to_string(terminal.getPrivateMode(12)) + " " + std::to_string((unsigned)(terminal.getCursorStyle())) + "\n");
            } else if (line == "CURSOR_PENDING_WRAP") {
                writeAll(controlFd, "OK " + std::to_string(terminal.getPendingWrap()) + "\n");
            } else if (line == "CURSOR_AT_PROMPT") {
                writeAll(controlFd, "OK " + std::to_string(terminal.cursorIsAtPrompt()) + "\n");
            } else if (line == "SEMANTIC_CLICK") {
                writeAll(controlFd, "OK " + std::to_string(terminal.getSemanticClick()) + "\n");
            } else if (line.compare(0, 13, "ROW_SEMANTIC ") == 0) {
                std::istringstream args(line.substr(13));
                int row;
                if (!(args >> row)) {
                    throw std::runtime_error("invalid semantic row");
                }
                writeAll(controlFd, "OK " + std::to_string(terminal.getRowSemantic(row)) + "\n");
            } else if (line.compare(0, 9, "TAB_STOP ") == 0) {
                std::istringstream args(line.substr(9));
                unsigned column;
                if (!(args >> column) || column > 65535) {
                    throw std::runtime_error("invalid tab stop column");
                }
                writeAll(controlFd, "OK " + std::to_string(terminal.getTabStop(column)) + "\n");
            } else if (line.compare(0, 10, "TAB_STOPS ") == 0) {
                std::istringstream args(line.substr(10));
                unsigned parsedColumns;
                if (!(args >> parsedColumns) || parsedColumns > 65535) {
                    throw std::runtime_error("invalid tab stop columns");
                }
                const u16 columns = parsedColumns;
                StringBuilder output;
                output << StringView(u8"OK ");
                for (u16 column = 0; column < columns; ++column) {
                    output << (terminal.getTabStop(column) ? StringView(u8"1") : StringView(u8"0"));
                }
                output << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line.compare(0, 12, "SET_WRAPPED ") == 0) {
                std::istringstream args(line.substr(12));
                unsigned row;
                if (!(args >> row) || row > 65535) {
                    throw std::runtime_error("invalid wrapped row");
                }
                terminal.setWrapped((u16)(row));
                writeAll(controlFd, "OK\n");
            } else if (line == "CONFORMANCE_STATE") {
                StringBuilder output;
                output << StringView(u8"OK screen=") << (terminal.getPrivateMode(47) ? StringView(u8"Alternate") : StringView(u8"Primary")) << StringView(u8" IRM=") << (unsigned)(terminal.getAnsiMode(4)) << StringView(u8" SRM=") << (unsigned)(terminal.getAnsiMode(12)) << StringView(u8" LNM=") << (unsigned)(terminal.getAnsiMode(20)) << StringView(u8" DECCKM=") << (unsigned)(terminal.getPrivateMode(1)) << StringView(u8" DECCOLM=") << (unsigned)(terminal.getPrivateMode(3)) << StringView(u8" DECSCLM=") << (unsigned)(terminal.getPrivateMode(4)) << StringView(u8" DECSCNM=") << (unsigned)(terminal.getPrivateMode(5)) << StringView(u8" DECOM=") << (unsigned)(terminal.getPrivateMode(6)) << StringView(u8" DECAWM=") << (unsigned)(terminal.getPrivateMode(7)) << StringView(u8" DECARM=") << (unsigned)(terminal.getPrivateMode(8)) << StringView(u8" DECTCEM=") << (unsigned)(terminal.getPrivateMode(25)) << StringView(u8" DECNKM=") << (unsigned)(terminal.getPrivateMode(66)) << StringView(u8" DECBKM=") << (unsigned)(terminal.getPrivateMode(67)) << StringView(u8" DECLRMM=") << (unsigned)(terminal.getPrivateMode(69)) << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line == "RECTANGLE_ORIGIN") {
                const RectangleOrigin origin = terminal.getRectangleOrigin();
                writeAll(controlFd, "OK " + std::to_string(origin.rowBase) + " " + std::to_string(origin.columnBase) + " " + std::to_string(origin.rowLimit) + " " + std::to_string(origin.columnLimit) + "\n");
            } else if (line == "PEN_STATE") {
                const TerminalPen pen = terminal.getPenState();
                StringBuilder output;
                output << StringView(u8"OK ") << cellFlags(pen.cell) << StringView(u8" ") << (unsigned)(pen.fg.red) << StringView(u8" ") << (unsigned)(pen.fg.green) << StringView(u8" ") << (unsigned)(pen.fg.blue) << StringView(u8" ") << (unsigned)(pen.bg.red) << StringView(u8" ") << (unsigned)(pen.bg.green) << StringView(u8" ") << (unsigned)(pen.bg.blue) << StringView(u8" ") << pen.cell.foreground().legacyIndex() << StringView(u8" ") << pen.cell.background().legacyIndex() << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line == "PARSER_TRACE_ON") {
                vtermTrace.clear();
                writeAll(controlFd, "OK\n");
            } else if (line == "PARSER_TRACE_CLEAR") {
                vtermTrace.clear();
                writeAll(controlFd, "OK\n");
            } else if (line == "READ_PARSER_TRACE") {
                writeAll(controlFd, "OK " + encodeHex(vtermTrace.drain()) + "\n");
            } else if (line.compare(0, 10, "UTF8_PUSH ") == 0) {
                const auto codepoints = testUtf8Decoder.push(decodeHex(line.substr(10)));
                StringBuilder output;
                output << StringView(u8"OK");
                for (const u32 codepoint : codepoints) {
                    output << StringView(u8" ") << Hex{codepoint};
                }
                output << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line == "UTF8_FLUSH") {
                const auto codepoints = testUtf8Decoder.flush();
                StringBuilder output;
                output << StringView(u8"OK");
                for (const u32 codepoint : codepoints) {
                    output << StringView(u8" ") << Hex{codepoint};
                }
                output << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line == "UTF8_RESET") {
                testUtf8Decoder.reset();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 17, "CODEPOINT_WIDTHS ") == 0) {
                std::istringstream args(line.substr(17));
                std::string token;
                StringBuilder output;
                output << StringView(u8"OK");
                size_t count = 0;
                while (args >> token) {
                    size_t consumed = 0;
                    const unsigned long codepoint = std::stoul(token, &consumed, 16);
                    if (consumed != token.size() || codepoint > 0x10ffff) {
                        throw std::runtime_error("invalid codepoint");
                    }
                    output << StringView(u8" ") << codepointWidth((u32)(codepoint));
                    ++count;
                }
                if (!count) {
                    throw std::runtime_error("empty codepoint width request");
                }
                output << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line == "RENDER_STATE") {
                writeAll(controlFd, renderer.renderState());
            } else if (line == "SELECTION_STATE") {
                writeAll(controlFd, renderer.selectionState());
            } else if (line == "CHARSET_STATE") {
                const VtermTestState state = testApi.inspect();
                writeAll(controlFd, "OK " + std::to_string(state.charsets[0]) + " " + std::to_string(state.charsets[1]) + " " + std::to_string(state.charsets[2]) + " " + std::to_string(state.charsets[3]) + "\n");
            } else if (line.compare(0, 13, "MOUSE_ENCODE ") == 0) {
                std::istringstream args(line.substr(13));
                unsigned encoding;
                unsigned type;
                unsigned modifiers;
                int motionButton;
                int button;
                int column;
                int row;
                if (!(args >> encoding >> type >> modifiers >> motionButton >> button >> column >> row) || encoding > 4 || type > 2) {
                    throw std::runtime_error("invalid mouse event");
                }
                writeAll(controlFd, "OK " + encodeHex(encodeMouseProtocol((MouseTrackingEnc)(encoding), (MouseEventType)(type), modifiers, motionButton, button, column, row)) + "\n");
            } else if (line.compare(0, 12, "SET_PRIMARY ") == 0) {
                const size_t separator = line.find(' ', 12);
                if (separator == std::string::npos) {
                    throw std::runtime_error("invalid primary selection");
                }
                const int autoCopy = std::stoi(line.substr(12, separator - 12));
                if (autoCopy < 0 || autoCopy > 1) {
                    throw std::runtime_error("invalid auto-copy state");
                }
                const std::string content = decodeHex(line.substr(separator + 1));
                const StringView selection((const u8*)(content.data()), content.size());
                clipboard.writePrimary(selection);
                if (autoCopy) {
                    clipboard.writeClipboard(selection);
                }
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 11, "SET_SYSTEM ") == 0) {
                const std::string content = decodeHex(line.substr(11));
                clipboard.writeClipboard(StringView((const u8*)(content.data()), content.size()));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 20, "SET_CLIPBOARD_CHUNK ") == 0) {
                const unsigned long long size = std::stoull(line.substr(20));
                if (size > SIZE_MAX) {
                    throw std::runtime_error("invalid clipboard chunk size");
                }
                clipboard.readChunk = (size_t)(size);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 14, "GET_SELECTION ") == 0) {
                const int primary = std::stoi(line.substr(14));
                if (primary < 0 || primary > 1) {
                    throw std::runtime_error("invalid selection kind");
                }
                const StringView content = primary ? StringView(clipboard.primary) : StringView(clipboard.system);
                writeAll(controlFd, "OK " + encodeHex(std::string((const char*)(content.data()), content.length())) + "\n");
            } else if (line == "GET_CWD") {
                StringBuilder output;
                output << StringView(u8"OK ");
                appendHex(output, vtermTrace.currentCwd());
                output << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line.compare(0, 9, "OSC7_CWD ") == 0) {
                const std::string input = "\x1b]7;" + decodeHex(line.substr(9)) + "\x1b\\";
                terminal.feedPtyOutput((const u8*)(input.data()), input.size());
                StringBuilder output;
                output << StringView(u8"OK ");
                appendHex(output, vtermTrace.currentCwd());
                output << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line.compare(0, 16, "PRESENTED_PIXEL ") == 0) {
                std::istringstream args(line.substr(16));
                u32 x = 0;
                u32 y = 0;
                if (!(args >> x >> y)) {
                    throw std::runtime_error("invalid presented pixel request");
                }
                const plt::HeadlessFrame image = window.presentedFrame();
                if (image.pixels == nullptr || image.format != plt::HeadlessPixelFormat::RGB8 || x >= image.width || y >= image.height) {
                    throw std::runtime_error("presented pixel unavailable");
                }
                const u8* const pixel = image.pixels + (size_t)(y)*image.stride + 3u * x;
                writeAll(controlFd, "OK " + std::to_string(pixel[0]) + " " + std::to_string(pixel[1]) + " " + std::to_string(pixel[2]) + "\n");
            } else if (line == "SNAPSHOT") {
                writeAll(controlFd, renderer.snapshot());
            } else if (line == "MODEL_SNAPSHOT") {
                writeAll(controlFd, renderer.modelSnapshot());
            } else if (line == "MODEL_DIGEST") {
                writeAll(controlFd, renderer.modelDigest());
            } else if (line == "SCROLLBACK_STATE") {
                writeAll(controlFd, renderer.scrollbackState());
            } else if (line == "SCREEN_TEXT") {
                writeAll(controlFd, "OK " + encodeHex(renderer.screenText()) + "\n");
            } else if (line == "ALL_TEXT") {
                const Buffer contents = terminal.allText();
                StringBuilder output;
                output << StringView(u8"OK ");
                appendHex(output, StringView(contents));
                output << StringView(u8"\n");
                writeAll(controlFd, StringView(output));
            } else if (line == "READ_INPUT") {
                // Responses are written from this very process, so the
                // authoritative "what was sent to the application" stream
                // is the capture taken at the write call itself.  Reading
                // it back through the kernel pty would race the
                // asynchronous master→slave delivery, and responses larger
                // than the pty buffer would never be visible to a single
                // opportunistic drain.  The slave queue is drained only to
                // unstick a stalled flush and to keep already-reported
                // bytes from reaching a later spawned child; a running
                // child owns the slave side.
                for (int attempt = 0; attempt < 1000 && !terminal.flushPtyOutput(); ++attempt) {
                    if (childPid > 0) {
                        break;
                    }
                    drainInput(io[1]);
                }
                if (childPid <= 0) {
                    drainInput(io[1]);
                }
                writeAll(controlFd, "OK " + encodeHex(terminalPty.takeWriteData()) + "\n");
            } else if (line == "FLUSH_OUTPUT") {
                terminal.flushPtyOutput();
                writeAll(controlFd, "OK\n");
            } else if (line == "FLUSH_OUTPUT_RESULT") {
                writeAll(controlFd, "OK " + std::to_string(terminal.flushPtyOutput()) + "\n");
            } else if (line == "READ_WRITTEN_PTY") {
                writeAll(controlFd, "OK " + encodeHex(writtenPtyData) + "\n");
                writtenPtyData.clear();
            } else if (line == "PENDING_SCRIPTED_PTY_READ_BYTES") {
                size_t count = 0;
                for (const auto& item : scriptedPtyReads) {
                    count += item.data.size();
                }
                writeAll(controlFd, "OK " + std::to_string(count) + "\n");
            } else if (line.compare(0, 12, "SERVICE_PTY ") == 0) {
                std::istringstream args(line.substr(12));
                int readable;
                int writable;
                if (!(args >> readable >> writable) || readable < 0 || readable > 1 || writable < 0 || writable > 1) {
                    throw std::runtime_error("invalid PTY service event");
                }
                writeAll(controlFd, "OK " + std::to_string(terminal.servicePty(readable, writable)) + "\n");
            } else if (line == "QUIT") {
                if (childPid > 0) {
                    kill(childPid, SIGKILL);
                    waitpid(childPid, nullptr, 0);
                    childPid = -1;
                }
                writeAll(controlFd, "OK\n");
                break;
            } else {
                writeAll(controlFd, "ERR unknown command\n");
            }
        } catch (Exception& error) {
            const StringView message = error.description();
            writeAll(controlFd, "ERR " + std::string((const char*)(message.data()), message.length()) + "\n");
        } catch (const std::exception& error) {
            writeAll(controlFd, std::string("ERR ") + error.what() + "\n");
        }
    }

    terminalPty.unlink();
    delete composer.ptyOutput;
    composer.ptyOutput = nullptr;
    composer.ptyOutputs = nullptr;
    composer.vterm = nullptr;
    composer.pty = nullptr;
    close(io[0]);
    close(io[1]);
    return 0;
}
