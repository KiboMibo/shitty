/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#pragma once

#include "frame.h"
#include "grapheme.h"
#include "utf8.h"
#include "vterm_host.h"

#include <cstdint>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <sys/types.h>

enum class VtKey {
    NONE,

    Space,
    Return,
    Backspace,
    Tab,
    Backtick,
    Tilde,
    Up,
    Down,
    Left,
    Right,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    K0,
    K1,
    K2,
    K3,
    K4,
    K5,
    K6,
    K7,
    K8,
    K9,

    KP_F1,
    KP_F2,
    KP_F3,
    KP_F4,
    KP_Insert,
    KP_Delete,
    KP_Up,
    KP_Down,
    KP_Left,
    KP_Right,
    KP_Home,
    KP_End,
    KP_PageUp,
    KP_PageDown,
    KP_Begin,
    KP_Plus,
    KP_Minus,
    KP_Star,
    KP_Slash,
    KP_Comma,
    KP_Dot,
    KP_Space,
    KP_Equal,
    KP_Tab,
    KP_Enter,
    KP_0,
    KP_1,
    KP_2,
    KP_3,
    KP_4,
    KP_5,
    KP_6,
    KP_7,
    KP_8,
    KP_9,

    CapsLock,
    ScrollLock,
    NumLock,
    Pause,
    Menu,
    Print,
    LeftShift,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper
};

enum class VtModifier : uint8_t {
    none = 0,
    shift = 1,
    control = 2,
    shift_control = 3,
    alt = 4,
    shift_alt = 5,
    control_alt = 6,
    shift_control_alt = 7
};
constexpr VtModifier operator|(VtModifier m1, VtModifier m2) {
    return static_cast<VtModifier>(
        static_cast<uint8_t>(m1) | static_cast<uint8_t>(m2));
}
constexpr VtModifier operator&(VtModifier m1, VtModifier m2) {
    return static_cast<VtModifier>(
        static_cast<uint8_t>(m1) & static_cast<uint8_t>(m2));
}

enum class MouseTrackingMode : uint8_t {
    Disabled = 0,
    X10_Compat,
    VT200,
    VT200_ButtonEvent,
    VT200_AnyEvent,
    VT200_Highlight
};
enum class MouseTrackingEnc : uint8_t {
    Default = 0,
    UTF8,
    SGR,
    URXVT,
    SGRPixels
};
struct MouseTrackingState {
    MouseTrackingMode mode = MouseTrackingMode::Disabled;
    MouseTrackingEnc enc = MouseTrackingEnc::Default;
    bool focusEventMode = false;
    uint32_t generation = 0;

    void setMode(MouseTrackingMode value) {
        if (mode != value) {
            mode = value;
            ++generation;
        }
    }
    void setEncoding(MouseTrackingEnc value) {
        if (enc != value) {
            enc = value;
            ++generation;
        }
    }
};

class Vterm {
public:
    enum class KeyEventType : uint8_t {
        Press = 1,
        Repeat = 2,
        Release = 3
    };

    Vterm(VtermHost& host, uint16_t glyphPx, uint16_t glyphPy,
          uint16_t winPx, uint16_t winPy,
          int ptyFd);

    ~Vterm() = default;

    bool getScreenReverseVideo() const { return screenReverseVideo; }
    uint8_t getLedState() const { return ledState; }
    bool getReverseWrapMode() const { return reverseWrapMode; }
    bool getNationalReplacementMode() const { return nationalReplacementMode; }
    bool getMetaMode() const { return eightBitInput; }

    void resize(uint16_t winPx, uint16_t winPy);

    void redraw();
    bool synchronizedOutputActive() const {
        return synchronizedOutputMode;
    }
    bool expireSynchronizedOutput(bool force = false);
    bool animationActive() const {
        return haveBlinkingText || cursorBlinkMode;
    }
    bool advanceAnimation(bool force = false);

    struct InputSpec {
        VtKey key;
        const char* input;
        size_t length = 0;

        size_t getLength() const {
            return length ? length : strlen(input);
        }
    };

    int writePty(VtKey key, VtModifier modifiers = VtModifier::none,
                 bool userInput = false);
    int writePty(uint8_t ch, VtModifier modifiers = VtModifier::none,
                 bool userInput = false);
    int writePty(const char* cstr, bool userInput = false);
    int writePty(const uint8_t* ucstr, size_t len,
                 bool userInput = false);
    bool flushPtyOutput();
    using PtyWriteHandlerFn =
        std::function<ssize_t(const uint8_t*, size_t)>;
    void setPtyWriteHandler(const PtyWriteHandlerFn& handler);
    bool hasPendingPtyOutput() const {
        return ptyOutputOffset < ptyOutput.size();
    }
    size_t pendingPtyOutputBytes() const {
        return ptyOutput.size() - ptyOutputOffset;
    }
    int writeKittyKey(VtKey key, uint16_t modifiers,
                      KeyEventType event);
    int writeKittyKey(uint32_t key, uint32_t shiftedKey,
                      uint32_t baseLayoutKey, uint16_t modifiers,
                      KeyEventType event);
    uint8_t getKittyKeyboardFlags() const;

    bool readPty();
    bool servicePty(bool readable, bool writable);
    using PtyReadHandlerFn = std::function<ssize_t(uint8_t*, size_t)>;
    void setPtyReadHandler(const PtyReadHandlerFn& handler);
    void feedPtyOutput(const std::string& output);

    const MouseTrackingState& getMouseTrackingState() const;
    bool mouseHighlightRelease(uint16_t endX, uint16_t endY,
                               uint16_t mouseX, uint16_t mouseY);
    void setLocatorPosition(uint16_t column, uint16_t row,
                            uint16_t pixelX, uint16_t pixelY,
                            uint8_t buttons = 0);
    void reportLocatorButton(uint8_t button, bool pressed);

    void setHasFocus(bool);
    void setHyperlink(const std::string& parametersAndUri);
    std::string getHyperlink(int pX, int pY) const;
    size_t getHyperlinkCount() const {
        return hyperlinks.size();
    }
    void mouseWheelUp(uint16_t count = 1);
    void mouseWheelDown(uint16_t count = 1);
    void pageUp();
    void pageDown();

    void selectStart(int pX, int pY, bool cycleSnapTo);
    void selectExtend(int pX, int pY, bool cycleSnapTo);
    void selectUpdate(int pX, int pY);
    bool selectFinish(std::string& utf8_selection);
    void selectClear();
    void selectRectangularModeToggle();

    void pasteSelection(const std::string& utf8_selection);

private:
    std::string getLocalEcho(const unsigned char* const begin,
                             const unsigned char* const end);
    bool processInput(
        const unsigned char* const input, int size, bool refresh = true);
    bool processInput(const std::string& str);

    struct PresentationState {
        Frame* frame;
        TerminalCursor cursor;
        Rect selection;
        uint16_t columns;
        uint16_t rows;
        uint16_t viewOffset;
        bool screenReverse;
        bool blinkVisible;
        bool cursorBlink;
        Color selectionForeground;
        Color selectionBackground;
        uint8_t selectionColorMask;
    };
    PresentationState capturePresentationState() const;
    bool presentationChanged(const PresentationState& before) const;
    void syncPresentationCursor();

    void writeCsiResponse(const std::string& payload);
    void writeDcsResponse(const std::string& payload);
    void writeOscResponse(const std::string& payload);

    struct InputSpecTable {
        std::function<bool()> predicate;
        const InputSpec* specs = nullptr;
        bool visited = false;
    };

    InputSpecTable* getInputSpecTable();
    void resetInputSpecTable();
    const InputSpec* selectInputSpecs();
    const InputSpec& getInputSpec(VtKey key);

    void unhandledInput(unsigned char ch);
    void traceNormalInput();
    void resetTerminal();
    void resetAttrs();
    void resetScreen(bool resetTabStops = true);
    void clearScreen();
    void fillScreen(uint16_t ch);
    void pruneHyperlinks();

    enum class InputState : uint8_t {
        Normal,
        IgnoreSequence,
        Escape,
        Escape_VT52,
        Esc_SPC,
        Esc_Hash,
        Esc_Pct,
        SelectCharset,
        CSI,
        DCS,
        DCS_Esc,
        OSC,
        OSC_Esc,
        String,
        String_Esc,
        VT52_CUP_Arg1,
        VT52_CUP_Arg2
    };
    const char* strInputState(InputState is) {
        static const char* enumerators[] =
            {
                "Normal",
                "IgnoreSequence",
                "Escape",
                "Escape_VT52",
                "Esc_SPC",
                "Esc_Hash",
                "Esc_Pct",
                "SelectCharset",
                "CSI",
                "DCS",
                "DCS_Esc",
                "OSC",
                "OSC_Esc",
                "String",
                "String_Esc",
                "VT52_CUP_Arg1",
                "VT52_CUP_Arg2"};
        return enumerators[(int)is];
    }

    void setState(InputState inputState);
    void beginCsi();
    bool executeC0InSequence(unsigned char ch);
    void processCsiByte(unsigned char ch);
    void dispatchCsi(unsigned char finalByte);

    void normalizeCursorPos();
    bool isCursorInsideMargins();
    void eraseRow(uint16_t pY);
    void eraseRows(uint16_t startY, uint16_t count);
    void copyRow(uint16_t dstY, uint16_t srcY);
    void insertRows(uint16_t startY, uint16_t count);
    void deleteRows(uint16_t startY, uint16_t count);
    void insertCols(uint16_t startX, uint16_t count);
    void deleteCols(uint16_t startX, uint16_t count);
    void clearWideCellAt(uint16_t row, uint16_t column);
    void normalizeWideCells(uint16_t row);

    struct Rectangle {
        uint16_t top;
        uint16_t left;
        uint16_t bottom;
        uint16_t right;
    };
    bool rectangleFromParams(size_t offset, Rectangle& rectangle) const;
    void rectangleOrigin(uint16_t& rowBase, uint16_t& columnBase,
                         uint16_t& rowLimit, uint16_t& columnLimit) const;

    void showCursor();
    void hideCursor();
    void inputGraphicChar(unsigned char ch);
    void placeGraphicChar();
    void resetGraphemeInput();
    void jumpToNextTabStop();
    void setFgFromPalIx();
    void setBgFromPalIx();

    void inp_LF();
    void inp_CR();
    void inp_HT();

    void esc_DCS(unsigned char fin);
    bool esc_IND();
    void esc_RI();
    void esc_NEL();
    void esc_BI();
    void esc_FI();
    void esc_HTS();
    void csi_SCOSC_SLRM();
    void csi_SCOSC();
    void csi_SCORC();
    void esc_DECSC();
    void esc_DECRC();
    void esc_RIS();
    void csi_DECSTR();

    void csi_CUU();
    void csi_CUD();
    void csi_CUF();
    void csi_CUB();
    void csi_CNL();
    void csi_CPL();
    void csi_CHA();
    void csi_HPA();
    void csi_HPR();
    void csi_VPA();
    void csi_VPR();
    void csi_CUP();
    void csi_SU();
    void csi_SD();
    void csi_CHT();
    void csi_CBT();
    void csi_REP();

    void csi_ED();
    void csi_EL();
    void csi_DECSED();
    void csi_DECSEL();
    void csi_DECSCA();
    void csi_DECFRA();
    void csi_DECCRA();
    void csi_DECERA(bool selective = false);
    void csi_DECCARA(bool reverse);
    void csi_DECRQCRA();
    void csi_IL();
    void csi_DL();
    void csi_ICH();
    void csi_DCH();
    void csi_ECH();
    void csi_DECIC();
    void csi_DECDC();

    void csi_STBM();
    void csi_SLRM();
    void csi_TBC();

    void csi_SM();
    void csi_RM();
    void csi_privSM();
    void csi_privRM();
    void csi_privSave();
    void csi_privRestore();
    void setPrivMode(uint32_t mode, bool set);
    bool getPrivMode(uint32_t mode) const;
    void csi_SGR();

    void csi_ecma48_SL();
    void csi_ecma48_SR();
    void csi_DECSCUSR();

    void csi_priDA();
    void csi_secDA();
    void csi_terDA();
    void csi_DSR();
    void esch_DECALN();
    void setLineAttribute(uint8_t attribute);
    void handle_DCS();
    void handle_OSC();
    void csiq_DECSCL();
    void csi_XTWINOPS();
    void csi_XTHIMOUSE();
    void csi_DECELR();
    void csi_DECSLE();
    void csi_DECRQLP();
    void csi_DECEFR();
    void csi_XTMODKEYS();
    void csi_XTQMODKEYS();
    void csi_kittyKeyboardPush();
    void csi_kittyKeyboardPop();
    void csi_kittyKeyboardSet();
    void csi_kittyKeyboardQuery();
    void csi_DECRQM(bool privateMode);
    void csi_XTVERSION();
    void csi_MC(bool privateMode);
    void csi_DECLL();
    std::string printableLine(uint16_t row) const;
    void printLine(uint16_t row);
    bool consumePrinterControllerByte(unsigned char ch);

    void dcs_DECRQSS(const std::string&);
    void dcs_XTGETTCAP(const std::string&);
    void dcs_DECUDK(const std::string&);

    void osc_PaletteQuery(int, const std::string&);
    void osc_DynamicColorQuery(int, const std::string&);
    void osc_ShellIntegration(const std::string&);
    void osc_Notification(const std::string&);
    void reportInBandResize();
    void writeTitleResponse(char, const std::string&);
    void applyPaletteColor(uint16_t index, Color color);

    VtermHost& host;
    uint16_t winPx;
    uint16_t winPy;
    uint16_t nCols;
    uint16_t nRows;
    uint16_t glyphPx;
    uint16_t glyphPy;
    int ptyFd;
    bool ptyReceivedInput = false;
    PtyReadHandlerFn onPtyRead;
    PtyWriteHandlerFn onPtyWrite;
    std::vector<uint8_t> ptyOutput;
    size_t ptyOutputOffset = 0;

    Frame frame_pri;
    Frame frame_alt;
    Frame* cf;
    uint16_t posX = 0;
    uint16_t posY = 0;
    uint16_t marginTop;
    uint16_t marginBottom;
    bool lastCol = false;

    TerminalCell attrs;
    Color* fg = &attrs.fg;
    Color* bg = &attrs.bg;
    int32_t* fgIndex = &attrs.fg_index;
    int32_t* bgIndex = &attrs.bg_index;
    Color palette256[256];
    Color originalPalette256[256];
    Color defaultFgColor;
    Color defaultBgColor;
    Color cursorColor;
    Color selectionFgColor;
    Color selectionBgColor;
    std::map<std::string, uint32_t> hyperlinkIds;
    std::map<uint32_t, std::string> hyperlinks;
    uint32_t activeHyperlink = 0;
    uint32_t nextHyperlink = 1;
    uint32_t currentSemantic = 0;
    std::string windowTitle;
    std::string iconTitle;
    struct SavedTitles {
        bool hasIcon = false;
        bool hasWindow = false;
        std::string icon;
        std::string window;
    };
    std::vector<SavedTitles> titleStack;
    struct NotificationPart {
        std::string text;
        std::string encoded;
    };
    struct Notification {
        NotificationPart title;
        NotificationPart body;
    };
    std::map<std::string, Notification> notifications;
    std::set<std::string> activeNotificationIds;
    int defaultFgPalIx;
    int defaultBgPalIx;
    int fgPalIx;
    int bgPalIx;
    int underlinePalIx = -2;
    bool reverseVideo = false;
    bool screenReverseVideo = false;
    bool underlineColorDefault = true;
    bool hasFocus = false;

    unsigned char inputBuf[32 * 1024];
    int readPos = 0;
    int lastEscBegin = 0;
    int lastNormalBegin = 0;
    int lastStopPos = 0;

    InputState inputState = InputState::Normal;
    // Whether a private/intermediate CSI prefix may still occur.  This is
    // parser state, rather than an input-buffer offset: PTY reads may split an
    // escape sequence at any byte.
    bool csiPrefixAllowed = false;
    std::string csiPrivatePrefix;
    std::string csiIntermediates;
    constexpr const static size_t maxEscOps = 32;
    constexpr const static size_t maxOscBytes = 1024 * 1024;
    uint32_t inputOps[maxEscOps];
    unsigned char inputSeparators[maxEscOps] = {};
    size_t nInputOps = 0;
    Utf8Decoder utf8dec;
    Frame::Grapheme inputGrapheme;
    GraphemeBreaker inputGraphemeBreaker;
    Frame* inputGraphemeFrame = nullptr;
    uint16_t inputGraphemeX = 0;
    uint16_t inputGraphemeY = 0;
    std::vector<unsigned char> argBuf;
    bool argBufOverflowed = false;
    unsigned char scsDst;
    unsigned char scsMod;

    VtModifier modifiers = VtModifier::none;

    bool showCursorMode = true;
    TerminalCursor::Style cursorShape =
        TerminalCursor::Style::filled_block;
    uint8_t cursorStyleParam = 2;
    bool cursorBlinkMode = false;
    bool haveBlinkingText = false;
    bool blinkVisible = true;
    std::chrono::steady_clock::time_point nextBlink;
    bool altScreenBufferMode = false;
    bool altScreenInitialized = false;
    bool autoWrapMode = true;
    bool autoNewlineMode = false;
    bool keyboardLocked = false;
    bool insertMode = false;
    bool bkspSendsDel = true;
    bool localEcho = false;
    bool bracketedPasteMode = false;
    bool synchronizedOutputMode = false;
    bool inBandResizeMode = false;
    bool printerControllerMode = false;
    bool autoPrintMode = false;
    std::string printerControllerPending;
    std::chrono::steady_clock::time_point synchronizedOutputDeadline;
    bool send8BitControls = false;
    bool altScrollMode = false;
    bool altSendsEscape = true;
    bool eightBitInput = false;
    bool reverseWrapMode = false;
    bool extendedReverseWrapMode = false;
    bool nationalReplacementMode = false;
    uint8_t ledState = 0;
    uint8_t modifyOtherKeys = 1;
    uint8_t modifyKeyResources[8] = {};
    uint8_t initialModifyKeyResources[8] = {};
    bool csiHadParams = false;
    std::map<uint32_t, bool> savedPrivModes;
    std::map<VtKey, std::string> userDefinedKeys;
    bool userDefinedKeysLocked = false;

    struct KittyKeyboardState {
        uint8_t flags = 0;
        std::vector<uint8_t> stack;
    };
    KittyKeyboardState kittyKeyboardPri;
    KittyKeyboardState kittyKeyboardAlt;

    KittyKeyboardState& kittyKeyboardState();
    const KittyKeyboardState& kittyKeyboardState() const;

    bool horizMarginMode = false;
    uint16_t nColsEff = 0;
    uint16_t hMargin = 0;

    std::vector<uint16_t> tabStops;
    bool tabStopsCustomized = false;

    enum class CompatibilityLevel : uint8_t {
        VT52,
        VT100,
        VT400
    };
    CompatibilityLevel compatLevel = CompatibilityLevel::VT400;

    enum class CursorKeyMode : uint8_t {
        ANSI,
        Application
    };
    CursorKeyMode cursorKeyMode = CursorKeyMode::ANSI;

    enum class KeypadMode : uint8_t {
        Normal,
        Application
    };
    KeypadMode keypadMode = KeypadMode::Normal;

    enum class OriginMode : uint8_t {
        Absolute,
        ScrollingRegion
    };
    OriginMode originMode = OriginMode::Absolute;

    enum class ColMode : uint8_t {
        C80,
        C132
    };
    ColMode colMode = ColMode::C80;

    void switchColMode(ColMode colMode);
    void switchScreenBufferMode(bool altScreenBufferMode,
                                bool clearAlternate = false);

    enum class Charset : uint8_t {
        UTF8,
        DecSpec,
        DecSuppl,
        DecUserPref,
        DecTechn,
        IsoLatin1,
        IsoUK,
        NrcDutch,
        NrcFinnish,
        NrcFrench,
        NrcFrenchCanadian,
        NrcGerman,
        NrcItalian,
        NrcNorwegianDanish,
        NrcPortuguese,
        NrcSpanish,
        NrcSwedish,
        NrcSwiss,
        NrcGreek,
        NrcHebrew,
        NrcRussian,
        NrcSerboCroatian,
        NrcTurkish
    };

    struct CharsetState {
        Charset g[4] =
            {Charset::UTF8, Charset::UTF8, Charset::UTF8, Charset::UTF8};

        uint8_t gl = 0;
        uint8_t gr = 2;

        uint8_t ss = 0;
    };
    CharsetState charsetState;

    static const uint16_t* charCodes[];
    uint32_t translateCharset(Charset charset, unsigned char ch) const;

    struct SavedCursor_SCO {
        bool isSet = false;
        uint16_t posX = 0;
        uint16_t posY = 0;
        bool lastCol = false;
    };
    struct SavedCursor_DEC: SavedCursor_SCO {
        TerminalCell attrs;
        OriginMode originMode = OriginMode::Absolute;
        CharsetState charsetState = CharsetState{};
    };
    SavedCursor_SCO savedCursor_SCO;
    SavedCursor_DEC savedCursor_DEC_pri;
    SavedCursor_DEC savedCursor_DEC_alt;
    SavedCursor_DEC* savedCursor_DEC = &savedCursor_DEC_pri;

    bool selectUpdatesTop = false;
    bool selectUpdatesLeft = false;

    MouseTrackingState mouseTrk;
    struct MouseHighlightState {
        bool active = false;
        uint16_t startX = 1;
        uint16_t startY = 1;
        uint16_t firstRow = 1;
        uint16_t lastRow = 1;
    } mouseHighlight;
    struct LocatorState {
        uint8_t enabled = 0;
        bool pixels = false;
        bool reportDown = false;
        bool reportUp = false;
        uint8_t buttons = 0;
        uint16_t column = 1;
        uint16_t row = 1;
        uint16_t pixelX = 1;
        uint16_t pixelY = 1;
        bool filter = false;
        uint16_t filterTop = 1;
        uint16_t filterLeft = 1;
        uint16_t filterBottom = 1;
        uint16_t filterRight = 1;
    } locator;

#ifdef DEBUG
    void traceFunction(const char* func);

    int debugStep = 0;
    int debugCnt = 0;
    void debugKey();
    void debugBreak();
#endif
};

#include "vterm.icc"
