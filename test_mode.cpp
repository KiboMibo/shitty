/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#ifndef SHITTY_FOR_TESTS
    #error "test_mode.cpp must not be compiled into the production binary"
#endif

#include "test_mode.h"

#include "clipboard.h"
#include "composer.h"
#include "grapheme.h"
#include "font_resolver.h"
#include "font_pack.h"
#include "frame.h"
#include "keyboard.h"
#include "options.h"
#include "mouse_protocol.h"
#include "mouse_frontend.h"
#include "osc_protocol.h"
#include "pty.h"
#include "startup.h"
#include "utf8.h"
#include "vk_presenter.h"
#include "vterm.h"
#include "vterm_host.h"
#include "vterm_trace.h"

#include <algorithm>
#include <std/mem/obj_pool.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <functional>
#include <iomanip>
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

namespace stl {}

using namespace stl;

namespace {
    extern "C" int openpty(int*, int*, char*, const termios*, const winsize*);

    class TestPty final: public Pty {
    public:
        explicit TestPty(int fd);

        int fd() const override;
        ssize_t read(u8* buffer, size_t size) override;
        ssize_t write(const u8* buffer, size_t size) override;
        void resize(u16 columns, u16 rows) override;

        void setReadHandler(std::function<ssize_t(u8*, size_t)> handler);
        void setWriteHandler(std::function<ssize_t(const u8*, size_t)> handler);
        std::string takeReadData();

    private:
        int fd_;
        std::function<ssize_t(u8*, size_t)> onRead;
        std::function<ssize_t(const u8*, size_t)> onWrite;
        std::string readData;
    };

    class TestUtf8Decoder {
    public:
        TestUtf8Decoder();

        std::vector<u32> push(const std::string& input);

    private:
        std::vector<u32> output;
        Utf8Decoder decoder;
    };

}

TestPty::TestPty(int fd)
    : fd_(fd)
    , onRead([this](u8* buffer, size_t size) {
        return ::read(fd_, buffer, size);
    })
    , onWrite([this](const u8* buffer, size_t size) {
        return ::write(fd_, buffer, size);
    })
{
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("test PTY nonblocking setup failed");
    }
}

int TestPty::fd() const {
    return fd_;
}

ssize_t TestPty::read(u8* buffer, size_t size) {
    const ssize_t count = onRead(buffer, size);
    if (count > 0) {
        readData.append((const char*)(buffer), (size_t)(count));
    }
    return count;
}

ssize_t TestPty::write(const u8* buffer, size_t size) {
    return onWrite(buffer, size);
}

void TestPty::resize(u16 columns, u16 rows) {
    pty_resize(fd_, columns, rows);
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

TestUtf8Decoder::TestUtf8Decoder()
    : decoder([this]() {
        output.push_back(decoder.getUnicode());
    })
{
}

std::vector<u32> TestUtf8Decoder::push(const std::string& input) {
    for (const unsigned char ch : input) {
        if (ch < 0x80) {
            decoder.checkPrematureEOS();
            decoder.onUnicode(ch);
        } else {
            decoder.pushByte(ch);
        }
    }
    std::vector<u32> result;
    result.swap(output);
    return result;
}

namespace {

    void writeAll(int fd, const std::string& data) {
        size_t offset = 0;
        while (offset < data.size()) {
            const ssize_t count = write(fd, data.data() + offset, data.size() - offset);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("test control write failed");
            }
            offset += (size_t)(count);
        }
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

    std::string encodeHex(const std::string& input) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string output;
        output.reserve(input.size() * 2);
        for (const unsigned char ch : input) {
            output.push_back(digits[ch >> 4]);
            output.push_back(digits[ch & 15]);
        }
        return output;
    }

    class TestDisplay {
    public:
        bool update(const Frame& frame);
        void failNextPresent();
        std::string snapshot() const;
        std::string modelSnapshot() const;
        std::string modelDigest() const;
        std::string renderState() const;
        std::string scrollbackState() const;
        std::string screenText() const;

    private:
        bool failNextUpdate = false;
        u16 columns = 0;
        u16 rows = 0;
        u16 viewOffset = 0;
        u16 historyRows = 0;
        u64 refreshCount = 0;
        bool delta = false;
        bool screenReverse = false;
        bool blinkVisible = true;
        bool cursorBlink = false;
        Color selectionForeground;
        Color selectionBackground;
        u8 selectionColorMask = 0;
        size_t graphemeCells = 0;
        size_t graphemeCodepoints = 0;
        TerminalCursor cursor;
        Rect selection;
        std::vector<RenderCell> cells;
        std::vector<TerminalCell> modelCells;
        std::vector<std::vector<u32>> cellGraphemes;
        std::vector<CellColor> modelUnderlineColors;
    };

    template <class Cell>
    unsigned cellUnderline(const Cell& cell) {
        return cell.underline;
    }

    template <>
    unsigned cellUnderline(const TerminalCell& cell) {
        return cell.underlined();
    }

    template <class Cell>
    unsigned cellFlags(const Cell& cell) {
        return (cell.dwidth << 0) | (cell.dwidth_cont << 1) | (cell.bold << 2) | (cell.italic << 3) | (cellUnderline(cell) << 4) | (cell.inverse << 5) | (cell.wrap << 6) | (cell.faint << 7) | (cell.blink << 8) | (cell.conceal << 9) | (cell.strike << 10) | (cell.overline << 11) | (cell.underline_style << 12) | ((cell.protected_char != 0) << 15) | (cell.line_attr << 16) | (cell.drawn << 18);
    }

    struct ModelDigest {
        u64 first = 14695981039346656037ull;
        u64 second = 1099511628211ull;

        void add(u64 value) {
            for (unsigned shift = 0; shift < 64; shift += 8) {
                const u8 byte = (u8)(value >> shift);
                first = (first ^ byte) * 1099511628211ull;
                second = (second ^ (byte + 0x9d)) * 14029467366897019727ull;
            }
        }
    };

}

bool TestDisplay::update(const Frame& frame) {
    if (failNextUpdate) {
        failNextUpdate = false;
        return false;
    }
    const size_t count = frame.nCols * frame.nRows;
    if (columns != frame.nCols || rows != frame.nRows) {
        columns = frame.nCols;
        rows = frame.nRows;
        cells.resize(count);
        modelCells.resize(count);
        delta = false;
    }
    if (delta) {
        frame.deltaCopyCells(cells.data());
    } else {
        frame.fullCopyCells(cells.data());
    }
    for (auto& cell : cells) {
        cell.dirty = 0;
    }
    cellGraphemes.resize(count);
    modelUnderlineColors.resize(count);
    for (u16 row = 0; row < rows; ++row) {
        for (u16 column = 0; column < columns; ++column) {
            const size_t index = (size_t)(row)*columns + column;
            modelCells[index] = frame.getViewCell(row, column);
            const auto grapheme = frame.getGrapheme(modelCells[index].extraRef());
            cellGraphemes[index].assign(grapheme.begin(), grapheme.end());
            modelUnderlineColors[index] = frame.getUnderlineColor(modelCells[index]);
        }
    }
    cursor = frame.getCursor();
    selection = frame.getSelectionForView();
    viewOffset = frame.getViewOffset();
    historyRows = frame.getHistoryRows();
    screenReverse = frame.getScreenReverseVideo();
    blinkVisible = frame.getBlinkVisible();
    cursorBlink = frame.getCursorBlink();
    selectionForeground = frame.getSelectionForeground();
    selectionBackground = frame.getSelectionBackground();
    selectionColorMask = frame.getSelectionColorMask();
    graphemeCells = 0;
    graphemeCodepoints = 0;
    for (const auto& cell : cells) {
        if (!cell.grapheme) {
            continue;
        }
        const auto& grapheme = frame.getGrapheme(cell.grapheme);
        if (grapheme.empty()) {
            continue;
        }
        ++graphemeCells;
        graphemeCodepoints += grapheme.size();
    }
    ++refreshCount;
    delta = true;
    return true;
}

void TestDisplay::failNextPresent() {
    failNextUpdate = true;
}

std::string TestDisplay::snapshot() const {
    std::ostringstream output;
    output << "OK " << columns << ' ' << rows << ' ' << cursor.posX << ' ' << cursor.posY << ' ' << (unsigned)(cursor.style) << ' ' << viewOffset << ' ' << refreshCount << ' ' << selection.tl.x << ' ' << selection.tl.y << ' ' << selection.br.x << ' ' << selection.br.y << ' ' << selection.rectangular << ' ';
    output << std::hex << std::setfill('0');
    for (const auto& cell : cells) {
        const unsigned flags = cellFlags(cell);
        output << std::setw(8) << cell.uc_pt << std::setw(8) << flags << std::setw(2) << (unsigned)(cell.fg.red) << std::setw(2) << (unsigned)(cell.fg.green) << std::setw(2) << (unsigned)(cell.fg.blue) << std::setw(2) << (unsigned)(cell.bg.red) << std::setw(2) << (unsigned)(cell.bg.green) << std::setw(2) << (unsigned)(cell.bg.blue) << std::setw(2) << (unsigned)(cell.underline_color.red) << std::setw(2) << (unsigned)(cell.underline_color.green) << std::setw(2) << (unsigned)(cell.underline_color.blue) << std::setw(8) << cell.hyperlink << std::setw(8) << cell.semantic;
    }
    output << '\n';
    return output.str();
}

std::string TestDisplay::modelSnapshot() const {
    std::ostringstream output;
    output << "OK " << columns << ' ' << rows << ' ' << cursor.posX << ' ' << cursor.posY << ' ' << (unsigned)(cursor.style) << ' ' << viewOffset << ' ' << refreshCount << ' ' << selection.tl.x << ' ' << selection.tl.y << ' ' << selection.br.x << ' ' << selection.br.y << ' ' << selection.rectangular << ' ';
    output << std::hex << std::setfill('0');
    for (size_t index = 0; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        const auto& modelCell = modelCells[index];
        const unsigned flags = cellFlags(modelCell);
        output << std::setw(8) << cell.uc_pt << std::setw(8) << flags << std::setw(2) << (unsigned)(cell.fg.red) << std::setw(2) << (unsigned)(cell.fg.green) << std::setw(2) << (unsigned)(cell.fg.blue) << std::setw(2) << (unsigned)(cell.bg.red) << std::setw(2) << (unsigned)(cell.bg.green) << std::setw(2) << (unsigned)(cell.bg.blue) << std::setw(2) << (unsigned)(cell.underline_color.red) << std::setw(2) << (unsigned)(cell.underline_color.green) << std::setw(2) << (unsigned)(cell.underline_color.blue) << std::setw(8) << cell.hyperlink << std::setw(8) << cell.semantic << std::setw(8) << (u32)(modelCell.foreground().legacyIndex()) << std::setw(8) << (u32)(modelCell.background().legacyIndex()) << std::setw(8) << (u32)(modelUnderlineColors[index].legacyIndex()) << std::setw(8) << cellGraphemes[index].size();
        for (const u32 codepoint : cellGraphemes[index]) {
            output << std::setw(8) << codepoint;
        }
    }
    output << '\n';
    return output.str();
}

std::string TestDisplay::modelDigest() const {
    ModelDigest digest;
    digest.add(columns);
    digest.add(rows);
    digest.add(cursor.style == TerminalCursor::Style::hidden ? (u64)-1 : cursor.posX);
    digest.add(cursor.style == TerminalCursor::Style::hidden ? (u64)-1 : cursor.posY);
    digest.add((u8)(cursor.style));
    digest.add(viewOffset);
    digest.add(selection.tl.x);
    digest.add(selection.tl.y);
    digest.add(selection.br.x);
    digest.add(selection.br.y);
    digest.add(selection.rectangular);
    digest.add(cells.size());
    for (size_t index = 0; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        const auto& modelCell = modelCells[index];
        digest.add(cell.uc_pt);
        digest.add(cellFlags(modelCell));
        digest.add(cell.fg.red);
        digest.add(cell.fg.green);
        digest.add(cell.fg.blue);
        digest.add(cell.bg.red);
        digest.add(cell.bg.green);
        digest.add(cell.bg.blue);
        digest.add(cell.underline_color.red);
        digest.add(cell.underline_color.green);
        digest.add(cell.underline_color.blue);
        digest.add(cell.hyperlink);
        digest.add(cell.semantic);
        digest.add((u32)(modelCell.foreground().legacyIndex()));
        digest.add((u32)(modelCell.background().legacyIndex()));
        digest.add((u32)(modelUnderlineColors[index].legacyIndex()));
        digest.add(cellGraphemes[index].size());
        for (const u32 codepoint : cellGraphemes[index]) {
            digest.add(codepoint);
        }
    }

    std::ostringstream output;
    output << "OK " << std::hex << std::setfill('0') << std::setw(16) << digest.first << ' ' << std::setw(16) << digest.second << '\n';
    return output.str();
}

std::string TestDisplay::scrollbackState() const {
    std::ostringstream output;
    output << "OK " << historyRows << ' ' << historyRows + rows << ' ' << rows << ' ' << historyRows - viewOffset << '\n';
    return output.str();
}

std::string TestDisplay::renderState() const {
    std::ostringstream output;
    output << "OK " << screenReverse << ' ' << blinkVisible << ' ' << cursorBlink << ' ' << (unsigned)(selectionColorMask) << ' ' << (unsigned)(selectionForeground.red) << ' ' << (unsigned)(selectionForeground.green) << ' ' << (unsigned)(selectionForeground.blue) << ' ' << (unsigned)(selectionBackground.red) << ' ' << (unsigned)(selectionBackground.green) << ' ' << (unsigned)(selectionBackground.blue) << ' ' << graphemeCells << ' ' << graphemeCodepoints << '\n';
    return output.str();
}

std::string TestDisplay::screenText() const {
    std::string output;
    output.reserve(cells.size() + rows);
    for (size_t index = 0; index < cells.size(); ++index) {
        const u32 codepoint = cells[index].uc_pt;
        output.push_back(codepoint >= 0x20 && codepoint <= 0x7e ? (char)(codepoint) : ' ');
        if ((index + 1) % columns == 0) {
            output.push_back('\n');
        }
    }
    return output;
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

    VtKey parseKey(const std::string& name) {
        static const std::map<std::string, VtKey> keys = {
            {"SPACE", VtKey::Space},
            {"RETURN", VtKey::Return},
            {"BACKSPACE", VtKey::Backspace},
            {"TAB", VtKey::Tab},
            {"UP", VtKey::Up},
            {"DOWN", VtKey::Down},
            {"LEFT", VtKey::Left},
            {"RIGHT", VtKey::Right},
            {"INSERT", VtKey::Insert},
            {"DELETE", VtKey::Delete},
            {"HOME", VtKey::Home},
            {"END", VtKey::End},
            {"PAGE_UP", VtKey::PageUp},
            {"PAGE_DOWN", VtKey::PageDown},
            {"F1", VtKey::F1},
            {"F2", VtKey::F2},
            {"F3", VtKey::F3},
            {"F4", VtKey::F4},
            {"F5", VtKey::F5},
            {"F6", VtKey::F6},
            {"F7", VtKey::F7},
            {"F8", VtKey::F8},
            {"F9", VtKey::F9},
            {"F10", VtKey::F10},
            {"F11", VtKey::F11},
            {"F12", VtKey::F12},
            {"F13", VtKey::F13},
            {"F14", VtKey::F14},
            {"F15", VtKey::F15},
            {"F16", VtKey::F16},
            {"F17", VtKey::F17},
            {"F18", VtKey::F18},
            {"F19", VtKey::F19},
            {"F20", VtKey::F20},
            {"KP_F1", VtKey::KP_F1},
            {"KP_F2", VtKey::KP_F2},
            {"KP_F3", VtKey::KP_F3},
            {"KP_F4", VtKey::KP_F4},
            {"KP_PLUS", VtKey::KP_Plus},
            {"KP_MINUS", VtKey::KP_Minus},
            {"KP_STAR", VtKey::KP_Star},
            {"KP_SLASH", VtKey::KP_Slash},
            {"KP_COMMA", VtKey::KP_Comma},
            {"KP_DOT", VtKey::KP_Dot},
            {"KP_EQUAL", VtKey::KP_Equal},
            {"KP_TAB", VtKey::KP_Tab},
            {"KP_SPACE", VtKey::KP_Space},
            {"KP_ENTER", VtKey::KP_Enter},
            {"KP_LEFT", VtKey::KP_Left},
            {"KP_RIGHT", VtKey::KP_Right},
            {"KP_UP", VtKey::KP_Up},
            {"KP_DOWN", VtKey::KP_Down},
            {"KP_HOME", VtKey::KP_Home},
            {"KP_END", VtKey::KP_End},
            {"KP_PAGE_UP", VtKey::KP_PageUp},
            {"KP_PAGE_DOWN", VtKey::KP_PageDown},
            {"KP_INSERT", VtKey::KP_Insert},
            {"KP_DELETE", VtKey::KP_Delete},
            {"KP_BEGIN", VtKey::KP_Begin},
            {"KP_0", VtKey::KP_0},
            {"KP_1", VtKey::KP_1},
            {"KP_2", VtKey::KP_2},
            {"KP_3", VtKey::KP_3},
            {"KP_4", VtKey::KP_4},
            {"KP_5", VtKey::KP_5},
            {"KP_6", VtKey::KP_6},
            {"KP_7", VtKey::KP_7},
            {"KP_8", VtKey::KP_8},
            {"KP_9", VtKey::KP_9},
            {"CAPS_LOCK", VtKey::CapsLock},
            {"SCROLL_LOCK", VtKey::ScrollLock},
            {"NUM_LOCK", VtKey::NumLock},
            {"PRINT", VtKey::Print},
            {"PAUSE", VtKey::Pause},
            {"MENU", VtKey::Menu},
            {"LEFT_SHIFT", VtKey::LeftShift},
            {"LEFT_CONTROL", VtKey::LeftControl},
            {"LEFT_ALT", VtKey::LeftAlt},
            {"LEFT_SUPER", VtKey::LeftSuper},
            {"RIGHT_SHIFT", VtKey::RightShift},
            {"RIGHT_CONTROL", VtKey::RightControl},
            {"RIGHT_ALT", VtKey::RightAlt},
            {"RIGHT_SUPER", VtKey::RightSuper},
        };
        const auto found = keys.find(name);
        if (found == keys.end()) {
            throw std::runtime_error("unknown key");
        }
        return found->second;
    }
}

int runTestMode(Composer& composer, TestModeInput& input, int controlFd, int argc, char* argv[]) {
    int io[2];
    if (openpty(&io[0], &io[1], nullptr, nullptr, nullptr) < 0) {
        throw std::runtime_error("test openpty failed");
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

    unsigned glyphPx = 1;
    unsigned glyphPy = 1;
    if (const char* geometry = std::getenv("SHITTY_TEST_GLYPH")) {
        std::istringstream input(geometry);
        char separator = 0;
        if (!(input >> glyphPx >> separator >> glyphPy) || separator != 'x' || !glyphPx || !glyphPy || input.peek() != EOF) {
            throw std::runtime_error("invalid test glyph geometry");
        }
    }
    const u16 width = 2 * opts.border + opts.nCols * glyphPx;
    const u16 height = 2 * opts.border + opts.nRows * glyphPy;
    TestPty terminalPty(io[0]);
    VtermHostCallbacks vtermHost;
    Vterm& terminal = *Vterm::create(composer, vtermHost, terminalPty, glyphPx, glyphPy, width, height);
    input.attachTestVterm(terminal);
    VtermTrace& vtermTrace = *VtermTrace::create(composer);
    terminalPty.resize(opts.nCols, opts.nRows);
    TestDisplay display;
    vtermHost.setRefreshHandler([&display](const Frame& frame) {
        return display.update(frame);
    });
    std::string actions;
    std::string printerOutput;
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
    MouseFrontendState mouseFrontend;
    ClipboardStore clipboard;
    std::string systemClipboard;
    clipboard.setHandlers([&systemClipboard] {
        return systemClipboard;
    }, [&systemClipboard](const std::string& content) {
        systemClipboard = content;
    });
    vtermHost.setOscHandler([&terminal, &actions, &clipboard](int command, const std::string& argument) {
        actions += "OSC " + std::to_string(command) + " " + encodeHex(argument) + "\n";
        if (command == 8) {
            terminal.setHyperlink(argument);
        } else if (command == 52) {
            const Osc52Request request = parseOsc52(argument, opts.osc52SelectClipboard);
            if (!request.valid) {
                return;
            }
            if (request.query) {
                std::string primary;
                std::string system;
                if (opts.allowOsc52Read) {
                    if (request.primary) {
                        primary = clipboard.get(true);
                    }
                    if (primary.empty() && request.clipboard) {
                        system = clipboard.get(false);
                    }
                }
                const std::string reply = encodeOsc52QueryReply(request, opts.allowOsc52Read, primary, system);
                terminal.writePty(reply.c_str());
            } else {
                clipboard.apply(request);
            }
        }
    });
    vtermHost.setBellHandler([&actions]() {
        actions += "BELL\n";
    });
    vtermHost.setPrinterHandler([&printerOutput](const std::string& output) {
        printerOutput += output;
    });
    vtermHost.setLedHandler([&actions](u8 state) {
        actions += "LEDS " + std::to_string(state) + "\n";
    });
    vtermHost.setNotificationHandler([&actions](const std::string& id, const std::string& title, const std::string& body, bool close) {
        if (close) {
            actions += "NOTIFY_CLOSE " + encodeHex(id) + "\n";
        } else {
            actions += "NOTIFY " + encodeHex(id) + " " + encodeHex(title) + " " + encodeHex(body) + "\n";
        }
    });
    vtermHost.setProgressHandler([&actions](u32 state, u32 percent) {
        actions += "PROGRESS " + std::to_string(state) + " " + std::to_string(percent) + "\n";
    });
    VtermWindowInfo windowInfo;
    windowInfo.x = 10;
    windowInfo.y = 20;
    windowInfo.pixelWidth = width;
    windowInfo.pixelHeight = height;
    windowInfo.screenPixelWidth = 1920;
    windowInfo.screenPixelHeight = 1080;
    VtermWindowInfo restoredWindowInfo = windowInfo;
    bool haveRestoredWindowInfo = false;
    const auto applyWindowSize = [&](u32 pixelWidth, u32 pixelHeight) {
        terminal.resize(pixelWidth, pixelHeight);
        windowInfo.pixelWidth = pixelWidth;
        windowInfo.pixelHeight = pixelHeight;
    };
    vtermHost.setWindowOpsHandler([&](u32 operation, u32 first, u32 second) {
        actions += "WINDOW " + std::to_string(operation) + " " + std::to_string(first) + " " + std::to_string(second) + "\n";
        if (operation == 1) {
            windowInfo.iconified = false;
        } else if (operation == 2) {
            windowInfo.iconified = true;
        } else if (operation == 3) {
            windowInfo.x = (i32)(first);
            windowInfo.y = (i32)(second);
        } else if (operation == 4 && first && second) {
            applyWindowSize(second, first);
        } else if (operation == 8 && first && second) {
            const u32 pixelWidth = 2 * opts.border + second * glyphPx;
            const u32 pixelHeight = 2 * opts.border + first * glyphPy;
            applyWindowSize(pixelWidth, pixelHeight);
        } else if (operation == 9) {
            if (first == 0) {
                if (haveRestoredWindowInfo) {
                    applyWindowSize(restoredWindowInfo.pixelWidth, restoredWindowInfo.pixelHeight);
                    windowInfo.maximized = false;
                    haveRestoredWindowInfo = false;
                }
            } else if (first <= 3) {
                if (!haveRestoredWindowInfo) {
                    restoredWindowInfo = windowInfo;
                    haveRestoredWindowInfo = true;
                }
                const u32 pixelWidth = first == 2 ? windowInfo.pixelWidth : windowInfo.screenPixelWidth;
                const u32 pixelHeight = first == 3 ? windowInfo.pixelHeight : windowInfo.screenPixelHeight;
                applyWindowSize(pixelWidth, pixelHeight);
                windowInfo.maximized = true;
            }
        } else if (operation == 10) {
            const bool enable = first == 1 || (first == 2 && !windowInfo.fullscreen);
            if (enable && !windowInfo.fullscreen) {
                if (!haveRestoredWindowInfo) {
                    restoredWindowInfo = windowInfo;
                    haveRestoredWindowInfo = true;
                }
                applyWindowSize(windowInfo.screenPixelWidth, windowInfo.screenPixelHeight);
                windowInfo.fullscreen = true;
            } else if (!enable && windowInfo.fullscreen) {
                if (haveRestoredWindowInfo) {
                    applyWindowSize(restoredWindowInfo.pixelWidth, restoredWindowInfo.pixelHeight);
                    haveRestoredWindowInfo = false;
                }
                windowInfo.fullscreen = false;
            }
        }
    });
    vtermHost.setWindowInfoHandler([&windowInfo]() {
        return windowInfo;
    });
    const auto mouseGeometry = [&]() {
        return MouseGeometry{(int)(windowInfo.pixelWidth), (int)(windowInfo.pixelHeight), (int)(opts.border), (int)(glyphPx), (int)(glyphPy)};
    };
    const auto sendMouseButton = [&](MouseEventType type, int button, int pixelX, int pixelY, unsigned modifiers) {
        const auto tracking = terminal.getMouseTrackingState();
        if (!mouseButtonReportAllowed(tracking.mode, type, button)) {
            return;
        }
        const MouseProtocolPoint point = mouseProtocolPoint(tracking.enc, pixelX, pixelY, mouseGeometry());
        if (tracking.mode == MouseTrackingMode::VT200_Highlight && type == MouseEventType::Release) {
            terminal.mouseHighlightRelease(point.column, point.row, point.column, point.row);
            return;
        }
        const unsigned protocolModifiers = tracking.mode == MouseTrackingMode::X10_Compat ? 0 : mouseProtocolModifiers(modifiers);
        terminal.writePty(encodeMouseProtocol(tracking.enc, type, protocolModifiers, mouseFrontend.motionButton(), button, point.column, point.row).c_str());
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
                drainInput(io[1]);
                terminal.feedPtyOutput((const u8*)input.data(), input.size());
                writeAll(controlFd, "OK " + encodeHex(drainInput(io[1])) + "\n");
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
            } else if (line.compare(0, 19, "FONTCONFIG_RESOLVE ") == 0) {
                const FontVariants variants = resolveFontconfig(decodeHex(line.substr(19)));
                const std::string encoded = variants.regular + '\0' + variants.bold + '\0' + variants.italic + '\0' + variants.boldItalic;
                writeAll(controlFd, "OK " + encodeHex(encoded) + "\n");
            } else if (line.compare(0, 10, "FONT_LOAD ") == 0) {
                const std::string request = decodeHex(line.substr(10));
                const size_t first = request.find('\0');
                if (first == std::string::npos) {
                    throw std::runtime_error("invalid font load request");
                }
                ObjPool::Ref fontPool = ObjPool::fromMemory();
                Composer fontComposer;
                fontComposer.pool = fontPool.mutPtr();
                Fontpack* fonts = Fontpack::create(fontComposer, request.substr(0, first), request.substr(first + 1));
                writeAll(controlFd, "OK " + std::to_string(fonts->getPx()) + " " + std::to_string(fonts->getPy()) + " " + std::to_string(fonts->hasBold()) + " " + std::to_string(fonts->hasItalic()) + " " + std::to_string(fonts->hasBoldItalic()) + " " + std::to_string(fonts->hasDoubleWidth()) + "\n");
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
                display.failNextPresent();
                writeAll(controlFd, "OK\n");
            } else if (line == "PRESENT") {
                terminal.redraw();
                writeAll(controlFd, "OK\n");
            } else if (line == "GPU_ATTRIBUTE_MASKS") {
                RenderCell cell;
                cell.dwidth = true;
                const u32 doubleWidth = VulkanPresenter::packCellAttributes(cell);
                cell.dwidth = false;
                cell.dwidth_cont = true;
                const u32 continuation = VulkanPresenter::packCellAttributes(cell);
                cell.dwidth_cont = false;
                cell.dirty = true;
                const u32 dirty = VulkanPresenter::packCellAttributes(cell);
                writeAll(controlFd, "OK " + std::to_string(doubleWidth) + " " + std::to_string(continuation) + " " + std::to_string(dirty) + "\n");
            } else if (line == "POLL_CHILD") {
                pumpChild();
                writeAll(controlFd, "OK " + std::to_string(childPid > 0) + " " + std::to_string(childExitStatus) + " " + encodeHex(display.screenText()) + "\n");
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
                const auto tracking = terminal.getMouseTrackingState();
                const bool reporting = mouseFrontend.protocolActive(modifiers, tracking.mode);
                const MouseWheelSteps steps = mouseFrontend.consumeWheel(x, y, reporting);
                if (!reporting) {
                    if (steps.y > 0) {
                        terminal.mouseWheelUp(steps.y);
                    }
                    if (steps.y < 0) {
                        terminal.mouseWheelDown(-steps.y);
                    }
                    writeAll(controlFd, "OK\n");
                    continue;
                }

                const MouseProtocolPoint point = mouseProtocolPoint(tracking.enc, pixelX, pixelY, mouseGeometry());
                const unsigned protocolModifiers = mouseProtocolModifiers(modifiers);
                const auto send = [&](int count, int button) {
                    for (int k = 0; k < count; ++k) {
                        terminal.writePty(encodeMouseProtocol(tracking.enc, MouseEventType::Press, protocolModifiers, mouseFrontend.motionButton(), button, point.column, point.row).c_str());
                    }
                };
                send(std::max(0, steps.y), 4);
                send(std::max(0, -steps.y), 5);
                send(std::max(0, -steps.x), 6);
                send(std::max(0, steps.x), 7);
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
                const MouseProtocolPoint locator = mouseProtocolPoint(MouseTrackingEnc::Default, pixelX, pixelY, mouseGeometry());
                terminal.setLocatorPosition(locator.column, locator.row, std::max(1, pixelX + 1), std::max(1, pixelY + 1));
                const auto tracking = terminal.getMouseTrackingState();
                if (mouseFrontend.protocolActive(modifiers, tracking.mode)) {
                    const bool allowed = tracking.mode == MouseTrackingMode::VT200_AnyEvent || (tracking.mode == MouseTrackingMode::VT200_ButtonEvent && mouseFrontend.primaryButtonPressed());
                    if (allowed) {
                        const MouseProtocolPoint point = mouseProtocolPoint(tracking.enc, pixelX, pixelY, mouseGeometry());
                        if (mouseFrontend.reportMotion(point.column, point.row, tracking.mode, tracking.enc, tracking.generation)) {
                            terminal.writePty(encodeMouseProtocol(tracking.enc, MouseEventType::Motion, mouseProtocolModifiers(modifiers), mouseFrontend.motionButton(), 0, point.column, point.row).c_str());
                        }
                    }
                } else if (mouseFrontend.buttons() & ((1u << 0) | (1u << 1))) {
                    terminal.selectUpdate(pixelX, pixelY);
                }
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 7, "BUTTON ") == 0) {
                std::istringstream args(line.substr(7));
                int button;
                unsigned pressed, modifiers;
                double x, y, time, scaleX, scaleY;
                if (!(args >> button >> pressed >> x >> y >> modifiers >> time >> scaleX >> scaleY) || button < 0 || pressed > 1 || modifiers > 7) {
                    throw std::runtime_error("invalid button event");
                }
                const int pixelX = mouseFramebufferCoordinate(x, scaleX);
                const int pixelY = mouseFramebufferCoordinate(y, scaleY);
                mouseFrontend.updateButton(button, pressed != 0);
                const int protocolButton = mouseTerminalButton(button);
                const MouseProtocolPoint locator = mouseProtocolPoint(MouseTrackingEnc::Default, pixelX, pixelY, mouseGeometry());
                terminal.setLocatorPosition(locator.column, locator.row, std::max(1, pixelX + 1), std::max(1, pixelY + 1));
                if (protocolButton >= 1 && protocolButton <= 4) {
                    terminal.reportLocatorButton(protocolButton, pressed != 0);
                }

                std::string selection;
                const auto tracking = terminal.getMouseTrackingState();
                if (mouseFrontend.protocolActive(modifiers, tracking.mode)) {
                    sendMouseButton(pressed ? MouseEventType::Press : MouseEventType::Release, protocolButton, pixelX, pixelY, modifiers);
                } else if (pressed) {
                    const bool cycle = mouseFrontend.registerClick(button, x, y, time) > 1;
                    if (button == 0) {
                        terminal.selectStart(pixelX, pixelY, cycle);
                        mouseFrontend.beginSelection();
                    } else if (button == 1) {
                        terminal.selectExtend(pixelX, pixelY, cycle);
                        mouseFrontend.beginSelection();
                    }
                } else if (button == 0 || button == 1) {
                    mouseFrontend.endSelection();
                    if (terminal.selectFinish(selection)) {
                        clipboard.setPrimary(selection, opts.autoCopyMode);
                    }
                } else if (button == 2) {
                    terminal.pasteSelection(clipboard.get(true));
                }
                writeAll(controlFd, "OK " + encodeHex(selection) + "\n");
            } else if (line.compare(0, 7, "RESIZE ") == 0) {
                std::istringstream args(line.substr(7));
                unsigned columns;
                unsigned rows;
                if (!(args >> columns >> rows) || !columns || !rows) {
                    throw std::runtime_error("invalid resize");
                }
                terminal.resize(2 * opts.border + columns * glyphPx, 2 * opts.border + rows * glyphPy);
                windowInfo.pixelWidth = 2 * opts.border + columns * glyphPx;
                windowInfo.pixelHeight = 2 * opts.border + rows * glyphPy;
                terminal.redraw();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 14, "RESIZE_PIXELS ") == 0) {
                std::istringstream args(line.substr(14));
                unsigned pixelWidth;
                unsigned pixelHeight;
                if (!(args >> pixelWidth >> pixelHeight) || pixelWidth <= 2 * opts.border || pixelHeight <= 2 * opts.border) {
                    throw std::runtime_error("invalid pixel resize");
                }
                terminal.resize(pixelWidth, pixelHeight);
                windowInfo.pixelWidth = pixelWidth;
                windowInfo.pixelHeight = pixelHeight;
                terminal.redraw();
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
                if (!(args >> x >> y >> pixelWidth >> pixelHeight >> screenWidth >> screenHeight >> iconified >> maximized >> fullscreen) || x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX || pixelWidth > UINT32_MAX || pixelHeight > UINT32_MAX || screenWidth > UINT32_MAX || screenHeight > UINT32_MAX || iconified > 1 || maximized > 1 || fullscreen > 1) {
                    throw std::runtime_error("invalid window info");
                }
                windowInfo.x = x;
                windowInfo.y = y;
                windowInfo.pixelWidth = pixelWidth;
                windowInfo.pixelHeight = pixelHeight;
                windowInfo.screenPixelWidth = screenWidth;
                windowInfo.screenPixelHeight = screenHeight;
                windowInfo.iconified = iconified;
                windowInfo.maximized = maximized;
                windowInfo.fullscreen = fullscreen;
                writeAll(controlFd, "OK\n");
            } else if (line == "WINSIZE") {
                winsize size{};
                if (ioctl(io[0], TIOCGWINSZ, &size) < 0) {
                    throw std::runtime_error("test TIOCGWINSZ failed");
                }
                writeAll(controlFd, "OK " + std::to_string(size.ws_col) + " " + std::to_string(size.ws_row) + "\n");
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
                input.testKeyEvent(key, scancode, action, modifiers);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 20, "FRONTEND_TEXT_EVENT ") == 0) {
                std::istringstream args(line.substr(20));
                unsigned codepoint;
                int modifiers;
                if (!(args >> codepoint >> modifiers) || codepoint > 0x10ffff || modifiers < 0) {
                    throw std::runtime_error("invalid frontend text event");
                }
                input.testTextInput(codepoint, modifiers);
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
                terminal.writeKittyKey(key, shifted, base, modifiers, (Vterm::KeyEventType)(event));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 14, "KITTY_SPECIAL ") == 0) {
                std::istringstream args(line.substr(14));
                std::string name;
                unsigned modifiers;
                unsigned event;
                if (!(args >> name >> modifiers >> event) || event < 1 || event > 3) {
                    throw std::runtime_error("invalid kitty special key");
                }
                terminal.writeKittyKey(parseKey(name), modifiers, (Vterm::KeyEventType)(event));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 6, "PASTE ") == 0) {
                terminal.pasteSelection(decodeHex(line.substr(6)));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 6, "FOCUS ") == 0) {
                terminal.setHasFocus(line.substr(6) == "1");
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
            } else if (line.compare(0, 13, "SELECT_START ") == 0 || line.compare(0, 14, "SELECT_UPDATE ") == 0) {
                const bool start = line.compare(0, 13, "SELECT_START ") == 0;
                std::istringstream args(line.substr(start ? 13 : 14));
                int column;
                int row;
                if (!(args >> column >> row)) {
                    throw std::runtime_error("invalid selection point");
                }
                if (start) {
                    terminal.selectStart(opts.border + column * glyphPx, opts.border + row * glyphPy, false);
                } else {
                    terminal.selectUpdate(opts.border + column * glyphPx, opts.border + row * glyphPy);
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
            } else if (line == "READ_ACTIONS") {
                writeAll(controlFd, "OK " + encodeHex(actions) + "\n");
                actions.clear();
            } else if (line == "READ_PRINTER") {
                writeAll(controlFd, "OK " + encodeHex(printerOutput) + "\n");
                printerOutput.clear();
            } else if (line == "STATE") {
                const auto& mouse = terminal.getMouseTrackingState();
                writeAll(controlFd, "OK " + std::to_string((unsigned)(mouse.mode)) + " " + std::to_string((unsigned)(mouse.enc)) + " " + std::to_string(mouse.focusEventMode) + " " + std::to_string(terminal.getKittyKeyboardFlags()) + "\n");
            } else if (line == "PROTOCOL_STATE") {
                writeAll(controlFd, "OK " + std::to_string(terminal.getScreenReverseVideo()) + " " + std::to_string(terminal.getLedState()) + " " + std::to_string(terminal.getReverseWrapMode()) + " " + std::to_string(terminal.getNationalReplacementMode()) + " 0\n");
            } else if (line == "CURSOR_STATE") {
                writeAll(controlFd, "OK " + std::to_string(terminal.getPrivateMode(25)) + " " + std::to_string(terminal.getPrivateMode(12)) + " " + std::to_string((unsigned)(terminal.getCursorStyle())) + "\n");
            } else if (line == "CONFORMANCE_STATE") {
                std::ostringstream output;
                output << "OK screen=" << (terminal.getPrivateMode(47) ? "Alternate" : "Primary") << " IRM=" << terminal.getAnsiMode(4) << " SRM=" << terminal.getAnsiMode(12) << " LNM=" << terminal.getAnsiMode(20) << " DECCKM=" << terminal.getPrivateMode(1) << " DECCOLM=" << terminal.getPrivateMode(3) << " DECSCLM=" << terminal.getPrivateMode(4) << " DECSCNM=" << terminal.getPrivateMode(5) << " DECOM=" << terminal.getPrivateMode(6) << " DECAWM=" << terminal.getPrivateMode(7) << " DECARM=" << terminal.getPrivateMode(8) << " DECTCEM=" << terminal.getPrivateMode(25) << " DECNKM=" << terminal.getPrivateMode(66) << " DECBKM=" << terminal.getPrivateMode(67) << " DECLRMM=" << terminal.getPrivateMode(69) << '\n';
                writeAll(controlFd, output.str());
            } else if (line == "RECTANGLE_ORIGIN") {
                const RectangleOrigin origin = terminal.getRectangleOrigin();
                writeAll(controlFd, "OK " + std::to_string(origin.rowBase) + " " + std::to_string(origin.columnBase) + " " + std::to_string(origin.rowLimit) + " " + std::to_string(origin.columnLimit) + "\n");
            } else if (line == "PEN_STATE") {
                const TerminalPen pen = terminal.getPenState();
                std::ostringstream output;
                output << "OK " << cellFlags(pen.cell) << ' ' << (unsigned)(pen.fg.red) << ' ' << (unsigned)(pen.fg.green) << ' ' << (unsigned)(pen.fg.blue) << ' ' << (unsigned)(pen.bg.red) << ' ' << (unsigned)(pen.bg.green) << ' ' << (unsigned)(pen.bg.blue) << ' ' << pen.cell.foreground().legacyIndex() << ' ' << pen.cell.background().legacyIndex() << '\n';
                writeAll(controlFd, output.str());
            } else if (line == "PARSER_TRACE_ON") {
                vtermTrace.clear();
                terminal.setParserTrace(&vtermTrace);
                writeAll(controlFd, "OK\n");
            } else if (line == "PARSER_TRACE_CLEAR") {
                vtermTrace.clear();
                writeAll(controlFd, "OK\n");
            } else if (line == "READ_PARSER_TRACE") {
                writeAll(controlFd, "OK " + encodeHex(vtermTrace.drain()) + "\n");
            } else if (line.compare(0, 10, "UTF8_PUSH ") == 0) {
                const auto codepoints = testUtf8Decoder.push(decodeHex(line.substr(10)));
                std::ostringstream output;
                output << "OK" << std::hex;
                for (const u32 codepoint : codepoints) {
                    output << ' ' << codepoint;
                }
                output << '\n';
                writeAll(controlFd, output.str());
            } else if (line == "RENDER_STATE") {
                writeAll(controlFd, display.renderState());
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
            } else if (line.compare(0, 6, "OSC52 ") == 0) {
                const Osc52Request request = parseOsc52(decodeHex(line.substr(6)));
                writeAll(controlFd, "OK " + std::to_string(request.valid) + " " + std::to_string(request.query) + " " + std::to_string(request.primary) + " " + std::to_string(request.clipboard) + " " + encodeHex(request.content) + "\n");
            } else if (line.compare(0, 12, "OSC52_REPLY ") == 0) {
                const size_t separator = line.find(' ', 12);
                if (separator == std::string::npos) {
                    throw std::runtime_error("invalid OSC 52 reply");
                }
                writeAll(controlFd, "OK " + encodeHex(encodeOsc52Reply(decodeHex(line.substr(12, separator - 12)), decodeHex(line.substr(separator + 1)))) + "\n");
            } else if (line.compare(0, 13, "OSC52_POLICY ") == 0) {
                std::istringstream args(line.substr(13));
                int allowRead;
                int selectClipboard;
                std::string encoded;
                if (!(args >> allowRead >> selectClipboard >> encoded) || allowRead < 0 || allowRead > 1 || selectClipboard < 0 || selectClipboard > 1) {
                    throw std::runtime_error("invalid OSC 52 policy request");
                }
                const std::string payload = decodeHex(encoded);
                const size_t first = payload.find('\0');
                const size_t second = first == std::string::npos ? std::string::npos : payload.find('\0', first + 1);
                if (first == std::string::npos || second == std::string::npos || payload.find('\0', second + 1) != std::string::npos) {
                    throw std::runtime_error("invalid OSC 52 policy payload");
                }
                const Osc52Request request = parseOsc52(payload.substr(0, first), selectClipboard);
                const std::string reply = request.valid && request.query ? encodeOsc52QueryReply(request, allowRead, payload.substr(first + 1, second - first - 1), payload.substr(second + 1)) : std::string{};
                writeAll(controlFd, "OK " + encodeHex(reply) + "\n");
            } else if (line.compare(0, 12, "SET_PRIMARY ") == 0) {
                const size_t separator = line.find(' ', 12);
                if (separator == std::string::npos) {
                    throw std::runtime_error("invalid primary selection");
                }
                const int autoCopy = std::stoi(line.substr(12, separator - 12));
                if (autoCopy < 0 || autoCopy > 1) {
                    throw std::runtime_error("invalid auto-copy state");
                }
                clipboard.setPrimary(decodeHex(line.substr(separator + 1)), autoCopy);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 11, "SET_SYSTEM ") == 0) {
                clipboard.setClipboard(decodeHex(line.substr(11)));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 14, "GET_SELECTION ") == 0) {
                const int primary = std::stoi(line.substr(14));
                if (primary < 0 || primary > 1) {
                    throw std::runtime_error("invalid selection kind");
                }
                writeAll(controlFd, "OK " + encodeHex(clipboard.get(primary)) + "\n");
            } else if (line.compare(0, 22, "APPLY_CLIPBOARD_OSC52 ") == 0) {
                const Osc52Request request = parseOsc52(decodeHex(line.substr(22)), opts.osc52SelectClipboard);
                if (!request.valid) {
                    throw std::runtime_error("invalid OSC 52 clipboard write");
                }
                clipboard.apply(request);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 9, "OSC7_CWD ") == 0) {
                writeAll(controlFd, "OK " + encodeHex(oscCwdToPath(decodeHex(line.substr(9)))) + "\n");
            } else if (line == "SNAPSHOT") {
                writeAll(controlFd, display.snapshot());
            } else if (line == "MODEL_SNAPSHOT") {
                writeAll(controlFd, display.modelSnapshot());
            } else if (line == "MODEL_DIGEST") {
                writeAll(controlFd, display.modelDigest());
            } else if (line == "SCROLLBACK_STATE") {
                writeAll(controlFd, display.scrollbackState());
            } else if (line == "SCREEN_TEXT") {
                writeAll(controlFd, "OK " + encodeHex(display.screenText()) + "\n");
            } else if (line == "READ_INPUT") {
                writeAll(controlFd, "OK " + encodeHex(drainInput(io[1])) + "\n");
            } else if (line == "PENDING_OUTPUT") {
                writeAll(controlFd, "OK " + std::to_string(terminal.pendingPtyOutputBytes()) + "\n");
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
        } catch (const std::exception& error) {
            writeAll(controlFd, std::string("ERR ") + error.what() + "\n");
        }
    }

    close(io[0]);
    close(io[1]);
    return 0;
}
