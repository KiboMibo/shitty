/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
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
#include <plt/loop_wake.h>
#include <plt/mutex.h>
#include <plt/poller.h>
#include <plt/platform.h>
#include <plt/window.h>

#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/thr/runable.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
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
    // The reader thread runs at most this far ahead of the parser; the
    // bound caps memory and the reply latency under a flooding child.
    constexpr size_t feedBacklogLimit = 1024 * 1024;
    // One parser bite between yields back to the loop.
    constexpr size_t feedSliceLimit = 256 * 1024;

    void resizePty(int fd, u32 columns, u32 rows, u32 pixelWidth, u32 pixelHeight);
    int openPtyMaster(char* slaveName, size_t capacity);
    int openPtySlave(const char* name);

    struct PtyImpl;

    struct PtyStreamOutput final: public Output {
        explicit PtyStreamOutput(PtyImpl* pty);

        size_t writeImpl(const void* data, size_t len) override;

        PtyImpl* pty;
    };

    struct PtyFeed final: public Runable {
        explicit PtyFeed(PtyImpl* pty);

        void run() override;

        PtyImpl* pty;
    };

    struct PtyImpl final: public Pty, public Listener, public plt::TimerCallback {
        PtyImpl(Composer& composer, int fd);
        ~PtyImpl();

        Output* output() override;
        size_t tryWrite(const u8* data, size_t len) override;
        void onListen(void*) override;
        // The reader thread's doorbell, delivered on the platform thread.
        void ready() override;

        void resize();
        void start();
        size_t rawWrite(const void* data, size_t len);
        plt::Scheduler* scheduler() const;
        static void* readerThread(void* opaque);

        Composer& composer_;
        int readFd_;
        int writeFd_ = -1;
        PtyStreamOutput output_;
        PtyFeed feed_;
        plt::LoopWake* wake_ = nullptr;
        plt::Fiber* feedFiber_ = nullptr;
        // The reader thread appends to fill_ under the mutex and rings the
        // doorbell on its empty-to-nonempty edge; the feed fiber swaps
        // fill_ for drain_ and parses without the lock. The condvar parks
        // the reader while the parser is feedBacklogLimit behind.
        pthread_mutex_t feedMutex_ = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t feedSpace_ = PTHREAD_COND_INITIALIZER;
        Buffer fill_;
        Buffer drain_;
        bool feedEof_ = false;
        // The feed fiber keeps the whole parser depth on this stack.
        alignas(16) u8 feedStack_[256 * 1024];
    };
}

PtyStreamOutput::PtyStreamOutput(PtyImpl* pty_)
    : pty(pty_)
{
}

size_t PtyStreamOutput::writeImpl(const void* data, size_t len) {
    plt::Scheduler* const scheduler = pty->scheduler();
    plt::FiberMutex* const mutex = pty->composer_.ptyMutex;
    if (scheduler->current() == nullptr) {
        // Teardown paths outside any fiber degrade to a best-effort write.
        return pty->rawWrite(data, len);
    }
    if (mutex->heldByCurrent(*scheduler)) {
        // A transaction owns the stream and writes through.
        return pty->rawWrite(data, len);
    }
    const plt::LockGuard guard(*mutex, *scheduler);
    return pty->rawWrite(data, len);
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
            if (awaiting->current() == nullptr) {
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

PtyFeed::PtyFeed(PtyImpl* pty_)
    : pty(pty_)
{
}

void PtyFeed::run() {
    PtyImpl& impl = *pty;
    impl.feedFiber_ = impl.scheduler()->current();
    for (;;) {
        pthread_mutex_lock(&impl.feedMutex_);
        if (impl.fill_.used() == 0) {
            const bool finished = impl.feedEof_;
            pthread_mutex_unlock(&impl.feedMutex_);
            if (finished) {
                break;
            }
            // The doorbell callback wakes us; a wake that raced a previous
            // batch is remembered and re-checks the buffer at once.
            impl.feedFiber_->park();
            continue;
        }
        impl.fill_.xchg(impl.drain_);
        pthread_mutex_unlock(&impl.feedMutex_);
        pthread_cond_signal(&impl.feedSpace_);

        const u8* data = (const u8*)(impl.drain_.data());
        size_t remaining = impl.drain_.used();
        while (remaining != 0) {
            const size_t slice = remaining < feedSliceLimit ? remaining : feedSliceLimit;
            impl.composer_.vterm->feedPty(StringView(data, slice));
            data += slice;
            remaining -= slice;
            // One slice per loop round keeps frames and input interleaved
            // with a flooding child; the reader drains the pty meanwhile.
            // The vterm schedules frames itself when the fed bytes actually
            // change the presentation.
            impl.scheduler()->yield();
        }
        impl.drain_.reset();
    }
    impl.composer_.window->requestClose();
}

// Runs forever on its own thread: the poll makes the read synchronous while
// the master stays nonblocking for the write side, which shares the file
// description. Draining the pty must not wait on the parser, or the child
// blocks on a full kernel buffer whenever the parser is busy - the whole
// point of the thread. There is no teardown: the thread dies with the
// process.
void* PtyImpl::readerThread(void* opaque) {
    auto* const pty = (PtyImpl*)(opaque);
    u8 buffer[64 * 1024];
    for (;;) {
        const ssize_t count = ::read(pty->readFd_, buffer, sizeof(buffer));
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Dry: sleep in poll until the child writes. Under a flood the
            // read almost always lands and the poll never runs.
            struct pollfd readable{pty->readFd_, POLLIN, 0};
            (void)!::poll(&readable, 1, -1);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        pthread_mutex_lock(&pty->feedMutex_);
        if (count > 0) {
            while (pty->fill_.used() >= feedBacklogLimit) {
                pthread_cond_wait(&pty->feedSpace_, &pty->feedMutex_);
            }
            const bool wasIdle = pty->fill_.used() == 0;
            pty->fill_.append(buffer, (size_t)(count));
            pthread_mutex_unlock(&pty->feedMutex_);
            if (wasIdle) {
                pty->wake_->signal();
            }
            continue;
        }
        // EOF or EIO: the feed drains what is buffered, then closes.
        pty->feedEof_ = true;
        pthread_mutex_unlock(&pty->feedMutex_);
        pty->wake_->signal();
        return nullptr;
    }
}

void PtyImpl::ready() {
    if (feedFiber_ != nullptr) {
        feedFiber_->wake();
    }
}

PtyImpl::PtyImpl(Composer& composer, int fd)
    : composer_(composer)
    , readFd_(fd)
    , output_(this)
    , feed_(this) {
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

Output* PtyImpl::output() {
    return &output_;
}

size_t PtyImpl::tryWrite(const u8* data, size_t len) {
    size_t accepted = 0;
    while (accepted != len) {
        const size_t chunk = len - accepted < maximumWrite ? len - accepted : maximumWrite;
        const ssize_t count = ::write(writeFd_, data + accepted, chunk);
        if (count > 0) {
            accepted += (size_t)(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    return accepted;
}

plt::Scheduler* PtyImpl::scheduler() const {
    return composer_.platform->scheduler();
}

void PtyImpl::onListen(void*) {
    resize();
}

void PtyImpl::resize() {
    resizePty(readFd_, composer_.columns, composer_.rows, composer_.columns * composer_.glyphWidth, composer_.rows * composer_.glyphHeight);
}

void PtyImpl::start() {
    composer_.resizedListeners.pushBack(this);
    wake_ = composer_.platform->createLoopWake(*composer_.pool, *this);
    // The fiber runs first and parks on the empty buffer, so the handle is
    // set before the reader can ring the doorbell.
    scheduler()->spawn(feed_, feedStack_, sizeof(feedStack_));
    pthread_t reader;
    if (pthread_create(&reader, nullptr, readerThread, this) != 0) {
        sysError("pty reader thread");
    }
    pthread_detach(reader);
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
        // O_NOCTTY: the descriptor is opened in the parent; the child takes
        // the controlling terminal explicitly with TIOCSCTTY after setsid.
        const int slave = open(name, O_RDWR | O_NOCTTY);
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
    // The slave opens before the fork and the child inherits it: the pty
    // always has a live slave side, so the parent's nonblocking setup on
    // the master cannot fail (macOS refuses F_SETFL until a slave exists)
    // and closing the parent's copy cannot kill the pty under the child.
    const int slave = openPtySlave(slaveName);
    const pid_t pid = fork();
    if (pid < 0) {
        const int error = errno;
        close(master);
        close(slave);
        errno = error;
        sysError("fork");
    }
    if (pid == 0) {
        if (setsid() < 0) {
            sysError("setsid");
        }
        if (ioctl(slave, TIOCSCTTY, 0) < 0) {
            sysError("TIOCSCTTY");
        }
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
    close(slave);
    pty->start();
    return pty;
}
