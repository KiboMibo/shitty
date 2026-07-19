#include "testmode.h"

#include "options.h"
#include "mouseprotocol.h"
#include "pty.h"
#include "vterm.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
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
        void update(const Frame& frame) {
            Frame current = frame;
            const size_t count = frame.nCols * frame.nRows;
            if (columns != frame.nCols || rows != frame.nRows) {
                columns = frame.nCols;
                rows = frame.nRows;
                cells.resize(count);
                delta = false;
            }
            if (delta) {
                current.deltaCopyCells(cells.data());
            } else {
                current.fullCopyCells(cells.data());
            }
            for (auto& cell : cells) {
                cell.dirty = 0;
            }
            cursor = frame.getCursor();
            selection = frame.getSelection();
            viewOffset = frame.getViewOffset();
            ++refreshCount;
            delta = true;
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
                    (cell.wrap << 6);
                output << std::setw(4) << cell.uc_pt
                       << std::setw(2) << flags
                       << std::setw(2) << static_cast<unsigned>(cell.fg.red)
                       << std::setw(2) << static_cast<unsigned>(cell.fg.green)
                       << std::setw(2) << static_cast<unsigned>(cell.fg.blue)
                       << std::setw(2) << static_cast<unsigned>(cell.bg.red)
                       << std::setw(2) << static_cast<unsigned>(cell.bg.green)
                       << std::setw(2) << static_cast<unsigned>(cell.bg.blue)
                       << std::setw(8) << cell.hyperlink;
            }
            output << '\n';
            return output.str();
        }

    private:
        uint16_t columns = 0;
        uint16_t rows = 0;
        uint16_t viewOffset = 0;
        uint64_t refreshCount = 0;
        bool delta = false;
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
            display.update(frame);
        });
    std::string actions;
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
    terminal.redraw();
    writeAll(controlFd, "READY\n");

    std::string buffered;
    std::string line;
    while (readLine(controlFd, buffered, line)) {
        try {
            if (line.compare(0, 6, "WRITE ") == 0) {
                terminal.feedPtyOutput(decodeHex(line.substr(6)));
                writeAll(controlFd, "OK\n");
            } else if (line == "PAGE_UP") {
                terminal.pageUp();
                writeAll(controlFd, "OK\n");
            } else if (line == "PAGE_DOWN") {
                terminal.pageDown();
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
            } else if (line == "READ_ACTIONS") {
                writeAll(controlFd, "OK " + encodeHex(actions) + "\n");
                actions.clear();
            } else if (line == "STATE") {
                const auto& mouse = terminal.getMouseTrackingState();
                writeAll(controlFd,
                         "OK " + std::to_string(static_cast<unsigned>(mouse.mode)) +
                         " " + std::to_string(static_cast<unsigned>(mouse.enc)) +
                         " " + std::to_string(mouse.focusEventMode) +
                         " " + std::to_string(terminal.getKittyKeyboardFlags()) +
                         "\n");
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
                      button >> column >> row) || encoding > 3 || type > 2) {
                    throw std::runtime_error("invalid mouse event");
                }
                writeAll(controlFd, "OK " + encodeHex(encodeMouseProtocol(
                    static_cast<MouseTrackingEnc>(encoding),
                    static_cast<MouseEventType>(type), modifiers,
                    motionButton, button, column, row)) + "\n");
            } else if (line == "SNAPSHOT") {
                writeAll(controlFd, display.snapshot());
            } else if (line == "READ_INPUT") {
                writeAll(controlFd, "OK " + encodeHex(drainInput(io[1])) + "\n");
            } else if (line == "QUIT") {
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
