/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "composer.h"
#include "vterm.h"

#include <std/lib/vector.h>

// The pane that fills the window: the composer's grid at origin zero.
// Named once here so no caller has to spell out its own idea of "one
// terminal, whole window" - and so the day panes really divide the
// window, the callers that must stop using it are the ones that still
// name it.
//
// A5-4: this lives with the layout and not in vterm.h. The type
// PaneGeometry belongs in the terminal's header - it is the contract
// both sides include - but a ready-made "pane == window" constructor
// sitting there let everyone who includes vterm.h (which is nearly the
// whole tree) build one without asking layout at all.
PaneGeometry windowPane(const Composer& composer);

// Which axis a split cuts. Vertical is what cmd+d does: the divider
// stands upright and the two panes sit side by side, the way iTerm2,
// Ghostty and Warp all spell it. Horizontal is cmd+shift+d: the divider
// lies flat and the panes stack.
enum class SplitDirection : u8 {
    Vertical,
    Horizontal,
};

// Where to look for a neighbour, in window coordinates.
enum class PaneSide : u8 {
    Left,
    Right,
    Up,
    Down,
};

// One pane and the rectangle it occupies, in the same pixels as the
// area it was laid out in.
struct PanePlacement {
    u64 pane = 0;
    PixelRect area;
};

// The gap between two sibling subtrees: what a pointer grabs to change
// the split's share. `split` names the node, for setShare().
struct PaneDivider {
    u32 split = 0;
    SplitDirection direction = SplitDirection::Vertical;
    PixelRect area;
};

// A4: one tab's panes as a binary tree. A node is either a leaf holding
// one pane or a split holding two children, a direction, and the near
// child's share of the axis.
//
// Panes are named by opaque ids rather than by terminal pointers, which
// is what keeps this a pure layout structure: session.cpp puts session
// ids in, the unit test puts plain numbers in, and neither has to know
// about the other. Zero is not a pane - it is the answer to "which pane
// has the focus" on an empty tree.
//
// Nodes live in one vector and are named by index. Closed nodes go on a
// free list and are handed back out: a tab that is split and unsplit all
// afternoon must not grow the vector all afternoon with it.
struct PaneTree {
    // Neither a node index nor a pane id.
    static constexpr u32 noNode = ~0u;
    // The denominator every share is counted in. A power of two so that
    // halving a split - which is all splitting ever does - stays exact
    // for the fifteen divisions nobody will ever make.
    static constexpr u32 shareScale = 1u << 15;

    // The tab's first pane. The tree must be empty.
    void plant(u64 pane);
    // Divides the focused pane in two, giving `pane` the far half and
    // the focus. False when there is nothing to divide.
    bool split(SplitDirection direction, u64 pane);
    // Drops one pane. Its space goes to the sibling subtree, and if it
    // held the focus, the focus goes to the first leaf of that subtree.
    // False when the last pane went and the tree is now empty.
    bool close(u64 pane);

    u64 focused() const;
    // Ignored when the pane is not in this tree: the caller is a chord
    // or a hit test, and both can name a pane that has just died.
    void focus(u64 pane);
    // Moves the focus one pane towards `side`. False when the focused
    // pane has no neighbour there.
    bool focusNeighbour(PaneSide side);

    size_t count() const;
    bool empty() const;
    bool holds(u64 pane) const;
    // Every live pane in visual order: near child before far child, so
    // left to right and top to bottom.
    void panes(stl::Vector<u64>& out) const;

    // Every live pane with the rectangle it occupies inside `area`, and
    // optionally the dividers between them. `divider` is the gap left
    // between two siblings, in the same pixels as `area`.
    //
    // Division leaves its remainder with the far child: the near child
    // gets exactly its share of what is left after the gap, and the far
    // child gets everything else, so the panes and the gaps always add
    // back up to `area` however the pixels divide.
    void layout(const PixelRect& area, u16 divider, stl::Vector<PanePlacement>& panes, stl::Vector<PaneDivider>* dividers = nullptr) const;
    // Moves one divider. The share is clamped to leave both sides a
    // share of their own; a minimum in *cells* is the caller's to
    // enforce, because only the caller knows the glyph size the share
    // will be divided by.
    void setShare(u32 split, u32 share);
    u32 share(u32 split) const;

    struct Node {
        // Leaf only; zero in a split.
        u64 pane = 0;
        u32 parent = noNode;
        // Both noNode in a leaf. `near` is the left or top child.
        u32 near = noNode;
        u32 far = noNode;
        // The near child's share of the axis, out of shareScale.
        u32 share = shareScale / 2;
        SplitDirection direction = SplitDirection::Vertical;
    };

    u32 find(u64 pane) const;
    u32 firstLeaf(u32 node) const;
    bool isLeaf(u32 node) const;

    u32 allocate();
    void release(u32 node);
    void collect(u32 node, stl::Vector<u64>& out) const;
    void place(u32 node, const PixelRect& area, u16 divider, stl::Vector<PanePlacement>& panes, stl::Vector<PaneDivider>* dividers) const;
    u32 descend(u32 node, SplitDirection direction, bool towardsFar) const;

    stl::Vector<Node> nodes;
    u32 root = noNode;
    u32 focused_ = noNode;
    u32 free_ = noNode;
    size_t count_ = 0;
};
