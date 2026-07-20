#include "testmode.h"

#include "options.h"
#include "mouseprotocol.h"
#include "oscprotocol.h"
#include "pty.h"
#include "vkpresenter.h"
#include "vterm.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
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

namespace {
    extern "C" int openpty(int*, int*, char*, const termios*, const winsize*);

    void writeAll(int fd, const std::string& data) {
        size_t offset = 0;
        while (offset < data.size()) {
            const ssize_t count = write(
                fd, data.data() + offset, data.size() - offset);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("test control write failed");
            }
            offset += static_cast<size_t>(count);
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
            buffered.append(chunk, static_cast<size_t>(count));
        }
    }

    uint8_t hexDigit(char ch) {
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
            output.push_back(static_cast<char>(
                (hexDigit(input[k]) << 4) | hexDigit(input[k + 1])));
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
        bool update(const Frame& frame) {
            if (failNextUpdate) {
                failNextUpdate = false;
                return false;
            }
            const size_t count = frame.nCols * frame.nRows;
            if (columns != frame.nCols || rows != frame.nRows) {
                columns = frame.nCols;
                rows = frame.nRows;
                cells.resize(count);
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
            cursor = frame.getCursor();
            selection = frame.getSelectionForView();
            viewOffset = frame.getViewOffset();
            screenReverse = frame.getScreenReverseVideo();
            blinkVisible = frame.getBlinkVisible();
            cursorBlink = frame.getCursorBlink();
            selectionForeground = frame.getSelectionForeground();
            selectionBackground = frame.getSelectionBackground();
            selectionColorMask = frame.getSelectionColorMask();
            graphemeCells = 0;
            graphemeCodepoints = 0;
            for (const auto& cell : cells) {
                if (!cell.grapheme) continue;
                const auto& grapheme = frame.getGrapheme(cell.grapheme);
                if (grapheme.empty()) continue;
                ++graphemeCells;
                graphemeCodepoints += grapheme.size();
            }
            ++refreshCount;
            delta = true;
            return true;
        }

        void failNextPresent() {
            failNextUpdate = true;
        }

        std::string snapshot() const {
            std::ostringstream output;
            output << "OK " << columns << ' ' << rows << ' '
                   << cursor.posX << ' ' << cursor.posY << ' '
                   << static_cast<unsigned>(cursor.style) << ' '
                   << viewOffset << ' ' << refreshCount << ' '
                   << selection.tl.x << ' ' << selection.tl.y << ' '
                   << selection.br.x << ' ' << selection.br.y << ' '
                   << selection.rectangular << ' ';
            output << std::hex << std::setfill('0');
            for (const auto& cell : cells) {
                const unsigned flags =
                    (cell.dwidth << 0) |
                    (cell.dwidth_cont << 1) |
                    (cell.bold << 2) |
                    (cell.italic << 3) |
                    (cell.underline << 4) |
                    (cell.inverse << 5) |
                    (cell.wrap << 6) |
                    (cell.faint << 7) |
                    (cell.blink << 8) |
                    (cell.conceal << 9) |
                    (cell.strike << 10) |
                    (cell.overline << 11) |
                    (cell.underline_style << 12) |
                    (cell.protected_char << 15) |
                    (cell.line_attr << 16);
                output << std::setw(8) << cell.uc_pt
                       << std::setw(8) << flags
                       << std::setw(2) << static_cast<unsigned>(cell.fg.red)
                       << std::setw(2) << static_cast<unsigned>(cell.fg.green)
                       << std::setw(2) << static_cast<unsigned>(cell.fg.blue)
                       << std::setw(2) << static_cast<unsigned>(cell.bg.red)
                       << std::setw(2) << static_cast<unsigned>(cell.bg.green)
                       << std::setw(2) << static_cast<unsigned>(cell.bg.blue)
                       << std::setw(2) << static_cast<unsigned>(cell.underline_color.red)
                       << std::setw(2) << static_cast<unsigned>(cell.underline_color.green)
                       << std::setw(2) << static_cast<unsigned>(cell.underline_color.blue)
                       << std::setw(8) << cell.hyperlink
                       << std::setw(8) << cell.semantic;
            }
            output << '\n';
            return output.str();
        }

        std::string renderState() const {
            std::ostringstream output;
            output << "OK " << screenReverse << ' '
                   << blinkVisible << ' ' << cursorBlink << ' '
                   << static_cast<unsigned>(selectionColorMask) << ' '
                   << static_cast<unsigned>(selectionForeground.red) << ' '
                   << static_cast<unsigned>(selectionForeground.green) << ' '
                   << static_cast<unsigned>(selectionForeground.blue) << ' '
                   << static_cast<unsigned>(selectionBackground.red) << ' '
                   << static_cast<unsigned>(selectionBackground.green) << ' '
                   << static_cast<unsigned>(selectionBackground.blue) << ' '
                   << graphemeCells << ' ' << graphemeCodepoints << '\n';
            return output.str();
        }

        std::string screenText() const {
            std::string output;
            output.reserve(cells.size() + rows);
            for (size_t index = 0; index < cells.size(); ++index) {
                const uint32_t codepoint = cells[index].uc_pt;
                output.push_back(codepoint >= 0x20 && codepoint <= 0x7e
                                     ? static_cast<char>(codepoint)
                                     : ' ');
                if ((index + 1) % columns == 0) output.push_back('\n');
            }
            return output;
        }

    private:
        bool failNextUpdate = false;
        uint16_t columns = 0;
        uint16_t rows = 0;
        uint16_t viewOffset = 0;
        uint64_t refreshCount = 0;
        bool delta = false;
        bool screenReverse = false;
        bool blinkVisible = true;
        bool cursorBlink = false;
        Color selectionForeground;
        Color selectionBackground;
        uint8_t selectionColorMask = 0;
        size_t graphemeCells = 0;
        size_t graphemeCodepoints = 0;
        CharVdev::Cursor cursor;
        Rect selection;
        std::vector<CharVdev::Cell> cells;
    };

    std::string drainInput(int fd) {
        std::string output;
        char chunk[4096];
        while (true) {
            const ssize_t count = read(fd, chunk, sizeof(chunk));
            if (count > 0) {
                output.append(chunk, static_cast<size_t>(count));
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
            {"SPACE", VtKey::Space}, {"RETURN", VtKey::Return},
            {"BACKSPACE", VtKey::Backspace}, {"TAB", VtKey::Tab},
            {"UP", VtKey::Up}, {"DOWN", VtKey::Down},
            {"LEFT", VtKey::Left}, {"RIGHT", VtKey::Right},
            {"INSERT", VtKey::Insert}, {"DELETE", VtKey::Delete},
            {"HOME", VtKey::Home}, {"END", VtKey::End},
            {"PAGE_UP", VtKey::PageUp}, {"PAGE_DOWN", VtKey::PageDown},
            {"F1", VtKey::F1}, {"F2", VtKey::F2},
            {"F3", VtKey::F3}, {"F4", VtKey::F4},
            {"F5", VtKey::F5}, {"F6", VtKey::F6},
            {"F7", VtKey::F7}, {"F8", VtKey::F8},
            {"F9", VtKey::F9}, {"F10", VtKey::F10},
            {"F11", VtKey::F11}, {"F12", VtKey::F12},
            {"F13", VtKey::F13}, {"F14", VtKey::F14},
            {"F15", VtKey::F15}, {"F16", VtKey::F16},
            {"F17", VtKey::F17}, {"F18", VtKey::F18},
            {"F19", VtKey::F19}, {"F20", VtKey::F20},
            {"KP_PLUS", VtKey::KP_Plus}, {"KP_MINUS", VtKey::KP_Minus},
            {"KP_STAR", VtKey::KP_Star}, {"KP_SLASH", VtKey::KP_Slash},
            {"KP_COMMA", VtKey::KP_Comma}, {"KP_DOT", VtKey::KP_Dot},
            {"KP_EQUAL", VtKey::KP_Equal}, {"KP_TAB", VtKey::KP_Tab},
            {"KP_SPACE", VtKey::KP_Space}, {"KP_ENTER", VtKey::KP_Enter},
            {"KP_LEFT", VtKey::KP_Left}, {"KP_RIGHT", VtKey::KP_Right},
            {"KP_UP", VtKey::KP_Up}, {"KP_DOWN", VtKey::KP_Down},
            {"KP_HOME", VtKey::KP_Home}, {"KP_END", VtKey::KP_End},
            {"KP_PAGE_UP", VtKey::KP_PageUp},
            {"KP_PAGE_DOWN", VtKey::KP_PageDown},
            {"KP_INSERT", VtKey::KP_Insert},
            {"KP_DELETE", VtKey::KP_Delete},
            {"KP_BEGIN", VtKey::KP_Begin}, {"KP_0", VtKey::KP_0},
            {"KP_1", VtKey::KP_1}, {"KP_2", VtKey::KP_2},
            {"KP_3", VtKey::KP_3}, {"KP_4", VtKey::KP_4},
            {"KP_5", VtKey::KP_5}, {"KP_6", VtKey::KP_6},
            {"KP_7", VtKey::KP_7}, {"KP_8", VtKey::KP_8},
            {"KP_9", VtKey::KP_9},
            {"CAPS_LOCK", VtKey::CapsLock},
            {"SCROLL_LOCK", VtKey::ScrollLock},
            {"NUM_LOCK", VtKey::NumLock}, {"PRINT", VtKey::Print},
            {"PAUSE", VtKey::Pause}, {"MENU", VtKey::Menu},
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

int runTestMode(int controlFd) {
    int io[2];
    if (openpty(&io[0], &io[1], nullptr, nullptr, nullptr) < 0) {
        throw std::runtime_error("test openpty failed");
    }
    termios ttyAttrs;
    if (tcgetattr(io[1], &ttyAttrs) < 0) {
        close(io[0]);
        close(io[1]);
        throw std::runtime_error("test tcgetattr failed");
    }
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

    const uint16_t width = 2 * opts.border + opts.nCols;
    const uint16_t height = 2 * opts.border + opts.nRows;
    Vterm terminal(1, 1, width, height, io[0]);
    pty_resize(io[0], opts.nCols, opts.nRows);
    TestDisplay display;
    terminal.setRefreshHandler(
        [&display](const Frame& frame) {
            return display.update(frame);
        });
    std::string actions;
    std::string printerOutput;
    pid_t childPid = -1;
    int childExitStatus = -1;
    terminal.setOscHandler(
        [&terminal, &actions](int command, const std::string& argument) {
            actions += "OSC " + std::to_string(command) + " " +
                       encodeHex(argument) + "\n";
            if (command == 8) {
                terminal.setHyperlink(argument);
            }
        });
    terminal.setBellHandler(
        [&actions]() {
        actions += "BELL\n";
    });
    terminal.setPrinterHandler(
        [&printerOutput](const std::string& output) {
            printerOutput += output;
        });
    terminal.setLedHandler(
        [&actions](uint8_t state) {
            actions += "LEDS " + std::to_string(state) + "\n";
        });
    terminal.setNotificationHandler(
        [&actions](const std::string& id, const std::string& title,
                   const std::string& body, bool close) {
        if (close) {
            actions += "NOTIFY_CLOSE " + encodeHex(id) + "\n";
        } else {
            actions += "NOTIFY " + encodeHex(id) + " " + encodeHex(title) +
                       " " + encodeHex(body) + "\n";
        }
    });
    terminal.setProgressHandler(
        [&actions](uint32_t state, uint32_t percent) {
        actions += "PROGRESS " + std::to_string(state) + " " +
                   std::to_string(percent) + "\n";
    });
    terminal.setWindowOpsHandler(
        [&actions](uint32_t operation, uint32_t first, uint32_t second) {
        actions += "WINDOW " + std::to_string(operation) + " " +
                   std::to_string(first) + " " +
                   std::to_string(second) + "\n";
    });
    terminal.setWindowInfoHandler(
        []() {
        Vterm::WindowInfo info;
        info.x = 10;
        info.y = 20;
        info.pixelWidth = opts.nCols + 2 * opts.border;
        info.pixelHeight = opts.nRows + 2 * opts.border;
        info.screenPixelWidth = 1920;
        info.screenPixelHeight = 1080;
        return info;
    });
    terminal.redraw();
    writeAll(controlFd, "READY\n");

    const auto pumpChild = [&]() {
        terminal.flushPtyOutput();
        pollfd source{io[0], POLLIN, 0};
        while (poll(&source, 1, 0) > 0 &&
               (source.revents & POLLIN)) {
            terminal.readPty();
            source.revents = 0;
        }
        terminal.flushPtyOutput();
        terminal.redraw();
        int status = 0;
        if (childPid > 0 &&
            waitpid(childPid, &status, WNOHANG) == childPid) {
            childPid = -1;
            if (WIFEXITED(status))
                childExitStatus = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                childExitStatus = 128 + WTERMSIG(status);
            else
                childExitStatus = 255;
        }
    };

    std::string buffered;
    std::string line;
    while (readLine(controlFd, buffered, line)) {
        try {
            if (line.compare(0, 6, "WRITE ") == 0) {
                terminal.feedPtyOutput(decodeHex(line.substr(6)));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 6, "INPUT ") == 0) {
                const std::string input = decodeHex(line.substr(6));
                terminal.writePty(
                    reinterpret_cast<const uint8_t*>(input.data()),
                    input.size(), false);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 6, "SPAWN ") == 0) {
                if (childPid > 0) throw std::runtime_error("child already running");
                const std::string encoded = decodeHex(line.substr(6));
                std::vector<std::string> arguments;
                size_t start = 0;
                while (start < encoded.size()) {
                    const size_t end = encoded.find('\0', start);
                    arguments.push_back(encoded.substr(
                        start, end == std::string::npos
                                   ? std::string::npos : end - start));
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                if (arguments.empty() || arguments[0].empty())
                    throw std::runtime_error("empty child command");
                childExitStatus = -1;
                childPid = fork();
                if (childPid < 0) throw std::runtime_error("test fork failed");
                if (childPid == 0) {
                    setsid();
                    ioctl(io[1], TIOCSCTTY, 0);
                    const int childFlags = fcntl(io[1], F_GETFL, 0);
                    if (childFlags >= 0)
                        fcntl(io[1], F_SETFL, childFlags & ~O_NONBLOCK);
                    dup2(io[1], STDIN_FILENO);
                    dup2(io[1], STDOUT_FILENO);
                    dup2(io[1], STDERR_FILENO);
                    close(io[0]);
                    if (io[1] > STDERR_FILENO) close(io[1]);
                    std::vector<char*> argv;
                    for (auto& argument : arguments)
                        argv.push_back(argument.data());
                    argv.push_back(nullptr);
                    execvp(argv[0], argv.data());
                    _exit(127);
                }
                writeAll(controlFd, "OK\n");
            } else if (line == "PUMP") {
                pumpChild();
                writeAll(controlFd, "OK\n");
            } else if (line == "READ_PTY") {
                writeAll(controlFd,
                         "OK " + std::to_string(terminal.readPty()) + "\n");
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
                CharVdev::Cell cell;
                cell.dwidth = true;
                const uint32_t doubleWidth =
                    VulkanPresenter::packCellAttributes(cell);
                cell.dwidth = false;
                cell.dwidth_cont = true;
                const uint32_t continuation =
                    VulkanPresenter::packCellAttributes(cell);
                cell.dwidth_cont = false;
                cell.dirty = true;
                const uint32_t dirty =
                    VulkanPresenter::packCellAttributes(cell);
                writeAll(controlFd,
                         "OK " + std::to_string(doubleWidth) + " " +
                         std::to_string(continuation) + " " +
                         std::to_string(dirty) + "\n");
            } else if (line == "POLL_CHILD") {
                pumpChild();
                writeAll(controlFd,
                         "OK " + std::to_string(childPid > 0) + " " +
                         std::to_string(childExitStatus) + " " +
                         encodeHex(display.screenText()) + "\n");
            } else if (line == "CHILD_STATUS") {
                writeAll(controlFd,
                         "OK " + std::to_string(childPid > 0) + " " +
                         std::to_string(childExitStatus) + "\n");
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
            } else if (line.compare(0, 7, "RESIZE ") == 0) {
                std::istringstream args(line.substr(7));
                unsigned columns;
                unsigned rows;
                if (!(args >> columns >> rows) || !columns || !rows) {
                    throw std::runtime_error("invalid resize");
                }
                terminal.resize(2 * opts.border + columns,
                                2 * opts.border + rows);
                terminal.redraw();
                writeAll(controlFd, "OK\n");
            } else if (line == "WINSIZE") {
                winsize size{};
                if (ioctl(io[0], TIOCGWINSZ, &size) < 0) {
                    throw std::runtime_error("test TIOCGWINSZ failed");
                }
                writeAll(controlFd,
                         "OK " + std::to_string(size.ws_col) + " " +
                         std::to_string(size.ws_row) + "\n");
            } else if (line.compare(0, 4, "KEY ") == 0) {
                std::istringstream args(line.substr(4));
                std::string name;
                unsigned modifiers;
                if (!(args >> name >> modifiers) || modifiers > 7) {
                    throw std::runtime_error("invalid key");
                }
                terminal.writePty(parseKey(name),
                                  static_cast<VtModifier>(modifiers), true);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 5, "CHAR ") == 0) {
                std::istringstream args(line.substr(5));
                unsigned character;
                unsigned modifiers;
                if (!(args >> character >> modifiers) || character > 255 ||
                    modifiers > 7) {
                    throw std::runtime_error("invalid char");
                }
                terminal.writePty(static_cast<uint8_t>(character),
                                  static_cast<VtModifier>(modifiers), true);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 10, "KITTY_KEY ") == 0) {
                std::istringstream args(line.substr(10));
                uint32_t key;
                uint32_t shifted;
                uint32_t base;
                unsigned modifiers;
                unsigned event;
                if (!(args >> key >> shifted >> base >> modifiers >> event) ||
                    event < 1 || event > 3) {
                    throw std::runtime_error("invalid kitty key");
                }
                terminal.writeKittyKey(
                    key, shifted, base, modifiers,
                    static_cast<Vterm::KeyEventType>(event));
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 14, "KITTY_SPECIAL ") == 0) {
                std::istringstream args(line.substr(14));
                std::string name;
                unsigned modifiers;
                unsigned event;
                if (!(args >> name >> modifiers >> event) ||
                    event < 1 || event > 3) {
                    throw std::runtime_error("invalid kitty special key");
                }
                terminal.writeKittyKey(
                    parseKey(name), modifiers,
                    static_cast<Vterm::KeyEventType>(event));
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
                if (!(args >> column >> row >> pixelX >> pixelY >> buttons))
                    throw std::runtime_error("invalid locator position");
                terminal.setLocatorPosition(column, row, pixelX, pixelY, buttons);
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 15, "LOCATOR_BUTTON ") == 0) {
                std::istringstream args(line.substr(15));
                unsigned button, pressed;
                if (!(args >> button >> pressed))
                    throw std::runtime_error("invalid locator button");
                terminal.reportLocatorButton(button, pressed != 0);
                writeAll(controlFd, "OK\n");
            } else if (line == "SYNC_TIMEOUT") {
                terminal.expireSynchronizedOutput(true);
                writeAll(controlFd, "OK\n");
            } else if (line == "BLINK_TICK") {
                if (terminal.advanceAnimation(true)) terminal.redraw();
                writeAll(controlFd, "OK\n");
            } else if (line.compare(0, 13, "SELECT_START ") == 0 ||
                       line.compare(0, 14, "SELECT_UPDATE ") == 0) {
                const bool start = line.compare(0, 13, "SELECT_START ") == 0;
                std::istringstream args(line.substr(start ? 13 : 14));
                int column;
                int row;
                if (!(args >> column >> row)) {
                    throw std::runtime_error("invalid selection point");
                }
                if (start) {
                    terminal.selectStart(opts.border + column,
                                         opts.border + row, false);
                } else {
                    terminal.selectUpdate(opts.border + column,
                                          opts.border + row);
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
                writeAll(controlFd, "OK " + encodeHex(terminal.getHyperlink(
                    opts.border + column, opts.border + row)) + "\n");
            } else if (line == "HYPERLINK_COUNT") {
                writeAll(controlFd, "OK " +
                         std::to_string(terminal.getHyperlinkCount()) + "\n");
            } else if (line == "READ_ACTIONS") {
                writeAll(controlFd, "OK " + encodeHex(actions) + "\n");
                actions.clear();
            } else if (line == "READ_PRINTER") {
                writeAll(controlFd, "OK " + encodeHex(printerOutput) + "\n");
                printerOutput.clear();
            } else if (line == "STATE") {
                const auto& mouse = terminal.getMouseTrackingState();
                writeAll(controlFd,
                         "OK " + std::to_string(static_cast<unsigned>(mouse.mode)) +
                         " " + std::to_string(static_cast<unsigned>(mouse.enc)) +
                         " " + std::to_string(mouse.focusEventMode) +
                         " " + std::to_string(terminal.getKittyKeyboardFlags()) +
                         "\n");
            } else if (line == "PROTOCOL_STATE") {
                writeAll(controlFd,
                         "OK " + std::to_string(terminal.getScreenReverseVideo()) +
                         " " + std::to_string(terminal.getLedState()) +
                         " " + std::to_string(terminal.getReverseWrapMode()) +
                         " " + std::to_string(terminal.getNationalReplacementMode()) +
                         " 0\n");
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
                if (!(args >> encoding >> type >> modifiers >> motionButton >>
                      button >> column >> row) || encoding > 4 || type > 2) {
                    throw std::runtime_error("invalid mouse event");
                }
                writeAll(controlFd, "OK " + encodeHex(encodeMouseProtocol(
                    static_cast<MouseTrackingEnc>(encoding),
                    static_cast<MouseEventType>(type), modifiers,
                    motionButton, button, column, row)) + "\n");
            } else if (line.compare(0, 6, "OSC52 ") == 0) {
                const Osc52Request request = parseOsc52(
                    decodeHex(line.substr(6)));
                writeAll(controlFd,
                         "OK " + std::to_string(request.valid) + " " +
                         std::to_string(request.query) + " " +
                         std::to_string(request.primary) + " " +
                         std::to_string(request.clipboard) + " " +
                         encodeHex(request.content) + "\n");
            } else if (line.compare(0, 12, "OSC52_REPLY ") == 0) {
                const size_t separator = line.find(' ', 12);
                if (separator == std::string::npos) {
                    throw std::runtime_error("invalid OSC 52 reply");
                }
                writeAll(controlFd, "OK " + encodeHex(encodeOsc52Reply(
                    decodeHex(line.substr(12, separator - 12)),
                    decodeHex(line.substr(separator + 1)))) + "\n");
            } else if (line.compare(0, 9, "OSC7_CWD ") == 0) {
                writeAll(controlFd, "OK " + encodeHex(oscCwdToPath(
                    decodeHex(line.substr(9)))) + "\n");
            } else if (line == "SNAPSHOT") {
                writeAll(controlFd, display.snapshot());
            } else if (line == "SCREEN_TEXT") {
                writeAll(controlFd,
                         "OK " + encodeHex(display.screenText()) + "\n");
            } else if (line == "READ_INPUT") {
                writeAll(controlFd, "OK " + encodeHex(drainInput(io[1])) + "\n");
            } else if (line == "PENDING_OUTPUT") {
                writeAll(controlFd, "OK " + std::to_string(
                    terminal.pendingPtyOutputBytes()) + "\n");
            } else if (line == "FLUSH_OUTPUT") {
                terminal.flushPtyOutput();
                writeAll(controlFd, "OK\n");
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
