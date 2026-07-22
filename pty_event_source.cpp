#include "pty_event_source.h"

#include "composer.h"
#include "pty.h"

#include <std/mem/obj_pool.h>

#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>


namespace stl {}
using namespace stl;

namespace {

    class PtyEventSourceImpl final: public PtyEventSource {
    public:
        PtyEventSourceImpl(Pty& pty, PtyEventHost& host);
        ~PtyEventSourceImpl();

        short events() override;
        void acknowledge() override;
        void setWriteInterest(bool enabled) override;

    private:
        Pty& pty;
        PtyEventHost& host;
        int wakePipe[2]{-1, -1};
        std::thread worker;
        std::mutex mutex;
        std::condition_variable condition;
        bool stopping = false;
        short pendingEvents = 0;
        bool wantWritable = false;

        void wakeWorker();
        void run();
    };

}

PtyEventSourceImpl::PtyEventSourceImpl(Pty& pty_, PtyEventHost& host_)
    : pty(pty_)
    , host(host_)
{
    if (pipe(wakePipe) < 0) {
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }
    for (const int fd : wakePipe) {
        const int flags = fcntl(fd, F_GETFD);
        if (flags >= 0) {
            fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }
    const int wakeFlags = fcntl(wakePipe[0], F_GETFL, 0);
    if (wakeFlags >= 0) {
        fcntl(wakePipe[0], F_SETFL, wakeFlags | O_NONBLOCK);
    }
    worker = std::thread(&PtyEventSourceImpl::run, this);
}

PtyEventSourceImpl::~PtyEventSourceImpl() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        pendingEvents = 0;
    }
    condition.notify_all();
    wakeWorker();
    if (worker.joinable()) {
        worker.join();
    }
    close(wakePipe[0]);
    close(wakePipe[1]);
}

short PtyEventSourceImpl::events() {
    std::lock_guard<std::mutex> lock(mutex);
    return pendingEvents;
}

void PtyEventSourceImpl::acknowledge() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        pendingEvents = 0;
    }
    condition.notify_one();
}

void PtyEventSourceImpl::setWriteInterest(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (wantWritable == enabled) {
            return;
        }
        wantWritable = enabled;
    }
    wakeWorker();
}

void PtyEventSourceImpl::wakeWorker() {
    const u8 byte = 1;
    while (::write(wakePipe[1], &byte, sizeof(byte)) < 0 && errno == EINTR)
        ;
}

void PtyEventSourceImpl::run() {
    struct pollfd pollSet[] = {
        {pty.fd(), 0, 0},
        {wakePipe[0], POLLIN, 0},
    };

    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            pollSet[0].events = POLLIN | POLLHUP | (wantWritable ? POLLOUT : 0);
        }
        const int result = poll(pollSet, 2, -1);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (pollSet[1].revents & POLLIN) {
            u8 bytes[64];
            while (::read(wakePipe[0], bytes, sizeof(bytes)) > 0)
                ;
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) {
                return;
            }
            continue;
        }
        const short ready = pollSet[0].revents & (POLLIN | POLLOUT | POLLHUP | POLLERR);
        if (!ready) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) {
                return;
            }
            pendingEvents = ready;
        }

        host.wake();

        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] {
            return stopping || !pendingEvents;
        });
        if (stopping) {
            return;
        }
    }
}

PtyEventSource* PtyEventSource::create(Composer& composer, Pty& pty, PtyEventHost& host) {
    return composer.pool->make<PtyEventSourceImpl>(pty, host);
}
