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

#include "application.h"
#include "composer.h"
#include "fd_redirect.h"
#include "listener.h"
#include "pty_output.h"
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

#include <stdexcept>
#include <string>

using namespace stl;

namespace {
    struct PtyImpl;

    struct CallPtyResize final: public Listener {
        explicit CallPtyResize(PtyImpl* pty);

        void onListen(void*) override;

        PtyImpl* pty;
    };

    struct PtyImpl final: public Pty, public plt::PollCallback {
        PtyImpl(Composer& composer, int fd);
        ~PtyImpl();

        int fd() const override;
        ssize_t read(u8* buffer, size_t size) override;
        ssize_t write(const u8* buffer, size_t size) override;
        void outputReady() override;
        void ready(PollFD event) override;

        void applySize();
        void wire();
        void updateInterest(bool outputPending);
        bool readInput();

        Composer& composer_;
        int fd_;
        bool handlingReady = false;
        bool finished = false;
        u8 inputBuffer[64 * 1024];
    };
}

CallPtyResize::CallPtyResize(PtyImpl* pty_)
    : pty(pty_)
{
}

void CallPtyResize::onListen(void*) {
    pty->applySize();
}

PtyImpl::PtyImpl(Composer& composer, int fd)
    : composer_(composer)
    , fd_(fd)
{
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

int PtyImpl::fd() const {
    return fd_;
}

ssize_t PtyImpl::read(u8* buffer, size_t size) {
    return ::read(fd_, buffer, size);
}

ssize_t PtyImpl::write(const u8* buffer, size_t size) {
    return ::write(fd_, buffer, size);
}

void PtyImpl::outputReady() {
    if (!handlingReady && !finished) {
        updateInterest(composer_.ptyOutputs->flush());
    }
}

void PtyImpl::applySize() {
    pty_resize(fd_, composer_.columns, composer_.rows, composer_.columns * composer_.glyphWidth, composer_.rows * composer_.glyphHeight);
}

void PtyImpl::wire() {
    composer_.resizedListeners.pushBack(composer_.pool->make<CallPtyResize>(this));
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
    composer_.application->defer();
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
    const ssize_t count = read(inputBuffer, sizeof(inputBuffer));
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

Pty* Pty::adopt(Composer& composer, int fd) {
    PtyImpl* const pty = composer.pool->make<PtyImpl>(composer, fd);
    pty->wire();
    return pty;
}

int ptym_open(char* pts_name, int pts_namesz) {
    char* ptr;
    int fdm;

    if ((fdm = posix_openpt(O_RDWR)) < 0) {
        sysError("can't open master pty: posix_openpt()");
    }
    if (grantpt(fdm) < 0) {
        sysError("can't open master pty: grantpt()");
    }
    if (unlockpt(fdm) < 0) {
        sysError("can't open master pty: unlockpt()");
    }
    if ((ptr = ptsname(fdm)) == nullptr) {
        sysError("can't open master pty: ptsname()");
    }

    strncpy(pts_name, ptr, pts_namesz);
    pts_name[pts_namesz - 1] = '\0';
    return fdm;
}

int ptys_open(char* pts_name) {
    int fds = open(pts_name, O_RDWR);
    if (fds < 0) {
        sysError("can't open slave pty: open()");
    }
    return fds;
}

pid_t pty_fork(int& o_ptyFd, int cols, int rows, int pixelWidth, int pixelHeight) {
    pid_t pid;
    char pts_name[PATH_MAX];
    int fdm = ptym_open(pts_name, sizeof(pts_name));

    pid = fork();

    if (pid < 0) {
        return pid;
    } else if (pid == 0) {
        if (setsid() < 0) {
            sysError("setsid");
        }

        int fds = ptys_open(pts_name);

        close(fdm);

        pty_resize(fds, cols, rows, pixelWidth, pixelHeight);

        redirectFds(fds);

        struct termios term;
        if (tcgetattr(STDIN_FILENO, &term) < 0) {
            sysError("tcgetattr");
        }
        term.c_iflag |= IUTF8;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &term) < 0) {
            sysError("tcsetattr");
        }
    } else {
        o_ptyFd = fdm;
    }
    return pid;
}

void pty_resize(int ptyFd, int cols, int rows, int pixelWidth, int pixelHeight) {
    struct winsize wsize{};
    wsize.ws_col = cols;
    wsize.ws_row = rows;
    wsize.ws_xpixel = pixelWidth;
    wsize.ws_ypixel = pixelHeight;
    if (ioctl(ptyFd, TIOCSWINSZ, &wsize) < 0) {
        sysError("TIOCSWINSZ on pty");
    }
}
