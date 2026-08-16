/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "font_face.h"

#include <std/lib/buffer.h>
#include <std/ptr/arc.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace stl;

namespace {
    struct CountedFontFace: public FontFace {
        u32 id() const noexcept override;
        void ref() noexcept override;
        i32 unref() noexcept override;
        i32 refCount() const noexcept override;

        const u32 id_ = nextFontFaceId();
        ARC arc_;
    };

    struct MemoryFontFace final: public CountedFontFace {
        MemoryFontFace(const void* data, size_t size, i32 faceIndex);

        const void* data() const override;
        size_t size() const override;
        i32 faceIndex() const override;

        const void* data_;
        size_t size_;
        i32 faceIndex_;
    };

    struct MmapFontFace final: public CountedFontFace {
        MmapFontFace(void* data, size_t size, i32 faceIndex);
        ~MmapFontFace() noexcept override;

        const void* data() const override;
        size_t size() const override;
        i32 faceIndex() const override;

        void* data_;
        size_t size_;
        i32 faceIndex_;
    };
}

u32 nextFontFaceId() noexcept {
    static u32 counter = 0;
    return counter++;
}

FontFace::~FontFace() noexcept {
}

u32 CountedFontFace::id() const noexcept {
    return id_;
}

void CountedFontFace::ref() noexcept {
    arc_.ref();
}

i32 CountedFontFace::unref() noexcept {
    return arc_.unref();
}

i32 CountedFontFace::refCount() const noexcept {
    return arc_.refCount();
}

MemoryFontFace::MemoryFontFace(const void* data, size_t size, i32 faceIndex)
    : data_(data)
    , size_(size)
    , faceIndex_(faceIndex)
{
}

const void* MemoryFontFace::data() const {
    return data_;
}

size_t MemoryFontFace::size() const {
    return size_;
}

i32 MemoryFontFace::faceIndex() const {
    return faceIndex_;
}

MmapFontFace::MmapFontFace(void* data, size_t size, i32 faceIndex)
    : data_(data)
    , size_(size)
    , faceIndex_(faceIndex)
{
}

MmapFontFace::~MmapFontFace() noexcept {
    munmap(data_, size_);
}

const void* MmapFontFace::data() const {
    return data_;
}

size_t MmapFontFace::size() const {
    return size_;
}

i32 MmapFontFace::faceIndex() const {
    return faceIndex_;
}

FontFace* createMemoryFontFace(const void* data, size_t size, i32 faceIndex) {
    return new MemoryFontFace(data, size, faceIndex);
}

FontFace* openFontFile(StringView path, i32 faceIndex) {
    Buffer filename(path);
    const int fd = open(filename.cStr(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        Errno().raise(StringBuilder() << StringView(u8"failed to open font ") << path);
    }
    struct stat status {};
    if (fstat(fd, &status) != 0 || status.st_size <= 0) {
        const int error = errno;
        close(fd);
        Errno(error == 0 ? EINVAL : error).raise(StringBuilder() << StringView(u8"failed to measure font ") << path);
    }
    void* const data = mmap(nullptr, (size_t)(status.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    const int error = errno;
    close(fd);
    if (data == MAP_FAILED) {
        Errno(error).raise(StringBuilder() << StringView(u8"failed to map font ") << path);
    }
    return new MmapFontFace(data, (size_t)(status.st_size), faceIndex);
}
