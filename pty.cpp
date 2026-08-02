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
    // The reader thread runs at most this far ahead of the parser; the
    // bound caps memory and the reply latency under a flooding child.
    constexpr size_t feedBacklogLimit = 1024 * 1024;
    // One parser bite between yields back to the loop.
    constexpr size_t feedSliceLimit = 256 * 1024;
    // A stuck child bounds the outgoing queue instead of the kernel buffer;
    // fiber writers park on the bound, tryWrite reports it.
    constexpr size_t writeBacklogLimit = 256 * 1024;

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
        size_t enqueueWrite(const u8* data, size_t len, bool& full);
        void writeSpaceReady();
        plt::Scheduler* scheduler() const;
        static void* readerThread(void* opaque);
        static void* writerThread(void* opaque);

        // The write-space doorbell: the writer thread rings it when a
        // parked fiber writer can continue.
        struct WriteWake final: public plt::TimerCallback {
            void ready() override;
            PtyImpl* pty = nullptr;
        };

        Composer& composer_;
        // Blocking master: the reader and writer threads sleep in read and
        // write on it, and no run loop ever waits on the descriptor.
        int fd_;
        PtyStreamOutput output_;
        PtyFeed feed_;
        plt::LoopWake* wake_ = nullptr;
        plt::Fiber* feedFiber_ = nullptr;
        // The reader thread appends to fill_ under the mutex and rings the
        // doorbell only when the feed fiber is parked; the fiber swaps
        // fill_ for drain_ and parses without the lock. The condvar parks
        // the reader while the parser is feedBacklogLimit behind.
        pthread_mutex_t feedMutex_ = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t feedSpace_ = PTHREAD_COND_INITIALIZER;
        Buffer fill_;
        Buffer drain_;
        bool feedEof_ = false;
        bool feedParked_ = false;
        // The mirror for the outgoing direction: producers on the platform
        // thread append to outFill_, the writer thread swaps it for
        // outDrain_ and sleeps in write. ptyMutex serializes fiber writers,
        // so at most one fiber ever waits for space.
        pthread_mutex_t outMutex_ = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t outData_ = PTHREAD_COND_INITIALIZER;
        Buffer outFill_;
        Buffer outDrain_;
        plt::Fiber* outWaiter_ = nullptr;
        plt::LoopWake* outWake_ = nullptr;
        WriteWake writeWake_;
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

// Appends to the outgoing queue and reports whether it hit the bound;
// wakes the writer thread on the queue's empty-to-nonempty edge.
size_t PtyImpl::enqueueWrite(const u8* data, size_t len, bool& full) {
    pthread_mutex_lock(&outMutex_);
    const size_t used = outFill_.used();
    const size_t space = used < writeBacklogLimit ? writeBacklogLimit - used : 0;
    const size_t accepted = len < space ? len : space;
    if (accepted != 0) {
        outFill_.append(data, accepted);
    }
    full = accepted < len;
    pthread_mutex_unlock(&outMutex_);
    if (used == 0 && accepted != 0) {
        pthread_cond_signal(&outData_);
    }
    return accepted;
}

size_t PtyImpl::rawWrite(const void* data, size_t len) {
    const u8* current = (const u8*)(data);
    size_t remaining = len;
    while (remaining != 0) {
        bool full = false;
        const size_t accepted = enqueueWrite(current, remaining, full);
        current += accepted;
        remaining -= accepted;
        if (!full) {
            continue;
        }
        plt::Fiber* const self = scheduler()->current();
        if (self == nullptr) {
            // Teardown paths outside any fiber stay best-effort.
            break;
        }
        pthread_mutex_lock(&outMutex_);
        if (outFill_.used() < writeBacklogLimit) {
            // The writer drained while we were enqueueing.
            pthread_mutex_unlock(&outMutex_);
            continue;
        }
        outWaiter_ = self;
        pthread_mutex_unlock(&outMutex_);
        self->park();
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
        impl.feedParked_ = false;
        if (impl.fill_.used() == 0) {
            const bool finished = impl.feedEof_;
            impl.feedParked_ = !finished;
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
        const ssize_t count = ::read(pty->fd_, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        pthread_mutex_lock(&pty->feedMutex_);
        if (count > 0) {
            while (pty->fill_.used() >= feedBacklogLimit) {
                pthread_cond_wait(&pty->feedSpace_, &pty->feedMutex_);
            }
            pty->fill_.append(buffer, (size_t)(count));
            // The doorbell rings only for a parked fiber: a running one
            // re-checks the buffer on its own and a flood stays silent.
            const bool ring = pty->feedParked_;
            pthread_mutex_unlock(&pty->feedMutex_);
            if (ring) {
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

// Runs forever on its own thread, sleeping in write with the queued bytes;
// like the reader, it dies with the process.
void* PtyImpl::writerThread(void* opaque) {
    auto* const pty = (PtyImpl*)(opaque);
    bool warned = false;
    for (;;) {
        pthread_mutex_lock(&pty->outMutex_);
        while (pty->outFill_.used() == 0) {
            pthread_cond_wait(&pty->outData_, &pty->outMutex_);
        }
        pty->outFill_.xchg(pty->outDrain_);
        const bool ring = pty->outWaiter_ != nullptr;
        pthread_mutex_unlock(&pty->outMutex_);
        if (ring) {
            pty->outWake_->signal();
        }

        const u8* current = (const u8*)(pty->outDrain_.data());
        size_t remaining = pty->outDrain_.used();
        while (remaining != 0) {
            const ssize_t count = ::write(pty->fd_, current, remaining);
            if (count > 0) {
                current += count;
                remaining -= (size_t)(count);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            // A dead child takes its input with it; drain and drop so the
            // producers never wedge.
            if (!warned) {
                sysWarn("pty write");
                warned = true;
            }
            break;
        }
        pty->outDrain_.reset();
    }
    return nullptr;
}

void PtyImpl::WriteWake::ready() {
    pty->writeSpaceReady();
}

void PtyImpl::writeSpaceReady() {
    pthread_mutex_lock(&outMutex_);
    plt::Fiber* const waiter = outWaiter_;
    outWaiter_ = nullptr;
    pthread_mutex_unlock(&outMutex_);
    if (waiter != nullptr) {
        waiter->wake();
    }
}

PtyImpl::PtyImpl(Composer& composer, int fd)
    : composer_(composer)
    , fd_(fd)
    , output_(this)
    , feed_(this) {
    writeWake_.pty = this;
}

PtyImpl::~PtyImpl() {
    // The master stays open: on Darwin closing a tty descriptor blocks
    // while the reader thread sleeps in read on it, and the eternal
    // threads own the descriptor anyway. Process exit reaps all three.
}

Output* PtyImpl::output() {
    return &output_;
}

size_t PtyImpl::tryWrite(const u8* data, size_t len) {
    bool full = false;
    return enqueueWrite(data, len, full);
}

plt::Scheduler* PtyImpl::scheduler() const {
    return composer_.platform->scheduler();
}

void PtyImpl::onListen(void*) {
    resize();
}

void PtyImpl::resize() {
    resizePty(fd_, composer_.columns, composer_.rows, composer_.columns * composer_.glyphWidth, composer_.rows * composer_.glyphHeight);
}

void PtyImpl::start() {
    composer_.resizedListeners.pushBack(this);
    wake_ = composer_.platform->createLoopWake(*composer_.pool, *this);
    outWake_ = composer_.platform->createLoopWake(*composer_.pool, writeWake_);
    // The fiber runs first and parks on the empty buffer, so the handle is
    // set before the reader can ring the doorbell.
    scheduler()->spawn(feed_, feedStack_, sizeof(feedStack_));
    pthread_t reader;
    if (pthread_create(&reader, nullptr, readerThread, this) != 0) {
        sysError("pty reader thread");
    }
    pthread_detach(reader);
    pthread_t writer;
    if (pthread_create(&writer, nullptr, writerThread, this) != 0) {
        sysError("pty writer thread");
    }
    pthread_detach(writer);
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
    // always has a live slave side, so closing the parent's copy cannot
    // kill the pty under the child.
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
