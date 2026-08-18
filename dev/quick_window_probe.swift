// Copyright (C) 2026 Shitty team
// MIT licensed
// See the file LICENSE.MIT for the full license.
//
// A small CLI for driving and observing the quick-terminal window live,
// without System Events/AppleScript - that route was tried for the
// panes-and-window-chrome plan's T3 and did not deliver reliable
// synthetic key presses or window enumeration in this environment.
// Compiled and wrapped by quick_window_probe.sh, which is the intended
// entry point; see its usage banner for the recipe this implements
// (docs/plans/reviews/panes-R2-qa.md).
//
// Needs Accessibility (System Settings -> Privacy & Security ->
// Accessibility) granted to whichever process runs this binary - the
// same TCC permission System Events itself would have needed, just
// requested directly instead. Screen Recording is not needed for any
// of this: geometry comes from CGWindowListCopyWindowInfo (window
// server metadata, no pixels), not from a screenshot.

import AppKit
import ApplicationServices
import CoreGraphics
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(1)
}

func usage() -> Never {
    fail("""
    usage:
      quick_window_probe geometry <pid>
          Prints one line per on-screen-server window owned by <pid>:
          "x y width height layer onscreen". Values come from
          CGWindowListCopyWindowInfo - window-server metadata, not a
          screenshot, so no Screen Recording permission is needed.

      quick_window_probe chord <keyCode> <modifiers>
          Posts a key down + key up for the AppKit virtual key <keyCode>
          (e.g. 50 for backtick/grave, 3 for F) with <modifiers> as a
          comma-separated list of: control, shift, option, command.
          Uses CGEventSource + .cghidEventTap, the same path real
          hardware input takes - a global Carbon hotkey registered via
          RegisterEventHotKey sees this exactly like a keypress. The
          very first chord after a target process starts is reliably
          swallowed somewhere in the pipeline (measured, not explained);
          send one throwaway warmup chord before a timed series.

      quick_window_probe move <pid> <x> <y>
      quick_window_probe resize <pid> <w> <h>
          Sets the focused window's position/size (top-left origin,
          points - AX's own convention, not AppKit's bottom-left one)
          via AXUIElementSetAttributeValue. Simulates the user dragging
          the window or its resize corner. Requires Accessibility.

      quick_window_probe screens
          Prints one line per attached display: "frame=x,y,w,h
          visible=x,y,w,h scale=s", all in AppKit points with AppKit's
          bottom-left global origin - the same numbers the quick
          window's own clamp works in (lib/shitty/ui_quick_hotkey.mm).

      quick_window_probe warp <x> <y>
          Moves the mouse pointer to a global position (top-left origin,
          points - CoreGraphics' own convention, the one `geometry`
          prints too) via CGWarpMouseCursorPosition. The quick window is
          shown on the screen under the pointer
          (WindowImpl::topOfActiveScreenFrame, ext/plt/platform_cocoa.mm),
          so this is how a cross-display scenario is driven without
          touching the mouse. Prints "x y" - where the pointer was
          before the warp, so a script can restore it afterwards.

    Grid size as a TUI would see it (`tput lines`/`tput cols`) is not
    this tool's job - it needs no Swift at all:
      stty -f "/dev/$(ps -o tty= -p "$(pgrep -P <st-pid>)")" size
    (quick_window_probe.sh wraps this as qwp_ptysize).
    """)
}

let args = CommandLine.arguments
guard args.count >= 2 else { usage() }

switch args[1] {
case "geometry":
    guard args.count == 3, let pid = Int32(args[2]) else { usage() }
    guard let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: AnyObject]] else {
        fail("CGWindowListCopyWindowInfo failed")
    }
    var printedAny = false
    for w in list {
        guard let ownerPid = w[kCGWindowOwnerPID as String] as? Int32, ownerPid == pid else {
            continue
        }
        guard let bounds = w[kCGWindowBounds as String] as? [String: CGFloat] else {
            continue
        }
        let layer = w[kCGWindowLayer as String] as? Int ?? -1
        let onscreen = w[kCGWindowIsOnscreen as String] as? Bool ?? false
        let x = bounds["X"] ?? 0
        let y = bounds["Y"] ?? 0
        let width = bounds["Width"] ?? 0
        let height = bounds["Height"] ?? 0
        print("\(x) \(y) \(width) \(height) \(layer) \(onscreen)")
        printedAny = true
    }
    if !printedAny {
        fail("no window owned by pid \(pid) in the window list (hidden, or the pid is wrong)")
    }

case "chord":
    guard args.count == 4, let keyCode = UInt16(args[2]) else { usage() }
    var flags: CGEventFlags = []
    for name in args[3].split(separator: ",") {
        switch name {
        case "control":
            flags.insert(.maskControl)
        case "shift":
            flags.insert(.maskShift)
        case "option":
            flags.insert(.maskAlternate)
        case "command":
            flags.insert(.maskCommand)
        default:
            fail("unknown modifier: \(name) (want control, shift, option, command)")
        }
    }
    guard let source = CGEventSource(stateID: .hidSystemState) else {
        fail("CGEventSource(stateID: .hidSystemState) failed")
    }
    guard let down = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: true) else {
        fail("CGEvent keyDown failed")
    }
    down.flags = flags
    down.post(tap: .cghidEventTap)
    guard let up = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: false) else {
        fail("CGEvent keyUp failed")
    }
    up.flags = flags
    up.post(tap: .cghidEventTap)

case "move", "resize":
    guard args.count == 5, let pid = Int32(args[2]), let a = Double(args[3]), let b = Double(args[4]) else {
        usage()
    }
    let app = AXUIElementCreateApplication(pid)
    // kAXFocusedWindowAttribute is unreliable for an unbundled binary
    // launched from a shell (no Info.plist) - it can report no value
    // even while the window is visibly shown and key. kAXWindowsAttribute
    // (the app's window list) worked in practice where the focused-window
    // lookup did not, and a quick-terminal process only ever has the one
    // window anyway.
    var windowValue: CFTypeRef?
    var lookup = AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute as CFString, &windowValue)
    if lookup != .success || windowValue == nil {
        var windowsValue: CFTypeRef?
        lookup = AXUIElementCopyAttributeValue(app, kAXWindowsAttribute as CFString, &windowsValue)
        if lookup == .success, let windows = windowsValue as? [AXUIElement], let first = windows.first {
            windowValue = first
            lookup = .success
        }
    }
    guard lookup == .success, let windowValue else {
        fail("AXUIElementCopyAttributeValue(kAXFocusedWindowAttribute/kAXWindowsAttribute) failed (\(lookup.rawValue)) - is the window shown, and is Accessibility granted?")
    }
    // swiftlint:disable:next force_cast - AXUIElementCopyAttributeValue's
    // own contract for this attribute name.
    let axWindow = windowValue as! AXUIElement
    if args[1] == "move" {
        var point = CGPoint(x: a, y: b)
        guard let value = AXValueCreate(.cgPoint, &point) else {
            fail("AXValueCreate(.cgPoint) failed")
        }
        let result = AXUIElementSetAttributeValue(axWindow, kAXPositionAttribute as CFString, value)
        if result != .success {
            fail("AXUIElementSetAttributeValue(kAXPosition) failed (\(result.rawValue))")
        }
    } else {
        var size = CGSize(width: a, height: b)
        guard let value = AXValueCreate(.cgSize, &size) else {
            fail("AXValueCreate(.cgSize) failed")
        }
        let result = AXUIElementSetAttributeValue(axWindow, kAXSizeAttribute as CFString, value)
        if result != .success {
            fail("AXUIElementSetAttributeValue(kAXSize) failed (\(result.rawValue))")
        }
    }

case "screens":
    guard args.count == 2 else { usage() }
    for screen in NSScreen.screens {
        let f = screen.frame
        let v = screen.visibleFrame
        print("frame=\(f.origin.x),\(f.origin.y),\(f.size.width),\(f.size.height) visible=\(v.origin.x),\(v.origin.y),\(v.size.width),\(v.size.height) scale=\(screen.backingScaleFactor)")
    }

case "warp":
    guard args.count == 4, let x = Double(args[2]), let y = Double(args[3]) else { usage() }
    // Where the pointer was, so a script can put it back: warping it out
    // from under someone who is using the machine is rude enough once.
    let was = CGEvent(source: nil)?.location ?? .zero
    print("\(was.x) \(was.y)")
    let result = CGWarpMouseCursorPosition(CGPoint(x: x, y: y))
    if result != .success {
        fail("CGWarpMouseCursorPosition failed (\(result.rawValue))")
    }
    // Without this the pointer snaps back to where the hardware last
    // left it as soon as the next mouse event arrives, and the warp
    // silently does nothing.
    CGAssociateMouseAndMouseCursorPosition(1)

default:
    usage()
}
