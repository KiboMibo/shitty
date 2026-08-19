/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/lib/vector.h>
#include <std/sys/types.h>

// A3: the device-side mirror of the glyph strip arenas, kept as one
// range per pane instead of one generation for the whole window.
//
// Every pane shapes through its own Screen, and every Screen numbers its
// arena generations from its own zero. A single scalar generation in the
// renderer therefore compares one pane's number against another pane's:
// equal numbers from different screens read as "nothing moved" and leave
// the device mirroring the wrong pane's bytes, while different numbers
// read as "everything moved" and re-upload both arenas whole on every
// frame - the regression A3 exists to prevent, on a project that
// publishes its throughput in the README. A generation only means
// something next to the identity of the pane that produced it, so the
// two always travel together here.
//
// The panes' arenas live back to back on the device, in draw order:
// pane 0 at zero, pane 1 after it, and so on. A pane owes the device the
// tail it has not sent yet - and its whole range, from zero, whenever
// its generation changed (its own strips moved) or its base moved
// (a pane before it grew, so its bytes are somewhere else now).

struct PaneArenaRequest {
    // The pane's identity: the Screen it shapes through, as an opaque
    // handle. Null is a legal value and simply never matches a previous
    // frame - a pane without a screen has no strips to mirror.
    const void* pane = nullptr;
    // The arena generation that pane reported for this frame.
    u32 generation = 0;
    // How much of its arena the pane filled, in whatever unit the strip
    // offsets are counted in: bytes for the mask plane, u32 pixels for
    // the color plane. The mirror only ever adds and compares, so it
    // does not care which - the caller keeps one mirror per plane and
    // stays in that plane's unit throughout.
    size_t used = 0;
};

struct PaneArenaCopy {
    // Where this pane's arena starts on the device. Strip offsets the
    // pane hands out are relative to its own arena, so this is what a
    // renderer adds to them before the shader sees them.
    size_t base = 0;
    // The half-open run of the pane's own arena the device still owes,
    // in the pane's own coordinates: copy [from, to) to [base + from,
    // base + to). Empty (from == to) when the mirror is already current.
    size_t from = 0;
    size_t to = 0;
};

struct PaneArenaMirror {
    // One call per frame, panes in draw order - one call, one frame, the
    // same shape as the update contract above it. `copies` receives one
    // entry per pane and must have room for `count` of them.
    //
    // The plan assumes every copy it names is then made. A caller that
    // cannot make them (an allocation failed, the buffer was replaced
    // and lost its contents) calls reset() so the next frame starts from
    // nothing rather than from a mirror that was never written.
    void plan(const PaneArenaRequest* panes, size_t count, PaneArenaCopy* copies);

    // Forget the mirror: the device buffers no longer hold what the last
    // plan said they hold.
    void reset();

    // The bytes (or pixels) the panes of the last plan occupy end to
    // end - what the device buffer has to hold.
    size_t used() const {
        return used_;
    }

    struct Range {
        const void* pane = nullptr;
        u32 generation = 0;
        size_t base = 0;
        size_t uploaded = 0;
    };

    // Last frame's ranges, in that frame's draw order. Panes are few and
    // the lookup is a scan: a map here would cost more than it saves.
    stl::Vector<Range> ranges;
    // Scratch for the frame being planned. A member so a frame plans
    // without allocating; the two swap at the end of plan().
    stl::Vector<Range> planned;
    size_t used_ = 0;
};

inline void PaneArenaMirror::reset() {
    ranges.clear();
    planned.clear();
    used_ = 0;
}

inline void PaneArenaMirror::plan(const PaneArenaRequest* panes, size_t count, PaneArenaCopy* copies) {
    planned.clear();
    size_t base = 0;
    for (size_t index = 0; index < count; ++index) {
        const PaneArenaRequest& request = panes[index];
        size_t uploaded = 0;
        for (size_t slot = 0; slot < ranges.length(); ++slot) {
            const Range& previous = ranges[slot];
            // The pane, its generation and its place in the arena, all
            // three: any one of them alone answers "yes, mirrored" for a
            // pane whose bytes are not there.
            if (previous.pane != request.pane || previous.pane == nullptr || previous.generation != request.generation || previous.base != base) {
                continue;
            }
            uploaded = previous.uploaded < request.used ? previous.uploaded : request.used;
            break;
        }
        copies[index] = {base, uploaded, request.used};
        planned.pushBack({request.pane, request.generation, base, request.used});
        base += request.used;
    }
    // The scan above reads last frame's ranges while this frame's are
    // built beside it: overwriting in place would hide a pane that moved
    // backwards behind the pane that took its slot.
    ranges.xchg(planned);
    used_ = base;
}
