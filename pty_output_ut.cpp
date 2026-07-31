/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "pty_output.h"

#include "pty.h"

#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/mem/small_obj_allocator.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <cerrno>

using namespace stl;

namespace {
    struct TestPty final: public Pty {
        ssize_t write(const u8* buffer, size_t size) override;
        void outputReady() override;

        Buffer written;
        size_t writeLimit = SIZE_MAX;
        int writeError = 0;
        size_t readyCount = 0;
    };
}

ssize_t TestPty::write(const u8* buffer, size_t size) {
    if (writeError != 0) {
        errno = writeError;
        return -1;
    }
    const size_t count = size < writeLimit ? size : writeLimit;
    written.append(buffer, count);
    return (ssize_t)(count);
}

void TestPty::outputReady() {
    ++readyCount;
}

STD_TEST_SUITE(PtyOutput) {
    STD_TEST(NotifiesPtyAfterFlushOrClose) {
        auto pool = ObjPool::fromMemory();
        SmallObjAllocator* const allocator = SmallObjAllocator::create(pool.mutPtr());
        TestPty pty;
        PtyOutputQueue* const queue = PtyOutputQueue::create(pool.mutPtr(), allocator, pty);
        Output* output = queue->append();

        output->write(StringView(u8"first").data(), 5);
        output->write(StringView(u8"second").data(), 6);
        STD_INSIST(pty.readyCount == 0);

        output->flush();
        STD_INSIST(pty.readyCount == 1);

        delete output;
        STD_INSIST(pty.readyCount == 2);
    }

    STD_TEST(AnOpenInsertionBlocksLaterOutput) {
        auto pool = ObjPool::fromMemory();
        SmallObjAllocator* const allocator = SmallObjAllocator::create(pool.mutPtr());
        TestPty pty;
        PtyOutputQueue* const queue = PtyOutputQueue::create(pool.mutPtr(), allocator, pty);
        Output* first = queue->append();
        Output* second = queue->append();

        first->write(StringView(u8"A").data(), 1);
        STD_INSIST(!queue->flush());
        second->write(StringView(u8"C").data(), 1);
        STD_INSIST(!queue->flush());
        STD_INSIST(StringView(pty.written) == StringView(u8"A"));

        first->write(StringView(u8"B").data(), 1);
        delete first;
        STD_INSIST(queue->flush());
        STD_INSIST(!queue->flush());
        STD_INSIST(StringView(pty.written) == StringView(u8"ABC"));

        delete second;
    }

    STD_TEST(PartialWritesResumeAtTheFirstUnsentByte) {
        auto pool = ObjPool::fromMemory();
        SmallObjAllocator* const allocator = SmallObjAllocator::create(pool.mutPtr());
        TestPty pty;
        PtyOutputQueue* const queue = PtyOutputQueue::create(pool.mutPtr(), allocator, pty);
        Output* output = queue->append();
        output->write(StringView(u8"abcdef").data(), 6);

        pty.writeLimit = 2;
        STD_INSIST(queue->flush());
        STD_INSIST(StringView(pty.written) == StringView(u8"ab"));

        pty.writeLimit = SIZE_MAX;
        STD_INSIST(!queue->flush());
        STD_INSIST(StringView(pty.written) == StringView(u8"abcdef"));

        delete output;
    }

    STD_TEST(OneFlushWritesAtMost64KiB) {
        auto pool = ObjPool::fromMemory();
        SmallObjAllocator* const allocator = SmallObjAllocator::create(pool.mutPtr());
        TestPty pty;
        PtyOutputQueue* const queue = PtyOutputQueue::create(pool.mutPtr(), allocator, pty);
        Output* output = queue->append();
        Buffer source(64 * 1024 + 1);
        source.zero(64 * 1024 + 1);
        output->write(source.data(), source.used());

        STD_INSIST(queue->flush());
        STD_INSIST(pty.written.used() == 64 * 1024);
        STD_INSIST(!queue->flush());
        STD_INSIST(pty.written.used() == 64 * 1024 + 1);

        delete output;
    }

    STD_TEST(RetriesTemporaryWriteErrors) {
        auto pool = ObjPool::fromMemory();
        SmallObjAllocator* const allocator = SmallObjAllocator::create(pool.mutPtr());
        TestPty pty;
        PtyOutputQueue* const queue = PtyOutputQueue::create(pool.mutPtr(), allocator, pty);
        Output* output = queue->append();
        output->write(StringView(u8"pending").data(), 7);

        pty.writeError = EAGAIN;
        STD_INSIST(queue->flush());
        pty.writeError = 0;
        STD_INSIST(!queue->flush());
        STD_INSIST(StringView(pty.written) == StringView(u8"pending"));

        delete output;
    }

    STD_TEST(DoesNotSpinOnFatalWriteErrors) {
        auto pool = ObjPool::fromMemory();
        SmallObjAllocator* const allocator = SmallObjAllocator::create(pool.mutPtr());
        TestPty pty;
        PtyOutputQueue* const queue = PtyOutputQueue::create(pool.mutPtr(), allocator, pty);
        Output* output = queue->append();
        output->write(StringView(u8"pending").data(), 7);

        pty.writeError = EIO;
        STD_INSIST(!queue->flush());

        delete output;
    }
}
