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

#include "startup.h"

#include <plt/fiber.h>

#include <std/ios/input.h>
#include <std/ios/out_buf.h>
#include <std/ios/output.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

using namespace stl;

namespace {
    // The forked terminal child rewires its standard descriptors onto the
    // PTY slave; the originals remain close-on-exec solely so a failure
    // between fork and exec can still report on the real stderr.
    int originalFds[3] = {-1, -1, -1};
    const int targetFds[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};

    void restoreFds() {
        for (int index = 0; index < 3; ++index) {
            if (originalFds[index] >= 0) {
                dup2(originalFds[index], targetFds[index]);
                close(originalFds[index]);
                originalFds[index] = -1;
            }
        }
    }

    [[noreturn]] void childError(const char* message) {
        restoreFds();
        (void)!write(STDERR_FILENO, message, __builtin_strlen(message));
        (void)!write(STDERR_FILENO, "\n", 1);
        _exit(127);
    }

    [[noreturn]] void sysError(const char* message) {
        const int error = errno;
        restoreFds();
        OutBuf output(stderrStream());
        output << StringView(u8"Error: ") << StringView(message) << StringView(u8": ") << StringView(strerror(error)) << StringView(u8" (errno=") << (i64)(error) << StringView(u8")") << endL << finI;
        exit(1);
    }

    void sysWarn(const char* message) {
        const int error = errno;
        sysE << StringView(u8"Warning: ") << StringView(message) << StringView(u8": ") << StringView(strerror(error)) << StringView(u8" (errno=") << (i64)(error) << StringView(u8")") << endL;
    }

    void saveFds() {
        for (int index = 0; index < 3; ++index) {
            originalFds[index] = fcntl(targetFds[index], F_DUPFD_CLOEXEC, 3);
            if (originalFds[index] < 0) {
                childError("Error: F_DUPFD_CLOEXEC");
            }
        }
    }

    void redirectFds(int fd) {
        saveFds();
        for (int index = 0; index < 3; ++index) {
            if (dup2(fd, targetFds[index]) != targetFds[index]) {
                childError("Error: dup2");
            }
        }
        if (fd != targetFds[0] && fd != targetFds[1] && fd != targetFds[2]) {
            close(fd);
        }
    }

    int openPtyMaster(char* slaveName, size_t capacity) {
        const int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (master < 0) {
            sysError("can't open master pty: posix_openpt()");
        }
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
        const StringView text(name);
        const size_t length = text.length() < capacity - 1 ? text.length() : capacity - 1;
        memcpy(slaveName, text.data(), length);
        slaveName[length] = '\0';
        return master;
    }

    int openPtySlave(const char* name) {
        const int slave = open(name, O_RDWR | O_NOCTTY);
        if (slave < 0) {
            sysError("can't open slave pty: open()");
        }
        return slave;
    }

    void resizePty(int fd, const PtySize& requested) {
        struct winsize size{};
        size.ws_col = (unsigned short)(requested.columns);
        size.ws_row = (unsigned short)(requested.rows);
        size.ws_xpixel = (unsigned short)(requested.pixelWidth);
        size.ws_ypixel = (unsigned short)(requested.pixelHeight);
        if (ioctl(fd, TIOCSWINSZ, &size) < 0) {
            sysWarn("TIOCSWINSZ on pty");
        }
    }

    bool waitFor(plt::Scheduler& scheduler, int fd, bool readable) {
        if (scheduler.current() != nullptr) {
            return readable ? scheduler.awaitReadable(fd, 0) : scheduler.awaitWritable(fd, 0);
        }
        pollfd event{fd, (short)(readable ? POLLIN : POLLOUT), 0};
        int result;
        do {
            result = poll(&event, 1, -1);
        } while (result < 0 && errno == EINTR);
        return result > 0;
    }

    struct PtyHandleImpl;

    struct PtyInput final: public Input {
        explicit PtyInput(PtyHandleImpl& handle);

        size_t readImpl(void* data, size_t len) override;

        PtyHandleImpl& handle;
    };

    struct PtyOutput final: public Output {
        explicit PtyOutput(PtyHandleImpl& handle);

        size_t writeImpl(const void* data, size_t len) override;

        PtyHandleImpl& handle;
        bool warned = false;
    };

    struct PtyHandleImpl final: public PtyHandle {
        PtyHandleImpl(plt::Scheduler& scheduler, int fd, pid_t pid);
        ~PtyHandleImpl() noexcept;

        Input* input() override;
        Output* output() override;
        void resize(const PtySize& size) override;

        plt::Scheduler& scheduler;
        int fd;
        pid_t pid;
        bool eof = false;
        PtyInput input_{*this};
        PtyOutput output_{*this};
    };

    struct PtyImpl final: public Pty {
        explicit PtyImpl(plt::Scheduler& scheduler);

        PtyHandle* spawn(ObjPool& owner, const LaunchCommand& command) override;

        plt::Scheduler& scheduler;
    };
}

PtyInput::PtyInput(PtyHandleImpl& handle_)
    : handle(handle_)
{
}

size_t PtyInput::readImpl(void* data, size_t len) {
    for (;;) {
        const ssize_t count = ::read(handle.fd, data, len);
        if (count > 0) {
            return (size_t)(count);
        }
        if (count == 0 || (count < 0 && errno == EIO)) {
            handle.eof = true;
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (waitFor(handle.scheduler, handle.fd, true)) {
                continue;
            }
            handle.eof = true;
            return 0;
        }
        sysWarn("pty read");
        handle.eof = true;
        return 0;
    }
}

PtyOutput::PtyOutput(PtyHandleImpl& handle_)
    : handle(handle_)
{
}

size_t PtyOutput::writeImpl(const void* data, size_t len) {
    for (;;) {
        const ssize_t count = ::write(handle.fd, data, len);
        if (count > 0) {
            return (size_t)(count);
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (waitFor(handle.scheduler, handle.fd, false)) {
                continue;
            }
        }
        // A dead child takes its input with it. Consume and drop so
        // Output::writeC cannot spin forever on a zero-length write.
        if (!warned && count < 0 && errno != EIO && errno != EPIPE && errno != EBADF) {
            sysWarn("pty write");
            warned = true;
        }
        return len;
    }
}

PtyHandleImpl::PtyHandleImpl(plt::Scheduler& scheduler_, int fd_, pid_t pid_)
    : scheduler(scheduler_)
    , fd(fd_)
    , pid(pid_)
{
}

PtyHandleImpl::~PtyHandleImpl() noexcept {
    if (pid > 0 && !eof) {
        // The child called setsid(), so its pid is also the session group.
        // Signal both to cover destruction racing the child's setsid().
        kill(-pid, SIGHUP);
        kill(pid, SIGHUP);
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

Input* PtyHandleImpl::input() {
    return &input_;
}

Output* PtyHandleImpl::output() {
    return &output_;
}

void PtyHandleImpl::resize(const PtySize& size) {
    if (fd >= 0) {
        resizePty(fd, size);
    }
}

PtyImpl::PtyImpl(plt::Scheduler& scheduler_)
    : scheduler(scheduler_)
{
}

PtyHandle* PtyImpl::spawn(ObjPool& owner, const LaunchCommand& command) {
    Vector<char*> arguments;
    for (size_t index = 0; index < command.offsets.length(); ++index) {
        arguments.pushBack(const_cast<char*>(command.argument(index)));
    }
    arguments.pushBack(nullptr);

    char slaveName[PATH_MAX];
    const int master = openPtyMaster(slaveName, sizeof(slaveName));
    const int slave = openPtySlave(slaveName);

    // A child that exits immediately must not outrun the caller's SIGCHLD
    // bookkeeping. The child restores the inherited mask before exec.
    sigset_t childSignal;
    sigemptyset(&childSignal);
    sigaddset(&childSignal, SIGCHLD);
    sigset_t previousMask;
    sigprocmask(SIG_BLOCK, &childSignal, &previousMask);
    const pid_t pid = fork();
    if (pid < 0) {
        const int error = errno;
        close(master);
        close(slave);
        sigprocmask(SIG_SETMASK, &previousMask, nullptr);
        errno = error;
        sysError("fork");
    }
    if (pid == 0) {
        sigprocmask(SIG_SETMASK, &previousMask, nullptr);
        if (setsid() < 0) {
            childError("Error: setsid");
        }
        if (ioctl(slave, TIOCSCTTY, 0) < 0) {
            childError("Error: TIOCSCTTY");
        }
        close(master);
        redirectFds(slave);

        struct termios term;
        if (tcgetattr(STDIN_FILENO, &term) < 0) {
            childError("Error: tcgetattr");
        }
        term.c_iflag |= IUTF8;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &term) < 0) {
            childError("Error: tcsetattr");
        }
        execvp(command.executable(), arguments.mutData());
        childError("Error: execvp");
    }
    sigprocmask(SIG_SETMASK, &previousMask, nullptr);
    close(slave);
    return owner.make<PtyHandleImpl>(scheduler, master, pid);
}

Pty* createPty(ObjPool& owner, plt::Scheduler& scheduler) {
    return owner.make<PtyImpl>(scheduler);
}
