#include "testmode.h"

#include "options.h"
#include "vterm.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
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
            viewOffset = frame.getViewOffset();
            delta = true;
        }

        std::string snapshot() const {
            std::ostringstream output;
            output << "OK " << columns << ' ' << rows << ' '
                   << cursor.posX << ' ' << cursor.posY << ' '
                   << static_cast<unsigned>(cursor.style) << ' '
                   << viewOffset << ' ';
            output << std::hex << std::setfill('0');
            for (const auto& cell : cells) {
                output << std::setw(4) << cell.uc_pt;
            }
            output << '\n';
            return output.str();
        }

    private:
        uint16_t columns = 0;
        uint16_t rows = 0;
        uint16_t viewOffset = 0;
        bool delta = false;
        CharVdev::Cursor cursor;
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
}

int runTestMode(int controlFd) {
    int io[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, io) < 0) {
        throw std::runtime_error("test socketpair failed");
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
    TestDisplay display;
    terminal.setRefreshHandler(
        [&display](const Frame& frame) {
            display.update(frame);
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
