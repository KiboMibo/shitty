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
#include "startup.h"
#include "vterm.h"

#include <plt/fiber.h>
#include <plt/mutex.h>
#include <plt/platform.h>
#include <plt/window.h>

#include <std/alg/xchg.h>
#include <std/ios/input.h>
#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/thr/runable.h>

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
    constexpr size_t maximumWrite = 64 * 1024;

    void resizePty(int fd, u32 columns, u32 rows, u32 pixelWidth, u32 pixelHeight);
    int openPtyMaster(char* slaveName, size_t capacity);
    int openPtySlave(const char* name);

    struct PtyImpl;

    struct PtyStreamInput final: public Input {
        explicit PtyStreamInput(PtyImpl* pty);

        size_t readImpl(void* data, size_t len) override;

        PtyImpl* pty;
    };

    struct PtyStreamOutput final: public Output {
        explicit PtyStreamOutput(PtyImpl* pty);

        size_t writeImpl(const void* data, size_t len) override;
        void flushImpl() override;

        PtyImpl* pty;
    };

    struct PtyFeed final: public Runable {
        explicit PtyFeed(PtyImpl* pty);

        void run() override;

        PtyImpl* pty;
    };

    struct PtyStager final: public Runable {
        explicit PtyStager(PtyImpl* pty);

        void run() override;

        PtyImpl* pty;
    };

    struct PtyImpl final: public Pty, public Listener {
        PtyImpl(Composer& composer, int fd);
        ~PtyImpl();

        Input* input() override;
        Output* output() override;
        void onListen(void*) override;

        void resize();
        void start();
        size_t rawWrite(const void* data, size_t len);
        plt::Scheduler* scheduler() const;

        Composer& composer_;
        int readFd_;
        int writeFd_ = -1;
        PtyStreamInput input_;
        PtyStreamOutput output_;
        PtyFeed feed_;
        PtyStager stager_;
        Buffer staged_;
        plt::Fiber* stagerFiber_ = nullptr;
        u8 inputBuffer[64 * 1024];
    };
}

PtyStreamInput::PtyStreamInput(PtyImpl* pty_)
    : pty(pty_)
{
}

size_t PtyStreamInput::readImpl(void* data, size_t len) {
    for (;;) {
        const ssize_t count = ::read(pty->readFd_, data, len);
        if (count > 0) {
            return (size_t)(count);
        }
        if (count == 0 || (count < 0 && errno == EIO)) {
            return 0;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            plt::Scheduler* const scheduler = pty->scheduler();
            if (scheduler == nullptr || !scheduler->inFiber()) {
                return 0;
            }
            if (!scheduler->awaitReadable(pty->readFd_, 0)) {
                return 0;
            }
            continue;
        }
        sysWarn("pty read");
        return 0;
    }
}

PtyStreamOutput::PtyStreamOutput(PtyImpl* pty_)
    : pty(pty_)
{
}

size_t PtyStreamOutput::writeImpl(const void* data, size_t len) {
    plt::Scheduler* const scheduler = pty->scheduler();
    if (scheduler != nullptr && scheduler->inFiber()) {
        // A fiber holds composer.ptyMutex by convention and may block on
        // the descriptor directly.
        return pty->rawWrite(data, len);
    }
    // An event callback must not block; the bytes wait for the staging
    // fiber, which replays them under the mutex in arrival order.
    pty->staged_.append(data, len);
    return len;
}

void PtyStreamOutput::flushImpl() {
    if (!pty->staged_.empty() && pty->stagerFiber_ != nullptr) {
        pty->stagerFiber_->wake();
    }
}

size_t PtyImpl::rawWrite(const void* data, size_t len) {
    const u8* current = (const u8*)(data);
    size_t remaining = len;
    while (remaining != 0) {
        const size_t chunk = remaining < maximumWrite ? remaining : maximumWrite;
        const ssize_t count = ::write(writeFd_, current, chunk);
        if (count > 0) {
            current += count;
            remaining -= (size_t)(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            plt::Scheduler* const awaiting = scheduler();
            if (awaiting == nullptr || !awaiting->inFiber()) {
                break;
            }
            if (!awaiting->awaitWritable(writeFd_, 0)) {
                break;
            }
            continue;
        }
        sysWarn("pty write");
        break;
    }
    return len;
}

PtyStager::PtyStager(PtyImpl* pty_)
    : pty(pty_)
{
}

void PtyStager::run() {
    PtyImpl& impl = *pty;
    plt::Scheduler* const scheduler = impl.scheduler();
    impl.stagerFiber_ = scheduler->current();
    Buffer local;
    for (;;) {
        while (impl.staged_.empty()) {
            impl.stagerFiber_->park();
        }
        impl.composer_.ptyMutex->lock(*scheduler);
        // Bytes staged while a replay blocks in the descriptor drain in the
        // same round, still ahead of any writer queued on the mutex.
        while (!impl.staged_.empty()) {
            xchg(local, impl.staged_);
            impl.rawWrite(local.data(), local.used());
            local.reset();
        }
        impl.composer_.ptyMutex->unlock();
    }
}

PtyFeed::PtyFeed(PtyImpl* pty_)
    : pty(pty_)
{
}

void PtyFeed::run() {
    PtyImpl& impl = *pty;
    plt::Scheduler* const scheduler = impl.scheduler();
    for (;;) {
        // One chunk per readiness round keeps frame callbacks and other
        // events interleaved with a flooding child, like the poll-driven
        // reader this fiber replaces.
        if (!scheduler->awaitReadable(impl.readFd_, 0)) {
            break;
        }
        const size_t count = impl.input_.read(impl.inputBuffer, sizeof(impl.inputBuffer));
        if (count == 0) {
            break;
        }
        Vterm* const vterm = impl.composer_.vterm;
        if (vterm != nullptr) {
            vterm->feedPty(StringView(impl.inputBuffer, count));
        }
        if (impl.composer_.window != nullptr) {
            impl.composer_.window->requestFrame();
        }
    }
    if (impl.composer_.window != nullptr) {
        impl.composer_.window->requestClose();
    }
}

PtyImpl::PtyImpl(Composer& composer, int fd)
    : composer_(composer)
    , readFd_(fd)
    , input_(this)
    , output_(this)
    , feed_(this)
    , stager_(this) {
    const int flags = fcntl(readFd_, F_GETFL, 0);
    if (flags < 0 || fcntl(readFd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        const int error = errno;
        close(readFd_);
        readFd_ = -1;
        throw std::runtime_error("cannot make PTY nonblocking: " + std::string(strerror(error)));
    }
    // The poller keys registrations by descriptor, and the read and write
    // fibers wait concurrently; give the write side its own descriptor for
    // the same PTY.
    writeFd_ = fcntl(readFd_, F_DUPFD_CLOEXEC, 0);
    if (writeFd_ < 0) {
        const int error = errno;
        close(readFd_);
        readFd_ = -1;
        throw std::runtime_error("cannot duplicate PTY descriptor: " + std::string(strerror(error)));
    }
}

PtyImpl::~PtyImpl() {
    if (readFd_ >= 0) {
        close(readFd_);
    }
    if (writeFd_ >= 0) {
        close(writeFd_);
    }
}

Input* PtyImpl::input() {
    return &input_;
}

Output* PtyImpl::output() {
    return &output_;
}

plt::Scheduler* PtyImpl::scheduler() const {
    return composer_.platform == nullptr ? nullptr : composer_.platform->scheduler();
}

void PtyImpl::onListen(void*) {
    resize();
}

void PtyImpl::resize() {
    resizePty(readFd_, composer_.columns, composer_.rows, composer_.columns * composer_.glyphWidth, composer_.rows * composer_.glyphHeight);
}

void PtyImpl::start() {
    composer_.resizedListeners.pushBack(this);
    scheduler()->spawn(stager_);
    scheduler()->spawn(feed_);
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
