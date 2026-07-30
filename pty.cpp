/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* part of this file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

/* The source code in this file is inspired by code samples in the book
 *   Advanced Programming in the UNIX Environment, 3rd Edition
 *   by W. Richard Stevens & Stephen A. Rago
 *   Addison-Wesley, 2013
 *
 * The original example code of the book is available from
 *   http://www.apuebook.com/code3e.html
 */

#if defined(__APPLE__)
    #define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700

#include "pty.h"

#include "composer.h"
#include "fd_redirect.h"
#include "listener.h"
#include "pty_output.h"
#include "startup.h"
#include "vterm.h"

#include <plt/platform.h>
#include <plt/poller.h>
#include <plt/window.h>

#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/thr/poll_fd.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace stl;

namespace {
    void resizePty(int fd, u32 columns, u32 rows, u32 pixelWidth, u32 pixelHeight);
    int openPtyMaster(char* slaveName, size_t capacity);
    int openPtySlave(const char* name);

    struct PtyImpl final: public Pty, public plt::PollCallback, public Listener {
        PtyImpl(Composer& composer, int fd);
        ~PtyImpl();

        ssize_t write(const u8* buffer, size_t size) override;
        void outputReady() override;
        void ready(PollFD event) override;
        void onListen(void*) override;

        void resize();
        void start();
        void updateInterest(bool outputPending);
        bool readInput();

        Composer& composer_;
        int fd_;
        bool handlingReady = false;
        bool finished = false;
        u8 inputBuffer[64 * 1024];
    };
}

PtyImpl::PtyImpl(Composer& composer, int fd)
    : composer_(composer)
    , fd_(fd) {
    const int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        const int error = errno;
        close(fd_);
        fd_ = -1;
        throw std::runtime_error("cannot make PTY nonblocking: " + std::string(strerror(error)));
    }
}

PtyImpl::~PtyImpl() {
    if (fd_ >= 0) {
        composer_.platform->poller()->disarm(fd_);
        close(fd_);
    }
}

ssize_t PtyImpl::write(const u8* buffer, size_t size) {
    return ::write(fd_, buffer, size);
}

void PtyImpl::outputReady() {
    if (!handlingReady && !finished) {
        updateInterest(composer_.ptyOutputs->flush());
    }
}

void PtyImpl::onListen(void*) {
    resize();
}

void PtyImpl::resize() {
    resizePty(fd_, composer_.columns, composer_.rows, composer_.columns * composer_.glyphWidth, composer_.rows * composer_.glyphHeight);
}

void PtyImpl::start() {
    composer_.resizedListeners.pushBack(this);
    composer_.platform->poller()->arm({fd_, PollFlag::In}, *this);
}

void PtyImpl::ready(PollFD event) {
    if (event.fd != fd_ || finished) {
        return;
    }

    handlingReady = true;
    if (event.flags & (PollFlag::In | PollFlag::Err | PollFlag::Hup)) {
        finished = readInput();
    }
    const bool outputPending = composer_.ptyOutputs->flush();
    handlingReady = false;

    if (finished) {
        composer_.platform->poller()->disarm(fd_);
        composer_.window->requestClose();
    } else {
        updateInterest(outputPending);
    }
    composer_.window->requestFrame();
}

void PtyImpl::updateInterest(bool outputPending) {
    u32 mode = PollFlag::In;
    if (outputPending) {
        mode |= PollFlag::Out;
    }
    composer_.platform->poller()->arm({fd_, mode}, *this);
}

bool PtyImpl::readInput() {
    Vterm* const vterm = composer_.vterm;
    const ssize_t count = ::read(fd_, inputBuffer, sizeof(inputBuffer));
    if (count > 0) {
        vterm->feedPty(StringView(inputBuffer, count));
        return false;
    }
    if (count == 0 || (count < 0 && errno == EIO)) {
        return true;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        return false;
    }
    sysWarn("pty read");
    return true;
}

namespace {
    int openPtyMaster(char* slaveName, size_t capacity) {
        const int master = posix_openpt(O_RDWR);
        if (master < 0) {
            sysError("can't open master pty: posix_openpt()");
        }
        // Close-on-exec: without it every spawned helper (xdg-open and the
        // browser it launches) inherits the master and can read terminal
        // output and inject input.
        if (fcntl(master, F_SETFD, FD_CLOEXEC) < 0) {
            sysError("can't open master pty: fcntl(FD_CLOEXEC)");
        }
        if (grantpt(master) < 0) {
            sysError("can't open master pty: grantpt()");
        }
        if (unlockpt(master) < 0) {
            sysError("can't open master pty: unlockpt()");
        }
        const char* const name = ptsname(master);
        if (name == nullptr) {
            sysError("can't open master pty: ptsname()");
        }
        strncpy(slaveName, name, capacity);
        slaveName[capacity - 1] = '\0';
        return master;
    }

    int openPtySlave(const char* name) {
        const int slave = open(name, O_RDWR);
        if (slave < 0) {
            sysError("can't open slave pty: open()");
        }
        return slave;
    }

    void resizePty(int fd, u32 columns, u32 rows, u32 pixelWidth, u32 pixelHeight) {
        struct winsize size{};
        size.ws_col = columns;
        size.ws_row = rows;
        size.ws_xpixel = pixelWidth;
        size.ws_ypixel = pixelHeight;
        if (ioctl(fd, TIOCSWINSZ, &size) < 0) {
            sysError("TIOCSWINSZ on pty");
        }
    }
}

Pty* Pty::create(Composer& composer, const LaunchCommand& command) {
    std::vector<char*> arguments;
    arguments.reserve(command.arguments.size() + 1);
    for (const std::string& argument : command.arguments) {
        arguments.push_back(const_cast<char*>(argument.c_str()));
    }
    arguments.push_back(nullptr);

    char slaveName[PATH_MAX];
    const int master = openPtyMaster(slaveName, sizeof(slaveName));
    const pid_t pid = fork();
    if (pid < 0) {
        const int error = errno;
        close(master);
        errno = error;
        sysError("fork");
    }
    if (pid == 0) {
        if (setsid() < 0) {
            sysError("setsid");
        }
        const int slave = openPtySlave(slaveName);
        close(master);
        resizePty(slave, composer.columns, composer.rows, composer.columns * composer.glyphWidth, composer.rows * composer.glyphHeight);
        redirectFds(slave);

        struct termios term;
        if (tcgetattr(STDIN_FILENO, &term) < 0) {
            sysError("tcgetattr");
        }
        term.c_iflag |= IUTF8;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &term) < 0) {
            sysError("tcsetattr");
        }
        configureTerminalChildEnvironment();
        execvp(command.executable.c_str(), arguments.data());
        sysError("execvp of ", command.executable.c_str());
    }
    PtyImpl* const pty = composer.pool->make<PtyImpl>(composer, master);
    pty->start();
    return pty;
}
