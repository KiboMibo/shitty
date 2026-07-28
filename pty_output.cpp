/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "pty_output.h"

#include "pty.h"
#include "small_obj_allocator.h"

#include <std/dbg/assert.h>
#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/lib/list.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>

#include <cstring>
#include <new>

using namespace stl;

namespace {
    constexpr size_t maximumWrite = 64 * 1024;

    struct PtyOutputQueueImpl;
    struct PtyOutputImpl;

    struct PtyOutputNode: public IntrusiveNode {
        explicit PtyOutputNode(PtyOutputImpl* output);

        StringView pending() const;
        void append(const void* data, size_t size);
        void consume(size_t size);

        PtyOutputImpl* output;
        Buffer buffer;
        size_t offset = 0;
        bool closed = false;
    };

    struct PtyOutputImpl final: public Output {
        PtyOutputImpl(SmallObjAllocator* allocator, PtyOutputQueueImpl* queue);
        ~PtyOutputImpl() noexcept override;

        void operator delete(PtyOutputImpl* output, std::destroying_delete_t) noexcept;

        size_t writeImpl(const void* data, size_t size) override;

        SmallObjAllocator* allocator;
        PtyOutputQueueImpl* queue;
        PtyOutputNode* node = nullptr;
    };

    struct PtyOutputQueueImpl final: public PtyOutputQueue {
        PtyOutputQueueImpl(SmallObjAllocator* allocator, Pty& pty);
        ~PtyOutputQueueImpl();

        Output* append() override;
        bool flush() override;

        void close(PtyOutputImpl& output);
        void releaseDrained();

        SmallObjAllocator* allocator;
        Pty& pty;
        IntrusiveList outputs;
        bool shuttingDown = false;
    };
}

PtyOutputNode::PtyOutputNode(PtyOutputImpl* output_)
    : output(output_)
{
}

StringView PtyOutputNode::pending() const {
    return StringView((const u8*)(buffer.data()) + offset, buffer.used() - offset);
}

void PtyOutputNode::append(const void* data, size_t size) {
    if (offset == buffer.used()) {
        buffer.reset();
        offset = 0;
    } else if (offset != 0 && buffer.used() + size > buffer.capacity()) {
        const size_t remaining = buffer.used() - offset;
        memmove(buffer.mutData(), (const u8*)(buffer.data()) + offset, remaining);
        buffer.seekAbsolute(remaining);
        offset = 0;
    }
    buffer.append(data, size);
}

void PtyOutputNode::consume(size_t size) {
    STD_ASSERT(size <= buffer.used() - offset);
    offset += size;
    if (offset == buffer.used()) {
        buffer.reset();
        offset = 0;
    }
}

PtyOutputImpl::PtyOutputImpl(SmallObjAllocator* allocator_, PtyOutputQueueImpl* queue_)
    : allocator(allocator_)
    , queue(queue_)
{
}

PtyOutputImpl::~PtyOutputImpl() noexcept {
    if (queue != nullptr) {
        queue->close(*this);
    }
}

void PtyOutputImpl::operator delete(PtyOutputImpl* output, std::destroying_delete_t) noexcept {
    SmallObjAllocator* const allocator = output->allocator;
    allocator->release(output);
}

size_t PtyOutputImpl::writeImpl(const void* data, size_t size) {
    if (node == nullptr || size == 0) {
        return size;
    }
    node->append(data, size);
    queue->pty.outputReady();
    return size;
}

PtyOutputQueueImpl::PtyOutputQueueImpl(SmallObjAllocator* allocator_, Pty& pty_)
    : allocator(allocator_)
    , pty(pty_)
{
}

PtyOutputQueueImpl::~PtyOutputQueueImpl() {
    shuttingDown = true;
    while (!outputs.empty()) {
        auto* node = static_cast<PtyOutputNode*>(outputs.popFront());
        if (node->output != nullptr) {
            node->output->queue = nullptr;
            node->output->node = nullptr;
        }
        allocator->release(node);
    }
}

Output* PtyOutputQueueImpl::append() {
    PtyOutputImpl* const output = allocator->make<PtyOutputImpl>(allocator, this);
    PtyOutputNode* const node = allocator->make<PtyOutputNode>(output);
    output->node = node;
    outputs.pushBack(node);
    return output;
}

bool PtyOutputQueueImpl::flush() {
    releaseDrained();
    if (outputs.empty()) {
        return false;
    }
    auto* node = static_cast<PtyOutputNode*>(outputs.mutFront());
    const StringView bytes = node->pending();
    if (bytes.empty()) {
        return false;
    }
    const size_t size = bytes.length() < maximumWrite ? bytes.length() : maximumWrite;
    const ssize_t count = pty.write(bytes.data(), size);
    if (count <= 0) {
        return true;
    }
    node->consume((size_t)(count));
    releaseDrained();
    if (outputs.empty()) {
        return false;
    }
    return !static_cast<PtyOutputNode*>(outputs.mutFront())->pending().empty();
}

void PtyOutputQueueImpl::close(PtyOutputImpl& output) {
    PtyOutputNode* const node = output.node;
    output.node = nullptr;
    output.queue = nullptr;
    if (node == nullptr) {
        return;
    }
    node->output = nullptr;
    node->closed = true;
    if (!shuttingDown) {
        pty.outputReady();
    }
}

void PtyOutputQueueImpl::releaseDrained() {
    while (!outputs.empty()) {
        auto* node = static_cast<PtyOutputNode*>(outputs.mutFront());
        if (!node->closed || !node->pending().empty()) {
            return;
        }
        node->unlink();
        allocator->release(node);
    }
}

PtyOutputQueue* PtyOutputQueue::create(ObjPool* pool, SmallObjAllocator* allocator, Pty& pty) {
    return pool->make<PtyOutputQueueImpl>(allocator, pty);
}
