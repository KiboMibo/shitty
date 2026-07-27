/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "poller.h"

#include "composer.h"
#include "listener.h"

#include <std/mem/obj_pool.h>
#include <std/sys/crt.h>
#include <std/tst/ut.h>

#include <poll.h>
#include <unistd.h>

using namespace stl;

namespace {
    struct CaptureReady final: public Listener {
        void onListen(void* argument) override;

        FDReady ready{};
        size_t calls = 0;
    };

    struct CaptureTimeout final: public Listener {
        explicit CaptureTimeout(Poller& poller);

        void onListen(void*) override;

        Poller& poller;
        size_t calls = 0;
        bool rearm = false;
    };
}

void CaptureReady::onListen(void* argument) {
    ready = *(const FDReady*)(argument);
    ++calls;
}

CaptureTimeout::CaptureTimeout(Poller& poller_)
    : poller(poller_)
{
}

void CaptureTimeout::onListen(void*) {
    ++calls;
    if (rearm) {
        rearm = false;
        poller.timeout(0);
    }
}

STD_TEST_SUITE(Poller) {
    STD_TEST(AppendsArmedDescriptorsAfterSourceDescriptors) {
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer composer(pool);
        CaptureReady capture;
        composer.onFDReady.pushBack(&capture);
        int sourcePipe[2];
        int externalPipe[2];
        STD_INSIST(pipe(sourcePipe) == 0);
        STD_INSIST(pipe(externalPipe) == 0);
        composer.poller->arm(externalPipe[0], PollRead);
        const u8 byte = 1;
        STD_INSIST(write(externalPipe[1], &byte, sizeof(byte)) == sizeof(byte));
        struct pollfd source{
            .fd = sourcePipe[0],
            .events = POLLIN,
        };
        double timeout = 0.0;

        const int result = composer.poller->poll(&source, 1, &timeout);

        STD_INSIST(result == 1);
        STD_INSIST(source.revents == 0);
        STD_INSIST(capture.calls == 0);
        STD_INSIST(composer.poller->poll(&source, 1, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 1);
        STD_INSIST(capture.ready.fd == externalPipe[0]);
        STD_INSIST(capture.ready.what == PollRead);
        close(sourcePipe[0]);
        close(sourcePipe[1]);
        close(externalPipe[0]);
        close(externalPipe[1]);
        delete pool;
    }

    STD_TEST(ArmReplacesModeAndDisarmErasesDescriptor) {
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer composer(pool);
        CaptureReady capture;
        composer.onFDReady.pushBack(&capture);
        int externalPipe[2];
        STD_INSIST(pipe(externalPipe) == 0);
        composer.poller->arm(externalPipe[1], PollRead);
        composer.poller->arm(externalPipe[1], PollWrite);
        double timeout = 0.0;

        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 1);
        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 1);
        STD_INSIST(capture.ready.fd == externalPipe[1]);
        STD_INSIST(capture.ready.what == PollWrite);

        composer.poller->disarm(externalPipe[1]);
        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 1);
        close(externalPipe[0]);
        close(externalPipe[1]);
        delete pool;
    }

    STD_TEST(DefersSourceReadinessBehindExternalReadiness) {
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer composer(pool);
        CaptureReady capture;
        composer.onFDReady.pushBack(&capture);
        int sourcePipe[2];
        int externalPipe[2];
        STD_INSIST(pipe(sourcePipe) == 0);
        STD_INSIST(pipe(externalPipe) == 0);
        composer.poller->arm(externalPipe[0], PollRead);
        const u8 byte = 1;
        STD_INSIST(write(sourcePipe[1], &byte, sizeof(byte)) == sizeof(byte));
        STD_INSIST(write(externalPipe[1], &byte, sizeof(byte)) == sizeof(byte));
        struct pollfd source{
            .fd = sourcePipe[0],
            .events = POLLIN,
        };
        double timeout = 0.0;

        STD_INSIST(composer.poller->poll(&source, 1, &timeout) == 2);
        STD_INSIST(source.revents == 0);
        STD_INSIST(capture.calls == 0);
        STD_INSIST(composer.poller->poll(&source, 1, &timeout) == 1);
        STD_INSIST(source.revents == POLLIN);
        STD_INSIST(capture.calls == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 1);
        STD_INSIST(capture.ready.fd == externalPipe[0]);
        STD_INSIST(capture.ready.what == PollRead);
        close(sourcePipe[0]);
        close(sourcePipe[1]);
        close(externalPipe[0]);
        close(externalPipe[1]);
        delete pool;
    }

    STD_TEST(ReturnsSourceReadinessWithoutPublishingIt) {
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer composer(pool);
        CaptureReady capture;
        composer.onFDReady.pushBack(&capture);
        int sourcePipe[2];
        STD_INSIST(pipe(sourcePipe) == 0);
        const u8 byte = 1;
        STD_INSIST(write(sourcePipe[1], &byte, sizeof(byte)) == sizeof(byte));
        struct pollfd source{
            .fd = sourcePipe[0],
            .events = POLLIN,
        };
        double timeout = 0.0;

        STD_INSIST(composer.poller->poll(&source, 1, &timeout) == 1);
        STD_INSIST(source.revents == POLLIN);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 0);
        close(sourcePipe[0]);
        close(sourcePipe[1]);
        delete pool;
    }

    STD_TEST(PublishesOnlyTheEarliestDeadline) {
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer composer(pool);
        CaptureTimeout capture(*composer.poller);
        composer.onTimeout.pushBack(&capture);
        composer.poller->deadline(monotonicNowUs());
        composer.poller->deadline(monotonicNowUs() + 10'000'000);
        double timeout = 1.0;

        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 1);

        timeout = 0.0;
        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 1);
        delete pool;
    }

    STD_TEST(ListenerCanRearmTimeout) {
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer composer(pool);
        CaptureTimeout capture(*composer.poller);
        capture.rearm = true;
        composer.onTimeout.pushBack(&capture);
        composer.poller->timeout(0);
        double timeout = 1.0;

        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 1);

        timeout = 1.0;
        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 2);
        delete pool;
    }

    STD_TEST(CallerTimeoutDoesNotConsumeLaterDeadline) {
        ObjPool* const pool = ObjPool::fromMemoryRaw();
        Composer composer(pool);
        CaptureTimeout capture(*composer.poller);
        composer.onTimeout.pushBack(&capture);
        composer.poller->timeout(100'000);
        double timeout = 0.0;

        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 0);

        composer.poller->timeout(0);
        timeout = 1.0;
        STD_INSIST(composer.poller->poll(nullptr, 0, &timeout) == 0);
        composer.poller->dispatch();
        STD_INSIST(capture.calls == 1);
        delete pool;
    }
}
