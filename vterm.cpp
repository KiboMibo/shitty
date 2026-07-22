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

#include "vterm.h"
#include "vterm_trace.h"

#include "base64.h"
#include "composer.h"
#include "frame.h"
#include "grapheme.h"
#include "log.h"
#include "options.h"
#include "pty.h"
#include "utf8.h"
#include "vterm_host.h"

#include <std/mem/obj_pool.h>
#include <std/ios/out.h>
#include <std/ios/output.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/fd.h>
#include <std/sys/throw.h>
#include <std/sys/types.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <signal.h>
#include <sstream>
#include <sys/types.h>
#include <thread>

using namespace stl;

void MouseTrackingState::setMode(MouseTrackingMode value) {
    if (mode != value) {
        mode = value;
        ++generation;
    }
}

void MouseTrackingState::setEncoding(MouseTrackingEnc value) {
    if (enc != value) {
        enc = value;
        ++generation;
    }
}

namespace {
    class GraphemeBuffer {
    public:
        void clear();
        void push_back(u32 codepoint);
        bool empty() const;
        size_t size() const;
        const u32* data() const;

    private:
        constexpr static size_t inlineCapacity = 4;
        std::array<u32, inlineCapacity> inlineValues = {};
        std::vector<u32> overflowValues;
        size_t size_ = 0;
    };

    class VtermImpl final: public Vterm {
    public:
        VtermImpl(VtermHost& host, Pty& pty, Output* dump, u16 glyphPx, u16 glyphPy, u16 winPx, u16 winPy);

        ~VtermImpl();

        bool getScreenReverseVideo() const;
        u8 getLedState() const;
        bool getReverseWrapMode() const;
        bool getNationalReplacementMode() const;
        bool getMetaMode() const;
        bool getAnsiMode(u32 mode) const;
        bool getPrivateMode(u32 mode) const;
        TerminalPen getPenState() const;

        void resize(u16 winPx, u16 winPy);

        void redraw();
        bool synchronizedOutputActive() const;
        bool expireSynchronizedOutput(bool force = false);
        bool animationActive() const;
        bool advanceAnimation(bool force = false);

        struct InputSpec {
            VtKey key;
            const char* input;
            size_t length = 0;

            size_t getLength() const;
        };

        int writePty(VtKey key, VtModifier modifiers = VtModifier::none, bool userInput = false);
        int writePty(u8 ch, VtModifier modifiers = VtModifier::none, bool userInput = false);
        int writePty(const char* cstr, bool userInput = false);
        int writePty(const char* data, size_t size, bool userInput);
        int writePty(const u8* ucstr, size_t len, bool userInput = false);
        bool flushPtyOutput();
        bool hasPendingPtyOutput() const;
        size_t pendingPtyOutputBytes() const;
        int writeKittyKey(VtKey key, u16 modifiers, KeyEventType event);
        int writeKittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, KeyEventType event);
        u8 getKittyKeyboardFlags() const;

        bool readPty();
        bool servicePty(bool readable, bool writable);
        void feedPtyOutput(const std::string& output);
        void setParserTrace(VtermTrace* trace);

        const MouseTrackingState& getMouseTrackingState() const;
        bool mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY);
        void setLocatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons = 0);
        void reportLocatorButton(u8 button, bool pressed);

        void setHasFocus(bool);
        void setHyperlink(const std::string& parametersAndUri);
        std::string getHyperlink(int pX, int pY) const;
        size_t getHyperlinkCount() const;
        void mouseWheelUp(u16 count = 1);
        void mouseWheelDown(u16 count = 1);
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
        Point selectionPoint(int pX, int pY) const;
        std::string getLocalEcho(const u8* const begin, const u8* const end);
        bool processInput(const u8* input, int size, bool refresh = true);
        template <bool traced>
        bool processInputImpl(const u8* input, int size, bool refresh);
        bool processInput(const std::string& str);

        struct PresentationState {
            Frame* frame;
            TerminalCursor cursor;
            Rect selection;
            u16 columns;
            u16 rows;
            u16 viewOffset;
            bool screenReverse;
            bool blinkVisible;
            bool cursorBlink;
            Color selectionForeground;
            Color selectionBackground;
            u8 selectionColorMask;
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
        void fillScreen(u16 ch);
        void pruneHyperlinks();

        enum class InputState : u8 {
            Normal,
            IgnoreSequence,
            Escape,
            EscapeIntermediate,
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
        const char* strInputState(InputState is);

        void setState(InputState inputState);
        bool stringUtf8Continuation(u8 ch);
        void beginCsi();
        template <bool traced>
        bool executeC0InSequence(unsigned char ch);
        template <bool traced>
        void processCsiByte(unsigned char ch);
        template <bool traced>
        void dispatchCsi(unsigned char finalByte);

        void normalizeCursorPos();
        bool isCursorInsideMargins();
        void eraseRow(u16 pY);
        void eraseRows(u16 startY, u16 count);
        void copyRow(u16 dstY, u16 srcY);
        void insertRows(u16 startY, u16 count);
        void deleteRows(u16 startY, u16 count);
        void insertCols(u16 startX, u16 count);
        void deleteCols(u16 startX, u16 count);
        TerminalCell& prepareCellAt(u16 row, u16 column);
        void normalizeWideCells(u16 row);

        struct Rectangle {
            u16 top;
            u16 left;
            u16 bottom;
            u16 right;
        };

        bool rectangleFromParams(size_t offset, Rectangle& rectangle) const;
        void rectangleOrigin(u16& rowBase, u16& columnBase, u16& rowLimit, u16& columnLimit) const;

        void showCursor();
        void hideCursor();
        void inputGraphicChar(unsigned char ch);
        void placeGraphicChar();
        void placeAsciiRun(const u8* input, size_t size);
        void resetGraphemeInput();
        void jumpToNextTabStop();
        void setFgFromPalIx();
        void setBgFromPalIx();

        void inp_LF();
        void inp_CR();
        void inp_HT();
        bool performIndex();
        void moveCursorBackward(u32 count);
        void scrollRegionUp(u16 count);

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
        void setPrivMode(u32 mode, bool set);
        void csi_SGR();

        void csi_ecma48_SL();
        void csi_ecma48_SR();
        void csi_DECSCUSR();

        void csi_priDA();
        void csi_secDA();
        void csi_terDA();
        void csi_DSR(bool privateMode = false);
        void esch_DECALN();
        void setLineAttribute(u8 attribute);
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
        std::string printableLine(u16 row) const;
        void printLine(u16 row);
        size_t consumePrinterController(const u8* input, size_t size);

        void dcs_DECRQSS(const std::string&);
        void dcs_XTGETTCAP(const std::string&);
        void dcs_DECUDK(const std::string&);

        void osc_PaletteQuery(int, const std::string&);
        void osc_DynamicColorQuery(int, const std::string&);
        void osc_ShellIntegration(const std::string&);
        void osc_Notification(const std::string&);
        void reportInBandResize();
        void reportColorScheme();
        void writeTitleResponse(char, const std::string&);
        void applyPaletteColor(u16 index, Color color);

        VtermHost& host;
        Pty& pty;
        Output* dump;
        VtermTrace* parserTrace = nullptr;
        u16 winPx;
        u16 winPy;
        u16 nCols;
        u16 nRows;
        u16 glyphPx;
        u16 glyphPy;
        bool ptyReceivedInput = false;
        std::u8string ptyOutput;
        size_t ptyOutputOffset = 0;

        TerminalColors colors;
        Color originalPalette256[256];
        Frame frame_pri;
        Frame frame_alt;
        Frame* cf;
        u16 posX = 0;
        u16 posY = 0;
        u16 marginTop;
        u16 marginBottom;
        bool lastCol = false;

        TerminalCell attrs;
        CellColor* fg = &attrs.fg;
        CellColor* bg = &attrs.bg;
        Color cursorColor;
        Color selectionFgColor;
        Color selectionBgColor;
        std::map<std::string, u32> hyperlinkIds;
        std::map<u32, std::string> hyperlinks;
        u32 activeHyperlink = 0;
        u32 nextHyperlink = 1;
        u32 currentSemantic = 0;
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

        u8 inputBuf[32 * 1024];
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
        u32 inputOps[maxEscOps];
        unsigned char inputSeparators[maxEscOps] = {};
        size_t nInputOps = 0;
        Utf8Decoder utf8dec;
        GraphemeBuffer inputGrapheme;
        u32 inputGraphemeBase = 0;
        GraphemeBreaker inputGraphemeBreaker;
        Frame* inputGraphemeFrame = nullptr;
        u16 inputGraphemeX = 0;
        u16 inputGraphemeY = 0;
        std::vector<unsigned char> argBuf;
        bool argBufOverflowed = false;
        u8 stringUtf8Remaining = 0;
        unsigned char scsDst;
        unsigned char scsMod;

        VtModifier modifiers = VtModifier::none;

        bool showCursorMode = true;
        bool smoothScrollMode = false;
        bool autoRepeatMode = false;
        TerminalCursor::Style cursorShape = TerminalCursor::Style::filled_block;
        u8 cursorStyleParam = 2;
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
        bool colorSchemeUpdateMode = false;
        bool inBandResizeMode = false;
        bool printerControllerMode = false;
        bool autoPrintMode = false;

        enum class PrinterControllerState : u8 {
            Normal,
            Escape,
            EscapeBracket,
            EscapeBracket4,
            Csi,
            Csi4
        };
        PrinterControllerState printerControllerState = PrinterControllerState::Normal;

        std::chrono::steady_clock::time_point synchronizedOutputDeadline;
        bool send8BitControls = false;
        bool altScrollMode = false;
        bool altSendsEscape = true;
        bool eightBitInput = false;
        bool reverseWrapMode = false;
        bool extendedReverseWrapMode = false;
        bool nationalReplacementMode = false;
        u8 ledState = 0;
        u8 modifyOtherKeys = 1;
        u8 modifyKeyResources[8] = {};
        u8 initialModifyKeyResources[8] = {};
        bool csiHadParams = false;
        std::map<u32, bool> savedPrivModes;
        std::map<VtKey, std::string> userDefinedKeys;
        bool userDefinedKeysLocked = false;

        struct KittyKeyboardState {
            u8 flags = 0;
            std::vector<u8> stack;
        };

        KittyKeyboardState kittyKeyboardPri;
        KittyKeyboardState kittyKeyboardAlt;

        KittyKeyboardState& kittyKeyboardState();
        const KittyKeyboardState& kittyKeyboardState() const;

        bool horizMarginMode = false;
        u16 nColsEff = 0;
        u16 hMargin = 0;

        std::vector<u16> tabStops;
        bool tabStopsCustomized = false;

        enum class CompatibilityLevel : u8 {
            VT52,
            VT100,
            VT400
        };
        CompatibilityLevel compatLevel = CompatibilityLevel::VT400;

        enum class CursorKeyMode : u8 {
            ANSI,
            Application
        };
        CursorKeyMode cursorKeyMode = CursorKeyMode::ANSI;

        enum class KeypadMode : u8 {
            Normal,
            Application
        };
        KeypadMode keypadMode = KeypadMode::Normal;

        enum class OriginMode : u8 {
            Absolute,
            ScrollingRegion
        };
        OriginMode originMode = OriginMode::Absolute;

        enum class ColMode : u8 {
            C80,
            C132
        };
        ColMode colMode = ColMode::C80;

        void switchColMode(ColMode colMode);
        void switchScreenBufferMode(bool altScreenBufferMode, bool clearAlternate = false);

        enum class Charset : u8 {
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
            Charset g[4] = {Charset::UTF8, Charset::UTF8, Charset::UTF8, Charset::UTF8};

            u8 gl = 0;
            u8 gr = 2;

            u8 ss = 0;
        };

        CharsetState charsetState;

        static const u16* charCodes[];
        u32 translateCharset(Charset charset, unsigned char ch) const;

        struct SavedCursor_SCO {
            bool isSet = false;
            u16 posX = 0;
            u16 posY = 0;
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
            u16 startX = 1;
            u16 startY = 1;
            u16 firstRow = 1;
            u16 lastRow = 1;
        } mouseHighlight;

        struct LocatorState {
            u8 enabled = 0;
            bool pixels = false;
            bool reportDown = false;
            bool reportUp = false;
            u8 buttons = 0;
            u16 column = 1;
            u16 row = 1;
            u16 pixelX = 1;
            u16 pixelY = 1;
            bool filter = false;
            u16 filterTop = 1;
            u16 filterLeft = 1;
            u16 filterBottom = 1;
            u16 filterRight = 1;
        } locator;

#ifdef DEBUG
        void traceFunction(const char* func);

        int debugStep = 0;
        int debugCnt = 0;
        void debugKey();
        void debugBreak();
#endif
    };

    void GraphemeBuffer::clear() {
        size_ = 0;
    }

    void GraphemeBuffer::push_back(u32 codepoint) {
        if (size_ < inlineCapacity) {
            inlineValues[size_++] = codepoint;
            return;
        }
        if (size_ == inlineCapacity) {
            overflowValues.assign(inlineValues.begin(), inlineValues.end());
        }
        overflowValues.push_back(codepoint);
        ++size_;
    }

    bool GraphemeBuffer::empty() const {
        return size_ == 0;
    }

    size_t GraphemeBuffer::size() const {
        return size_;
    }

    const u32* GraphemeBuffer::data() const {
        return size_ <= inlineCapacity ? inlineValues.data() : overflowValues.data();
    }
}

namespace {
    bool parseOscColor(const std::string& spec, Color& color) {
        const auto hexDigit = [](unsigned char ch) -> int {
            if (ch >= '0' && ch <= '9') {
                return ch - '0';
            }
            if (ch >= 'a' && ch <= 'f') {
                return ch - 'a' + 10;
            }
            if (ch >= 'A' && ch <= 'F') {
                return ch - 'A' + 10;
            }
            return -1;
        };
        const auto component = [&](const std::string& value, u8& out) {
            if (value.empty() || value.size() > 4) {
                return false;
            }
            unsigned parsed = 0;
            for (unsigned char ch : value) {
                const int digit = hexDigit(ch);
                if (digit < 0) {
                    return false;
                }
                parsed = parsed * 16 + digit;
            }
            const unsigned maximum = (1u << (4 * value.size())) - 1;
            out = (u8)((parsed * 255 + maximum / 2) / maximum);
            return true;
        };

        if (spec.size() == 7 && spec[0] == '#') {
            return component(spec.substr(1, 2), color.red) && component(spec.substr(3, 2), color.green) && component(spec.substr(5, 2), color.blue);
        }
        if (spec.compare(0, 4, "rgb:") != 0) {
            return false;
        }
        const size_t first = spec.find('/', 4);
        const size_t second = first == std::string::npos ? std::string::npos : spec.find('/', first + 1);
        return first != std::string::npos && second != std::string::npos && spec.find('/', second + 1) == std::string::npos && component(spec.substr(4, first - 4), color.red) && component(spec.substr(first + 1, second - first - 1), color.green) && component(spec.substr(second + 1), color.blue);
    }
}

VtermImpl::~VtermImpl() = default;

bool VtermImpl::getScreenReverseVideo() const {
    return screenReverseVideo;
}

u8 VtermImpl::getLedState() const {
    return ledState;
}

bool VtermImpl::getReverseWrapMode() const {
    return reverseWrapMode;
}

bool VtermImpl::getNationalReplacementMode() const {
    return nationalReplacementMode;
}

bool VtermImpl::getMetaMode() const {
    return eightBitInput;
}

bool VtermImpl::getAnsiMode(u32 mode) const {
    switch (mode) {
        case 4:
            return insertMode;
        case 12:
            return !localEcho;
        case 20:
            return autoNewlineMode;
        default:
            return false;
    }
}

bool VtermImpl::synchronizedOutputActive() const {
    return synchronizedOutputMode;
}

bool VtermImpl::animationActive() const {
    return haveBlinkingText || cursorBlinkMode;
}

size_t VtermImpl::InputSpec::getLength() const {
    return length ? length : strlen(input);
}

bool VtermImpl::hasPendingPtyOutput() const {
    return ptyOutputOffset < ptyOutput.size();
}

size_t VtermImpl::pendingPtyOutputBytes() const {
    return ptyOutput.size() - ptyOutputOffset;
}

size_t VtermImpl::getHyperlinkCount() const {
    return hyperlinks.size();
}

const char* VtermImpl::strInputState(InputState is) {
    static const char* enumerators[] = {"Normal", "IgnoreSequence", "Escape", "EscapeIntermediate", "Escape_VT52", "Esc_SPC", "Esc_Hash", "Esc_Pct", "SelectCharset", "CSI", "DCS", "DCS_Esc", "OSC", "OSC_Esc", "String", "String_Esc", "VT52_CUP_Arg1", "VT52_CUP_Arg2"};
    return enumerators[(int)is];
}

#ifdef DEBUG

void VtermImpl::debugKey() {
    switch (debugStep) {
        case 0:
            debugStep = 1;
            break;
        case 1:
            debugStep = 10;
            break;
        case 10:
            debugStep = 100;
            break;
        case 100:
            debugStep = 0;
            break;
    }
    debugCnt = debugStep;
    logT << "*** DEBUG step=" << debugStep << std::endl;
}

void VtermImpl::debugBreak() {
    if (!debugStep || --debugCnt > 0) {
        return;
    }

    debugCnt = debugStep;
    logT << "*** DEBUG STOP (step=" << debugStep << "), " << readPos + 1 - lastStopPos << " bytes since last:\n        " << dumpBuffer(inputBuf + lastStopPos, inputBuf + readPos + 1);
    lastStopPos = readPos + 1;

    logT << "Issue 'kill -CONT " << getpid() << "' or 'fg' to continue." << std::endl;

    redraw();
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(100ms);

    kill(getpid(), SIGSTOP);
}

    #define DEBUG_BREAK debugBreak()
    #define TRACE_FUN                                                                                     \
        do {                                                                                              \
            logT << __FUNCTION__ << " [";                                                                 \
            for (size_t k = 0; k < nInputOps; ++k) {                                                      \
                if (k) {                                                                                  \
                    vlog << ";";                                                                          \
                }                                                                                         \
                vlog << inputOps[k];                                                                      \
            }                                                                                             \
            vlog << "] \t"                                                                                \
                 << "p(" << posY << "," << posX << ")  "                                                  \
                 << "d(" << nRows << "," << nCols << ")  "                                                \
                 << "mgn[" << marginTop << "," << marginBottom << ")  "                                   \
                 << "hmgn:" << horizMarginMode << " [" << hMargin << "," << nColsEff << ")" << std::endl; \
        } while (0)
#else
    #define DEBUG_BREAK
    #define TRACE_FUN
#endif

void VtermImpl::unhandledInput(unsigned char ch) {
    logT << "Unhandled input char '" << ch << "' (" << (int)ch << ") in state " << strInputState(inputState) << ". Escape sequence so far: " << dumpBuffer(inputBuf + lastEscBegin, inputBuf + readPos + 1);
    if ((ch >= 0x20 && ch <= 0x2f) || ch == ':') {
        switch (inputState) {
            case InputState::CSI:
                setState(InputState::IgnoreSequence);
                return;
            default:
                break;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::traceNormalInput() {
#ifdef DEBUG
    if (lastNormalBegin < readPos) {
        auto dumpbufs = dumpBuffer(inputBuf + lastNormalBegin, inputBuf + readPos);
        if (dumpbufs.length()) {
            logT << "Inserted: " << dumpbufs;
        }
    }
    lastNormalBegin = readPos + 1;
#endif
}

void VtermImpl::redraw() {
    if (synchronizedOutputMode) {
        return;
    }

    if (host.present(*cf)) {
        cf->resetDamage();
    }
}

bool VtermImpl::advanceAnimation(bool force) {
    if (!animationActive()) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < nextBlink) {
        return false;
    }
    blinkVisible = !blinkVisible;
    nextBlink = now + std::chrono::milliseconds(500);
    frame_pri.setBlinkState(blinkVisible, cursorBlinkMode);
    frame_alt.setBlinkState(blinkVisible, cursorBlinkMode);
    return true;
}

bool VtermImpl::expireSynchronizedOutput(bool force) {
    if (!synchronizedOutputMode || (!force && std::chrono::steady_clock::now() < synchronizedOutputDeadline)) {
        return false;
    }
    synchronizedOutputMode = false;
    redraw();
    return true;
}

const MouseTrackingState& VtermImpl::getMouseTrackingState() const {
    return mouseTrk;
}

bool VtermImpl::mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY) {
    if (mouseTrk.mode != MouseTrackingMode::VT200_Highlight || !mouseHighlight.active) {
        return false;
    }
    mouseHighlight.active = false;
    endY = std::clamp(endY, mouseHighlight.firstRow, mouseHighlight.lastRow);
    std::ostringstream response;
    if (mouseTrk.enc == MouseTrackingEnc::SGR || mouseTrk.enc == MouseTrackingEnc::SGRPixels) {
        response << '<' << mouseHighlight.startX << ';' << mouseHighlight.startY << ';' << endX << ';' << endY << ';' << mouseX << ';' << mouseY << 'T';
        writeCsiResponse(response.str());
    } else {
        const auto coordinate = [](u16 value) {
            return (char)(32 + std::clamp<u16>(value, 1, 223));
        };
        response << (mouseHighlight.startX == endX && mouseHighlight.startY == endY ? 't' : 'T') << coordinate(mouseHighlight.startX) << coordinate(mouseHighlight.startY);
        if (mouseHighlight.startX != endX || mouseHighlight.startY != endY) {
            response << coordinate(endX) << coordinate(endY) << coordinate(mouseX) << coordinate(mouseY);
        }
        writeCsiResponse(response.str());
    }
    return true;
}

void VtermImpl::setLocatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons) {
    locator.column = std::max<u16>(1, column);
    locator.row = std::max<u16>(1, row);
    locator.pixelX = std::max<u16>(1, pixelX);
    locator.pixelY = std::max<u16>(1, pixelY);
    locator.buttons = buttons & 15;
    if (locator.enabled && locator.filter) {
        const u16 x = locator.pixels ? locator.pixelX : locator.column;
        const u16 y = locator.pixels ? locator.pixelY : locator.row;
        if (y < locator.filterTop || y > locator.filterBottom || x < locator.filterLeft || x > locator.filterRight) {
            std::ostringstream response;
            response << "10;" << (unsigned)(locator.buttons) << ';' << y << ';' << x << ";0&w";
            writeCsiResponse(response.str());
            locator.filter = false;
            if (locator.enabled == 2) {
                locator.enabled = 0;
            }
        }
    }
}

void VtermImpl::reportLocatorButton(u8 button, bool pressed) {
    if (!locator.enabled || (pressed ? !locator.reportDown : !locator.reportUp) || button < 1 || button > 4) {
        return;
    }
    const u8 bits[] = {0, 4, 2, 1, 8};
    if (pressed) {
        locator.buttons |= bits[button];
    } else {
        locator.buttons &= ~bits[button];
    }
    const u32 event = 2 + (button - 1) * 2 + (pressed ? 0 : 1);
    const u16 x = locator.pixels ? locator.pixelX : locator.column;
    const u16 y = locator.pixels ? locator.pixelY : locator.row;
    std::ostringstream response;
    response << event << ';' << (unsigned)(locator.buttons) << ';' << y << ';' << x << ";0&w";
    writeCsiResponse(response.str());
    if (locator.enabled == 2) {
        locator.enabled = 0;
    }
}

VtermImpl::KittyKeyboardState& VtermImpl::kittyKeyboardState() {
    return altScreenBufferMode ? kittyKeyboardAlt : kittyKeyboardPri;
}

const VtermImpl::KittyKeyboardState& VtermImpl::kittyKeyboardState() const {
    return altScreenBufferMode ? kittyKeyboardAlt : kittyKeyboardPri;
}

u8 VtermImpl::getKittyKeyboardFlags() const {
    return kittyKeyboardState().flags;
}

void VtermImpl::setHasFocus(bool hasFocus_) {
    hasFocus = hasFocus_;
    if (mouseTrk.focusEventMode) {
        writeCsiResponse(hasFocus ? "I" : "O");
    }
    showCursor();
    redraw();
}

void VtermImpl::pageUp() {
    if (altScrollMode && altScreenBufferMode) {
        inputOps[0] = 1;
        nInputOps = 1;
        for (int k = 0; k < (marginBottom - marginTop) / 2; ++k) {
            writePty(VtKey::Up);
        }
    } else {
        cf->pageUp(nRows / 2);
        redraw();
    }
}

void VtermImpl::pageDown() {
    if (altScrollMode && altScreenBufferMode) {
        inputOps[0] = 1;
        nInputOps = 1;
        for (int k = 0; k < (marginBottom - marginTop) / 2; ++k) {
            writePty(VtKey::Down);
        }
    } else {
        cf->pageDown(nRows / 2);
        redraw();
    }
}

void VtermImpl::mouseWheelUp(u16 count) {
    if (altScrollMode && altScreenBufferMode) {
        inputOps[0] = 1;
        nInputOps = 1;
        for (u16 k = 0; k < count; ++k) {
            writePty(VtKey::Up);
        }
    } else {
        cf->pageUp(count);
        redraw();
    }
}

void VtermImpl::mouseWheelDown(u16 count) {
    if (altScrollMode && altScreenBufferMode) {
        inputOps[0] = 1;
        nInputOps = 1;
        for (u16 k = 0; k < count; ++k) {
            writePty(VtKey::Down);
        }
    } else {
        cf->pageDown(count);
        redraw();
    }
}

void VtermImpl::resetTerminal() {
    switchScreenBufferMode(false, true);
    resetScreen();
    resetAttrs();

    switchColMode(ColMode::C80);

    cf->dropScrollbackHistory();
    marginTop = 0;
    marginBottom = nRows;
    clearScreen();

    switchScreenBufferMode(false);
    altScrollMode = opts.altScrollMode;
    altSendsEscape = opts.altSendsEscape;
    modifyOtherKeys = opts.modifyOtherKeys;
    std::copy(std::begin(initialModifyKeyResources), std::end(initialModifyKeyResources), std::begin(modifyKeyResources));
    savedPrivModes.clear();
    userDefinedKeys.clear();
    userDefinedKeysLocked = false;
    kittyKeyboardPri = {};
    kittyKeyboardAlt = {};
    savedCursor_DEC_pri.isSet = false;
    savedCursor_DEC_alt.isSet = false;
    activeHyperlink = 0;
    hyperlinkIds.clear();
    hyperlinks.clear();
    nextHyperlink = 1;
    currentSemantic = 0;
    titleStack.clear();
    notifications.clear();
    activeNotificationIds.clear();

    horizMarginMode = false;
    hMargin = 0;
    nColsEff = nCols;

    setState(InputState::Normal);

    if (host.handlesOsc()) {
        argBuf.clear();
        argBuf.push_back('0');
        argBuf.push_back(';');
        for (const char* p = opts.title; *p != '\0'; ++p) {
            argBuf.push_back(*p);
        }
        handle_OSC();
    }
}

void VtermImpl::resetScreen(bool resetTabStops) {
    utf8dec.setUnicode(0);
    showCursorMode = true;
    smoothScrollMode = false;
    autoRepeatMode = false;
    cursorShape = TerminalCursor::Style::filled_block;
    cursorStyleParam = 2;
    cursorBlinkMode = false;
    haveBlinkingText = false;
    blinkVisible = true;
    nextBlink = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    frame_pri.setBlinkState(true, false);
    frame_alt.setBlinkState(true, false);
    autoWrapMode = true;
    autoNewlineMode = false;
    keyboardLocked = false;
    insertMode = false;
    bkspSendsDel = true;
    localEcho = false;
    bracketedPasteMode = false;
    synchronizedOutputMode = false;
    colorSchemeUpdateMode = false;
    inBandResizeMode = false;
    printerControllerMode = false;
    autoPrintMode = false;
    printerControllerState = PrinterControllerState::Normal;
    screenReverseVideo = false;
    frame_pri.setScreenReverseVideo(false);
    frame_alt.setScreenReverseVideo(false);
    eightBitInput = false;
    reverseWrapMode = false;
    extendedReverseWrapMode = false;
    nationalReplacementMode = false;
    ledState = 0;
    host.leds(ledState);
    send8BitControls = false;
    altScrollMode = opts.altScrollMode;
    altSendsEscape = opts.altSendsEscape;

    compatLevel = CompatibilityLevel::VT400;
    cursorKeyMode = CursorKeyMode::ANSI;
    keypadMode = KeypadMode::Normal;
    originMode = OriginMode::Absolute;
    charsetState = CharsetState{};

    savedCursor_SCO.isSet = false;
    savedCursor_DEC->isSet = false;

    mouseTrk.setMode(MouseTrackingMode::Disabled);
    mouseTrk.setEncoding(MouseTrackingEnc::Default);
    mouseTrk.focusEventMode = false;
    locator = LocatorState{};

    if (resetTabStops) {
        tabStops.clear();
        tabStopsCustomized = false;
    }
    cf->getSelection().clear();
}

void VtermImpl::resetAttrs() {
    reverseVideo = false;
    fg = &attrs.fg;
    bg = &attrs.bg;

    inputOps[0] = 0;
    nInputOps = 1;
    csi_SGR();
}

void VtermImpl::clearScreen() {
    posX = 0;
    posY = 0;
    lastCol = false;
    fillScreen(' ');
}

void VtermImpl::fillScreen(u16 ch) {
    cf->fillCells(ch, attrs);
}

void VtermImpl::switchColMode(ColMode colMode_) {
    if (colMode == colMode_) {
        return;
    }

    clearScreen();

    if (colMode_ == ColMode::C80) {
        logT << "DECCOLM: Selected 80 columns per line" << std::endl;
    } else {
        logT << "DECCOLM: Selected 132 columns per line" << std::endl;
    }

    colMode = colMode_;
}

void VtermImpl::switchScreenBufferMode(bool altScreenBufferMode_, bool clearAlternate) {
    if (altScreenBufferMode == altScreenBufferMode_) {
        if (clearAlternate) {
            if (altScreenBufferMode_) {
                kittyKeyboardAlt = {};
                frame_alt = Frame(winPx, winPy, nCols, nRows, marginTop, marginBottom, &colors, opts.saveLines);
                altScreenInitialized = true;
                cf = &frame_alt;
                cf->expose();
            } else if (altScreenInitialized) {
                frame_alt.freeCells();
                altScreenInitialized = false;
                savedCursor_DEC_alt.isSet = false;
            }
        }
        return;
    }

    if (altScreenBufferMode_) {
        if (clearAlternate || !altScreenInitialized) {
            kittyKeyboardAlt = {};
            frame_alt = Frame(winPx, winPy, nCols, nRows, marginTop, marginBottom, &colors, opts.saveLines);
            altScreenInitialized = true;
        } else {
            frame_alt.resize(winPx, winPy, nCols, nRows, marginTop, marginBottom);
        }
        cf = &frame_alt;
        cf->expose();

        savedCursor_DEC = &savedCursor_DEC_alt;
        altScreenBufferMode = true;
    } else {
        frame_pri.resize(winPx, winPy, nCols, nRows, marginTop, marginBottom);
        cf = &frame_pri;
        cf->expose();
        if (clearAlternate) {
            frame_alt.freeCells();
            altScreenInitialized = false;
            savedCursor_DEC_alt.isSet = false;
        }
        savedCursor_DEC = &savedCursor_DEC_pri;
        altScreenBufferMode = false;
    }
}

void VtermImpl::setState(InputState newState) {
    if (newState == inputState) {
        return;
    }

    stringUtf8Remaining = 0;

    if (newState == InputState::Normal) {
        DEBUG_BREAK;
        csiPrefixAllowed = false;
        nInputOps = 0;
        inputOps[0] = 0;
        lastNormalBegin = readPos + 1;
    } else if (inputState == InputState::Normal) {
        resetGraphemeInput();
        traceNormalInput();
    }

    inputState = newState;
}

bool VtermImpl::stringUtf8Continuation(u8 ch) {
    if (stringUtf8Remaining != 0) {
        if ((ch & 0xc0) == 0x80) {
            --stringUtf8Remaining;
            return true;
        }
        stringUtf8Remaining = 0;
    }

    if (ch >= 0xc2 && ch <= 0xdf) {
        stringUtf8Remaining = 1;
    } else if (ch >= 0xe0 && ch <= 0xef) {
        stringUtf8Remaining = 2;
    } else if (ch >= 0xf0 && ch <= 0xf4) {
        stringUtf8Remaining = 3;
    }
    return false;
}

bool VtermImpl::readPty() {
    // Drain everything already available. PTYs do not preserve application
    // update boundaries, so individual read() calls must not become frames.
    // The cap only prevents a producer that never reaches EAGAIN from
    // monopolizing the window thread indefinitely.
    constexpr size_t maxDrainBytes = 20 * 1024 * 1024;
    size_t drainBytes = 0;
    bool presentationPending = false;
    bool finished = false;
    while (drainBytes < maxDrainBytes) {
        const ssize_t n = pty.read(inputBuf, sizeof(inputBuf));
        if (n > 0) {
            if (!ptyReceivedInput) {
                pty.resize(nCols, nRows);
                ptyReceivedInput = true;
            }

            logT << "pty read: " << dumpBuffer(inputBuf, inputBuf + n);
            if (dump != nullptr) {
                dump->write(inputBuf, n);
            }
            presentationPending |= processInput(inputBuf, n, false);
            drainBytes += (size_t)(n);
            continue;
        }
        if (n == 0) {
            finished = true;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EIO) {
            finished = true;
            break;
        }
        SYS_WARN("pty read");
        finished = true;
        break;
    }
    if (presentationPending) {
        redraw();
    }
    return finished;
}

void VtermImpl::normalizeCursorPos() {
    if (nColsEff < posX + 1) {
        posX = nColsEff - 1;
    }

    if (nRows < posY + 1) {
        posY = nRows - 1;
    }

    lastCol = false;
}

bool VtermImpl::isCursorInsideMargins() {
    return posX >= hMargin && posX < nColsEff && posY >= marginTop && posY < marginBottom;
}

void VtermImpl::eraseRow(u16 pY) {
    cf->eraseInRow(pY, hMargin, nColsEff - hMargin, attrs);
}

void VtermImpl::eraseRows(u16 startY, u16 count) {
    for (u16 pY = startY; pY < startY + count; ++pY) {
        eraseRow(pY);
    }
}

void VtermImpl::copyRow(u16 dstY, u16 srcY) {
    cf->copyRow(dstY, srcY, hMargin, nColsEff - hMargin);
}

void VtermImpl::insertRows(u16 startY, u16 count) {
    for (u16 pY = marginBottom - count; pY > startY;) {
        --pY;
        copyRow(pY + count, pY);
    }

    for (u16 pY = startY; pY < startY + count; ++pY) {
        eraseRow(pY);
    }
}

void VtermImpl::deleteRows(u16 startY, u16 count) {
    for (u16 pY = startY; pY < marginBottom - count; ++pY) {
        copyRow(pY, pY + count);
    }

    for (u16 pY = marginBottom - count; pY < marginBottom; ++pY) {
        eraseRow(pY);
    }
}

void VtermImpl::insertCols(u16 startX, u16 count) {
    for (u16 r = marginTop; r < marginBottom; ++r) {
        cf->moveInRow(r, startX + count, startX, nColsEff - startX - count);
        cf->eraseInRow(r, startX, count, attrs);
        normalizeWideCells(r);
    }
}

void VtermImpl::deleteCols(u16 startX, u16 count) {
    for (u16 r = marginTop; r < marginBottom; ++r) {
        cf->moveInRow(r, startX, startX + count, nColsEff - startX - count);
        cf->eraseInRow(r, nColsEff - count, count, attrs);
        normalizeWideCells(r);
    }
}

TerminalCell& VtermImpl::prepareCellAt(u16 row, u16 column) {
    auto& cell = cf->getCell(row, column);
    if (cell.dwidth_cont && column > 0) {
        cf->eraseInRow(row, column - 1, 1, attrs);
    } else if (cell.dwidth && column + 1 < nCols) {
        cf->eraseInRow(row, column + 1, 1, attrs);
    }
    return cell;
}

void VtermImpl::normalizeWideCells(u16 row) {
    const Frame& frame = *cf;
    for (u16 column = 0; column < nCols; ++column) {
        const auto& cell = frame.getCell(row, column);
        if (cell.dwidth && (column + 1 >= nCols || !frame.getCell(row, column + 1).dwidth_cont)) {
            cf->eraseInRow(row, column, 1, attrs);
        } else if (cell.dwidth_cont && (column == 0 || !frame.getCell(row, column - 1).dwidth)) {
            cf->eraseInRow(row, column, 1, attrs);
        }
    }
}

void VtermImpl::rectangleOrigin(u16& rowBase, u16& columnBase, u16& rowLimit, u16& columnLimit) const {
    if (originMode == OriginMode::ScrollingRegion) {
        rowBase = marginTop;
        columnBase = hMargin;
        rowLimit = marginBottom;
        columnLimit = nColsEff;
    } else {
        rowBase = 0;
        columnBase = 0;
        rowLimit = nRows;
        columnLimit = nCols;
    }
}

bool VtermImpl::rectangleFromParams(size_t offset, Rectangle& rectangle) const {
    u16 rowBase, columnBase, rowLimit, columnLimit;
    rectangleOrigin(rowBase, columnBase, rowLimit, columnLimit);
    const u32 rows = rowLimit - rowBase;
    const u32 columns = columnLimit - columnBase;
    const u32 rawTop = inputOps[offset] ? inputOps[offset] : 1;
    const u32 rawLeft = inputOps[offset + 1] ? inputOps[offset + 1] : 1;
    const u32 rawBottom = inputOps[offset + 2] ? inputOps[offset + 2] : rows;
    const u32 rawRight = inputOps[offset + 3] ? inputOps[offset + 3] : columns;
    if (rawTop > rawBottom || rawLeft > rawRight) {
        return false;
    }

    rectangle.top = rowBase + std::min(rawTop, rows) - 1;
    rectangle.left = columnBase + std::min(rawLeft, columns) - 1;
    rectangle.bottom = rowBase + std::min(rawBottom, rows);
    rectangle.right = columnBase + std::min(rawRight, columns);
    return true;
}

void VtermImpl::inputGraphicChar(unsigned char ch) {
    if ((ch & 0x80) == 0) {
        utf8dec.checkPrematureEOS();

        Charset cs;
        if (charsetState.ss) {
            cs = charsetState.g[charsetState.ss];
            charsetState.ss = 0;
        } else {
            cs = charsetState.g[charsetState.gl];
        }

        if (cs == Charset::UTF8) {
            utf8dec.onUnicode(ch < 127 ? ch : 0);
        } else if (ch >= 32 && (cs == Charset::IsoLatin1 || ch < 127)) {
            utf8dec.onUnicode(translateCharset(cs, ch));
        }
    } else {
        Charset cs = charsetState.g[charsetState.gr];
        if (cs == Charset::UTF8) {
            utf8dec.pushByte(ch);
        } else if (ch >= 160 && (cs == Charset::IsoLatin1 || ch < 255)) {
            // ISO 2022 invokes a 94/96-character G-set in GR by moving its
            // 7-bit code positions into the right half.  Strip that bit and
            // use the same translation path as GL; NRC sets are deliberately
            // outside charCodes, so indexing the table directly was both
            // incorrect and out of bounds for LS1R/LS2R/LS3R.
            utf8dec.onUnicode(translateCharset(cs, ch & 0x7f));
        }
    }
}

void VtermImpl::resetGraphemeInput() {
    inputGrapheme.clear();
    inputGraphemeBase = 0;
    inputGraphemeBreaker.reset();
    inputGraphemeFrame = nullptr;
}

void VtermImpl::placeGraphicChar() {
    auto pt = utf8dec.getUnicode();
    auto w = pt >= 0x20 && pt < 0x7f ? 1 : wcwidth(pt);

    if (inputGraphemeFrame != cf) {
        inputGraphemeBreaker.reset();
    }
    const bool graphemeBoundary = inputGraphemeBreaker.breakBefore(pt);
    if (inputGraphemeFrame == cf && !graphemeBoundary) {
        if (inputGrapheme.empty()) {
            inputGrapheme.push_back(inputGraphemeBase);
        }
        inputGrapheme.push_back(pt);
        auto& cell = cf->getCell(inputGraphemeY, inputGraphemeX);
        cell.grapheme = cf->internGrapheme(inputGrapheme.data(), inputGrapheme.size());
        cf->expose();
        return;
    }

    if (w < 0) {
        w = 1;
        pt = Unicode_Replacement_Character;
    }

    const u8 lineAttribute = static_cast<const Frame&>(*cf).getCell(posY, 0).line_attr;
    const u16 lineCols = lineAttribute
        ? hMargin + std::max<u16>(1, (nColsEff - hMargin) / 2)
        : nColsEff;
    if (posX >= lineCols) {
        posX = lineCols - 1;
        lastCol = false;
    }
    bool changedRow = false;
    if (autoWrapMode && lastCol) {
        cf->getCell(posY, posX).wrap = 1;
        inp_CR();
        inp_LF();
        changedRow = true;
    }

    if (w == 2 && posX == lineCols - 1 && autoWrapMode) {
        // The wide glyph belongs wholly to the next row.  Mark the last
        // occupied cell as the soft-wrap boundary, not the unused final
        // column: otherwise copying the logical line invents a space.
        const u16 wrapColumn = posX > hMargin ? posX - 1 : posX;
        cf->getCell(posY, wrapColumn).wrap = 1;
        inp_CR();
        inp_LF();
        changedRow = true;
    }

    if (insertMode) {
        nInputOps = 1;
        inputOps[0] = 1;
        csi_ICH();
    }

    if (w == 0) {
        w = 1;
    }

    const u16 clusterX = posX;
    const u16 clusterY = posY;
    auto& c = prepareCellAt(posY, posX);
    c = attrs;
    c.uc_pt = pt;
    c.hyperlink = activeHyperlink;
    c.semantic = currentSemantic;
    c.line_attr = changedRow ? static_cast<const Frame&>(*cf).getCell(posY, 0).line_attr : lineAttribute;
    if (c.blink) {
        haveBlinkingText = true;
    }

    inputGrapheme.clear();
    inputGraphemeBase = pt;
    inputGraphemeFrame = cf;
    inputGraphemeX = clusterX;
    inputGraphemeY = clusterY;

    if (w == 2 && posX < lineCols - 1) {
        c.dwidth = 1;
        auto& continuation = cf->getCell(posY, ++posX);
        continuation = attrs;
        continuation.dwidth_cont = 1;
        continuation.hyperlink = activeHyperlink;
    }

    if (posX == lineCols - 1) {
        lastCol = true;
    } else {
        ++posX;
    }
}

void VtermImpl::placeAsciiRun(const u8* input, size_t size) {
    if (size == 0) {
        return;
    }

    utf8dec.setUnicode(*input++);
    placeGraphicChar();
    --size;

    while (size > 0) {
        if (insertMode) {
            utf8dec.setUnicode(*input++);
            placeGraphicChar();
            --size;
            continue;
        }
        if (autoWrapMode && lastCol) {
            cf->getCell(posY, posX).wrap = 1;
            inp_CR();
            inp_LF();
        }

        const Frame& frame = *cf;
        const u8 lineAttribute = frame.getCell(posY, 0).line_attr;
        const u16 lineCols = lineAttribute
            ? hMargin + std::max<u16>(1, (nColsEff - hMargin) / 2)
            : nColsEff;
        if (posX >= lineCols) {
            utf8dec.setUnicode(*input++);
            placeGraphicChar();
            --size;
            continue;
        }
        const u16 count = std::min<size_t>(size, lineCols - posX);
        const u16 startX = posX;
        const u16 endX = startX + count;
        if (frame.getCell(posY, startX).dwidth_cont && startX > 0) {
            cf->eraseInRow(posY, startX - 1, 1, attrs);
        }
        if (frame.getCell(posY, endX - 1).dwidth && endX < nCols) {
            cf->eraseInRow(posY, endX, 1, attrs);
        }

        TerminalCell* cells = cf->writeSpan(posY, startX, count);
        for (u16 index = 0; index < count; ++index) {
            auto& cell = cells[index];
            cell = attrs;
            cell.uc_pt = input[index];
            cell.hyperlink = activeHyperlink;
            cell.semantic = currentSemantic;
            cell.line_attr = lineAttribute;
        }
        if (attrs.blink) {
            haveBlinkingText = true;
        }

        const u16 clusterX = endX - 1;
        const u32 codepoint = input[count - 1];
        inputGrapheme.clear();
        inputGraphemeBase = codepoint;
        inputGraphemeFrame = cf;
        inputGraphemeX = clusterX;
        inputGraphemeY = posY;
        inputGraphemeBreaker.setBoundaryAfter(codepoint);
        utf8dec.setUnicode(codepoint);

        if (endX == lineCols) {
            posX = lineCols - 1;
            lastCol = true;
        } else {
            posX = endX;
            lastCol = false;
        }
        input += count;
        size -= count;
    }
}

void VtermImpl::inp_LF() {
    TRACE_FUN;
    if (esc_IND()) {
        cf->eraseInRow(posY, posX, nColsEff - posX, attrs);
    }
}

void VtermImpl::inp_CR() {
    TRACE_FUN;
    if (originMode == OriginMode::Absolute && posX < hMargin) {
        posX = 0;
    } else {
        posX = hMargin;
    }
    lastCol = false;
}

void VtermImpl::jumpToNextTabStop() {
    const bool insideMargins = isCursorInsideMargins();
    const u16 left = insideMargins ? hMargin : 0;
    const u16 right = insideMargins ? nColsEff : nCols;
    if (!tabStopsCustomized) {
        do {
            posX = ((posX / 8) + 1) * 8;
        } while (posX < left);
        posX = std::min<int>(posX, right - 1);
    } else {
        auto ts = std::upper_bound(tabStops.begin(), tabStops.end(), posX);
        posX = ts == tabStops.end() || *ts >= right ? right - 1 : *ts;
    }
    lastCol = false;
}

void VtermImpl::inp_HT() {
    TRACE_FUN;
    jumpToNextTabStop();
}

void VtermImpl::showCursor() {
    TRACE_FUN;
    if (showCursorMode && inputState == InputState::Normal) {
        cf->setCursorPos(posY, posX);
        using CS = TerminalCursor::Style;
        cf->setCursorStyle(hasFocus ? cursorShape : CS::hollow_block);
    }
}

void VtermImpl::hideCursor() {
    TRACE_FUN;
    using CS = TerminalCursor::Style;
    cf->setCursorStyle(CS::hidden);
}

void VtermImpl::esc_DCS(unsigned char fin) {
    TRACE_FUN;

#ifdef DEBUG
    logT << "Designate Character Set: destination '" << scsDst << "', charset '";
    if (scsMod) {
        vlog << scsMod;
    }
    vlog << fin << "'" << std::endl;
#endif

    u8 ix = 0;
    bool cs96 = false;
    switch (scsDst) {
        case '(':
            ix = 0;
            break;
        case ')':
            ix = 1;
            break;
        case '*':
            ix = 2;
            break;
        case '+':
            ix = 3;
            break;
        case '-':
            ix = 1;
            cs96 = true;
            break;
        case '.':
            ix = 2;
            cs96 = true;
            break;
        case '/':
            ix = 3;
            cs96 = true;
            break;
    }

    Charset cs = Charset::UTF8;
    switch (fin) {
        case 'A':
            cs = cs96 ? Charset::IsoLatin1 : Charset::IsoUK;
            break;
        case 'B':
            cs = Charset::UTF8;
            break;
        case '0':
            cs = Charset::DecSpec;
            break;
        case '5':
            if (scsMod == '%') {
                cs = Charset::DecSuppl;
            } else if (scsMod == '&') {
                cs = Charset::NrcRussian;
            } else {
                cs = Charset::NrcFinnish;
            }
            break;
        case '<':
            cs = Charset::DecUserPref;
            break;
        case '>':
            cs = scsMod == '"' ? Charset::NrcGreek : Charset::DecTechn;
            break;
        case '4':
            cs = Charset::NrcDutch;
            break;
        case 'C':
            cs = Charset::NrcFinnish;
            break;
        case 'R':
        case 'f':
            cs = Charset::NrcFrench;
            break;
        case '9':
        case 'Q':
            cs = Charset::NrcFrenchCanadian;
            break;
        case 'K':
            cs = Charset::NrcGerman;
            break;
        case 'Y':
            cs = Charset::NrcItalian;
            break;
        case '`':
        case 'E':
        case '6':
            cs = scsMod == '%' ? Charset::NrcPortuguese : Charset::NrcNorwegianDanish;
            break;
        case 'Z':
            cs = Charset::NrcSpanish;
            break;
        case '7':
        case 'H':
            cs = Charset::NrcSwedish;
            break;
        case '=':
            cs = scsMod == '%' ? Charset::NrcHebrew : Charset::NrcSwiss;
            break;
        case '3':
            if (scsMod == '%') {
                cs = Charset::NrcSerboCroatian;
            }
            break;
        case '2':
            if (scsMod == '%') {
                cs = Charset::NrcTurkish;
            }
            break;
    }

    charsetState.g[ix] = cs;
    setState(InputState::Normal);
}

bool VtermImpl::esc_IND() {
    TRACE_FUN;
    const bool scrolled = performIndex();
    setState(InputState::Normal);
    return scrolled;
}

bool VtermImpl::performIndex() {
    if (autoPrintMode) {
        printLine(posY);
    }
    bool scrolled = false;
    if (posY == marginBottom - 1) {
        scrollRegionUp(1);
        scrolled = true;
    } else if (posY < nRows - 1) {
        ++posY;
        lastCol = false;
    }
    return scrolled;
}

std::string VtermImpl::printableLine(u16 row) const {
    std::vector<u32> codepoints;
    for (u16 column = 0; column < nCols; ++column) {
        const auto& cell = cf->getCell(row, column);
        if (cell.dwidth_cont) {
            continue;
        }
        const auto& grapheme = cf->getGrapheme(cell.grapheme);
        if (grapheme.empty()) {
            codepoints.push_back(cell.uc_pt);
        } else {
            codepoints.insert(codepoints.end(), grapheme.begin(), grapheme.end());
        }
    }
    while (!codepoints.empty() && codepoints.back() == ' ') {
        codepoints.pop_back();
    }
    std::string result;
    const auto sink = [&result](char ch) {
        result.push_back(ch);
    };
    for (const u32 codepoint : codepoints) {
        Utf8Encoder::pushUnicode(codepoint, sink);
    }
    result.push_back('\n');
    return result;
}

void VtermImpl::printLine(u16 row) {
    host.print(printableLine(std::min<u16>(row, nRows - 1)));
}

void VtermImpl::csi_MC(bool privateMode) {
    TRACE_FUN;
    const u32 operation = inputOps[0];
    if (privateMode) {
        if (operation == 1) {
            printLine(posY);
        } else if (operation == 4) {
            autoPrintMode = false;
        } else if (operation == 5) {
            autoPrintMode = true;
        }
    } else {
        if (operation == 0) {
            std::string screen;
            for (u16 row = 0; row < nRows; ++row) {
                screen += printableLine(row);
            }
            host.print(screen);
        } else if (operation == 4) {
            printerControllerMode = false;
            printerControllerState = PrinterControllerState::Normal;
        } else if (operation == 5) {
            printerControllerMode = true;
            printerControllerState = PrinterControllerState::Normal;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECLL() {
    TRACE_FUN;
    for (size_t index = 0; index < nInputOps; ++index) {
        const u32 operation = inputOps[index];
        if (operation == 0) {
            ledState = 0;
        } else if (operation >= 1 && operation <= 3) {
            ledState |= (u8)(1u << (operation - 1));
        } else if (operation >= 21 && operation <= 23) {
            ledState &= (u8)(~(1u << (operation - 21)));
        }
    }
    host.leds(ledState);
    setState(InputState::Normal);
}

size_t VtermImpl::consumePrinterController(const u8* input, size_t size) {
    const bool handlesPrinter = host.handlesPrinter();
    std::string output;
    if (handlesPrinter) {
        output.reserve(size);
    }

    const auto beginPrefix = [&](u8 ch) {
        if (ch == 0x1b) {
            printerControllerState = PrinterControllerState::Escape;
        } else if (ch == 0x9b) {
            printerControllerState = PrinterControllerState::Csi;
        } else {
            printerControllerState = PrinterControllerState::Normal;
            if (handlesPrinter) {
                output.push_back((char)(ch));
            }
        }
    };
    const auto appendPrefix = [&](const char* prefix, size_t prefixSize) {
        if (handlesPrinter) {
            output.append(prefix, prefixSize);
        }
    };

    size_t consumed = 0;
    while (consumed < size) {
        if (printerControllerState == PrinterControllerState::Normal) {
            const u8* const begin = input + consumed;
            const size_t remaining = size - consumed;
            const u8* const escape = static_cast<const u8*>(memchr(begin, 0x1b, remaining));
            const size_t prefixSize = escape == nullptr ? remaining : escape - begin;
            const u8* const csi = static_cast<const u8*>(memchr(begin, 0x9b, prefixSize));
            const u8* const next = csi == nullptr ? escape : csi;
            if (next == nullptr) {
                if (handlesPrinter) {
                    output.append((const char*)(begin), remaining);
                }
                consumed = size;
                break;
            }
            if (handlesPrinter) {
                output.append((const char*)(begin), next - begin);
            }
            consumed += next - begin + 1;
            beginPrefix(*next);
            continue;
        }

        const u8 ch = input[consumed++];
        switch (printerControllerState) {
            case PrinterControllerState::Escape:
                if (ch == '[') {
                    printerControllerState = PrinterControllerState::EscapeBracket;
                } else {
                    appendPrefix("\x1b", 1);
                    beginPrefix(ch);
                }
                break;
            case PrinterControllerState::EscapeBracket:
                if (ch == '4') {
                    printerControllerState = PrinterControllerState::EscapeBracket4;
                } else {
                    appendPrefix("\x1b[", 2);
                    beginPrefix(ch);
                }
                break;
            case PrinterControllerState::EscapeBracket4:
                if (ch == 'i') {
                    printerControllerState = PrinterControllerState::Normal;
                    printerControllerMode = false;
                } else {
                    appendPrefix("\x1b[4", 3);
                    beginPrefix(ch);
                }
                break;
            case PrinterControllerState::Csi:
                if (ch == '4') {
                    printerControllerState = PrinterControllerState::Csi4;
                } else {
                    appendPrefix("\x9b", 1);
                    beginPrefix(ch);
                }
                break;
            case PrinterControllerState::Csi4:
                if (ch == 'i') {
                    printerControllerState = PrinterControllerState::Normal;
                    printerControllerMode = false;
                } else {
                    appendPrefix(
                        "\x9b"
                        "4",
                        2
                    );
                    beginPrefix(ch);
                }
                break;
            case PrinterControllerState::Normal:
                break;
        }
        if (!printerControllerMode) {
            break;
        }
    }

    if (!output.empty()) {
        host.print(output);
    }
    return consumed;
}

void VtermImpl::esc_RI() {
    TRACE_FUN;
    if (posY == marginTop) {
        nInputOps = 1;
        inputOps[0] = 1;
        csi_SD();
    } else if (posY > 0) {
        --posY;
        lastCol = false;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_ecma48_SL() {
    TRACE_FUN;
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1u;
        arg = std::min<u32>(arg, nColsEff - hMargin);
        deleteCols(hMargin, (u16)(arg));
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_ecma48_SR() {
    TRACE_FUN;
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1u;
        arg = std::min<u32>(arg, nColsEff - hMargin);
        insertCols(hMargin, (u16)(arg));
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECSCUSR() {
    TRACE_FUN;
    using CS = TerminalCursor::Style;
    switch (inputOps[0]) {
        case 0:
        case 1:
        case 2:
            cursorStyleParam = inputOps[0] == 0 ? 1 : inputOps[0];
            cursorShape = CS::filled_block;
            break;
        case 3:
        case 4:
            cursorStyleParam = inputOps[0];
            cursorShape = CS::underline;
            break;
        case 5:
        case 6:
            cursorStyleParam = inputOps[0];
            cursorShape = CS::bar;
            break;
        default:
            logT << "DECSCUSR with illegal param: " << inputOps[0] << std::endl;
            break;
    }
    cursorBlinkMode = (cursorStyleParam & 1) != 0;
    blinkVisible = true;
    nextBlink = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    frame_pri.setBlinkState(true, cursorBlinkMode);
    frame_alt.setBlinkState(true, cursorBlinkMode);
    setState(InputState::Normal);
}

void VtermImpl::csi_DECIC() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1u;
    if (isCursorInsideMargins()) {
        arg = std::min<u32>(arg, nColsEff - posX);
        insertCols(posX, (u16)(arg));
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECDC() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1u;
    if (isCursorInsideMargins()) {
        arg = std::min<u32>(arg, nColsEff - posX);
        deleteCols(posX, (u16)(arg));
    }
    setState(InputState::Normal);
}

void VtermImpl::esc_FI() {
    TRACE_FUN;
    nInputOps = 1;
    inputOps[0] = 1;
    if (posX < nColsEff - 1) {
        csi_CUF();
    } else {
        csi_ecma48_SL();
    }
    setState(InputState::Normal);
}

void VtermImpl::esc_BI() {
    TRACE_FUN;
    nInputOps = 1;
    inputOps[0] = 1;
    if (posX > hMargin) {
        csi_CUB();
    } else {
        csi_ecma48_SR();
    }
    setState(InputState::Normal);
}

void VtermImpl::esc_NEL() {
    TRACE_FUN;
    esc_IND();
    inp_CR();
    setState(InputState::Normal);
}

void VtermImpl::esc_HTS() {
    TRACE_FUN;
    if (!tabStopsCustomized) {
        for (unsigned column = 8; column < nCols; column += 8) {
            tabStops.push_back((u16)(column));
        }
        tabStopsCustomized = true;
    }
    if (std::find(tabStops.begin(), tabStops.end(), posX) == tabStops.end()) {
        tabStops.push_back(posX);
        std::sort(tabStops.begin(), tabStops.end());
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_SCOSC_SLRM() {
    if (horizMarginMode) {
        csi_SLRM();
    } else {
        csi_SCOSC();
    }
}

void VtermImpl::csi_SCOSC() {
    TRACE_FUN;
    savedCursor_SCO.posX = posX;
    savedCursor_SCO.posY = posY;
    savedCursor_SCO.isSet = true;
    setState(InputState::Normal);
}

void VtermImpl::csi_SCORC() {
    TRACE_FUN;
    if (!savedCursor_SCO.isSet) {
        logT << "Asked to restore cursor (SCORC) but it has not been saved." << std::endl;
    } else {
        posX = savedCursor_SCO.posX;
        posY = savedCursor_SCO.posY;
        normalizeCursorPos();
    }
    setState(InputState::Normal);
}

void VtermImpl::esc_DECSC() {
    TRACE_FUN;
    savedCursor_DEC->posX = posX;
    savedCursor_DEC->posY = posY;
    savedCursor_DEC->lastCol = lastCol;
    savedCursor_DEC->attrs = attrs;
    savedCursor_DEC->originMode = originMode;
    savedCursor_DEC->charsetState = charsetState;
    savedCursor_DEC->isSet = true;
    setState(InputState::Normal);
}

void VtermImpl::esc_DECRC() {
    TRACE_FUN;
    if (!savedCursor_DEC->isSet) {
        logT << "Asked to restore cursor (DECRC) but it has not been saved." << std::endl;
    } else {
        posX = savedCursor_DEC->posX;
        posY = savedCursor_DEC->posY;
        normalizeCursorPos();
        lastCol = savedCursor_DEC->lastCol;
        attrs = savedCursor_DEC->attrs;
        reverseVideo = attrs.inverse;
        fg = &attrs.fg;
        bg = &attrs.bg;
        originMode = savedCursor_DEC->originMode;
        charsetState = savedCursor_DEC->charsetState;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_CUU() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u16 top = posY >= marginTop ? marginTop : 0;
    arg = std::min<u32>(arg, posY - top);
    posY -= arg;
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_CUD() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u16 bottom = posY < marginBottom ? marginBottom : nRows;
    arg = std::min<u32>(arg, bottom - posY - 1);
    posY += arg;
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_CUF() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const bool insideMargins = posX >= hMargin && posX < nColsEff;
    const u16 right = insideMargins ? nColsEff : nCols;
    arg = std::min<u32>(arg, right - posX - 1);
    posX += arg;
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_CUB() {
    TRACE_FUN;
    moveCursorBackward(inputOps[0] ? inputOps[0] : 1);
    setState(InputState::Normal);
}

void VtermImpl::moveCursorBackward(u32 count) {
    const bool insideMargins = posX >= hMargin && posX < nColsEff;
    if (posX == nColsEff) {
        count = std::min<u32>(count == UINT32_MAX ? count : count + 1, posX);
    }
    while (count > 0) {
        const u16 leftEdge = insideMargins ? hMargin : 0;
        const u16 left = posX >= leftEdge ? posX - leftEdge : posX;
        if (left > 0) {
            const u32 step = std::min<u32>(count, left);
            posX -= step;
            count -= step;
            continue;
        }
        if (!reverseWrapMode || posY == 0 || (!extendedReverseWrapMode && !cf->getCell(posY - 1, nColsEff - 1).wrap)) {
            break;
        }
        --posY;
        posX = insideMargins ? nColsEff : nCols;
    }
    lastCol = false;
}

void VtermImpl::csi_CNL() {
    TRACE_FUN;
    csi_CUD();
    inp_CR();
    setState(InputState::Normal);
}

void VtermImpl::csi_CPL() {
    TRACE_FUN;
    csi_CUU();
    inp_CR();
    setState(InputState::Normal);
}

void VtermImpl::csi_CHA() {
    TRACE_FUN;
    u32 col = inputOps[0] ? inputOps[0] : 1;
    col = std::max<u32>(1, std::min<u32>(col, nCols));
    posX = col - 1;
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_HPA() {
    TRACE_FUN;
    csi_CHA();
    setState(InputState::Normal);
}

void VtermImpl::csi_HPR() {
    TRACE_FUN;
    const u32 arg = inputOps[0] ? inputOps[0] : 1;
    inputOps[0] = std::min<u64>((u64)(posX) + arg + 1, UINT32_MAX);
    csi_CHA();
    setState(InputState::Normal);
}

void VtermImpl::csi_VPA() {
    TRACE_FUN;
    u32 row = inputOps[0] ? inputOps[0] : 1;
    row = std::max<u32>(1, std::min<u32>(row, nRows));
    posY = row - 1;
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_VPR() {
    TRACE_FUN;
    const u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u32 row = std::min<u64>((u64)(posY) + arg + 1, nRows);
    posY = row - 1;
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_CUP() {
    TRACE_FUN;
    u32 row = inputOps[0] ? inputOps[0] : 1;
    u32 col = (nInputOps > 1 && inputOps[1]) ? inputOps[1] : 1;
    switch (originMode) {
        case OriginMode::Absolute:
            row = std::max<u32>(1, std::min<u32>(row, nRows)) - 1;
            col = std::max<u32>(1, std::min<u32>(col, nCols)) - 1;
            break;
        case OriginMode::ScrollingRegion:
            row = marginTop + std::max<u32>(1, std::min<u32>(row, marginBottom - marginTop)) - 1;
            col = hMargin + std::max<u32>(1, std::min<u32>(col, nColsEff - hMargin)) - 1;
            break;
    }

    posX = col;
    posY = row;
    lastCol = false;
    setState(InputState::Normal);
    logT << "Cursor positioned to (" << posY << "," << posX << ")" << std::endl;
}

void VtermImpl::csi_SU() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    arg = std::min<u32>(arg, marginBottom - marginTop);
    const bool pendingWrap = lastCol;
    scrollRegionUp((u16)(arg));
    lastCol = pendingWrap;
    setState(InputState::Normal);
}

void VtermImpl::scrollRegionUp(u16 count) {
    if (horizMarginMode) {
        deleteRows(marginTop, count);
    } else {
        cf->scrollUp(marginTop, marginBottom, count);
        eraseRows(marginBottom - count, count);
        lastCol = false;
    }
}

void VtermImpl::csi_SD() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    arg = std::min<u32>(arg, marginBottom - marginTop);
    const bool pendingWrap = lastCol;
    if (horizMarginMode) {
        insertRows(marginTop, (u16)(arg));
    } else {
        cf->scrollDown(marginTop, marginBottom, (u16)(arg));
        eraseRows(marginTop, (u16)(arg));
        lastCol = false;
    }
    lastCol = pendingWrap;
    setState(InputState::Normal);
}

void VtermImpl::csi_CHT() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    arg = std::min<u32>(arg, nCols);
    if (arg == 1) {
        inp_HT();
    } else {
        for (int k = 0; k < arg; ++k) {
            jumpToNextTabStop();
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_CBT() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    arg = std::min<u32>(arg, nCols);
    for (u32 k = 0; k < arg; ++k) {
        const u16 left = isCursorInsideMargins() ? hMargin : 0;
        if (!tabStopsCustomized) {
            if (posX > 0 && posX % 8 == 0) {
                posX -= 8;
            } else {
                posX = (posX / 8) * 8;
            }
            posX = std::max(posX, left);
        } else {
            auto ts = std::lower_bound(tabStops.begin(), tabStops.end(), posX);
            if (ts == tabStops.begin() || *(--ts) < left) {
                posX = left;
            } else {
                posX = *ts;
            }
        }
        lastCol = false;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_REP() {
    TRACE_FUN;
    if (!utf8dec.getUnicode()) {
        setState(InputState::Normal);
        return;
    }
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u64 observableCells = ((u64)(opts.saveLines) + nRows + 1) * nCols;
    if (arg > observableCells) {
        arg = (u32)(observableCells + (arg - observableCells) % nCols);
    }
    for (u32 k = 0; k < arg; ++k) {
        placeGraphicChar();
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_ED() {
    TRACE_FUN;
    normalizeCursorPos();
    switch (inputOps[0]) {
        case 0:
            cf->eraseInRow(posY, posX, nCols - posX, attrs);
            for (u16 pY = posY + 1; pY < nRows; ++pY) {
                eraseRow(pY);
            }
            break;
        case 1:
            for (u16 pY = 0; pY < posY; ++pY) {
                eraseRow(pY);
            }
            cf->eraseInRow(posY, 0, posX + 1, attrs);
            break;
        case 3:
            cf->dropScrollbackHistory();
            break;

        case 2:
            for (u16 pY = 0; pY < nRows; ++pY) {
                eraseRow(pY);
            }
            break;
        default:
            logT << "Erase in Display with illegal param: " << inputOps[0] << std::endl;
            break;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_EL() {
    TRACE_FUN;
    normalizeCursorPos();
    switch (inputOps[0]) {
        case 0:
            cf->eraseInRow(posY, posX, nCols - posX, attrs);
            break;
        case 1:
            cf->eraseInRow(posY, 0, posX + 1, attrs);
            break;
        case 2:
            cf->eraseInRow(posY, 0, nCols, attrs);
            break;
        default:
            logT << "Erase in Line with illegal param: " << inputOps[0] << std::endl;
            break;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECSED() {
    TRACE_FUN;
    normalizeCursorPos();
    if (inputOps[0] == 0 || inputOps[0] == 2) {
        const u16 firstRow = inputOps[0] == 2 ? 0 : posY;
        const u16 lastRow = nRows - 1;
        for (u16 row = firstRow; row <= lastRow; ++row) {
            const u16 first = row == posY && inputOps[0] == 0 ? posX : 0;
            cf->selectiveEraseInRow(row, first, nCols - first, attrs);
        }
    } else if (inputOps[0] == 1) {
        for (u16 row = 0; row <= posY; ++row) {
            const u16 count = row == posY ? posX + 1 : nCols;
            cf->selectiveEraseInRow(row, 0, count, attrs);
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECSEL() {
    TRACE_FUN;
    normalizeCursorPos();
    if (inputOps[0] == 0) {
        cf->selectiveEraseInRow(posY, posX, nCols - posX, attrs);
    } else if (inputOps[0] == 1) {
        cf->selectiveEraseInRow(posY, 0, posX + 1, attrs);
    } else if (inputOps[0] == 2) {
        cf->selectiveEraseInRow(posY, 0, nCols, attrs);
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECSCA() {
    TRACE_FUN;
    attrs.protected_char = inputOps[0] == 1;
    setState(InputState::Normal);
}

void VtermImpl::csi_DECFRA() {
    TRACE_FUN;
    if (nInputOps >= 5 && inputOps[0] >= 32 && inputOps[0] <= 0x10ffff) {
        Rectangle rectangle;
        if (!rectangleFromParams(1, rectangle)) {
            setState(InputState::Normal);
            return;
        }
        for (u16 y = rectangle.top; y < rectangle.bottom; ++y) {
            for (u16 x = rectangle.left; x < rectangle.right; ++x) {
                auto& cell = cf->getCell(y, x);
                const u8 lineAttribute = cell.line_attr;
                cell = attrs;
                cell.line_attr = lineAttribute;
                cell.uc_pt = inputOps[0];
                cell.dirty = 1;
            }
            normalizeWideCells(y);
        }
        cf->expose();
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECERA(bool selective) {
    TRACE_FUN;
    if (nInputOps >= 4) {
        Rectangle rectangle;
        if (!rectangleFromParams(0, rectangle)) {
            setState(InputState::Normal);
            return;
        }
        for (u16 y = rectangle.top; y < rectangle.bottom; ++y) {
            if (selective) {
                cf->selectiveEraseInRow(y, rectangle.left, rectangle.right - rectangle.left, attrs);
            } else {
                cf->eraseInRow(y, rectangle.left, rectangle.right - rectangle.left, attrs);
            }
            normalizeWideCells(y);
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECCRA() {
    TRACE_FUN;
    if (nInputOps >= 8) {
        Rectangle source;
        if (!rectangleFromParams(0, source)) {
            setState(InputState::Normal);
            return;
        }
        u16 rowBase, columnBase, rowLimit, columnLimit;
        rectangleOrigin(rowBase, columnBase, rowLimit, columnLimit);
        const u32 targetRow = inputOps[5] ? inputOps[5] : 1;
        const u32 targetColumn = inputOps[6] ? inputOps[6] : 1;
        const u16 targetTop = rowBase + std::min<u32>(targetRow, rowLimit - rowBase) - 1;
        const u16 targetLeft = columnBase + std::min<u32>(targetColumn, columnLimit - columnBase) - 1;
        const u16 height = std::min<u16>(source.bottom - source.top, rowLimit - targetTop);
        const u16 width = std::min<u16>(source.right - source.left, columnLimit - targetLeft);
        std::vector<TerminalCell> copied;
        copied.reserve(height * width);
        for (u16 y = 0; y < height; ++y) {
            for (u16 x = 0; x < width; ++x) {
                copied.push_back(cf->getCell(source.top + y, source.left + x));
            }
        }
        for (u16 y = 0; y < height; ++y) {
            for (u16 x = 0; x < width; ++x) {
                auto& cell = cf->getCell(targetTop + y, targetLeft + x);
                const u8 lineAttribute = cell.line_attr;
                cell = copied[y * width + x];
                cell.line_attr = lineAttribute;
                cell.dirty = 1;
            }
            normalizeWideCells(targetTop + y);
        }
        cf->expose();
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECCARA(bool reverse) {
    TRACE_FUN;
    if (nInputOps >= 5) {
        Rectangle rectangle;
        if (!rectangleFromParams(0, rectangle)) {
            setState(InputState::Normal);
            return;
        }
        const auto apply = [reverse](TerminalCell& cell, u32 mode) {
            switch (mode) {
                case 0:
                    cell.bold = reverse ? !cell.bold : false;
                    cell.underline = reverse ? !cell.underline : false;
                    cell.blink = reverse ? !cell.blink : false;
                    cell.inverse = reverse ? !cell.inverse : false;
                    break;
                case 1:
                    cell.bold = reverse ? !cell.bold : true;
                    break;
                case 4:
                    cell.underline = reverse ? !cell.underline : true;
                    break;
                case 5:
                    cell.blink = reverse ? !cell.blink : true;
                    break;
                case 7:
                    cell.inverse = reverse ? !cell.inverse : true;
                    break;
                case 8:
                    cell.conceal = reverse ? !cell.conceal : true;
                    break;
                case 22:
                    if (!reverse) {
                        cell.bold = 0;
                    }
                    break;
                case 24:
                    if (!reverse) {
                        cell.underline = 0;
                    }
                    break;
                case 25:
                    if (!reverse) {
                        cell.blink = 0;
                    }
                    break;
                case 27:
                    if (!reverse) {
                        cell.inverse = 0;
                    }
                    break;
                case 28:
                    if (!reverse) {
                        cell.conceal = 0;
                    }
                    break;
            }
        };
        for (u16 y = rectangle.top; y < rectangle.bottom; ++y) {
            for (u16 x = rectangle.left; x < rectangle.right; ++x) {
                auto& cell = cf->getCell(y, x);
                for (size_t k = 4; k < nInputOps; ++k) {
                    apply(cell, inputOps[k]);
                }
                cell.dirty = 1;
            }
        }
        cf->expose();
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECRQCRA() {
    TRACE_FUN;
    if (nInputOps >= 6) {
        Rectangle rectangle;
        if (!rectangleFromParams(2, rectangle)) {
            setState(InputState::Normal);
            return;
        }
        u16 checksum = 0;
        for (u16 y = rectangle.top; y < rectangle.bottom; ++y) {
            for (u16 x = rectangle.left; x < rectangle.right; ++x) {
                const auto& cell = cf->getCell(y, x);
                if (cell.uc_pt != ' ') {
                    checksum += cell.uc_pt & 0xff;
                }
            }
        }
        checksum = -checksum;
        std::ostringstream response;
        response << inputOps[0] << "!~" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << checksum;
        writeDcsResponse(response.str());
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_IL() {
    TRACE_FUN;
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1;
        arg = std::min<u32>(arg, marginBottom - posY);
        insertRows(posY, (u16)(arg));
        inp_CR();
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DL() {
    TRACE_FUN;
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1;
        arg = std::min<u32>(arg, marginBottom - posY);
        deleteRows(posY, (u16)(arg));
        inp_CR();
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_ICH() {
    TRACE_FUN;
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1;
        u32 len = nColsEff - posX;
        arg = std::min(arg, len);
        len -= arg;

        if (len > 0 && cf->getCell(posY, posX + arg + len - 1).wrap) {
            cf->getCell(posY, posX + arg + len - 1).wrap = 0;
            cf->getCell(posY, posX + len - 1).wrap = 1;
        }

        cf->moveInRow(posY, posX + arg, posX, len);
        cf->eraseInRow(posY, posX, arg, attrs);
        normalizeWideCells(posY);
    }
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_DCH() {
    TRACE_FUN;
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1;
        u32 len = nColsEff - posX;
        arg = std::min(arg, len);
        len -= arg;

        cf->moveInRow(posY, posX, posX + arg, len);
        cf->eraseInRow(posY, posX + len, arg, attrs);
        normalizeWideCells(posY);
    }
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_ECH() {
    TRACE_FUN;
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u16 right = isCursorInsideMargins() ? nColsEff : nCols;
    const u32 len = posX < right ? right - posX : 0;
    arg = std::min(arg, len);
    cf->eraseInRow(posY, posX, arg, attrs);
    normalizeWideCells(posY);
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_STBM() {
    TRACE_FUN;
    if (nInputOps <= 2) {
        u32 newMarginTop = inputOps[0] > 0 ? inputOps[0] - 1 : 0;
        u32 newMarginBottom = nInputOps < 2 || inputOps[1] == 0 ? nRows : inputOps[1];

        if (newMarginTop >= nRows || newMarginBottom > nRows || newMarginBottom <= newMarginTop + 1) {
            logT << "Illegal arguments to SetTopBottomMargins: top=" << inputOps[0] << ", bottom=" << inputOps[1] << std::endl;
        } else if (newMarginTop != marginTop || newMarginBottom != marginBottom) {
            marginTop = (u16)(newMarginTop);
            marginBottom = (u16)(newMarginBottom);
        }
    }

    if (originMode == OriginMode::Absolute) {
        posX = 0;
        posY = 0;
    } else {
        posX = hMargin;
        posY = marginTop;
    }
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_SLRM() {
    TRACE_FUN;
    if (nInputOps <= 2) {
        u32 newMarginLeft = inputOps[0] > 0 ? inputOps[0] - 1 : 0;
        u32 newMarginRight = nInputOps < 2 || inputOps[1] == 0 ? nCols : inputOps[1];

        if (newMarginLeft >= nCols || newMarginRight > nCols || newMarginRight <= newMarginLeft + 1) {
            logT << "Illegal arguments to SetLeftRightMargins: left=" << inputOps[0] << ", right=" << inputOps[1] << std::endl;
        } else if (newMarginLeft != hMargin || newMarginRight != nColsEff) {
            hMargin = (u16)(newMarginLeft);
            nColsEff = (u16)(newMarginRight);
        }
    }

    if (originMode == OriginMode::Absolute) {
        posX = 0;
        posY = 0;
    } else {
        posX = hMargin;
        posY = marginTop;
    }
    lastCol = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_TBC() {
    TRACE_FUN;
    switch (inputOps[0]) {
        case 0: {
            if (!tabStopsCustomized) {
                for (unsigned column = 8; column < nCols; column += 8) {
                    tabStops.push_back((u16)(column));
                }
                tabStopsCustomized = true;
            }
            auto it = std::find(tabStops.begin(), tabStops.end(), posX);
            if (it != tabStops.end()) {
                tabStops.erase(it);
            }
        } break;
        case 3:
            tabStops.clear();
            tabStopsCustomized = true;
            break;
        default:
            break;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_SM() {
    TRACE_FUN;
    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& arg = inputOps[k];

        switch (arg) {
            case 2:
                keyboardLocked = true;
                break;
            case 4:
                insertMode = true;
                break;
            case 12:
                localEcho = false;
                break;
            case 20:
                autoNewlineMode = true;
                break;
            default:
                logT << "Ignored bogus set mode " << arg << std::endl;
                break;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_RM() {
    TRACE_FUN;
    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& arg = inputOps[k];

        switch (arg) {
            case 2:
                keyboardLocked = false;
                break;
            case 4:
                insertMode = false;
                break;
            case 12:
                localEcho = true;
                break;
            case 20:
                autoNewlineMode = false;
                break;
            default:
                logT << "Ignored bogus reset mode " << arg << std::endl;
                break;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::setPrivMode(u32 arg, bool set) {
    if (set) {
        switch (arg) {
            case 1:
                cursorKeyMode = CursorKeyMode::Application;
                break;
            case 2:
                charsetState = CharsetState{};
                compatLevel = CompatibilityLevel::VT400;
                break;
            case 3:
                switchColMode(ColMode::C132);
                break;
            case 4:
                smoothScrollMode = true;
                logT << "DECSCLM: Set smooth scroll" << std::endl;
                break;
            case 5:
                screenReverseVideo = true;
                frame_pri.setScreenReverseVideo(true);
                frame_alt.setScreenReverseVideo(true);
                break;
            case 6:
                originMode = OriginMode::ScrollingRegion;
                posX = hMargin;
                posY = marginTop;
                lastCol = false;
                break;
            case 7:
                autoWrapMode = true;
                break;
            case 8:
                autoRepeatMode = true;
                logU << "DECARM: Set auto-repeat mode" << std::endl;
                break;
            case 42:
                nationalReplacementMode = true;
                break;
            case 45:
                reverseWrapMode = true;
                break;
            case 9:
                mouseTrk.setMode(MouseTrackingMode::X10_Compat);
                break;
            case 12:
                cursorBlinkMode = true;
                nextBlink = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                frame_pri.setBlinkState(blinkVisible, true);
                frame_alt.setBlinkState(blinkVisible, true);
                break;
            case 25:
                showCursorMode = true;
                break;
            case 47:
                switchScreenBufferMode(true);
                break;
            case 67:
                bkspSendsDel = false;
                break;
            case 69:
                horizMarginMode = true;
                hMargin = 0;
                nColsEff = nCols;
                break;
            case 1000:
                mouseTrk.setMode(MouseTrackingMode::VT200);
                break;
            case 1001:
                mouseTrk.setMode(MouseTrackingMode::VT200_Highlight);
                mouseHighlight.active = false;
                break;
            case 1002:
                mouseTrk.setMode(MouseTrackingMode::VT200_ButtonEvent);
                break;
            case 1003:
                mouseTrk.setMode(MouseTrackingMode::VT200_AnyEvent);
                break;
            case 1004:
                mouseTrk.focusEventMode = true;
                break;
            case 1005:
                mouseTrk.setEncoding(MouseTrackingEnc::UTF8);
                break;
            case 1006:
                mouseTrk.setEncoding(MouseTrackingEnc::SGR);
                break;
            case 1007:
                altScrollMode = true;
                break;
            case 1015:
                mouseTrk.setEncoding(MouseTrackingEnc::URXVT);
                break;
            case 1016:
                mouseTrk.setEncoding(MouseTrackingEnc::SGRPixels);
                break;
            case 1034:
                eightBitInput = true;
                break;
            case 1036:
            case 1039:
                altSendsEscape = true;
                break;
            case 1047:
                switchScreenBufferMode(true);
                break;
            case 1048:
                esc_DECSC();
                break;
            case 1049:
                esc_DECSC();
                switchScreenBufferMode(true, true);
                break;
            case 1045:
                extendedReverseWrapMode = true;
                break;
            case 2004:
                bracketedPasteMode = true;
                break;
            case 2026:
                synchronizedOutputMode = true;
                synchronizedOutputDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
                break;
            case 2031:
                colorSchemeUpdateMode = true;
                break;
            case 2048:
                inBandResizeMode = true;
                reportInBandResize();
                break;
            default:
                logU << "set priv mode " << arg << std::endl;
                break;
        }
    } else {
        switch (arg) {
            case 1:
                cursorKeyMode = CursorKeyMode::ANSI;
                break;
            case 2:
                charsetState = CharsetState{};
                compatLevel = CompatibilityLevel::VT52;
                break;
            case 3:
                switchColMode(ColMode::C80);
                break;
            case 4:
                smoothScrollMode = false;
                logT << "DECSCLM: Set jump scroll" << std::endl;
                break;
            case 5:
                screenReverseVideo = false;
                frame_pri.setScreenReverseVideo(false);
                frame_alt.setScreenReverseVideo(false);
                break;
            case 6:
                originMode = OriginMode::Absolute;
                posX = 0;
                posY = 0;
                lastCol = false;
                break;
            case 7:
                autoWrapMode = false;
                break;
            case 8:
                autoRepeatMode = false;
                logU << "DECARM: Reset auto-repeat mode" << std::endl;
                break;
            case 42:
                nationalReplacementMode = false;
                break;
            case 45:
                reverseWrapMode = false;
                break;
            case 9:
            case 1000:
            case 1001:
            case 1002:
            case 1003:
                mouseTrk.setMode(MouseTrackingMode::Disabled);
                mouseHighlight.active = false;
                break;
            case 12:
                cursorBlinkMode = false;
                blinkVisible = true;
                frame_pri.setBlinkState(true, false);
                frame_alt.setBlinkState(true, false);
                break;
            case 25:
                showCursorMode = false;
                break;
            case 47:
                switchScreenBufferMode(false);
                break;
            case 67:
                bkspSendsDel = true;
                break;
            case 69:
                horizMarginMode = false;
                hMargin = 0;
                nColsEff = nCols;
                break;
            case 1004:
                mouseTrk.focusEventMode = false;
                break;
            case 1005:
            case 1006:
            case 1015:
            case 1016:
                mouseTrk.setEncoding(MouseTrackingEnc::Default);
                break;
            case 1034:
                eightBitInput = false;
                break;
            case 1007:
                altScrollMode = false;
                break;
            case 1036:
            case 1039:
                altSendsEscape = false;
                break;
            case 1047:
                switchScreenBufferMode(false, true);
                break;
            case 1048:
                esc_DECRC();
                break;
            case 1049:
                switchScreenBufferMode(false, true);
                esc_DECRC();
                break;
            case 1045:
                extendedReverseWrapMode = false;
                break;
            case 2004:
                bracketedPasteMode = false;
                break;
            case 2026:
                synchronizedOutputMode = false;
                break;
            case 2031:
                colorSchemeUpdateMode = false;
                break;
            case 2048:
                inBandResizeMode = false;
                break;
            default:
                logU << "reset priv mode " << arg << std::endl;
                break;
        }
    }
}

bool VtermImpl::getPrivateMode(u32 arg) const {
    switch (arg) {
        case 1:
            return cursorKeyMode == CursorKeyMode::Application;
        case 3:
            return colMode == ColMode::C132;
        case 4:
            return smoothScrollMode;
        case 5:
            return screenReverseVideo;
        case 6:
            return originMode == OriginMode::ScrollingRegion;
        case 7:
            return autoWrapMode;
        case 8:
            return autoRepeatMode;
        case 12:
            return cursorBlinkMode;
        case 42:
            return nationalReplacementMode;
        case 45:
            return reverseWrapMode;
        case 9:
            return mouseTrk.mode == MouseTrackingMode::X10_Compat;
        case 25:
            return showCursorMode;
        case 47:
        case 1047:
            return altScreenBufferMode;
        case 66:
            return keypadMode == KeypadMode::Application;
        case 67:
            return !bkspSendsDel;
        case 69:
            return horizMarginMode;
        case 1000:
            return mouseTrk.mode == MouseTrackingMode::VT200;
        case 1001:
            return mouseTrk.mode == MouseTrackingMode::VT200_Highlight;
        case 1002:
            return mouseTrk.mode == MouseTrackingMode::VT200_ButtonEvent;
        case 1003:
            return mouseTrk.mode == MouseTrackingMode::VT200_AnyEvent;
        case 1004:
            return mouseTrk.focusEventMode;
        case 1005:
            return mouseTrk.enc == MouseTrackingEnc::UTF8;
        case 1006:
            return mouseTrk.enc == MouseTrackingEnc::SGR;
        case 1007:
            return altScrollMode;
        case 1015:
            return mouseTrk.enc == MouseTrackingEnc::URXVT;
        case 1016:
            return mouseTrk.enc == MouseTrackingEnc::SGRPixels;
        case 1034:
            return eightBitInput;
        case 1036:
        case 1039:
            return altSendsEscape;
        case 1045:
            return extendedReverseWrapMode;
        case 2004:
            return bracketedPasteMode;
        case 2031:
            return colorSchemeUpdateMode;
        case 2048:
            return inBandResizeMode;
        case 2026:
            return synchronizedOutputMode;
        default:
            return false;
    }
}

TerminalPen VtermImpl::getPenState() const {
    TerminalPen result;
    result.cell = attrs;
    result.fg = colors.resolve(result.cell.fg);
    result.bg = colors.resolve(result.cell.bg);
    return result;
}

void VtermImpl::csi_privSM() {
    TRACE_FUN;
    for (size_t k = 0; k < nInputOps; ++k) {
        setPrivMode(inputOps[k], true);
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_privRM() {
    TRACE_FUN;
    for (size_t k = 0; k < nInputOps; ++k) {
        setPrivMode(inputOps[k], false);
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_privSave() {
    TRACE_FUN;
    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& arg = inputOps[k];
        switch (arg) {
            case 2:
            case 1048:
            case 1049:
                logU << "save priv mode " << arg << std::endl;
                break;
            default:
                savedPrivModes[arg] = getPrivateMode(arg);
                break;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_privRestore() {
    TRACE_FUN;
    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& arg = inputOps[k];
        const auto it = savedPrivModes.find(arg);
        if (it != savedPrivModes.end()) {
            setPrivMode(arg, it->second);
        } else {
            logU << "restore priv mode " << arg << " (never saved)" << std::endl;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::setFgFromPalIx() {
    if (fgPalIx < 0) {
        *fg = CellColor::defaultForeground();
    } else if (fgPalIx > 255) {
        return;
    } else if (opts.boldColors && attrs.bold && fgPalIx >= 0 && fgPalIx <= 7) {
        *fg = CellColor::indexed(fgPalIx + 8);
    } else {
        *fg = CellColor::indexed(fgPalIx);
    }
    if (underlineColorDefault) {
        attrs.underline_color = *fg;
    }
}

void VtermImpl::setBgFromPalIx() {
    if (bgPalIx < 0) {
        *bg = CellColor::defaultBackground();
    } else if (bgPalIx > 255) {
        return;
    } else {
        *bg = CellColor::indexed(bgPalIx);
    }
}

void VtermImpl::csi_SGR() {
    TRACE_FUN;
    const auto parseColor = [this](size_t& k, CellColor& color, int* palette) {
        if (k + 1 >= nInputOps) {
            return false;
        }
        const bool colon = inputSeparators[k + 1] == ':';
        const u32 mode = inputOps[++k];
        if (colon) {
            const size_t first = k + 1;
            size_t end = k;
            while (end + 1 < nInputOps && inputSeparators[end + 1] == ':') {
                ++end;
            }
            k = end;

            if (mode == 5) {
                if (end - first + 1 != 1 || inputOps[first] > 255) {
                    return false;
                }
                color = CellColor::indexed(inputOps[first]);
                if (palette) {
                    *palette = inputOps[first];
                }
                return true;
            }
            // ISO 8613-6 direct color is 2:Pi:Pr:Pg:Pb.  Pi is the
            // color-space identifier and is currently ignored, as in xterm.
            if (mode != 2 || end - first + 1 != 4 || inputOps[first + 1] > 255 || inputOps[first + 2] > 255 || inputOps[first + 3] > 255) {
                return false;
            }
            color = CellColor::direct({
                (u8)(inputOps[first + 1]),
                (u8)(inputOps[first + 2]),
                (u8)(inputOps[first + 3]),
            });
            if (palette) {
                *palette = -1;
            }
            return true;
        }

        if (mode == 5) {
            if (k + 1 >= nInputOps) {
                return false;
            }
            const unsigned index = inputOps[++k];
            if (index > 255) {
                return false;
            }
            color = CellColor::indexed(index);
            if (palette) {
                *palette = index;
            }
            return true;
        }
        if (mode != 2) {
            return false;
        }

        const size_t first = k + 1;
        const size_t available = nInputOps - first;
        k += std::min<size_t>(available, 3);
        if (available < 3 || inputOps[first] > 255 || inputOps[first + 1] > 255 || inputOps[first + 2] > 255) {
            return false;
        }
        color = CellColor::direct({
            (u8)(inputOps[first]),
            (u8)(inputOps[first + 1]),
            (u8)(inputOps[first + 2]),
        });
        k = first + 2;
        if (palette) {
            *palette = -1;
        }
        return true;
    };

    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& attr = inputOps[k];

        switch (attr) {
            case 0:
                attrs.uc_pt = ' ';
                attrs.bold = 0;
                attrs.faint = 0;
                attrs.italic = 0;
                attrs.underline = 0;
                attrs.underline_style = 0;
                attrs.blink = 0;
                attrs.conceal = 0;
                attrs.strike = 0;
                attrs.overline = 0;
                attrs.inverse = 0;
                reverseVideo = false;
                fg = &attrs.fg;
                bg = &attrs.bg;
                fgPalIx = defaultFgPalIx;
                setFgFromPalIx();
                bgPalIx = defaultBgPalIx;
                setBgFromPalIx();
                underlineColorDefault = true;
                attrs.underline_color = *fg;
                break;
            case 1:
                attrs.bold = 1;
                setFgFromPalIx();
                break;
            case 2:
                attrs.faint = 1;
                break;
            case 3:
                attrs.italic = 1;
                break;
            case 4:
                if (k + 1 < nInputOps && inputSeparators[k + 1] == ':') {
                    const u32 style = inputOps[++k];
                    if (style <= 5) {
                        attrs.underline_style = style;
                        attrs.underline = style != 0;
                    }
                } else {
                    attrs.underline = 1;
                    attrs.underline_style = 1;
                }
                break;
            case 5:
            case 6:
                attrs.blink = 1;
                break;
            case 7:
                if (!reverseVideo) {
                    reverseVideo = true;
                    attrs.inverse = 1;
                }
                break;
            case 8:
                attrs.conceal = 1;
                break;
            case 9:
                attrs.strike = 1;
                break;
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
            case 18:
            case 19:
                break;
            case 21:
                attrs.underline = 1;
                attrs.underline_style = 2;
                break;
            case 22:
                attrs.bold = 0;
                attrs.faint = 0;
                setFgFromPalIx();
                break;
            case 23:
                attrs.italic = 0;
                break;
            case 25:
                attrs.blink = 0;
                break;
            case 24:
                attrs.underline = 0;
                attrs.underline_style = 0;
                break;
            case 27:
                if (reverseVideo) {
                    reverseVideo = false;
                    attrs.inverse = 0;
                }
                break;
            case 28:
                attrs.conceal = 0;
                break;
            case 29:
                attrs.strike = 0;
                break;

            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
                fgPalIx = attr - 30;
                setFgFromPalIx();
                break;

            case 38:
                if (parseColor(k, *fg, &fgPalIx)) {
                }
                if (underlineColorDefault) {
                    attrs.underline_color = *fg;
                }
                break;
            case 39:
                fgPalIx = defaultFgPalIx;
                setFgFromPalIx();
                break;

            case 40:
            case 41:
            case 42:
            case 43:
            case 44:
            case 45:
            case 46:
            case 47:
                bgPalIx = attr - 40;
                setBgFromPalIx();
                break;

            case 48:
                if (parseColor(k, *bg, &bgPalIx)) {
                }
                break;
            case 49:
                bgPalIx = defaultBgPalIx;
                setBgFromPalIx();
                break;

            case 58:
                underlinePalIx = -1;
                if (parseColor(k, attrs.underline_color, &underlinePalIx)) {
                    underlineColorDefault = false;
                }
                break;
            case 59:
                underlineColorDefault = true;
                attrs.underline_color = *fg;
                break;

            case 53:
                attrs.overline = 1;
                break;
            case 55:
                attrs.overline = 0;
                break;

            case 90:
            case 91:
            case 92:
            case 93:
            case 94:
            case 95:
            case 96:
            case 97:
                fgPalIx = attr - 82;
                setFgFromPalIx();
                break;

            case 100:
            case 101:
            case 102:
            case 103:
            case 104:
            case 105:
            case 106:
            case 107:
                bgPalIx = attr - 92;
                setBgFromPalIx();
                break;

            default:
                logU << "attribute: " << attr << std::endl;
                break;
        }
    }
    if (underlineColorDefault) {
        attrs.underline_color = reverseVideo ? attrs.bg : attrs.fg;
    }
    setState(InputState::Normal);
}

/* 64 - VT420 family
    *  9 - National Replacement Character-sets
    * 15 - DEC technical set
    * 21 - horizontal scrolling
    * 22 - color
    */
#define DEVICE_ID "64;1;2;6;8;9;15;21;22;28;29c"

void VtermImpl::csi_priDA() {
    TRACE_FUN;
    writeCsiResponse("?" DEVICE_ID);
    setState(InputState::Normal);
}

void VtermImpl::csi_secDA() {
    TRACE_FUN;
    writeCsiResponse(">41;14;0c");
    setState(InputState::Normal);
}

void VtermImpl::csi_terDA() {
    TRACE_FUN;
    writeDcsResponse("!|00000000");
    setState(InputState::Normal);
}

void VtermImpl::csi_XTVERSION() {
    TRACE_FUN;
    writeDcsResponse(">|Zutty " ZUTTY_VERSION);
    setState(InputState::Normal);
}

void VtermImpl::csi_DECRQM(bool privateMode) {
    TRACE_FUN;
    const u32 mode = inputOps[0];
    u8 state = 0;

    if (privateMode) {
        switch (mode) {
            case 4:
                state = 4;
                break;
            case 8:
                state = 3;
                break;
            case 1:
            case 2:
            case 3:
            case 5:
            case 6:
            case 7:
            case 9:
            case 12:
            case 25:
            case 42:
            case 45:
            case 47:
            case 67:
            case 69:
            case 1000:
            case 1001:
            case 1002:
            case 1003:
            case 1004:
            case 1005:
            case 1006:
            case 1007:
            case 1015:
            case 1016:
            case 1034:
            case 1036:
            case 1039:
            case 1045:
            case 1047:
            case 2004:
            case 2026:
            case 2031:
            case 2048:
                state = (mode == 2 ? compatLevel != CompatibilityLevel::VT52 : getPrivateMode(mode)) ? 1 : 2;
                break;
            case 1049:
                state = altScreenBufferMode ? 1 : 2;
                break;
            default:
                break;
        }
    } else {
        switch (mode) {
            case 1:
            case 3:
            case 5:
            case 7:
            case 10:
            case 11:
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
            case 18:
            case 19:
                state = 4;
                break;
            case 2:
                state = keyboardLocked ? 1 : 2;
                break;
            case 4:
                state = insertMode ? 1 : 2;
                break;
            case 12:
                state = !localEcho ? 1 : 2;
                break;
            case 20:
                state = autoNewlineMode ? 1 : 2;
                break;
            default:
                break;
        }
    }

    std::ostringstream oss;
    oss << (privateMode ? "?" : "") << mode << ";" << (unsigned)(state) << "$y";
    writeCsiResponse(oss.str());
    setState(InputState::Normal);
}

void VtermImpl::csi_DSR(bool privateMode) {
    TRACE_FUN;
    if (privateMode) {
        if (inputOps[0] == 996) {
            reportColorScheme();
        }
        setState(InputState::Normal);
        return;
    }
    switch (inputOps[0]) {
        case 5:
            writeCsiResponse("0n");
            break;
        case 6: {
            std::ostringstream oss;
            if (originMode == OriginMode::Absolute) {
                oss << (posY + 1) << ";" << (posX + 1) << "R";
            } else {
                oss << (posY - marginTop + 1) << ";" << (posX - hMargin + 1) << "R";
            }
            writeCsiResponse(oss.str());
        } break;
        default:
            break;
    }
    setState(InputState::Normal);
}

void VtermImpl::esch_DECALN() {
    TRACE_FUN;

    TerminalCell origAttrs = attrs;
    CellColor* origFg = &attrs.fg;
    CellColor* origBg = &attrs.bg;

    resetAttrs();
    fillScreen('E');

    fg = origFg;
    bg = origBg;
    attrs = origAttrs;
    reverseVideo = attrs.inverse;

    setState(InputState::Normal);
}

void VtermImpl::setLineAttribute(u8 attribute) {
    TRACE_FUN;
    for (u16 x = 0; x < nCols; ++x) {
        auto& cell = cf->getCell(posY, x);
        cell.line_attr = attribute;
        cell.dirty = 1;
    }
    if (attribute) {
        posX = std::min<u16>(posX, std::max(1, nCols / 2) - 1);
    }
    cf->expose();
    setState(InputState::Normal);
}

void VtermImpl::esc_RIS() {
    TRACE_FUN;
    resetTerminal();
    setState(InputState::Normal);
}

void VtermImpl::csi_DECSTR() {
    TRACE_FUN;
    resetScreen(false);
    resetAttrs();
    marginTop = 0;
    marginBottom = nRows;
    horizMarginMode = false;
    hMargin = 0;
    nColsEff = nCols;
    posX = 0;
    posY = 0;
    lastCol = false;
    savedCursor_DEC->posX = 0;
    savedCursor_DEC->posY = 0;
    savedCursor_DEC->lastCol = false;
    savedCursor_DEC->attrs = attrs;
    savedCursor_DEC->originMode = OriginMode::Absolute;
    savedCursor_DEC->charsetState = CharsetState{};
    savedCursor_DEC->isSet = true;
    setState(InputState::Normal);
}

void VtermImpl::handle_DCS() {
    TRACE_FUN;
    auto arg = std::string((char*)argBuf.data(), argBuf.size());
    if (arg.substr(0, 2) == "$q") {
        dcs_DECRQSS(arg);
    } else if (arg.substr(0, 2) == "+q") {
        dcs_XTGETTCAP(arg.substr(2));
    } else if (arg.find('|') != std::string::npos) {
        dcs_DECUDK(arg);
    } else {
        logU << "DCS: '" << arg << "'" << std::endl;
    }
    setState(InputState::Normal);
}

void VtermImpl::dcs_DECUDK(const std::string& request) {
    if (userDefinedKeysLocked) {
        return;
    }
    const size_t bar = request.find('|');
    if (bar == std::string::npos) {
        return;
    }
    u32 clear = 0;
    u32 lock = 0;
    const std::string parameters = request.substr(0, bar);
    const size_t separator = parameters.find(';');
    if (separator != std::string::npos && parameters.find(';', separator + 1) != std::string::npos) {
        return;
    }
    const auto parseParameter = [](const char* begin, const char* end, u32& value) {
        if (begin == end) {
            value = 0;
            return true;
        }
        const auto result = std::from_chars(begin, end, value);
        return result.ec == std::errc{} && result.ptr == end && value <= 1;
    };
    const char* const parameterBegin = parameters.data();
    const char* const parameterEnd = parameterBegin + parameters.size();
    const char* const clearEnd = separator == std::string::npos ? parameterEnd : parameterBegin + separator;
    if (!parseParameter(parameterBegin, clearEnd, clear)) {
        return;
    }
    if (separator != std::string::npos && !parseParameter(clearEnd + 1, parameterEnd, lock)) {
        return;
    }
    if (clear == 0) {
        userDefinedKeys.clear();
    }

    const auto keyForCode = [](u32 code) {
        switch (code) {
            case 17:
                return VtKey::F6;
            case 18:
                return VtKey::F7;
            case 19:
                return VtKey::F8;
            case 20:
                return VtKey::F9;
            case 21:
                return VtKey::F10;
            case 23:
                return VtKey::F11;
            case 24:
                return VtKey::F12;
            case 25:
                return VtKey::F13;
            case 26:
                return VtKey::F14;
            case 28:
                return VtKey::F15;
            case 29:
                return VtKey::F16;
            case 31:
                return VtKey::F17;
            case 32:
                return VtKey::F18;
            case 33:
                return VtKey::F19;
            case 34:
                return VtKey::F20;
            default:
                return VtKey::NONE;
        }
    };
    size_t begin = bar + 1;
    while (begin <= request.size()) {
        const size_t end = request.find(';', begin);
        const std::string definition = request.substr(begin, end - begin);
        const size_t slash = definition.find('/');
        u32 code = 0;
        const auto codeResult = slash == std::string::npos ? std::from_chars(definition.data(), definition.data(), code) : std::from_chars(definition.data(), definition.data() + slash, code);
        if (slash != std::string::npos && codeResult.ec == std::errc{} && codeResult.ptr == definition.data() + slash) {
            std::string value;
            const size_t encodedSize = definition.size() - slash - 1;
            bool valid = encodedSize % 2 == 0 && encodedSize / 2 <= 255;
            for (size_t k = slash + 1; valid && k < definition.size(); k += 2) {
                const auto digit = [](char ch) -> int {
                    if (ch >= '0' && ch <= '9') {
                        return ch - '0';
                    }
                    if (ch >= 'a' && ch <= 'f') {
                        return ch - 'a' + 10;
                    }
                    if (ch >= 'A' && ch <= 'F') {
                        return ch - 'A' + 10;
                    }
                    return -1;
                };
                const int high = digit(definition[k]);
                const int low = digit(definition[k + 1]);
                valid = high >= 0 && low >= 0;
                if (valid) {
                    value.push_back((char)(high * 16 + low));
                }
            }
            const VtKey key = keyForCode(code);
            if (valid && key != VtKey::NONE) {
                userDefinedKeys[key] = value;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    userDefinedKeysLocked = lock == 0;
}

void VtermImpl::dcs_DECRQSS(const std::string& arg) {
    TRACE_FUN;

    std::ostringstream value;
    const std::string query = arg.substr(2);
    if (query == "\"p") {
        value << (compatLevel == CompatibilityLevel::VT100 ? 61 : 64) << ";1\"p";
    } else if (query == "m") {
        value << "0";
        if (attrs.bold) {
            value << ";1";
        }
        if (attrs.faint) {
            value << ";2";
        }
        if (attrs.italic) {
            value << ";3";
        }
        if (attrs.underline) {
            value << ";4";
            if (attrs.underline_style > 1) {
                value << ":" << (unsigned)(attrs.underline_style);
            }
        }
        if (attrs.blink) {
            value << ";5";
        }
        if (reverseVideo) {
            value << ";7";
        }
        if (attrs.conceal) {
            value << ";8";
        }
        if (attrs.strike) {
            value << ";9";
        }
        if (attrs.overline) {
            value << ";53";
        }
        if (fgPalIx >= 0) {
            value << ";38;5;" << fgPalIx;
        } else if (fg->source() == CellColor::Source::Direct) {
            const Color color = fg->color();
            value << ";38;2;" << (unsigned)(color.red) << ";" << (unsigned)(color.green) << ";" << (unsigned)(color.blue);
        }
        if (bgPalIx >= 0) {
            value << ";48;5;" << bgPalIx;
        } else if (bg->source() == CellColor::Source::Direct) {
            const Color color = bg->color();
            value << ";48;2;" << (unsigned)(color.red) << ";" << (unsigned)(color.green) << ";" << (unsigned)(color.blue);
        }
        if (!underlineColorDefault) {
            if (underlinePalIx >= 0) {
                value << ";58;5;" << underlinePalIx;
            } else {
                const Color color = attrs.underline_color.color();
                value << ";58;2;" << (unsigned)(color.red) << ";" << (unsigned)(color.green) << ";" << (unsigned)(color.blue);
            }
        }
        value << "m";
    } else if (query == "r") {
        value << marginTop + 1 << ";" << marginBottom << "r";
    } else if (query == "s") {
        value << hMargin + 1 << ";" << nColsEff << "s";
    } else if (query == " q") {
        value << (unsigned)(cursorStyleParam) << " q";
    } else if (query == "\"q") {
        value << (attrs.protected_char ? 1 : 0) << "\"q";
    } else {
        writeDcsResponse("0$r");
        return;
    }

    writeDcsResponse("1$r" + value.str());
}

void VtermImpl::dcs_XTGETTCAP(const std::string& request) {
    TRACE_FUN;
    const auto hexValue = [](const std::string& value) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(value.size() * 2);
        for (unsigned char ch : value) {
            result.push_back(hex[ch >> 4]);
            result.push_back(hex[ch & 0x0f]);
        }
        return result;
    };
    const auto decodeHex = [](const std::string& value, std::string& result) {
        const auto digit = [](unsigned char ch) -> int {
            if (ch >= '0' && ch <= '9') {
                return ch - '0';
            }
            if (ch >= 'a' && ch <= 'f') {
                return ch - 'a' + 10;
            }
            if (ch >= 'A' && ch <= 'F') {
                return ch - 'A' + 10;
            }
            return -1;
        };
        if (value.size() % 2 != 0) {
            return false;
        }
        result.clear();
        for (size_t pos = 0; pos < value.size(); pos += 2) {
            const int high = digit(value[pos]);
            const int low = digit(value[pos + 1]);
            if (high < 0 || low < 0) {
                return false;
            }
            result.push_back((char)((high << 4) | low));
        }
        return true;
    };

    size_t begin = 0;
    while (begin <= request.size()) {
        const size_t end = request.find(';', begin);
        const std::string encoded = request.substr(begin, end - begin);
        std::string name;
        std::string value;
        bool found = decodeHex(encoded, name);
        if (name == "TN") {
            value = "xterm-256color";
        } else if (name == "Co" || name == "colors") {
            value = "256";
        } else if (name == "RGB") {
            value = "8";
        } else {
            found = false;
        }

        std::string reply = std::string(found ? "1+r" : "0+r") + encoded;
        if (found) {
            reply += "=" + hexValue(value);
        }
        writeDcsResponse(reply);
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
}

void VtermImpl::handle_OSC() {
    TRACE_FUN;
    auto osc = std::string((char*)argBuf.data(), argBuf.size());
    std::size_t p = osc.find_first_of(";");
    std::string arg;
    if (p != std::string::npos) {
        arg = osc.substr(p + 1);
    }

    int cmd = -1;
    const char* commandEnd = p == std::string::npos ? osc.data() + osc.size() : osc.data() + p;
    const auto parsed = std::from_chars(osc.data(), commandEnd, cmd);
    if (osc.empty() || parsed.ec != std::errc{} || parsed.ptr != commandEnd || cmd < 0) {
        logT << "OSC: malformed command string '" << osc << "'" << std::endl;
    } else {
        switch (cmd) {
            case 0:
                iconTitle = arg;
                windowTitle = arg;
                host.osc(cmd, arg);
                break;
            case 1:
                iconTitle = arg;
                host.osc(cmd, arg);
                break;
            case 2:
                windowTitle = arg;
                host.osc(cmd, arg);
                break;
            case 4:
                osc_PaletteQuery(cmd, arg);
                break;
            case 104: {
                if (arg.empty()) {
                    std::copy(std::begin(originalPalette256), std::end(originalPalette256), std::begin(colors.palette));
                    frame_pri.expose();
                    frame_alt.expose();
                } else {
                    bool changed = false;
                    std::stringstream indices(arg);
                    std::string value;
                    while (std::getline(indices, value, ';')) {
                        int index = -1;
                        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), index);
                        if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && index >= 0 && index <= 255) {
                            colors.palette[index] = originalPalette256[index];
                            changed = true;
                        }
                    }
                    if (changed) {
                        frame_pri.expose();
                        frame_alt.expose();
                    }
                }
            } break;
            case 10:
            case 11:
            case 12:
            case 17:
            case 19:
                osc_DynamicColorQuery(cmd, arg);
                break;
            case 110:
                colors.defaultForeground = opts.fg;
                defaultFgPalIx = -1;
                frame_pri.expose();
                frame_alt.expose();
                break;
            case 111:
                colors.defaultBackground = opts.bg;
                defaultBgPalIx = -1;
                frame_pri.expose();
                frame_alt.expose();
                break;
            case 112:
                cursorColor = opts.cr;
                frame_pri.setCursorColor(cursorColor);
                frame_alt.setCursorColor(cursorColor);
                break;
            case 117:
                selectionBgColor = opts.bg;
                frame_pri.setSelectionColor(false, selectionBgColor, false);
                frame_alt.setSelectionColor(false, selectionBgColor, false);
                break;
            case 119:
                selectionFgColor = opts.fg;
                frame_pri.setSelectionColor(true, selectionFgColor, false);
                frame_alt.setSelectionColor(true, selectionFgColor, false);
                break;
            case 9: {
                if (arg.compare(0, 2, "4;") == 0) {
                    const size_t separator = arg.find(';', 2);
                    u32 state = 0;
                    u32 percent = 0;
                    if (separator != std::string::npos) {
                        const auto stateResult = std::from_chars(arg.data() + 2, arg.data() + separator, state);
                        const auto percentResult = std::from_chars(arg.data() + separator + 1, arg.data() + arg.size(), percent);
                        if (stateResult.ec == std::errc{} && stateResult.ptr == arg.data() + separator && percentResult.ec == std::errc{} && percentResult.ptr == arg.data() + arg.size() && state <= 4 && percent <= 100) {
                            host.progress(state, percent);
                        }
                    }
                } else {
                    host.notify({}, windowTitle, arg, false);
                }
            } break;
            case 99:
                osc_Notification(arg);
                break;
            case 133:
                osc_ShellIntegration(arg);
                host.osc(cmd, arg);
                break;

            default:
                host.osc(cmd, arg);
                break;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::osc_ShellIntegration(const std::string& arg) {
    if (arg.empty()) {
        return;
    }
    const size_t separator = arg.find(';');
    const std::string marker = arg.substr(0, separator);
    if (marker == "A") {
        currentSemantic = 1;
    } else if (marker == "B" && currentSemantic == 1) {
        currentSemantic = 2;
    } else if (marker == "C" && currentSemantic == 2) {
        currentSemantic = 3;
    } else if (marker == "D" && currentSemantic == 3) {
        currentSemantic = 0;
    }
}

void VtermImpl::osc_Notification(const std::string& arg) {
    const size_t separator = arg.find(';');
    if (separator == std::string::npos) {
        return;
    }

    std::string id;
    std::string payloadType = "title";
    bool encoded = false;
    bool finalChunk = true;
    bool valid = true;
    size_t begin = 0;
    const std::string metadata = arg.substr(0, separator);
    while (begin <= metadata.size()) {
        const size_t end = metadata.find(':', begin);
        const std::string field = metadata.substr(begin, end - begin);
        if (field.empty() && metadata.empty()) {
            break;
        }
        const size_t equal = field.find('=');
        if (equal == 1 && std::isalpha((unsigned char)(field[0]))) {
            const std::string key = field.substr(0, equal);
            const std::string value = field.substr(equal + 1);
            if (key == "i") {
                id = value;
            } else if (key == "p") {
                payloadType = value;
            } else if (key == "e") {
                if (value != "0" && value != "1") {
                    valid = false;
                } else {
                    encoded = value == "1";
                }
            } else if (key == "d") {
                if (value != "0" && value != "1") {
                    valid = false;
                } else {
                    finalChunk = value == "1";
                }
            }
        } else {
            valid = false;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }

    const auto validIdentifier = [](const std::string& value) {
        if (value.size() > 256) {
            return false;
        }
        return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '+' || ch == '.';
        });
    };
    if (!valid || !validIdentifier(id)) {
        return;
    }

    if (payloadType == "?") {
        writePty(("\x1b]99;i=" + id + ":p=?;p=title,body,close\x1b\\").c_str());
        return;
    }
    if (payloadType == "close") {
        if (id.empty() || !activeNotificationIds.erase(id)) {
            return;
        }
        host.notify(id, {}, {}, true);
        notifications.erase(id);
        return;
    }
    if (payloadType != "title" && payloadType != "body") {
        return;
    }

    auto& notification = notifications[id];
    NotificationPart& destination = payloadType == "title" ? notification.title : notification.body;
    const auto flushEncoded = [](NotificationPart& part) {
        if (part.encoded.empty()) {
            return true;
        }
        std::string decoded;
        if (!base64::decode(part.encoded, decoded) || part.text.size() + decoded.size() > 8192) {
            return false;
        }
        part.text += decoded;
        part.encoded.clear();
        return true;
    };

    const std::string payload = arg.substr(separator + 1);
    if (encoded) {
        if (payload.size() > 4096) {
            notifications.erase(id);
            return;
        }
        destination.encoded += payload;
        const bool decodableBoundary = finalChunk || (!destination.encoded.empty() && destination.encoded.back() == '=') || destination.encoded.size() % 4 == 0;
        if (decodableBoundary && !flushEncoded(destination)) {
            notifications.erase(id);
            return;
        }
    } else {
        if (payload.size() > 2048 || !flushEncoded(destination) || destination.text.size() + payload.size() > 8192) {
            notifications.erase(id);
            return;
        }
        destination.text += payload;
    }

    if (!finalChunk) {
        return;
    }
    if (!flushEncoded(notification.title) || !flushEncoded(notification.body)) {
        notifications.erase(id);
        return;
    }

    const auto escapeSafeUtf8 = [](const std::string& value) {
        for (size_t k = 0; k < value.size();) {
            const unsigned char first = value[k++];
            if (first <= 0x1f || first == 0x7f) {
                return false;
            }
            if (first < 0x80) {
                continue;
            }
            size_t continuation = 0;
            unsigned char secondMin = 0x80;
            unsigned char secondMax = 0xbf;
            if (first >= 0xc2 && first <= 0xdf) {
                continuation = 1;
                if (first == 0xc2) {
                    secondMin = 0xa0;
                }
            } else if (first >= 0xe0 && first <= 0xef) {
                continuation = 2;
                if (first == 0xe0) {
                    secondMin = 0xa0;
                }
                if (first == 0xed) {
                    secondMax = 0x9f;
                }
            } else if (first >= 0xf0 && first <= 0xf4) {
                continuation = 3;
                if (first == 0xf0) {
                    secondMin = 0x90;
                }
                if (first == 0xf4) {
                    secondMax = 0x8f;
                }
            } else {
                return false;
            }
            if (k + continuation > value.size()) {
                return false;
            }
            const unsigned char second = value[k];
            if (second < secondMin || second > secondMax) {
                return false;
            }
            for (size_t n = 0; n < continuation; ++n) {
                const unsigned char ch = value[k++];
                if ((ch & 0xc0) != 0x80) {
                    return false;
                }
            }
        }
        return true;
    };
    if (!escapeSafeUtf8(notification.title.text) || !escapeSafeUtf8(notification.body.text)) {
        notifications.erase(id);
        return;
    }
    if (notification.title.text.empty()) {
        notification.title.text = std::move(notification.body.text);
        notification.body.text.clear();
    }
    if (notification.title.text.empty()) {
        notifications.erase(id);
        return;
    }
    host.notify(id, notification.title.text, notification.body.text, false);
    if (!id.empty()) {
        activeNotificationIds.insert(id);
    }
    notifications.erase(id);
}

void VtermImpl::reportInBandResize() {
    std::ostringstream response;
    response << "48;" << nRows << ';' << nCols << ';' << nRows * glyphPy << ';' << nCols * glyphPx << 't';
    writeCsiResponse(response.str());
}

void VtermImpl::reportColorScheme() {
    // Zutty has no runtime profile or operating-system theme switching.  Its
    // configured background therefore remains the authoritative preference;
    // application-originated OSC color changes must not affect this report.
    const u32 brightness = 299 * opts.bg.red + 587 * opts.bg.green + 114 * opts.bg.blue;
    const u8 scheme = brightness >= 128000 ? 2 : 1;
    std::ostringstream response;
    response << "?997;" << (unsigned)scheme << 'n';
    writeCsiResponse(response.str());
}

void VtermImpl::writeTitleResponse(char kind, const std::string& title) {
    std::string response = "\x1b]";
    response.push_back(kind);
    response += title;
    response += "\x1b\\";
    writePty(response.c_str());
}

void VtermImpl::applyPaletteColor(u16 index, Color color) {
    colors.palette[index] = color;
    frame_pri.expose();
    frame_alt.expose();
}

void VtermImpl::osc_PaletteQuery(int cmd, const std::string& arg) {
    std::stringstream fields(arg);
    std::string indexText;
    std::string spec;
    while (std::getline(fields, indexText, ';') && std::getline(fields, spec, ';')) {
        int paletteIdx = -1;
        const auto parsed = std::from_chars(indexText.data(), indexText.data() + indexText.size(), paletteIdx);
        if (parsed.ec != std::errc{} || parsed.ptr != indexText.data() + indexText.size() || paletteIdx < 0 || paletteIdx > 255) {
            continue;
        }
        if (spec == "?") {
            std::ostringstream reply;
            reply << cmd << ";" << paletteIdx << ";" << colors.palette[paletteIdx];
            writeOscResponse(reply.str());
        } else {
            Color color;
            if (parseOscColor(spec, color)) {
                applyPaletteColor(paletteIdx, color);
            }
        }
    }
}

void VtermImpl::osc_DynamicColorQuery(int cmd, const std::string& arg) {
    if (arg == "?") {
        Color c;
        switch (cmd) {
            case 10:
                c = colors.defaultForeground;
                break;
            case 11:
                c = colors.defaultBackground;
                break;
            case 12:
                c = cursorColor;
                break;
            case 17:
                c = selectionBgColor;
                break;
            case 19:
                c = selectionFgColor;
                break;
            default:
                return;
        }
        std::ostringstream oss;
        oss << cmd << ";" << c;
        writeOscResponse(oss.str());
    } else {
        Color color;
        if (!parseOscColor(arg, color)) {
            return;
        }
        switch (cmd) {
            case 10:
                colors.defaultForeground = color;
                defaultFgPalIx = -1;
                frame_pri.expose();
                frame_alt.expose();
                break;
            case 11:
                colors.defaultBackground = color;
                defaultBgPalIx = -1;
                frame_pri.expose();
                frame_alt.expose();
                break;
            case 12:
                cursorColor = color;
                frame_pri.setCursorColor(color);
                frame_alt.setCursorColor(color);
                break;
            case 17:
                selectionBgColor = color;
                frame_pri.setSelectionColor(false, color, true);
                frame_alt.setSelectionColor(false, color, true);
                break;
            case 19:
                selectionFgColor = color;
                frame_pri.setSelectionColor(true, color, true);
                frame_alt.setSelectionColor(true, color, true);
                break;
        }
    }
}

void VtermImpl::csiq_DECSCL() {
    TRACE_FUN;
    if (nInputOps > 0) {
        switch (inputOps[0]) {
            case 61:
                compatLevel = CompatibilityLevel::VT100;
                break;
            case 62:
                compatLevel = CompatibilityLevel::VT400;
                break;
            case 63:
                compatLevel = CompatibilityLevel::VT400;
                break;
            case 64:
                compatLevel = CompatibilityLevel::VT400;
                break;
            case 65:
                compatLevel = CompatibilityLevel::VT400;
                break;
            default:
                logU << "DECSCL: compatibility mode " << inputOps[0] << std::endl;
                break;
        }
    }
    if (nInputOps > 1) {
        switch (inputOps[1]) {
            case 0:
                logT << "DECSCL: 8-bit controls" << std::endl;
                send8BitControls = true;
                break;
            case 1:
                logT << "DECSCL: 7-bit controls" << std::endl;
                send8BitControls = false;
                break;
            case 2:
                logT << "DECSCL: 8-bit controls" << std::endl;
                send8BitControls = true;
                break;
            default:
                logU << "DECSCL: C1 control transmission mode: " << inputOps[1] << std::endl;
                break;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_XTWINOPS() {
    TRACE_FUN;
    if (!opts.allowWindowOps) {
        setState(InputState::Normal);
        return;
    }
    const u32 operation = inputOps[0];
    std::ostringstream response;
    switch (operation) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
            host.windowOperation(operation, nInputOps > 1 ? inputOps[1] : 0, nInputOps > 2 ? inputOps[2] : 0);
            break;
        case 11:
            writeCsiResponse(host.windowInfo().iconified ? "2t" : "1t");
            break;
        case 13: {
            const auto info = host.windowInfo();
            response << "3;" << (u16)(info.x) << ';' << (u16)(info.y) << 't';
            writeCsiResponse(response.str());
        } break;
        case 14:
            if (nInputOps > 1 && inputOps[1] == 2) {
                const auto info = host.windowInfo();
                response << "4;" << info.pixelHeight << ';' << info.pixelWidth << 't';
            } else {
                response << "4;" << nRows * glyphPy << ';' << nCols * glyphPx << 't';
            }
            writeCsiResponse(response.str());
            break;
        case 15: {
            const auto info = host.windowInfo();
            response << "5;" << info.screenPixelHeight << ';' << info.screenPixelWidth << 't';
            writeCsiResponse(response.str());
        } break;
        case 16:
            response << "6;" << glyphPy << ';' << glyphPx << 't';
            writeCsiResponse(response.str());
            break;
        case 18:
            response << "8;" << nRows << ';' << nCols << 't';
            writeCsiResponse(response.str());
            break;
        case 19: {
            const auto info = host.windowInfo();
            response << "9;" << info.screenPixelHeight / glyphPy << ';' << info.screenPixelWidth / glyphPx << 't';
            writeCsiResponse(response.str());
        } break;
        case 20:
            writeTitleResponse('L', iconTitle);
            break;
        case 21:
            writeTitleResponse('l', windowTitle);
            break;
        case 22: {
            const u32 which = nInputOps > 1 ? inputOps[1] : 0;
            if (which > 2) {
                break;
            }
            SavedTitles saved;
            if (which == 0 || which == 1) {
                saved.hasIcon = true;
                saved.icon = iconTitle;
            }
            if (which == 0 || which == 2) {
                saved.hasWindow = true;
                saved.window = windowTitle;
            }
            titleStack.push_back(std::move(saved));
            if (titleStack.size() > 10) {
                titleStack.erase(titleStack.begin());
            }
        } break;
        case 23: {
            const u32 which = nInputOps > 1 ? inputOps[1] : 0;
            if (which > 2) {
                break;
            }
            if (!titleStack.empty()) {
                SavedTitles saved = std::move(titleStack.back());
                titleStack.pop_back();
                if ((which == 0 || which == 1) && saved.hasIcon) {
                    iconTitle = saved.icon;
                    host.osc(1, iconTitle);
                }
                if ((which == 0 || which == 2) && saved.hasWindow) {
                    windowTitle = saved.window;
                    host.osc(2, windowTitle);
                }
            }
        } break;
        default:
            break;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_XTHIMOUSE() {
    TRACE_FUN;
    if (mouseTrk.mode == MouseTrackingMode::VT200_Highlight && nInputOps == 5 && inputOps[0] != 0) {
        mouseHighlight.active = true;
        mouseHighlight.startX = std::max<u32>(1, inputOps[1]);
        mouseHighlight.startY = std::max<u32>(1, inputOps[2]);
        mouseHighlight.firstRow = std::max<u32>(1, inputOps[3]);
        mouseHighlight.lastRow = std::max<u32>(mouseHighlight.firstRow, inputOps[4]);
    } else {
        mouseHighlight.active = false;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECELR() {
    const u32 mode = inputOps[0];
    locator.enabled = mode <= 2 ? mode : 0;
    locator.pixels = nInputOps > 1 && inputOps[1] == 1;
    locator.filter = false;
    setState(InputState::Normal);
}

void VtermImpl::csi_DECSLE() {
    for (size_t k = 0; k < nInputOps; ++k) {
        switch (inputOps[k]) {
            case 0:
                locator.reportDown = locator.reportUp = false;
                locator.filter = false;
                break;
            case 1:
                locator.reportDown = true;
                break;
            case 2:
                locator.reportDown = false;
                break;
            case 3:
                locator.reportUp = true;
                break;
            case 4:
                locator.reportUp = false;
                break;
        }
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECRQLP() {
    if (locator.enabled) {
        const u16 x = locator.pixels ? locator.pixelX : locator.column;
        const u16 y = locator.pixels ? locator.pixelY : locator.row;
        std::ostringstream response;
        response << "1;" << (unsigned)(locator.buttons) << ';' << y << ';' << x << ";0&w";
        writeCsiResponse(response.str());
        if (locator.enabled == 2) {
            locator.enabled = 0;
        }
    } else {
        writeCsiResponse("0&w");
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_DECEFR() {
    const auto value = [this](size_t k, u16 current) {
        return k < nInputOps && inputOps[k] ? (u16)(inputOps[k]) : current;
    };
    locator.filterTop = value(0, locator.pixels ? locator.pixelY : locator.row);
    locator.filterLeft = value(1, locator.pixels ? locator.pixelX : locator.column);
    locator.filterBottom = value(2, locator.filterTop);
    locator.filterRight = value(3, locator.filterLeft);
    locator.filter = locator.enabled != 0;
    setState(InputState::Normal);
}

void VtermImpl::csi_XTMODKEYS() {
    TRACE_FUN;
    const auto supported = [](u32 resource) {
        return resource <= 4 || resource == 6 || resource == 7;
    };
    if (!csiHadParams) {
        std::copy(std::begin(initialModifyKeyResources), std::end(initialModifyKeyResources), std::begin(modifyKeyResources));
    } else if (supported(inputOps[0])) {
        const u32 resource = inputOps[0];
        const u32 value = nInputOps > 1 ? inputOps[1] : initialModifyKeyResources[resource];
        const u32 maximum = resource == 4 ? 2 : 4;
        if (value <= maximum) {
            modifyKeyResources[resource] = value;
        }
    }
    modifyOtherKeys = modifyKeyResources[4];
    setState(InputState::Normal);
}

void VtermImpl::csi_XTQMODKEYS() {
    TRACE_FUN;
    const u32 resource = inputOps[0];
    if (resource <= 4 || resource == 6 || resource == 7) {
        std::ostringstream response;
        response << '>' << resource << ';' << (unsigned)(modifyKeyResources[resource]) << 'm';
        writeCsiResponse(response.str());
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_kittyKeyboardPush() {
    TRACE_FUN;
    constexpr size_t maxStackDepth = 16;
    auto& state = kittyKeyboardState();
    if (state.stack.size() == maxStackDepth) {
        state.stack.erase(state.stack.begin());
    }
    state.stack.push_back(state.flags);
    state.flags = inputOps[0] & 0x1f;
    setState(InputState::Normal);
}

void VtermImpl::csi_kittyKeyboardPop() {
    TRACE_FUN;
    auto& state = kittyKeyboardState();
    const u32 count = inputOps[0] ? inputOps[0] : 1;
    for (u32 k = 0; k < count; ++k) {
        if (state.stack.empty()) {
            state.flags = 0;
            break;
        }
        state.flags = state.stack.back();
        state.stack.pop_back();
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_kittyKeyboardSet() {
    TRACE_FUN;
    auto& state = kittyKeyboardState();
    const u8 flags = inputOps[0] & 0x1f;
    const u32 mode = nInputOps > 1 ? inputOps[1] : 1;
    switch (mode) {
        case 1:
            state.flags = flags;
            break;
        case 2:
            state.flags |= flags;
            break;
        case 3:
            state.flags &= ~flags;
            break;
        default:
            break;
    }
    setState(InputState::Normal);
}

void VtermImpl::csi_kittyKeyboardQuery() {
    TRACE_FUN;
    std::ostringstream reply;
    reply << "?" << (unsigned)(getKittyKeyboardFlags()) << 'u';
    writeCsiResponse(reply.str());
    setState(InputState::Normal);
}

namespace {
    using Key = VtKey;
    using InputSpec = VtermImpl::InputSpec;

#define ESC "\x1b"
#define CSI ESC "["
#define SS3 ESC "O"

#define MC "\xff"

    const InputSpec is_modOtherKeys2[] = {
        {Key::K2, CSI "27;" MC ";50~"},
        {Key::K3, CSI "27;" MC ";51~"},
        {Key::K4, CSI "27;" MC ";52~"},
        {Key::K5, CSI "27;" MC ";53~"},
        {Key::K6, CSI "27;" MC ";54~"},
        {Key::K7, CSI "27;" MC ";55~"},
        {Key::K8, CSI "27;" MC ";56~"},
        {Key::Backtick, CSI "27;" MC ";96~"},
        {Key::Tilde, CSI "27;" MC ";126~"},
        {Key::Tab, CSI "27;" MC ";9~"},
        {Key::Return, CSI "27;" MC ";13~"},
        {Key::Space, CSI "27;" MC ";32~"},
        {Key::Backspace, CSI "27;" MC ";127~"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Alt[] = {

        {Key::K0, "\xc2\xb0"},
        {Key::K1, "\xc2\xb1"},
        {Key::K2, "\xc2\xb2"},
        {Key::K3, "\xc2\xb3"},
        {Key::K4, "\xc2\xb4"},
        {Key::K5, "\xc2\xb5"},
        {Key::K6, "\xc2\xb6"},
        {Key::K7, "\xc2\xb7"},
        {Key::K8, "\xc2\xb8"},
        {Key::K9, "\xc2\xb9"},
        {Key::Backtick, "\xc3\xa0"},
        {Key::Tilde, "\xc3\xbe"},
        {Key::Backspace, "\xc3\xbf"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Alt_altSendsEscape[] = {
        {Key::K0, ESC "0"},
        {Key::K1, ESC "1"},
        {Key::K2, ESC "2"},
        {Key::K3, ESC "3"},
        {Key::K4, ESC "4"},
        {Key::K5, ESC "5"},
        {Key::K6, ESC "6"},
        {Key::K7, ESC "7"},
        {Key::K8, ESC "8"},
        {Key::K9, ESC "9"},
        {Key::Backtick, ESC "`"},
        {Key::Tilde, ESC "~"},
        {Key::Backspace, ESC "\x7f"},
        {Key::Space, ESC " "},
        {Key::Tab, ESC "\t"},
        {Key::Return, ESC "\n"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Control_modOtherKeys[] = {
        {Key::K0, CSI "27;" MC ";48~"},
        {Key::K1, CSI "27;" MC ";49~"},
        {Key::K9, CSI "27;" MC ";57~"},
        {Key::Tab, CSI "27;" MC ";9~"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_ControlAlt_altSendsEscape[] = {
        {Key::K2, ESC "\x00", 2},
        {Key::K3, ESC "\x1b", 2},
        {Key::K4, ESC "\x1c", 2},
        {Key::K5, ESC "\x1d", 2},
        {Key::K6, ESC "\x1e", 2},
        {Key::K7, ESC "\x1f", 2},
        {Key::K8, ESC "\x7f", 2},
        {Key::Backtick, ESC "\x00", 2},
        {Key::Tilde, ESC "\x1e", 2},
        {Key::Space, ESC "\x00", 2},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Control[] = {
        {Key::K2, "\x00", 1},
        {Key::K3, "\x1b", 1},
        {Key::K4, "\x1c", 1},
        {Key::K5, "\x1d", 1},
        {Key::K6, "\x1e", 1},
        {Key::K7, "\x1f", 1},
        {Key::K8, "\x7f", 1},
        {Key::Backtick, "\x00", 1},
        {Key::Tilde, "\x1e", 1},
        {Key::Space, "\x00", 1},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Shift[] = {
        {Key::Tab, CSI "Z"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_modOtherKeys[] = {
        {Key::Return, CSI "27;" MC ";13~"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Ansi[] = {
        {Key::K0, "0"},
        {Key::K1, "1"},
        {Key::K2, "2"},
        {Key::K3, "3"},
        {Key::K4, "4"},
        {Key::K5, "5"},
        {Key::K6, "6"},
        {Key::K7, "7"},
        {Key::K8, "8"},
        {Key::K9, "9"},
        {Key::Backtick, "`"},
        {Key::Tilde, "~"},
        {Key::Space, " "},
        {Key::Backspace, "\x7f"},
        {Key::Tab, "\t"},
        {Key::Return, "\r"},
        {Key::Insert, CSI "2~"},
        {Key::Delete, CSI "3~"},
        {Key::PageUp, CSI "5~"},
        {Key::PageDown, CSI "6~"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Mod_Ansi[] = {
        {Key::Insert, CSI "2;" MC "~"},
        {Key::Delete, CSI "3;" MC "~"},
        {Key::PageUp, CSI "5;" MC "~"},
        {Key::PageDown, CSI "6;" MC "~"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Ansi_FunctionKeys[] = {
        {Key::F1, SS3 "P"},
        {Key::KP_F1, SS3 "P"},
        {Key::F2, SS3 "Q"},
        {Key::KP_F2, SS3 "Q"},
        {Key::F3, SS3 "R"},
        {Key::KP_F3, SS3 "R"},
        {Key::F4, SS3 "S"},
        {Key::KP_F4, SS3 "S"},
        {Key::F5, CSI "15~"},
        {Key::F6, CSI "17~"},
        {Key::F7, CSI "18~"},
        {Key::F8, CSI "19~"},
        {Key::F9, CSI "20~"},
        {Key::F10, CSI "21~"},
        {Key::F11, CSI "23~"},
        {Key::F12, CSI "24~"},
        {Key::F13, CSI "25~"},
        {Key::F14, CSI "26~"},
        {Key::F15, CSI "28~"},
        {Key::F16, CSI "29~"},
        {Key::F17, CSI "31~"},
        {Key::F18, CSI "32~"},
        {Key::F19, CSI "33~"},
        {Key::F20, CSI "34~"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Mod_Ansi_FunctionKeys[] = {
        {Key::F1, CSI "1;" MC "P"},
        {Key::KP_F1, CSI "1;" MC "P"},
        {Key::F2, CSI "1;" MC "Q"},
        {Key::KP_F2, CSI "1;" MC "Q"},
        {Key::F3, CSI "1;" MC "R"},
        {Key::KP_F3, CSI "1;" MC "R"},
        {Key::F4, CSI "1;" MC "S"},
        {Key::KP_F4, CSI "1;" MC "S"},
        {Key::F5, CSI "15;" MC "~"},
        {Key::F6, CSI "17;" MC "~"},
        {Key::F7, CSI "18;" MC "~"},
        {Key::F8, CSI "19;" MC "~"},
        {Key::F9, CSI "20;" MC "~"},
        {Key::F10, CSI "21;" MC "~"},
        {Key::F11, CSI "23;" MC "~"},
        {Key::F12, CSI "24;" MC "~"},
        {Key::F13, CSI "25;" MC "~"},
        {Key::F14, CSI "26;" MC "~"},
        {Key::F15, CSI "28;" MC "~"},
        {Key::F16, CSI "29;" MC "~"},
        {Key::F17, CSI "31;" MC "~"},
        {Key::F18, CSI "32;" MC "~"},
        {Key::F19, CSI "33;" MC "~"},
        {Key::F20, CSI "34;" MC "~"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Ansi_KeypadKeys[] = {
        {Key::KP_Space, " "},
        {Key::KP_Tab, "\t"},
        {Key::KP_Enter, "\r"},
        {Key::KP_Star, "*"},
        {Key::KP_Plus, "+"},
        {Key::KP_Comma, ","},
        {Key::KP_Minus, "-"},
        {Key::KP_Slash, "/"},
        {Key::KP_Delete, "."},
        {Key::KP_Dot, "."},
        {Key::KP_Insert, "0"},
        {Key::KP_0, "0"},
        {Key::KP_End, "1"},
        {Key::KP_1, "1"},
        {Key::KP_Down, "2"},
        {Key::KP_2, "2"},
        {Key::KP_PageDown, "3"},
        {Key::KP_3, "3"},
        {Key::KP_Left, "4"},
        {Key::KP_4, "4"},
        {Key::KP_Begin, "5"},
        {Key::KP_5, "5"},
        {Key::KP_Right, "6"},
        {Key::KP_6, "6"},
        {Key::KP_Home, "7"},
        {Key::KP_7, "7"},
        {Key::KP_Up, "8"},
        {Key::KP_8, "8"},
        {Key::KP_PageUp, "9"},
        {Key::KP_9, "9"},
        {Key::KP_Equal, "="},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Appl_KeypadKeys[] = {
        {Key::KP_Space, SS3 " "},
        {Key::KP_Tab, SS3 "I"},
        {Key::KP_Enter, SS3 "M"},
        {Key::KP_Star, SS3 "j"},
        {Key::KP_Plus, SS3 "k"},
        {Key::KP_Comma, SS3 "l"},
        {Key::KP_Minus, SS3 "m"},
        {Key::KP_Delete, SS3 "n"},
        {Key::KP_Dot, SS3 "n"},
        {Key::KP_Slash, SS3 "o"},
        {Key::KP_Insert, SS3 "p"},
        {Key::KP_0, SS3 "p"},
        {Key::KP_End, SS3 "q"},
        {Key::KP_1, SS3 "q"},
        {Key::KP_Down, SS3 "r"},
        {Key::KP_2, SS3 "r"},
        {Key::KP_PageDown, SS3 "s"},
        {Key::KP_3, SS3 "s"},
        {Key::KP_Left, SS3 "t"},
        {Key::KP_4, SS3 "t"},
        {Key::KP_Begin, SS3 "u"},
        {Key::KP_5, SS3 "u"},
        {Key::KP_Right, SS3 "v"},
        {Key::KP_6, SS3 "v"},
        {Key::KP_Home, SS3 "w"},
        {Key::KP_7, SS3 "w"},
        {Key::KP_Up, SS3 "x"},
        {Key::KP_8, SS3 "x"},
        {Key::KP_PageUp, SS3 "y"},
        {Key::KP_9, SS3 "y"},
        {Key::KP_Equal, SS3 "X"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Mod_Appl_KeypadKeys[] = {
        {Key::KP_Space, SS3 MC " "},
        {Key::KP_Tab, SS3 MC "I"},
        {Key::KP_Enter, SS3 MC "M"},
        {Key::KP_Star, SS3 MC "j"},
        {Key::KP_Plus, SS3 MC "k"},
        {Key::KP_Comma, SS3 MC "l"},
        {Key::KP_Minus, SS3 MC "m"},
        {Key::KP_Delete, SS3 MC "n"},
        {Key::KP_Dot, SS3 MC "n"},
        {Key::KP_Slash, SS3 MC "o"},
        {Key::KP_Insert, SS3 MC "p"},
        {Key::KP_0, SS3 MC "p"},
        {Key::KP_End, SS3 MC "q"},
        {Key::KP_1, SS3 MC "q"},
        {Key::KP_Down, SS3 MC "r"},
        {Key::KP_2, SS3 MC "r"},
        {Key::KP_PageDown, SS3 MC "s"},
        {Key::KP_3, SS3 MC "s"},
        {Key::KP_Left, SS3 MC "t"},
        {Key::KP_4, SS3 MC "t"},
        {Key::KP_Begin, SS3 MC "u"},
        {Key::KP_5, SS3 MC "u"},
        {Key::KP_Right, SS3 MC "v"},
        {Key::KP_6, SS3 MC "v"},
        {Key::KP_Home, SS3 MC "w"},
        {Key::KP_7, SS3 MC "w"},
        {Key::KP_Up, SS3 MC "x"},
        {Key::KP_8, SS3 MC "x"},
        {Key::KP_PageUp, SS3 MC "y"},
        {Key::KP_9, SS3 MC "y"},
        {Key::KP_Equal, SS3 MC "X"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_VT52_KeypadKeys[] = {
        {Key::KP_Space, ESC "? "},
        {Key::KP_Tab, ESC "?I"},
        {Key::KP_Enter, ESC "?M"},
        {Key::KP_Star, ESC "?j"},
        {Key::KP_Plus, ESC "?k"},
        {Key::KP_Comma, ESC "?l"},
        {Key::KP_Minus, ESC "?m"},
        {Key::KP_Delete, ESC "?n"},
        {Key::KP_Dot, ESC "?n"},
        {Key::KP_Slash, ESC "?o"},
        {Key::KP_Insert, ESC "?p"},
        {Key::KP_0, ESC "?p"},
        {Key::KP_End, ESC "?q"},
        {Key::KP_1, ESC "?q"},
        {Key::KP_Down, ESC "?r"},
        {Key::KP_2, ESC "?r"},
        {Key::KP_PageDown, ESC "?s"},
        {Key::KP_3, ESC "?s"},
        {Key::KP_Left, ESC "?t"},
        {Key::KP_4, ESC "?t"},
        {Key::KP_Begin, ESC "?u"},
        {Key::KP_5, ESC "?u"},
        {Key::KP_Right, ESC "?v"},
        {Key::KP_6, ESC "?v"},
        {Key::KP_Home, ESC "?w"},
        {Key::KP_7, ESC "?w"},
        {Key::KP_Up, ESC "?x"},
        {Key::KP_8, ESC "?x"},
        {Key::KP_PageUp, ESC "?y"},
        {Key::KP_9, ESC "?y"},
        {Key::KP_Equal, ESC "?X"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_VT52_FunctionKeys[] = {
        {Key::F1, ESC "P"},
        {Key::KP_F1, ESC "P"},
        {Key::F2, ESC "Q"},
        {Key::KP_F2, ESC "Q"},
        {Key::F3, ESC "R"},
        {Key::KP_F3, ESC "R"},
        {Key::F4, ESC "S"},
        {Key::KP_F4, ESC "S"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Ansi_CursorKeys[] = {
        {Key::Up, CSI "A"},
        {Key::Down, CSI "B"},
        {Key::Right, CSI "C"},
        {Key::Left, CSI "D"},
        {Key::Home, CSI "H"},
        {Key::End, CSI "F"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Appl_CursorKeys[] = {
        {Key::Up, SS3 "A"},
        {Key::Down, SS3 "B"},
        {Key::Right, SS3 "C"},
        {Key::Left, SS3 "D"},
        {Key::Home, SS3 "H"},
        {Key::End, SS3 "F"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Mod_CursorKeys[] = {
        {Key::Up, CSI "1;" MC "A"},
        {Key::Down, CSI "1;" MC "B"},
        {Key::Right, CSI "1;" MC "C"},
        {Key::Left, CSI "1;" MC "D"},
        {Key::Home, CSI "1;" MC "H"},
        {Key::End, CSI "1;" MC "F"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_VT52_CursorKeys[] = {
        {Key::Up, ESC "A"},
        {Key::Down, ESC "B"},
        {Key::Right, ESC "C"},
        {Key::Left, ESC "D"},
        {Key::Home, ESC "H"},
        {Key::End, ESC "F"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_ReturnKey_ANL[] = {
        {Key::Return, "\r\n"},
        {Key::KP_Enter, "\r\n"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_BackspaceKey_BkSp[] = {
        {Key::Backspace, "\b"},
        {Key::NONE, nullptr},
    };

    const InputSpec is_Alt_BackspaceKey_BkSp[] = {
        {Key::Backspace, ESC "\b"},
        {Key::NONE, nullptr},
    };

#undef ESC
#undef CSI
#undef SS3

    inline u8 getModifierCode(VtModifier modifiers) {
        switch (modifiers) {
            case VtModifier::none:
                return 0;
            case VtModifier::shift:
                return 2;
            case VtModifier::alt:
                return 3;
            case VtModifier::shift_alt:
                return 4;
            case VtModifier::control:
                return 5;
            case VtModifier::shift_control:
                return 6;
            case VtModifier::control_alt:
                return 7;
            case VtModifier::shift_control_alt:
                return 8;
        }
        return 0;
    }

    struct KittyKeySpec {
        u32 code = 0;
        char final = 'u';
    };

    bool isKittyModifierKey(VtKey key) {
        return key >= VtKey::LeftShift && key <= VtKey::RightSuper;
    }

    bool isKittyRecoveryKey(VtKey key) {
        return key == VtKey::Return || key == VtKey::Tab || key == VtKey::Backspace;
    }

    VtModifier kittyToLegacyModifiers(u16 modifiers) {
        VtModifier result = VtModifier::none;
        if (modifiers & 1) {
            result = result | VtModifier::shift;
        }
        if (modifiers & 2) {
            result = result | VtModifier::alt;
        }
        if (modifiers & 4) {
            result = result | VtModifier::control;
        }
        return result;
    }

    bool validKittyAssociatedText(u32 codepoint) {
        return codepoint >= 0x20 && !(codepoint >= 0x7f && codepoint <= 0x9f);
    }

    KittyKeySpec kittyKeySpec(VtKey key) {
        using Key = VtKey;
        switch (key) {
            case Key::Return:
                return {13, 'u'};
            case Key::Backspace:
                return {127, 'u'};
            case Key::Tab:
                return {9, 'u'};
            case Key::Insert:
                return {2, '~'};
            case Key::Delete:
                return {3, '~'};
            case Key::Up:
                return {1, 'A'};
            case Key::Down:
                return {1, 'B'};
            case Key::Right:
                return {1, 'C'};
            case Key::Left:
                return {1, 'D'};
            case Key::Home:
                return {1, 'H'};
            case Key::End:
                return {1, 'F'};
            case Key::PageUp:
                return {5, '~'};
            case Key::PageDown:
                return {6, '~'};
            case Key::F1:
            case Key::KP_F1:
                return {1, 'P'};
            case Key::F2:
            case Key::KP_F2:
                return {1, 'Q'};
            case Key::F3:
            case Key::KP_F3:
                return {1, 'R'};
            case Key::F4:
            case Key::KP_F4:
                return {1, 'S'};
            case Key::F5:
                return {15, '~'};
            case Key::F6:
                return {17, '~'};
            case Key::F7:
                return {18, '~'};
            case Key::F8:
                return {19, '~'};
            case Key::F9:
                return {20, '~'};
            case Key::F10:
                return {21, '~'};
            case Key::F11:
                return {23, '~'};
            case Key::F12:
                return {24, '~'};
            case Key::F13:
                return {57376, 'u'};
            case Key::F14:
                return {57377, 'u'};
            case Key::F15:
                return {57378, 'u'};
            case Key::F16:
                return {57379, 'u'};
            case Key::F17:
                return {57380, 'u'};
            case Key::F18:
                return {57381, 'u'};
            case Key::F19:
                return {57382, 'u'};
            case Key::F20:
                return {57383, 'u'};
            case Key::KP_0:
                return {57399, 'u'};
            case Key::KP_1:
                return {57400, 'u'};
            case Key::KP_2:
                return {57401, 'u'};
            case Key::KP_3:
                return {57402, 'u'};
            case Key::KP_4:
                return {57403, 'u'};
            case Key::KP_5:
                return {57404, 'u'};
            case Key::KP_6:
                return {57405, 'u'};
            case Key::KP_7:
                return {57406, 'u'};
            case Key::KP_8:
                return {57407, 'u'};
            case Key::KP_9:
                return {57408, 'u'};
            case Key::KP_Dot:
                return {57409, 'u'};
            case Key::KP_Slash:
                return {57410, 'u'};
            case Key::KP_Star:
                return {57411, 'u'};
            case Key::KP_Minus:
                return {57412, 'u'};
            case Key::KP_Plus:
                return {57413, 'u'};
            case Key::KP_Enter:
                return {57414, 'u'};
            case Key::KP_Equal:
                return {57415, 'u'};
            case Key::KP_Comma:
                return {57416, 'u'};
            case Key::KP_Left:
                return {57417, 'u'};
            case Key::KP_Right:
                return {57418, 'u'};
            case Key::KP_Up:
                return {57419, 'u'};
            case Key::KP_Down:
                return {57420, 'u'};
            case Key::KP_PageUp:
                return {57421, 'u'};
            case Key::KP_PageDown:
                return {57422, 'u'};
            case Key::KP_Home:
                return {57423, 'u'};
            case Key::KP_End:
                return {57424, 'u'};
            case Key::KP_Insert:
                return {57425, 'u'};
            case Key::KP_Delete:
                return {57426, 'u'};
            case Key::KP_Begin:
                return {57427, 'u'};
            case Key::CapsLock:
                return {57358, 'u'};
            case Key::ScrollLock:
                return {57359, 'u'};
            case Key::NumLock:
                return {57360, 'u'};
            case Key::Print:
                return {57361, 'u'};
            case Key::Pause:
                return {57362, 'u'};
            case Key::Menu:
                return {57363, 'u'};
            case Key::LeftShift:
                return {57441, 'u'};
            case Key::LeftControl:
                return {57442, 'u'};
            case Key::LeftAlt:
                return {57443, 'u'};
            case Key::LeftSuper:
                return {57444, 'u'};
            case Key::RightShift:
                return {57447, 'u'};
            case Key::RightControl:
                return {57448, 'u'};
            case Key::RightAlt:
                return {57449, 'u'};
            case Key::RightSuper:
                return {57450, 'u'};
            default:
                return {};
        }
    }

    void makePalette256(Color p[]) {
        opts.getColor("color0", p[0]);
        opts.getColor("color1", p[1]);
        opts.getColor("color2", p[2]);
        opts.getColor("color3", p[3]);
        opts.getColor("color4", p[4]);
        opts.getColor("color5", p[5]);
        opts.getColor("color6", p[6]);
        opts.getColor("color7", p[7]);
        opts.getColor("color8", p[8]);
        opts.getColor("color9", p[9]);
        opts.getColor("color10", p[10]);
        opts.getColor("color11", p[11]);
        opts.getColor("color12", p[12]);
        opts.getColor("color13", p[13]);
        opts.getColor("color14", p[14]);
        opts.getColor("color15", p[15]);

        for (u8 r = 0; r < 6; ++r) {
            for (u8 g = 0; g < 6; ++g) {
                for (u8 b = 0; b < 6; ++b) {
                    u8 ri = r ? 55 + 40 * r : 0;
                    u8 gi = g ? 55 + 40 * g : 0;
                    u8 bi = b ? 55 + 40 * b : 0;
                    p[16 + 36 * r + 6 * g + b] = {ri, gi, bi};
                }
            }
        }

        for (u8 s = 0; s < 24; ++s) {
            u8 i = 8 + 10 * s;
            p[232 + s] = {i, i, i};
        }
    }

    /* These tables perform translation of built-in "hard" character sets
    * to 16-bit Unicode points. All sets are defined as 96 characters, even
    * those originally designated by DEC as 94-character sets.
    *
    * These tables are referenced by VtermImpl::charCodes (see below).
    */

    const u16 uc_DecSpec[] = {
        0x0020,
        0x0021,
        0x0022,
        0x0023,
        0x0024,
        0x0025,
        0x0026,
        0x0027,
        0x0028,
        0x0029,
        0x002a,
        0x002b,
        0x002c,
        0x002d,
        0x002e,
        0x002f,
        0x0030,
        0x0031,
        0x0032,
        0x0033,
        0x0034,
        0x0035,
        0x0036,
        0x0037,
        0x0038,
        0x0039,
        0x003a,
        0x003b,
        0x003c,
        0x003d,
        0x003e,
        0x003f,

        0x0040,
        0x0041,
        0x0042,
        0x0043,
        0x0044,
        0x0045,
        0x0046,
        0x0047,
        0x0048,
        0x0049,
        0x004a,
        0x004b,
        0x004c,
        0x004d,
        0x004e,
        0x004f,
        0x0050,
        0x0051,
        0x0052,
        0x0053,
        0x0054,
        0x0055,
        0x0056,
        0x0057,
        0x0058,
        0x0059,
        0x005a,
        0x005b,
        0x005c,
        0x005d,
        0x005e,
        0x00a0,

        0x25c6,
        0x2592,
        0x2409,
        0x240c,
        0x240d,
        0x240a,
        0x00b0,
        0x00b1,
        0x2424,
        0x240b,
        0x2518,
        0x2510,
        0x250c,
        0x2514,
        0x253c,
        0x23ba,
        0x23bb,
        0x2500,
        0x23bc,
        0x23bd,
        0x251c,
        0x2524,
        0x2534,
        0x252c,
        0x2502,
        0x2264,
        0x2265,
        0x03c0,
        0x2260,
        0x00a3,
        0x00b7,
        0x0020,
    };

    const u16 uc_DecSuppl[] = {
        0x0020,
        0x00a1,
        0x00a2,
        0x00a3,
        0x0024,
        0x00a5,
        0x0026,
        0x00a7,
        0x00a4,
        0x00a9,
        0x00aa,
        0x00ab,
        0x002c,
        0x002d,
        0x002e,
        0x002f,
        0x00b0,
        0x00b1,
        0x00b2,
        0x00b3,
        0x0034,
        0x00b5,
        0x00b6,
        0x00b7,
        0x0038,
        0x00b9,
        0x00ba,
        0x00bb,
        0x00bc,
        0x00bd,
        0x003e,
        0x00bf,

        0x00c0,
        0x00c1,
        0x00c2,
        0x00c3,
        0x00c4,
        0x00c5,
        0x00c6,
        0x00c7,
        0x00c8,
        0x00c9,
        0x00ca,
        0x00cb,
        0x00cc,
        0x00cd,
        0x00ce,
        0x00cf,
        0x0050,
        0x00d1,
        0x00d2,
        0x00d3,
        0x00d4,
        0x00d5,
        0x00d6,
        0x0152,
        0x00d8,
        0x00d9,
        0x00da,
        0x00db,
        0x00dc,
        0x0178,
        0x005e,
        0x00df,

        0x00e0,
        0x00e1,
        0x00e2,
        0x00e3,
        0x00e4,
        0x00e5,
        0x00e6,
        0x00e7,
        0x00e8,
        0x00e9,
        0x00ea,
        0x00eb,
        0x00ec,
        0x00ed,
        0x00ee,
        0x00ef,
        0x0070,
        0x00f1,
        0x00f2,
        0x00f3,
        0x00f4,
        0x00f5,
        0x00f6,
        0x0153,
        0x00f8,
        0x00f9,
        0x00fa,
        0x00fb,
        0x00fc,
        0x00ff,
        0x007e,
        0x007f,
    };

    const u16 uc_DecTechn[] = {
        0x0020,
        0x23b7,
        0x250c,
        0x2500,
        0x2320,
        0x2321,
        0x2502,
        0x23a1,
        0x23a3,
        0x23a4,
        0x23a6,
        0x239b,
        0x239d,
        0x239e,
        0x23a0,
        0x23a8,
        0x23ac,
        0x0020,
        0x0020,
        0x0020,
        0x0020,
        0x0020,
        0x0020,
        0x0020,
        0x0020,
        0x0020,
        0x0020,
        0x0020,
        0x2264,
        0x2260,
        0x2265,
        0x222b,

        0x2234,
        0x221d,
        0x221e,
        0x00f7,
        0x0394,
        0x2207,
        0x03a6,
        0x0393,
        0x223c,
        0x2243,
        0x0398,
        0x00d7,
        0x039b,
        0x21d4,
        0x21d2,
        0x2261,
        0x03a0,
        0x03a8,
        0x0020,
        0x03a3,
        0x0020,
        0x0020,
        0x221a,
        0x03a9,
        0x039e,
        0x03a5,
        0x2282,
        0x2283,
        0x2229,
        0x222a,
        0x2227,
        0x2228,

        0x00ac,
        0x03b1,
        0x03b2,
        0x03c7,
        0x03b4,
        0x03b5,
        0x03c6,
        0x03b3,
        0x03b7,
        0x03b9,
        0x03b8,
        0x03ba,
        0x03bb,
        0x0020,
        0x03bd,
        0x2202,
        0x03c0,
        0x03c8,
        0x03c1,
        0x03c3,
        0x03c4,
        0x0020,
        0x0192,
        0x03c9,
        0x03be,
        0x03c5,
        0x03b6,
        0x2190,
        0x2191,
        0x2192,
        0x2193,
        0x007f,
    };

    const u16 uc_IsoLatin1[] = {
        0x00a0,
        0x00a1,
        0x00a2,
        0x00a3,
        0x00a4,
        0x00a5,
        0x00a6,
        0x00a7,
        0x00a8,
        0x00a9,
        0x00aa,
        0x00ab,
        0x00ac,
        0x00ad,
        0x00ae,
        0x00af,
        0x00b0,
        0x00b1,
        0x00b2,
        0x00b3,
        0x00b4,
        0x00b5,
        0x00b6,
        0x00b7,
        0x00b8,
        0x00b9,
        0x00ba,
        0x00bb,
        0x00bc,
        0x00bd,
        0x00be,
        0x00bf,

        0x00c0,
        0x00c1,
        0x00c2,
        0x00c3,
        0x00c4,
        0x00c5,
        0x00c6,
        0x00c7,
        0x00c8,
        0x00c9,
        0x00ca,
        0x00cb,
        0x00cc,
        0x00cd,
        0x00ce,
        0x00cf,
        0x00d0,
        0x00d1,
        0x00d2,
        0x00d3,
        0x00d4,
        0x00d5,
        0x00d6,
        0x00d7,
        0x00d8,
        0x00d9,
        0x00da,
        0x00db,
        0x00dc,
        0x00dd,
        0x00de,
        0x00df,

        0x00e0,
        0x00e1,
        0x00e2,
        0x00e3,
        0x00e4,
        0x00e5,
        0x00e6,
        0x00e7,
        0x00e8,
        0x00e9,
        0x00ea,
        0x00eb,
        0x00ec,
        0x00ed,
        0x00ee,
        0x00ef,
        0x00f0,
        0x00f1,
        0x00f2,
        0x00f3,
        0x00f4,
        0x00f5,
        0x00f6,
        0x00f7,
        0x00f8,
        0x00f9,
        0x00fa,
        0x00fb,
        0x00fc,
        0x00fd,
        0x00fe,
        0x00ff,
    };

    const u16 uc_IsoUK[] = {
        0x0020,
        0x0021,
        0x0022,
        0x00a3,
        0x0024,
        0x0025,
        0x0026,
        0x0027,
        0x0028,
        0x0029,
        0x002a,
        0x002b,
        0x002c,
        0x002d,
        0x002e,
        0x002f,
        0x0030,
        0x0031,
        0x0032,
        0x0033,
        0x0034,
        0x0035,
        0x0036,
        0x0037,
        0x0038,
        0x0039,
        0x003a,
        0x003b,
        0x003c,
        0x003d,
        0x003e,
        0x003f,

        0x0040,
        0x0041,
        0x0042,
        0x0043,
        0x0044,
        0x0045,
        0x0046,
        0x0047,
        0x0048,
        0x0049,
        0x004a,
        0x004b,
        0x004c,
        0x004d,
        0x004e,
        0x004f,
        0x0050,
        0x0051,
        0x0052,
        0x0053,
        0x0054,
        0x0055,
        0x0056,
        0x0057,
        0x0058,
        0x0059,
        0x005a,
        0x005b,
        0x005c,
        0x005d,
        0x005e,
        0x005f,

        0x0060,
        0x0061,
        0x0062,
        0x0063,
        0x0064,
        0x0065,
        0x0066,
        0x0067,
        0x0068,
        0x0069,
        0x006a,
        0x006b,
        0x006c,
        0x006d,
        0x006e,
        0x006f,
        0x0070,
        0x0071,
        0x0072,
        0x0073,
        0x0074,
        0x0075,
        0x0076,
        0x0077,
        0x0078,
        0x0079,
        0x007a,
        0x007b,
        0x007c,
        0x007d,
        0x007e,
        0x007f,
    };

}

const u16* VtermImpl::charCodes[] = {

    nullptr,
    uc_DecSpec,
    uc_DecSuppl,
    uc_DecSuppl,
    uc_DecTechn,
    uc_IsoLatin1,
    uc_IsoUK
};

u32 VtermImpl::translateCharset(Charset charset, unsigned char ch) const {
    if (charset <= Charset::IsoUK) {
        return charCodes[(u8)(charset)][ch - 32];
    }
    if (!nationalReplacementMode) {
        return ch;
    }

    const auto lookup = [ch](const std::pair<u8, u16>* table, size_t size) -> u32 {
        for (size_t index = 0; index < size; ++index) {
            if (table[index].first == ch) {
                return table[index].second;
            }
        }
        return ch;
    };
#define NRC_TABLE(name, ...) static const std::pair<u8, u16> name[] = {__VA_ARGS__}
    NRC_TABLE(dutch, {'#', 0x00a3}, {'@', 0x00be}, {'[', 0x0133}, {'\\', 0x00bd}, {']', 0x007c}, {'{', 0x00a8}, {'|', 0x0192}, {'}', 0x00bc}, {'~', 0x00b4});
    NRC_TABLE(finnish, {'[', 0x00c4}, {'\\', 0x00d6}, {']', 0x00c5}, {'^', 0x00dc}, {'`', 0x00e9}, {'{', 0x00e4}, {'|', 0x00f6}, {'}', 0x00e5}, {'~', 0x00fc});
    NRC_TABLE(french, {'#', 0x00a3}, {'@', 0x00e0}, {'[', 0x00b0}, {'\\', 0x00e7}, {']', 0x00a7}, {'{', 0x00e9}, {'|', 0x00f9}, {'}', 0x00e8}, {'~', 0x00a8});
    NRC_TABLE(frenchCanadian, {'@', 0x00e0}, {'[', 0x00e2}, {'\\', 0x00e7}, {']', 0x00ea}, {'^', 0x00ee}, {'`', 0x00f4}, {'{', 0x00e9}, {'|', 0x00f9}, {'}', 0x00e8}, {'~', 0x00fb});
    NRC_TABLE(german, {'@', 0x00a7}, {'[', 0x00c4}, {'\\', 0x00d6}, {']', 0x00dc}, {'{', 0x00e4}, {'|', 0x00f6}, {'}', 0x00fc}, {'~', 0x00df});
    NRC_TABLE(italian, {'#', 0x00a3}, {'@', 0x00a7}, {'[', 0x00b0}, {'\\', 0x00e7}, {']', 0x00e9}, {'`', 0x00f9}, {'{', 0x00e0}, {'|', 0x00f2}, {'}', 0x00e8}, {'~', 0x00ec});
    NRC_TABLE(norwegian, {'@', 0x00c4}, {'[', 0x00c6}, {'\\', 0x00d8}, {']', 0x00c5}, {'^', 0x00dc}, {'`', 0x00e4}, {'{', 0x00e6}, {'|', 0x00f8}, {'}', 0x00e5}, {'~', 0x00fc});
    NRC_TABLE(portuguese, {'[', 0x00c3}, {'\\', 0x00c7}, {']', 0x00d5}, {'{', 0x00e3}, {'|', 0x00e7}, {'}', 0x00f5});
    NRC_TABLE(spanish, {'#', 0x00a3}, {'@', 0x00a7}, {'[', 0x00a1}, {'\\', 0x00d1}, {']', 0x00bf}, {'{', 0x00b0}, {'|', 0x00f1}, {'}', 0x00e7});
    NRC_TABLE(swedish, {'@', 0x00c9}, {'[', 0x00c4}, {'\\', 0x00d6}, {']', 0x00c5}, {'^', 0x00dc}, {'`', 0x00e9}, {'{', 0x00e4}, {'|', 0x00f6}, {'}', 0x00e5}, {'~', 0x00fc});
    NRC_TABLE(swiss, {'#', 0x00f9}, {'@', 0x00e0}, {'[', 0x00e9}, {'\\', 0x00e7}, {']', 0x00ea}, {'^', 0x00ee}, {'_', 0x00e8}, {'`', 0x00f4}, {'{', 0x00e4}, {'|', 0x00f6}, {'}', 0x00fc}, {'~', 0x00fb});
    NRC_TABLE(serboCroatian, {'@', 0x017d}, {'[', 0x0160}, {'\\', 0x0110}, {']', 0x0106}, {'^', 0x010c}, {'`', 0x017e}, {'{', 0x0161}, {'|', 0x0111}, {'}', 0x0107}, {'~', 0x010d});
    NRC_TABLE(turkish, {'&', 0x011f}, {'@', 0x0130}, {'[', 0x015e}, {'\\', 0x00d6}, {']', 0x00c7}, {'^', 0x00dc}, {'`', 0x011e}, {'{', 0x015f}, {'|', 0x00f6}, {'}', 0x00e7}, {'~', 0x00fc});
#undef NRC_TABLE

#define LOOKUP(name) return lookup(name, sizeof(name) / sizeof(name[0]))
    switch (charset) {
        case Charset::NrcDutch:
            LOOKUP(dutch);
        case Charset::NrcFinnish:
            LOOKUP(finnish);
        case Charset::NrcFrench:
            LOOKUP(french);
        case Charset::NrcFrenchCanadian:
            LOOKUP(frenchCanadian);
        case Charset::NrcGerman:
            LOOKUP(german);
        case Charset::NrcItalian:
            LOOKUP(italian);
        case Charset::NrcNorwegianDanish:
            LOOKUP(norwegian);
        case Charset::NrcPortuguese:
            LOOKUP(portuguese);
        case Charset::NrcSpanish:
            LOOKUP(spanish);
        case Charset::NrcSwedish:
            LOOKUP(swedish);
        case Charset::NrcSwiss:
            LOOKUP(swiss);
        case Charset::NrcSerboCroatian:
            LOOKUP(serboCroatian);
        case Charset::NrcTurkish:
            LOOKUP(turkish);
        case Charset::NrcGreek: {
            static const u16 greek[] = {0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397, 0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x03a7, 0x039f, 0x03a0, 0x03a1, 0x03a3, 0x03a4, 0x03a5, 0x03a6, 0x039e, 0x03a8, 0x03a9};
            return ch >= 'a' && ch <= 'x' ? greek[ch - 'a'] : ch;
        }
        case Charset::NrcHebrew:
            return ch >= '`' && ch <= 'z' ? 0x05d0 + ch - '`' : ch;
        case Charset::NrcRussian: {
            static const u16 russian[] = {0x042e, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413, 0x0425, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f, 0x042f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412, 0x042c, 0x042b, 0x0417, 0x0428, 0x042d, 0x0429, 0x0427};
            return ch >= '`' && ch <= '~' ? russian[ch - '`'] : ch;
        }
        default:
            return ch;
    }
#undef LOOKUP
}

VtermImpl::VtermImpl(VtermHost& host_, Pty& pty_, Output* dump_, u16 glyphPx_, u16 glyphPy_, u16 winPx_, u16 winPy_)
    : host(host_)
    , pty(pty_)
    , dump(dump_)
    , winPx(winPx_)
    , winPy(winPy_)
    , nCols((winPx - 2 * opts.border) / glyphPx_)
    , nRows((winPy - 2 * opts.border) / glyphPy_)
    , glyphPx(glyphPx_)
    , glyphPy(glyphPy_)
    , frame_pri(winPx, winPy, nCols, nRows, marginTop, marginBottom, &colors, opts.saveLines)
    , cf(&frame_pri)
    , utf8dec([this]() {
        placeGraphicChar();
    })
    , nColsEff(nCols)
    , hMargin(0)
{
    makePalette256(colors.palette);
    std::copy(std::begin(colors.palette), std::end(colors.palette), std::begin(originalPalette256));
    colors.defaultForeground = opts.fg;
    colors.defaultBackground = opts.bg;
    cursorColor = opts.cr;
    selectionFgColor = opts.fg;
    selectionBgColor = opts.bg;
    initialModifyKeyResources[0] = 0;
    initialModifyKeyResources[1] = 2;
    initialModifyKeyResources[2] = 2;
    initialModifyKeyResources[3] = 0;
    initialModifyKeyResources[4] = opts.modifyOtherKeys;
    initialModifyKeyResources[6] = 0;
    initialModifyKeyResources[7] = 0;
    windowTitle = opts.title;
    iconTitle = opts.title;

    defaultFgPalIx = -1;
    defaultBgPalIx = -1;
    fgPalIx = defaultFgPalIx;
    bgPalIx = defaultBgPalIx;

    resetTerminal();
}

bool VtermImpl::servicePty(bool readable, bool writable) {
    // Bytes already queued by the frontend precede replies generated while
    // parsing newly readable PTY input.
    if (writable) {
        flushPtyOutput();
    }
    return readable && readPty();
}

void VtermImpl::resize(u16 winPx_, u16 winPy_) {
    if (winPx == winPx_ && winPy == winPy_) {
        return;
    }
    winPx = winPx_;
    winPy = winPy_;

    u16 nCols_ = std::max(1, (winPx - 2 * opts.border) / glyphPx);
    u16 nRows_ = std::max(1, (winPy - 2 * opts.border) / glyphPy);

    if (nCols == nCols_ && nRows == nRows_) {
        cf->winPx = winPx;
        cf->winPy = winPy;
        if (inBandResizeMode) {
            reportInBandResize();
        }
        return;
    }

    hideCursor();

    if (nRows_ < posY + 1) {
        // Preserve every row above the cursor that still fits.  Scrolling
        // by the full height delta needlessly discards additional rows
        // whenever the cursor is not on the old bottom row.
        const u16 nScroll = posY + 1 - nRows_;
        cf->scrollUp(0, nRows, nScroll);
        posY -= nScroll;
    }

    // Both buffers have real history and must obey the same resize contract.
    // Keeping the inactive alternate allocation also lets mode 47 restore it
    // after a primary-screen resize instead of dereferencing freed storage.
    cf->resize(winPx, winPy, nCols_, nRows_, marginTop, marginBottom);

    if (nRows < nRows_) {
        const int nScroll = std::min(nRows_ - nRows, (int)(cf->getHistoryRows()));
        cf->restoreHistory(nScroll);
        posY += nScroll;
    }
    nCols = nCols_;
    nRows = nRows_;

    // Frame::resize resets the vertical scrolling region.  Reset the
    // horizontal region to the resized page as well; retaining a clipped
    // right edge made subsequent growth keep a stale narrow region.
    nColsEff = nCols;
    hMargin = 0;
    normalizeCursorPos();
    showCursor();

    pty.resize(nCols, nRows);
    if (inBandResizeMode) {
        reportInBandResize();
    }
}

std::string VtermImpl::getLocalEcho(const u8* const begin, const u8* const end) {
    std::ostringstream oss;
    for (const u8* p = begin; p < end; ++p) {
        if (*p == '\r' || *p >= ' ') {
            oss << (char)*p;
        } else {
            oss << '^' << (char)(*p + 0x40);
        }
    }
    return oss.str();
}

int VtermImpl::writePty(VtKey key, VtModifier modifiers_, bool userInput) {
#ifdef DEBUG
    if (key == VtKey::Print) {
        debugKey();
        return 0;
    }
#endif
    const auto userDefined = userDefinedKeys.find(key);
    if (userDefined != userDefinedKeys.end()) {
        return writePty(userDefined->second.data(), userDefined->second.size(), userInput);
    }
    modifiers = modifiers_;
    const auto& spec = getInputSpec(key);
    if (modifiers == VtModifier::none) {
        return writePty(spec.input, spec.getLength(), userInput);
    } else {
        static u8 buf[32];
        int k = 0;
        const char* end = spec.input + spec.getLength();
        for (const char* p = spec.input; p != end; ++p) {
            if (*p == *MC) {
                buf[k++] = '0' + getModifierCode(modifiers);
            } else {
                buf[k++] = *p;
            }
        }
        buf[k] = '\0';
        return writePty(buf, k, userInput);
    }
}

int VtermImpl::writePty(u8 ch, VtModifier modifiers, bool userInput) {
    using VM = VtModifier;

    auto uch = &ch;
    logT << "pty write (mod=" << (int)modifiers << "): " << dumpBuffer(uch, uch + 1);

    const auto& mod2_encode = [&](u8 ch) {
        const char* exempt = "!#$%&*()-+=?.,:;<>'\"";
        auto x = (char*)(exempt);

        while (*x) {
            if (ch == *x++) {
                return (modifiers & VM::control_alt) != VM::none;
            }
        }

        return modifiers != VM::none;
    };

    if (eightBitInput && (modifiers & VM::alt) != VM::none) {
        ch |= 0x80;
        return writePty(&ch, 1, userInput);
    } else if ((modifyOtherKeys == 2 && mod2_encode(ch)) || (modifyOtherKeys == 1 && (modifiers & VM::control) != VM::none && ch > ' ')) {
        if (ch < ' ' && (modifiers & VM::control) != VM::none) {
            const char* ctrlmap = ((modifiers & VM::shift) != VM::none) ? "@ABCDEFGHIJKLMNOPQRSTUVWXYZ{|}^/" : " abcdefghijklmnopqrstuvwxyz[\\]^/";
            ch = ctrlmap[ch];
        }

        u8 wbuf[16] = {'\x1b', '[', '2', '7', ';', '_', ';'};
        wbuf[5] = '0' + getModifierCode(modifiers);
        u8 pos = 7;

        if (ch > 99) {
            wbuf[pos] = ch / 100;
            ch -= 100 * wbuf[pos];
            wbuf[pos] += '0';
            ++pos;
        }
        if (pos > 7 || ch > 9) {
            wbuf[pos] = ch / 10;
            ch -= 10 * wbuf[pos];
            wbuf[pos] += '0';
            ++pos;
        }
        wbuf[pos++] = '0' + ch;
        wbuf[pos++] = '~';
        wbuf[pos] = '\0';

        return writePty(wbuf, pos, userInput);
    } else if ((modifiers & VM::alt) != VM::none) {
        if (altSendsEscape) {
            static u8 wbuf[2] = {'\x1b', '\0'};
            wbuf[1] = ch;
            return writePty(wbuf, 2, userInput);
        } else {
            std::vector<char> utf8_out;
            auto sinkFn = [&](char ch) {
                utf8_out.push_back(ch);
            };
            Utf8Encoder::pushUnicode(ch | 0x80, sinkFn);
            return writePty(utf8_out.data(), utf8_out.size(), userInput);
        }
    } else {
        return writePty(uch, 1, userInput);
    }
}

int VtermImpl::writeKittyKey(VtKey key, u16 modifiers, KeyEventType event) {
    const KittyKeySpec spec = kittyKeySpec(key);
    if (!spec.code) {
        return 0;
    }

    if (isKittyRecoveryKey(key) && !(getKittyKeyboardFlags() & 0x08)) {
        if (event == KeyEventType::Release) {
            return 0;
        }
        return writePty(key, kittyToLegacyModifiers(modifiers), true);
    }

    if (isKittyModifierKey(key) && !(getKittyKeyboardFlags() & 0x08)) {
        return 0;
    }

    if (event == KeyEventType::Release && !(getKittyKeyboardFlags() & 0x02)) {
        return 0;
    }

    std::ostringstream sequence;
    sequence << "\x1b[" << spec.code << ';' << modifiers + 1;
    if (getKittyKeyboardFlags() & 0x02) {
        sequence << ':' << (unsigned)(event);
    }
    if ((getKittyKeyboardFlags() & 0x10) && event != KeyEventType::Release && isKittyRecoveryKey(key) && validKittyAssociatedText(spec.code)) {
        sequence << ';' << spec.code;
    }
    sequence << spec.final;
    const std::string encoded = sequence.str();
    return writePty(encoded.data(), encoded.size(), true);
}

int VtermImpl::writeKittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, KeyEventType event) {
    if (!key || (event == KeyEventType::Release && !(getKittyKeyboardFlags() & 0x02))) {
        return 0;
    }

    std::ostringstream sequence;
    sequence << "\x1b[" << key;
    if (getKittyKeyboardFlags() & 0x04) {
        const u32 alternateShifted = shiftedKey != key ? shiftedKey : 0;
        const u32 alternateBase = baseLayoutKey != key ? baseLayoutKey : 0;
        if (alternateShifted) {
            sequence << ':' << alternateShifted;
            if (alternateBase) {
                sequence << ':' << alternateBase;
            }
        } else if (alternateBase) {
            sequence << "::" << alternateBase;
        }
    }
    sequence << ';' << modifiers + 1;
    if (getKittyKeyboardFlags() & 0x02) {
        sequence << ':' << (unsigned)(event);
    }
    if ((getKittyKeyboardFlags() & 0x10) && event != KeyEventType::Release) {
        const u32 text = (modifiers & 1) && shiftedKey ? shiftedKey : key;
        if (validKittyAssociatedText(text)) {
            sequence << ';' << text;
        }
    }
    sequence << 'u';
    const std::string encoded = sequence.str();
    return writePty(encoded.data(), encoded.size(), true);
}

int VtermImpl::writePty(const char* cstr, bool userInput) {
    return writePty(cstr, strlen(cstr), userInput);
}

int VtermImpl::writePty(const char* data, size_t size, bool userInput) {
    const std::u8string bytes(data, data + size);
    return writePty(bytes.data(), bytes.size(), userInput);
}

void VtermImpl::writeCsiResponse(const std::string& payload) {
    const std::string response = (send8BitControls ? std::string("\x9b") : std::string("\x1b[")) + payload;
    writePty(response.data(), response.size(), false);
}

void VtermImpl::writeDcsResponse(const std::string& payload) {
    const std::string response = (send8BitControls ? std::string("\x90") : std::string("\x1bP")) + payload + (send8BitControls ? std::string("\x9c") : std::string("\x1b\\"));
    writePty(response.data(), response.size(), false);
}

void VtermImpl::writeOscResponse(const std::string& payload) {
    const std::string response = (send8BitControls ? std::string("\x9d") : std::string("\x1b]")) + payload + (send8BitControls ? std::string("\x9c") : std::string("\x1b\\"));
    writePty(response.data(), response.size(), false);
}

int VtermImpl::writePty(const u8* ucstr, size_t len, bool userInput) {
    if (userInput && keyboardLocked) {
        logT << "pty write: discarding due to keyboard lock (DECKAM): " << dumpBuffer(ucstr, ucstr + len);
        return len;
    }

    if (userInput && cf->pageToBottom()) {
        redraw();
    }

    logT << "pty write: " << dumpBuffer(ucstr, ucstr + len);
    if (userInput && localEcho) {
        processInput(getLocalEcho(ucstr, ucstr + len));
    }
    if (ptyOutputOffset == ptyOutput.size()) {
        ptyOutput.clear();
        ptyOutputOffset = 0;
    }
    ptyOutput.insert(ptyOutput.end(), ucstr, ucstr + len);
    flushPtyOutput();
    return len;
}

bool VtermImpl::flushPtyOutput() {
    while (ptyOutputOffset < ptyOutput.size()) {
        const ssize_t count = pty.write(ptyOutput.data() + ptyOutputOffset, ptyOutput.size() - ptyOutputOffset);
        if (count > 0) {
            ptyOutputOffset += (size_t)(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false;
        }
        if (count < 0) {
            SYS_WARN("pty write");
        }
        return false;
    }
    ptyOutput.clear();
    ptyOutputOffset = 0;
    return true;
}

using Key = VtKey;
using Mod = VtModifier;

VtermImpl::InputSpecTable* VtermImpl::getInputSpecTable() {
    static InputSpecTable ist[] = {
        {[this]() {
        return (autoNewlineMode == true);
    }, is_ReturnKey_ANL},

        {[this]() {
        return ((modifiers & Mod::alt) != Mod::none && bkspSendsDel == false);
    }, is_Alt_BackspaceKey_BkSp},

        {[this]() {
        return (modifyOtherKeys == 2 && modifiers != Mod::none);
    }, is_modOtherKeys2},

        {[this]() {
        return (modifyOtherKeys > 0 && modifiers != Mod::none);
    }, is_modOtherKeys},

        {[this]() {
        return (modifyOtherKeys > 0 && (modifiers & Mod::control) != Mod::none);
    }, is_Control_modOtherKeys},

        {[this]() {
        return (altSendsEscape && (modifiers & Mod::control_alt) == Mod::control_alt);
    }, is_ControlAlt_altSendsEscape},

        {[this]() {
        return (altSendsEscape && (modifiers & Mod::alt) != Mod::none);
    }, is_Alt_altSendsEscape},

        {[this]() {
        return ((modifiers & Mod::alt) != Mod::none);
    }, is_Alt},

        {[this]() {
        return ((modifiers & Mod::control) != Mod::none);
    }, is_Control},

        {[this]() {
        return ((modifiers & Mod::shift) != Mod::none);
    }, is_Shift},

        {[this]() {
        return (bkspSendsDel == false);
    }, is_BackspaceKey_BkSp},

        {[this]() {
        return (compatLevel == CompatibilityLevel::VT52 && keypadMode == KeypadMode::Application);
    }, is_VT52_KeypadKeys},
        {[this]() {
        return (compatLevel == CompatibilityLevel::VT52);
    }, is_VT52_CursorKeys},
        {[this]() {
        return (compatLevel == CompatibilityLevel::VT52);
    }, is_VT52_FunctionKeys},

        {[this]() {
        return (modifiers != Mod::none && modifyKeyResources[3] != 0 && keypadMode == KeypadMode::Application);
    }, is_Mod_Appl_KeypadKeys},
        {[this]() {
        return (keypadMode == KeypadMode::Application);
    }, is_Appl_KeypadKeys},
        {[this]() {
        return (modifiers != Mod::none && modifyKeyResources[1] != 0);
    }, is_Mod_CursorKeys},
        {[this]() {
        return (cursorKeyMode == CursorKeyMode::Application);
    }, is_Appl_CursorKeys},

        {[this]() {
        return (modifiers != Mod::none && modifyKeyResources[0] != 0);
    }, is_Mod_Ansi},
        {[this]() {
        return (modifiers != Mod::none && modifyKeyResources[2] != 0);
    }, is_Mod_Ansi_FunctionKeys},

        {[]() {
        return true;
    }, is_Ansi},
        {[]() {
        return true;
    }, is_Ansi_CursorKeys},
        {[]() {
        return true;
    }, is_Ansi_FunctionKeys},
        {[]() {
        return true;
    }, is_Ansi_KeypadKeys},

        {[]() {
        return true;
    }, nullptr}
    };
    return ist;
}

void VtermImpl::resetInputSpecTable() {
    for (InputSpecTable* e = getInputSpecTable(); e->specs != nullptr; ++e) {
        e->visited = false;
    }
}

const VtermImpl::InputSpec* VtermImpl::selectInputSpecs() {
    InputSpecTable* ist = getInputSpecTable();
    for (auto e = ist; e->specs != nullptr; ++e) {
        if (!e->visited) {
            e->visited = true;
            if (e->predicate()) {
                return e->specs;
            }
        }
    }
    return nullptr;
}

const VtermImpl::InputSpec& VtermImpl::getInputSpec(Key key) {
    static InputSpec nullSpec = {Key::NONE, ""};

    resetInputSpecTable();
    const InputSpec* specs;
    while ((specs = selectInputSpecs()) != nullptr) {
        for (int k = 0; specs[k].key != Key::NONE; ++k) {
            if (specs[k].key == key) {
                return specs[k];
            }
        }
    }

    return nullSpec;
}

#define IGNORE_SEQUENCE_ON_BAD_PARAMS         \
    case '<':                                 \
    case '=':                                 \
    case '>':                                 \
    case '?':                                 \
        setState(InputState::IgnoreSequence); \
        break

#define COLLECT_NUMERIC_PARAMS                                                                          \
    case '0':                                                                                           \
    case '1':                                                                                           \
    case '2':                                                                                           \
    case '3':                                                                                           \
    case '4':                                                                                           \
    case '5':                                                                                           \
    case '6':                                                                                           \
    case '7':                                                                                           \
    case '8':                                                                                           \
    case '9':                                                                                           \
        csiHadParams = true;                                                                            \
        csiPrefixAllowed = false;                                                                       \
        if (inputOps[nInputOps - 1] > (UINT32_MAX - (u32)(ch - '0')) / 10) {                            \
            inputOps[nInputOps - 1] = UINT32_MAX;                                                       \
        } else {                                                                                        \
            inputOps[nInputOps - 1] *= 10;                                                              \
            inputOps[nInputOps - 1] += ch - '0';                                                        \
        }                                                                                               \
        break;                                                                                          \
    case ';':                                                                                           \
    case ':':                                                                                           \
        csiHadParams = true;                                                                            \
        csiPrefixAllowed = false;                                                                       \
        if (nInputOps < maxEscOps) {                                                                    \
            inputSeparators[nInputOps] = ch;                                                            \
            inputOps[nInputOps++] = 0;                                                                  \
        } else {                                                                                        \
            logT << "inputOps full, increase maxEscOps (currently: " << maxEscOps << ")!" << std::endl; \
            setState(InputState::IgnoreSequence);                                                       \
        }                                                                                               \
        break

VtermImpl::PresentationState VtermImpl::capturePresentationState() const {
    return {
        cf,
        cf->getCursor(),
        cf->getSelectionForView(),
        cf->nCols,
        cf->nRows,
        cf->getViewOffset(),
        cf->getScreenReverseVideo(),
        cf->getBlinkVisible(),
        cf->getCursorBlink(),
        cf->getSelectionForeground(),
        cf->getSelectionBackground(),
        cf->getSelectionColorMask(),
    };
}

bool VtermImpl::presentationChanged(const PresentationState& before) const {
    if (before.frame != cf || cf->hasDamage() || before.columns != cf->nCols || before.rows != cf->nRows || before.viewOffset != cf->getViewOffset() || before.screenReverse != cf->getScreenReverseVideo() || before.blinkVisible != cf->getBlinkVisible() || before.cursorBlink != cf->getCursorBlink() || !(before.selectionForeground == cf->getSelectionForeground()) || !(before.selectionBackground == cf->getSelectionBackground()) || before.selectionColorMask != cf->getSelectionColorMask()) {
        return true;
    }
    const auto cursor = cf->getCursor();
    if (before.cursor.posX != cursor.posX || before.cursor.posY != cursor.posY || before.cursor.style != cursor.style || !(before.cursor.color == cursor.color)) {
        return true;
    }
    const Rect selection = cf->getSelectionForView();
    return !(before.selection.tl == selection.tl) || !(before.selection.br == selection.br) || before.selection.rectangular != selection.rectangular;
}

void VtermImpl::syncPresentationCursor() {
    cf->setCursorPos(posY, posX);
    using CS = TerminalCursor::Style;
    cf->setCursorStyle(showCursorMode ? (hasFocus ? cursorShape : CS::hollow_block) : CS::hidden);
}

bool VtermImpl::processInput(const std::string& str) {
    const std::u8string input(str.begin(), str.end());
    return processInput(input.data(), (int)input.size());
}

void VtermImpl::feedPtyOutput(const std::string& output) {
    processInput(output);
}

void VtermImpl::setParserTrace(VtermTrace* trace) {
    parserTrace = trace;
}

void VtermImpl::beginCsi() {
    inputOps[0] = 0;
    inputSeparators[0] = 0;
    nInputOps = 1;
    csiHadParams = false;
    csiPrefixAllowed = true;
    csiPrivatePrefix.clear();
    csiIntermediates.clear();
    setState(InputState::CSI);
}

template <bool traced>
bool VtermImpl::executeC0InSequence(unsigned char ch) {
    if (ch >= 0x20 || ch == '\x18' || ch == '\x1a' || ch == '\x1b') {
        return false;
    }

    if constexpr (traced) {
        if (inputState != InputState::String && inputState != InputState::String_Esc) {
            parserTrace->control(ch);
        }
    }

    if (ch == '\a') {
        host.bell();
        return true;
    }
    if (ch == '\x0e') {
        charsetState.gl = 1;
        return true;
    }
    if (ch == '\x0f') {
        charsetState.gl = 0;
        return true;
    }
    if (ch != '\b' && ch != '\t' && ch != '\n' && ch != '\v' && ch != '\f' && ch != '\r') {
        return true;
    }

    switch (ch) {
        case '\b':
            moveCursorBackward(1);
            break;
        case '\t':
            inp_HT();
            break;
        case '\n':
        case '\v':
        case '\f':
            performIndex();
            break;
        case '\r':
            inp_CR();
            break;
        default:
            break;
    }

    return true;
}

template <bool traced>
void VtermImpl::dispatchCsi(unsigned char finalByte) {
    if constexpr (traced) {
        parserTrace->csi(finalByte, csiPrivatePrefix, csiIntermediates,
            inputOps, inputSeparators, nInputOps, csiHadParams);
    }
    const std::string key = csiPrivatePrefix + csiIntermediates + (char)(finalByte);
    if (key == "A") {
        csi_CUU();
    } else if (key == "B") {
        csi_CUD();
    } else if (key == "C") {
        csi_CUF();
    } else if (key == "D") {
        csi_CUB();
    } else if (key == "E") {
        csi_CNL();
    } else if (key == "F") {
        csi_CPL();
    } else if (key == "G") {
        csi_CHA();
    } else if (key == "H" || key == "f") {
        csi_CUP();
    } else if (key == "I") {
        csi_CHT();
    } else if (key == "J") {
        csi_ED();
    } else if (key == "K") {
        csi_EL();
    } else if (key == "L") {
        csi_IL();
    } else if (key == "M") {
        csi_DL();
    } else if (key == "P") {
        csi_DCH();
    } else if (key == "S") {
        csi_SU();
    } else if (key == "T") {
        if (nInputOps == 5 && mouseTrk.mode == MouseTrackingMode::VT200_Highlight) {
            csi_XTHIMOUSE();
        } else {
            csi_SD();
        }
    } else if (key == "X") {
        csi_ECH();
    } else if (key == "Z") {
        csi_CBT();
    } else if (key == "@") {
        csi_ICH();
    } else if (key == "`") {
        csi_HPA();
    } else if (key == "a") {
        csi_HPR();
    } else if (key == "b") {
        csi_REP();
    } else if (key == "c") {
        csi_priDA();
    } else if (key == "d") {
        csi_VPA();
    } else if (key == "e") {
        csi_VPR();
    } else if (key == "g") {
        csi_TBC();
    } else if (key == "h") {
        csi_SM();
    } else if (key == "l") {
        csi_RM();
    } else if (key == "m") {
        csi_SGR();
    } else if (key == "n") {
        csi_DSR();
    } else if (key == "?n") {
        csi_DSR(true);
    } else if (key == "q") {
        csi_DECLL();
    } else if (key == "i") {
        csi_MC(false);
    } else if (key == "r") {
        csi_STBM();
    } else if (key == "s") {
        csi_SCOSC_SLRM();
    } else if (key == "t") {
        csi_XTWINOPS();
    } else if (key == "u") {
        csi_SCORC();
    } else if (key == "!p") {
        csi_DECSTR();
    } else if (key == "'}") {
        csi_DECIC();
    } else if (key == "'~") {
        csi_DECDC();
    } else if (key == "'z") {
        csi_DECELR();
    } else if (key == "'{") {
        csi_DECSLE();
    } else if (key == "'|") {
        csi_DECRQLP();
    } else if (key == "'w") {
        csi_DECEFR();
    } else if (key == "\"p") {
        csiq_DECSCL();
    } else if (key == "\"q") {
        csi_DECSCA();
    } else if (key == " @") {
        csi_ecma48_SL();
    } else if (key == " A") {
        csi_ecma48_SR();
    } else if (key == " q") {
        csi_DECSCUSR();
    } else if (key == ">c") {
        csi_secDA();
    } else if (key == ">m") {
        csi_XTMODKEYS();
    } else if (key == ">u") {
        csi_kittyKeyboardPush();
    } else if (key == ">q") {
        csi_XTVERSION();
    } else if (key == "<u") {
        csi_kittyKeyboardPop();
    } else if (key == "=u") {
        csi_kittyKeyboardSet();
    } else if (key == "=c") {
        csi_terDA();
    } else if (key == "?h") {
        csi_privSM();
    } else if (key == "?l") {
        csi_privRM();
    } else if (key == "?s") {
        csi_privSave();
    } else if (key == "?r") {
        csi_privRestore();
    } else if (key == "?u") {
        csi_kittyKeyboardQuery();
    } else if (key == "?m") {
        csi_XTQMODKEYS();
    } else if (key == "?J") {
        csi_DECSED();
    } else if (key == "?K") {
        csi_DECSEL();
    } else if (key == "?i") {
        csi_MC(true);
    } else if (key == "$p") {
        csi_DECRQM(false);
    } else if (key == "$r") {
        csi_DECCARA(false);
    } else if (key == "$t") {
        csi_DECCARA(true);
    } else if (key == "$v") {
        csi_DECCRA();
    } else if (key == "$x") {
        csi_DECFRA();
    } else if (key == "$z") {
        csi_DECERA();
    } else if (key == "${") {
        csi_DECERA(true);
    } else if (key == "*y") {
        csi_DECRQCRA();
    } else if (key == "?$p") {
        csi_DECRQM(true);
    } else {
        setState(InputState::Normal);
    }
}

template <bool traced>
void VtermImpl::processCsiByte(unsigned char ch) {
    if (ch == 0x7f || executeC0InSequence<traced>(ch)) {
        return;
    }
    if (ch >= '0' && ch <= '9') {
        if (!csiIntermediates.empty()) {
            setState(InputState::IgnoreSequence);
            return;
        }
        csiHadParams = true;
        csiPrefixAllowed = false;
        if (inputOps[nInputOps - 1] > (UINT32_MAX - (u32)(ch - '0')) / 10) {
            inputOps[nInputOps - 1] = UINT32_MAX;
        } else {
            inputOps[nInputOps - 1] = inputOps[nInputOps - 1] * 10 + ch - '0';
        }
        return;
    }
    if (ch == ';' || ch == ':') {
        if (!csiIntermediates.empty() || nInputOps >= maxEscOps) {
            setState(InputState::IgnoreSequence);
            return;
        }
        csiHadParams = true;
        csiPrefixAllowed = false;
        inputSeparators[nInputOps] = ch;
        inputOps[nInputOps++] = 0;
        return;
    }
    if (ch >= '<' && ch <= '?' && csiPrefixAllowed && csiPrivatePrefix.empty()) {
        csiPrivatePrefix.push_back((char)(ch));
        return;
    }
    if (ch >= 0x20 && ch <= 0x2f) {
        csiPrefixAllowed = false;
        if (csiIntermediates.size() >= 4) {
            setState(InputState::IgnoreSequence);
            return;
        }
        csiIntermediates.push_back((char)(ch));
        return;
    }
    if (ch >= 0x40 && ch <= 0x7e) {
        dispatchCsi<traced>(ch);
        return;
    }
    setState(InputState::IgnoreSequence);
}

bool VtermImpl::processInput(const u8* input, int inputSize, bool refresh) {
    if (parserTrace) {
        return processInputImpl<true>(input, inputSize, refresh);
    }
    return processInputImpl<false>(input, inputSize, refresh);
}

template <bool traced>
bool VtermImpl::processInputImpl(const u8* input, int inputSize, bool refresh) {
    const PresentationState presentationBefore = capturePresentationState();
    lastEscBegin = 0;
    lastNormalBegin = 0;
    lastStopPos = 0;
    hideCursor();
    for (readPos = 0; readPos < inputSize; ++readPos) {
        const u8& ch = input[readPos];
        if (printerControllerMode) {
            const size_t consumed = consumePrinterController(input + readPos, inputSize - readPos);
            readPos += (int)(consumed)-1;
            continue;
        }
        if (inputState == InputState::Normal && ch >= 0x20 && ch < 0x7f && !utf8dec.expectsContinuation() && charsetState.ss == 0 && charsetState.g[charsetState.gl] == Charset::UTF8) {
            int end = readPos + 1;
            while (end < inputSize && input[end] >= 0x20 && input[end] < 0x7f) {
                ++end;
            }
            const int count = end - readPos;
            if constexpr (traced) {
                parserTrace->text(input + readPos, count);
            }
            if (count >= 4) {
                placeAsciiRun(input + readPos, count);
            } else {
                while (readPos < end) {
                    utf8dec.setUnicode(input[readPos++]);
                    placeGraphicChar();
                }
            }
            readPos = end - 1;
            continue;
        }
        if ((inputState == InputState::DCS || inputState == InputState::OSC || inputState == InputState::String) && ch >= 0x20 && ch < 0x7f) {
            stringUtf8Remaining = 0;
            int end = readPos + 1;
            while (end < inputSize && input[end] >= 0x20 && input[end] < 0x7f) {
                ++end;
            }
            if constexpr (traced) {
                parserTrace->stringData(input + readPos, end - readPos);
            }
            if (inputState != InputState::String && !argBufOverflowed) {
                const size_t limit = inputState == InputState::DCS ? 4095 : maxOscBytes;
                const size_t count = end - readPos;
                const size_t available = argBuf.size() < limit ? limit - argBuf.size() : 0;
                const size_t append = std::min(count, available);
                argBuf.insert(argBuf.end(), input + readPos, input + readPos + append);
                if (append < count) {
                    logT << (inputState == InputState::DCS ? "DCS" : "OSC") << " argument string overflow" << std::endl;
                    argBufOverflowed = true;
                }
            }
            readPos = end - 1;
            continue;
        }
        const bool utf8StringContinuation =
            (inputState == InputState::DCS || inputState == InputState::OSC || inputState == InputState::String)
            && stringUtf8Continuation(ch);
        if (ch == '\x18' || ch == '\x1a') {
            if constexpr (traced) {
                parserTrace->control(ch);
            }
            if (inputState == InputState::Normal) {
                resetGraphemeInput();
            } else {
                if constexpr (traced) {
                    parserTrace->stringCancel();
                    parserTrace->escapeCancel();
                }
                setState(InputState::Normal);
            }
            continue;
        }
        if (ch == 0x7f) {
            continue;
        }
        if (ch == '\x1b' && inputState != InputState::Normal && inputState != InputState::Escape && inputState != InputState::Escape_VT52 && inputState != InputState::DCS && inputState != InputState::DCS_Esc && inputState != InputState::OSC && inputState != InputState::OSC_Esc && inputState != InputState::String && inputState != InputState::String_Esc) {
            if constexpr (traced) {
                parserTrace->stringCancel();
                parserTrace->escapeCancel();
                parserTrace->escapeBegin();
            }
            setState(compatLevel == CompatibilityLevel::VT52 ? InputState::Escape_VT52 : InputState::Escape);
            inputOps[0] = 0;
            inputSeparators[0] = 0;
            nInputOps = 1;
            lastEscBegin = readPos;
            continue;
        }
        if (ch >= 0xa0 && inputState != InputState::Normal
            && inputState != InputState::DCS && inputState != InputState::DCS_Esc
            && inputState != InputState::OSC && inputState != InputState::OSC_Esc
            && inputState != InputState::String && inputState != InputState::String_Esc) {
            if constexpr (traced) {
                parserTrace->escapeCancel();
            }
            setState(InputState::Normal);
            --readPos;
            continue;
        }
        if (inputState != InputState::Normal && !utf8StringContinuation) {
            switch (ch) {
                case 0x90:
                    argBuf.clear();
                    argBufOverflowed = false;
                    if constexpr (traced) {
                        parserTrace->stringBegin(VtermTraceString::Dcs);
                    }
                    setState(InputState::DCS);
                    continue;
                case 0x98:
                    if constexpr (traced) {
                        parserTrace->stringBegin(VtermTraceString::Sos);
                    }
                    setState(InputState::String);
                    continue;
                case 0x9e:
                    if constexpr (traced) {
                        parserTrace->stringBegin(VtermTraceString::Pm);
                    }
                    setState(InputState::String);
                    continue;
                case 0x9f:
                    if constexpr (traced) {
                        parserTrace->stringBegin(VtermTraceString::Apc);
                    }
                    setState(InputState::String);
                    continue;
                case 0x9b:
                    beginCsi();
                    continue;
                case 0x9c:
                    if (inputState != InputState::DCS && inputState != InputState::OSC) {
                        if constexpr (traced) {
                            parserTrace->escapeCancel();
                            parserTrace->control(ch);
                        }
                        setState(InputState::Normal);
                        continue;
                    }
                    break;
                case 0x9d:
                    argBuf.clear();
                    argBufOverflowed = false;
                    if constexpr (traced) {
                        parserTrace->stringBegin(VtermTraceString::Osc);
                    }
                    setState(InputState::OSC);
                    continue;
            }
        }
        switch (inputState) {
            case InputState::Normal:
                if constexpr (traced) {
                    if (ch > 0 && ch < 0x20 && ch != '\x1b') {
                        parserTrace->control(ch);
                    } else if (ch >= 0xa0 || (ch >= 0x80 && utf8dec.expectsContinuation())) {
                        parserTrace->text(&ch, 1);
                    } else if (ch >= 0x80 && ch <= 0x9f
                        && ch != 0x90 && ch != 0x98 && ch != 0x9b
                        && ch != 0x9d && ch != 0x9e && ch != 0x9f) {
                        parserTrace->control(ch);
                    }
                }
                if (utf8dec.expectsContinuation() && ch >= 0x80) {
                    inputGraphicChar(ch);
                    break;
                }
                if (ch < 0x20 || ch == 0x7f || (ch >= 0x80 && ch <= 0x9f)) {
                    resetGraphemeInput();
                }
                switch (ch) {
                    case '\x00':
                    case '\x7f':
                        break;
                    case '\x1b':
                        if constexpr (traced) {
                            parserTrace->escapeBegin();
                        }
                        setState(compatLevel == CompatibilityLevel::VT52 ? InputState::Escape_VT52 : InputState::Escape);
                        inputOps[0] = 0;
                        inputSeparators[0] = 0;
                        nInputOps = 1;
                        lastEscBegin = readPos;
                        break;
                    case 0x84:
                        esc_IND();
                        break;
                    case 0x85:
                        esc_NEL();
                        break;
                    case 0x88:
                        esc_HTS();
                        break;
                    case 0x8d:
                        esc_RI();
                        break;
                    case 0x8e:
                        charsetState.ss = 2;
                        break;
                    case 0x8f:
                        charsetState.ss = 3;
                        break;
                    case 0x90:
                        argBuf.clear();
                        argBufOverflowed = false;
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Dcs);
                        }
                        setState(InputState::DCS);
                        break;
                    case 0x98:
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Sos);
                        }
                        setState(InputState::String);
                        break;
                    case 0x9e:
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Pm);
                        }
                        setState(InputState::String);
                        break;
                    case 0x9f:
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Apc);
                        }
                        setState(InputState::String);
                        break;
                    case 0x9b:
                        beginCsi();
                        break;
                    case 0x9c:
                        break;
                    case 0x9d:
                        argBuf.clear();
                        argBufOverflowed = false;
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Osc);
                        }
                        setState(InputState::OSC);
                        break;
                    case '\r':
                        traceNormalInput();
                        inp_CR();
                        break;
                    case '\f':
                    case '\v':
                    case '\n':
                        traceNormalInput();
                        if (autoNewlineMode) {
                            inp_CR();
                        }
                        esc_IND();
                        break;
                    case '\t':
                        traceNormalInput();
                        inp_HT();
                        break;
                    case '\b':
                        traceNormalInput();
                        csi_CUB();
                        break;
                    case '\a':
                        traceNormalInput();
                        host.bell();
                        break;
                    case '\x0e':
                        traceNormalInput();
                        charsetState.gl = 1;
                        break;
                    case '\x0f':
                        traceNormalInput();
                        charsetState.gl = 0;
                        break;
                    case '\x05':
                        traceNormalInput();
                        break;
                    default:
                        inputGraphicChar(ch);
                }
                break;
            case InputState::IgnoreSequence:
                if (executeC0InSequence<traced>(ch)) {
                    break;
                } else if (ch >= '\x40' && ch <= '\x7e') {
                    if constexpr (traced) {
                        parserTrace->escapeCancel();
                    }
                    setState(InputState::Normal);
                }
                break;
            case InputState::Escape_VT52:
                switch (ch) {
                    case '\x18':
                    case '\x1a':
                        setState(InputState::Normal);
                        break;
                    case '\x1b':
                        inputOps[0] = 0;
                        nInputOps = 1;
                        lastEscBegin = readPos;
                        break;
                    case '=':
                        keypadMode = KeypadMode::Application;
                        setState(InputState::Normal);
                        break;
                    case '>':
                        keypadMode = KeypadMode::Normal;
                        setState(InputState::Normal);
                        break;
                    case '<':
                        compatLevel = CompatibilityLevel::VT100;
                        setState(InputState::Normal);
                        break;
                    case 'A':
                        csi_CUU();
                        break;
                    case 'B':
                        csi_CUD();
                        break;
                    case 'C':
                        csi_CUF();
                        break;
                    case 'D':
                        csi_CUB();
                        break;
                    case 'F':
                        charsetState = CharsetState{};
                        charsetState.g[charsetState.gl] = Charset::DecSpec;
                        setState(InputState::Normal);
                        break;
                    case 'G':
                        charsetState = CharsetState{};
                        setState(InputState::Normal);
                        break;
                    case 'H':
                        csi_CUP();
                        break;
                    case 'I':
                        esc_RI();
                        break;
                    case 'J':
                        csi_ED();
                        break;
                    case 'K':
                        csi_EL();
                        break;
                    case 'Y':
                        setState(InputState::VT52_CUP_Arg1);
                        break;
                    case 'Z':
                        writePty("\x1b/Z");
                        break;
                    case 'c':
                        esc_RIS();
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::VT52_CUP_Arg1:
                inputOps[0] = input[readPos] - 31;
                setState(InputState::VT52_CUP_Arg2);
                break;
            case InputState::VT52_CUP_Arg2:
                inputOps[1] = input[readPos] - 31;
                nInputOps = 2;
                csi_CUP();
                break;
            case InputState::Escape:
                if constexpr (traced) {
                    if (ch == '\x1b') {
                        parserTrace->escapeCancel();
                        parserTrace->escapeBegin();
                    } else if (ch > 0 && ch < 0x20) {
                        parserTrace->control(ch);
                    } else if (ch >= 0x20 && ch <= 0x2f) {
                        parserTrace->escapeByte(ch);
                    } else if (ch >= 0x40 && ch <= 0x5f
                        && ch != 'P' && ch != 'X' && ch != '[' && ch != '\\'
                        && ch != ']' && ch != '^' && ch != '_') {
                        parserTrace->escapeCancel();
                        parserTrace->control(ch + 0x40);
                    } else if (ch != 'P' && ch != 'X' && ch != '[' && ch != '\\'
                        && ch != ']' && ch != '^' && ch != '_') {
                        parserTrace->escapeByte(ch);
                        parserTrace->escapeEnd();
                    }
                }
                switch (ch) {
                    case '\x18':
                    case '\x1a':
                        setState(InputState::Normal);
                        break;
                    case '\x1b':
                        inputOps[0] = 0;
                        nInputOps = 1;
                        lastEscBegin = readPos;
                        break;
                    case ' ':
                        setState(InputState::Esc_SPC);
                        break;
                    case '#':
                        setState(InputState::Esc_Hash);
                        break;
                    case '%':
                        setState(InputState::Esc_Pct);
                        break;
                    case '[':
                        beginCsi();
                        break;
                    case ']':
                        argBuf.clear();
                        argBufOverflowed = false;
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Osc);
                        }
                        setState(InputState::OSC);
                        break;
                    case 'X':
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Sos);
                        }
                        setState(InputState::String);
                        break;
                    case '^':
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Pm);
                        }
                        setState(InputState::String);
                        break;
                    case '_':
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Apc);
                        }
                        setState(InputState::String);
                        break;
                    case '(':
                    case ')':
                    case '*':
                    case '+':
                    case '-':
                    case '.':
                    case '/':
                    case ',':
                    case '$':
                        scsDst = ch;
                        scsMod = '\0';
                        setState(InputState::SelectCharset);
                        break;
                    case 'D':
                        esc_IND();
                        break;
                    case 'M':
                        esc_RI();
                        break;
                    case 'E':
                        esc_NEL();
                        break;
                    case 'H':
                        esc_HTS();
                        break;
                    case 'N':
                        charsetState.ss = 2;
                        setState(InputState::Normal);
                        break;
                    case 'O':
                        charsetState.ss = 3;
                        setState(InputState::Normal);
                        break;
                    case 'P':
                        argBuf.clear();
                        argBufOverflowed = false;
                        if constexpr (traced) {
                            parserTrace->stringBegin(VtermTraceString::Dcs);
                        }
                        setState(InputState::DCS);
                        break;
                    case 'c':
                        esc_RIS();
                        break;
                    case '6':
                        esc_BI();
                        break;
                    case '7':
                        esc_DECSC();
                        break;
                    case '8':
                        esc_DECRC();
                        break;
                    case '9':
                        esc_FI();
                        break;
                    case '=':
                        keypadMode = KeypadMode::Application;
                        setState(InputState::Normal);
                        break;
                    case '>':
                        keypadMode = KeypadMode::Normal;
                        setState(InputState::Normal);
                        break;
                    case '<':
                        compatLevel = CompatibilityLevel::VT400;
                        setState(InputState::Normal);
                        break;
                    case '~':
                        charsetState.gr = 1;
                        setState(InputState::Normal);
                        break;
                    case 'n':
                        charsetState.gl = 2;
                        setState(InputState::Normal);
                        break;
                    case '}':
                        charsetState.gr = 2;
                        setState(InputState::Normal);
                        break;
                    case 'o':
                        charsetState.gl = 3;
                        setState(InputState::Normal);
                        break;
                    case '|':
                        charsetState.gr = 3;
                        setState(InputState::Normal);
                        break;
                    case '\\':
                        if constexpr (traced) {
                            parserTrace->escapeByte(ch);
                            parserTrace->escapeEnd();
                        }
                        setState(InputState::Normal);
                        break;
                    default:
                        if (ch >= 0x20 && ch <= 0x2f) {
                            setState(InputState::EscapeIntermediate);
                        } else {
                            unhandledInput(ch);
                        }
                        break;
                }
                break;
            case InputState::EscapeIntermediate:
                if (executeC0InSequence<traced>(ch)) {
                    break;
                }
                if (ch >= 0x20 && ch <= 0x2f) {
                    if constexpr (traced) {
                        parserTrace->escapeByte(ch);
                    }
                } else if (ch >= 0x30 && ch <= 0x7e) {
                    if constexpr (traced) {
                        parserTrace->escapeByte(ch);
                        parserTrace->escapeEnd();
                    }
                    setState(InputState::Normal);
                } else {
                    unhandledInput(ch);
                }
                break;
            case InputState::Esc_SPC:
                if constexpr (traced) {
                    parserTrace->escapeByte(ch);
                    parserTrace->escapeEnd();
                }
                switch (ch) {
                    case 'F':
                        logU << "S7C1T: Send 7-bit controls" << std::endl;
                        send8BitControls = false;
                        setState(InputState::Normal);
                        break;
                    case 'G':
                        logU << "S8C1T: Send 8-bit controls" << std::endl;
                        send8BitControls = true;
                        setState(InputState::Normal);
                        break;
                    case 'L':
                        logU << "Set ANSI conformance level 1" << std::endl;
                        setState(InputState::Normal);
                        break;
                    case 'M':
                        logU << "Set ANSI conformance level 2" << std::endl;
                        setState(InputState::Normal);
                        break;
                    case 'N':
                        logU << "Set ANSI conformance level 3" << std::endl;
                        setState(InputState::Normal);
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::Esc_Hash:
                if constexpr (traced) {
                    parserTrace->escapeByte(ch);
                    parserTrace->escapeEnd();
                }
                switch (ch) {
                    case '3':
                        setLineAttribute(1);
                        break;
                    case '4':
                        setLineAttribute(2);
                        break;
                    case '5':
                        setLineAttribute(0);
                        break;
                    case '6':
                        setLineAttribute(3);
                        break;
                    case '8':
                        esch_DECALN();
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::Esc_Pct:
                if constexpr (traced) {
                    parserTrace->escapeByte(ch);
                    parserTrace->escapeEnd();
                }
                switch (ch) {
                    case '@':
                        logT << "Select charset: default (ISO-8859-1)" << std::endl;
                        charsetState = CharsetState{};
                        charsetState.g[charsetState.gr] = Charset::IsoLatin1;
                        setState(InputState::Normal);
                        break;
                    case 'G':
                        logT << "Select charset: UTF-8" << std::endl;
                        charsetState = CharsetState{};
                        setState(InputState::Normal);
                        break;
                    default:
                        unhandledInput(ch);
                        break;
                }
                break;
            case InputState::SelectCharset:
                if (ch < 0x30) {
                    if constexpr (traced) {
                        parserTrace->escapeByte(ch);
                    }
                    scsMod = ch;
                } else {
                    if constexpr (traced) {
                        parserTrace->escapeByte(ch);
                        parserTrace->escapeEnd();
                    }
                    esc_DCS(ch);
                }
                break;
            case InputState::CSI:
                processCsiByte<traced>(ch);
                break;
            case InputState::DCS:
                switch (ch) {
                    case 0x9c:
                        if (!utf8StringContinuation) {
                            if constexpr (traced) {
                                parserTrace->stringEnd();
                            }
                            if (argBufOverflowed) {
                                setState(InputState::Normal);
                            } else {
                                handle_DCS();
                            }
                            break;
                        }
                        [[fallthrough]];
                    default:
                        if (executeC0InSequence<traced>(ch)) {
                            break;
                        } else if (argBuf.size() < 4095) {
                            if constexpr (traced) {
                                parserTrace->stringData(&ch, 1);
                            }
                            argBuf.push_back(ch);
                        } else if (!argBufOverflowed) {
                            logT << "DCS argument string overflow" << std::endl;
                            argBufOverflowed = true;
                        }
                        break;
                    case '\x1b':
                        setState(InputState::DCS_Esc);
                        break;
                    case '\x7f':
                        break;
                }
                break;
            case InputState::DCS_Esc:
                switch (ch) {
                    case '\\':
                        if constexpr (traced) {
                            parserTrace->stringEnd();
                        }
                        if (argBufOverflowed) {
                            setState(InputState::Normal);
                        } else {
                            handle_DCS();
                        }
                        break;
                    case '\x1b':
                        if (!argBufOverflowed && argBuf.size() < 4095) {
                            if constexpr (traced) {
                                parserTrace->stringData(&ch, 1);
                            }
                            argBuf.push_back('\x1b');
                        } else {
                            argBufOverflowed = true;
                        }
                        break;
                    default:
                        if (!argBufOverflowed && argBuf.size() <= 4093) {
                            if constexpr (traced) {
                                const u8 prefix[] = {'\x1b', ch};
                                parserTrace->stringData(prefix, 2);
                            }
                            argBuf.push_back('\x1b');
                            argBuf.push_back(ch);
                        } else {
                            argBufOverflowed = true;
                        }
                        setState(InputState::DCS);
                        break;
                }
                break;
            case InputState::OSC:
                switch (ch) {
                    case 0x9c:
                        if (!utf8StringContinuation) {
                            if constexpr (traced) {
                                parserTrace->stringEnd();
                            }
                            if (argBufOverflowed) {
                                setState(InputState::Normal);
                            } else {
                                handle_OSC();
                            }
                            break;
                        }
                        [[fallthrough]];
                    default:
                        if (executeC0InSequence<traced>(ch)) {
                            break;
                        } else if (argBuf.size() < maxOscBytes) {
                            if constexpr (traced) {
                                parserTrace->stringData(&ch, 1);
                            }
                            argBuf.push_back(ch);
                        } else if (!argBufOverflowed) {
                            logT << "OSC argument string overflow" << std::endl;
                            argBufOverflowed = true;
                        }
                        break;
                    case '\a':
                        if constexpr (traced) {
                            parserTrace->stringEnd();
                        }
                        if (argBufOverflowed) {
                            setState(InputState::Normal);
                        } else {
                            handle_OSC();
                        }
                        break;
                    case '\x1b':
                        setState(InputState::OSC_Esc);
                        break;
                    case '\x7f':
                        break;
                }
                break;
            case InputState::OSC_Esc:
                switch (ch) {
                    case '\\':
                        if constexpr (traced) {
                            parserTrace->stringEnd();
                        }
                        if (argBufOverflowed) {
                            setState(InputState::Normal);
                        } else {
                            handle_OSC();
                        }
                        break;
                    case '\x1b':
                        if (!argBufOverflowed && argBuf.size() < maxOscBytes) {
                            if constexpr (traced) {
                                parserTrace->stringData(&ch, 1);
                            }
                            argBuf.push_back('\x1b');
                        } else {
                            argBufOverflowed = true;
                        }
                        break;
                    default:
                        if (!argBufOverflowed && argBuf.size() <= maxOscBytes - 2) {
                            if constexpr (traced) {
                                const u8 prefix[] = {'\x1b', ch};
                                parserTrace->stringData(prefix, 2);
                            }
                            argBuf.push_back('\x1b');
                            argBuf.push_back(ch);
                        } else {
                            argBufOverflowed = true;
                        }
                        setState(InputState::OSC);
                        break;
                }
                break;
            case InputState::String:
                if (ch == 0x9c && !utf8StringContinuation) {
                    if constexpr (traced) {
                        parserTrace->stringEnd();
                    }
                    setState(InputState::Normal);
                } else if (ch == '\x1b') {
                    setState(InputState::String_Esc);
                } else if (executeC0InSequence<traced>(ch)) {
                    if constexpr (traced) {
                        parserTrace->stringData(&ch, 1);
                    }
                    break;
                } else if constexpr (traced) {
                    parserTrace->stringData(&ch, 1);
                }
                break;
            case InputState::String_Esc:
                if (ch == '\\') {
                    if constexpr (traced) {
                        parserTrace->stringEnd();
                    }
                    setState(InputState::Normal);
                } else if (ch != '\x1b') {
                    if constexpr (traced) {
                        const u8 prefix[] = {'\x1b', ch};
                        parserTrace->stringData(prefix, 2);
                    }
                    setState(InputState::String);
                }
                break;
        }
    }
    traceNormalInput();
    syncPresentationCursor();
    const bool changed = presentationChanged(presentationBefore);
    if (refresh && changed) {
        redraw();
    }
    return changed;
}

void VtermImpl::setHyperlink(const std::string& parametersAndUri) {
    const size_t separator = parametersAndUri.find(';');
    if (separator == std::string::npos) {
        logT << "Malformed OSC 8 argument" << std::endl;
        return;
    }

    const std::string parameters = parametersAndUri.substr(0, separator);
    const std::string uri = parametersAndUri.substr(separator + 1);
    if (uri.empty()) {
        activeHyperlink = 0;
        return;
    }

    if (hyperlinks.size() >= 256 && hyperlinks.size() % 256 == 0) {
        pruneHyperlinks();
    }

    std::string identity = "uri=" + uri;
    size_t begin = 0;
    while (begin <= parameters.size()) {
        const size_t end = parameters.find(':', begin);
        const std::string parameter = parameters.substr(begin, end - begin);
        if (parameter.compare(0, 3, "id=") == 0) {
            identity = parameter + ";uri=" + uri;
            break;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }

    const auto known = hyperlinkIds.find(identity);
    if (known != hyperlinkIds.end()) {
        activeHyperlink = known->second;
        return;
    }

    if (nextHyperlink == 0) {
        logW << "OSC 8 hyperlink identifier space exhausted" << std::endl;
        activeHyperlink = 0;
        return;
    }

    activeHyperlink = nextHyperlink++;
    hyperlinkIds.emplace(identity, activeHyperlink);
    hyperlinks.emplace(activeHyperlink, uri);
}

void VtermImpl::pruneHyperlinks() {
    std::set<u32> used;
    frame_pri.collectHyperlinkIds(used);
    frame_alt.collectHyperlinkIds(used);
    if (activeHyperlink != 0) {
        used.insert(activeHyperlink);
    }

    for (auto it = hyperlinks.begin(); it != hyperlinks.end();) {
        if (used.count(it->first)) {
            ++it;
            continue;
        }
        const u32 id = it->first;
        it = hyperlinks.erase(it);
        for (auto key = hyperlinkIds.begin(); key != hyperlinkIds.end();) {
            if (key->second == id) {
                key = hyperlinkIds.erase(key);
            } else {
                ++key;
            }
        }
    }
}

std::string VtermImpl::getHyperlink(int pX, int pY) const {
    if (pX < opts.border || pY < opts.border || pX >= winPx - opts.border || pY >= winPy - opts.border) {
        return {};
    }

    const u16 column = (pX - opts.border) / glyphPx;
    const u16 row = (pY - opts.border) / glyphPy;
    if (column >= cf->nCols || row >= cf->nRows) {
        return {};
    }

    const u32 id = cf->getViewCell(row, column).hyperlink;
    const auto link = hyperlinks.find(id);
    return link == hyperlinks.end() ? std::string{} : link->second;
}

Point VtermImpl::selectionPoint(int pX, int pY) const {
    const int contentWidth = std::max(0, (int)winPx - 2 * opts.border);
    const int contentHeight = std::max(1, (int)winPy - 2 * opts.border);
    pX = std::min(std::max(0, pX - opts.border), contentWidth);
    pY = std::min(std::max(0, pY - opts.border), contentHeight - 1);
    return cf->getLogicalPoint(Point(
        std::min(pX / glyphPx, (int)nCols),
        std::min(pY / glyphPy, (int)nRows - 1)));
}

void VtermImpl::selectStart(int pX, int pY, bool cycleSnapTo) {
    logT << "selectStart (" << pX << "," << pY << "), cycleSnapTo=" << cycleSnapTo << std::endl;

    if (cycleSnapTo) {
        selectExtend(pX, pY, true);
        return;
    }

    Point pt = selectionPoint(pX, pY);

    Rect& selection = cf->getSelection();
    cf->setSelectSnapTo(Frame::SelectSnapTo::Char);
    selection.tl = pt;
    selection.br = pt;
    selectUpdatesTop = false;
    selectUpdatesLeft = false;

    hideCursor();
    redraw();
}

void VtermImpl::selectExtend(int pX, int pY, bool cycleSnapTo) {
    logT << "selectExtend (" << pX << "," << pY << "), cycleSnapTo=" << cycleSnapTo << std::endl;

    Point pt = selectionPoint(pX, pY);

    Rect& selection = cf->getSelection();
    if (cycleSnapTo) {
        cf->cycleSelectSnapTo();
    }

    if (selection.rectangular) {
        selectUpdatesLeft = pt.x < selection.mid().x;
        selectUpdatesTop = pt.y < selection.mid().y;
    } else {
        selectUpdatesLeft = selectUpdatesTop = pt < selection.mid();
    }

    if (selectUpdatesTop && selectUpdatesLeft) {
        selection.tl = pt;
    } else if (selectUpdatesTop) {
        selection.br.x = pt.x;
        selection.tl.y = pt.y;
    } else if (selectUpdatesLeft) {
        selection.tl.x = pt.x;
        selection.br.y = pt.y;
    } else {
        selection.br = pt;
    }

    hideCursor();
    redraw();
}

void VtermImpl::selectUpdate(int pX, int pY) {
    logT << "selectUpdate (" << pX << "," << pY << ")" << std::endl;

    Point pt = selectionPoint(pX, pY);

    Rect& selection = cf->getSelection();

    if (selection.rectangular) {
        if (selectUpdatesLeft && pt.x > selection.br.x) {
            std::swap(selection.tl.x, selection.br.x);
            selectUpdatesLeft = false;
        } else if (!selectUpdatesLeft && pt.x < selection.tl.x) {
            std::swap(selection.tl.x, selection.br.x);
            selectUpdatesLeft = true;
        }

        if (selectUpdatesTop && pt.y > selection.br.y) {
            std::swap(selection.tl.y, selection.br.y);
            selectUpdatesTop = false;
        } else if (!selectUpdatesTop && pt.y < selection.tl.y) {
            std::swap(selection.tl.y, selection.br.y);
            selectUpdatesTop = true;
        }

        if (selectUpdatesTop && selectUpdatesLeft) {
            selection.tl = pt;
        } else if (selectUpdatesTop) {
            selection.br.x = pt.x;
            selection.tl.y = pt.y;
        } else if (selectUpdatesLeft) {
            selection.tl.x = pt.x;
            selection.br.y = pt.y;
        } else {
            selection.br = pt;
        }
    } else if (selectUpdatesTop) {
        if (selection.br < pt) {
            selection.tl = selection.br;
            selection.br = pt;
            selectUpdatesTop = selectUpdatesLeft = false;
        } else {
            selection.tl = pt;
        }
    } else {
        if (pt < selection.tl) {
            selection.br = selection.tl;
            selection.tl = pt;
            selectUpdatesTop = selectUpdatesLeft = true;
        } else {
            selection.br = pt;
        }
    }
    redraw();
}

bool VtermImpl::selectFinish(std::string& utf8_selection) {
    logT << "selectFinish ()" << std::endl;

    showCursor();
    redraw();

    return cf->getSelectedUtf8(utf8_selection);
}

void VtermImpl::selectClear() {
    logT << "selectClear ()" << std::endl;
    cf->getSelection().clear();
    redraw();
}

void VtermImpl::selectRectangularModeToggle() {
    logT << "selectRectangularModeToggle ()" << std::endl;
    Rect& selection = cf->getSelection();
    selection.toggleRectangular();
    if (selection.rectangular && selection.br.x < selection.tl.x) {
        // A valid linear selection is ordered by row and may therefore have
        // its top endpoint to the right of its bottom endpoint.  Rectangular
        // selection requires independently ordered axes.  Preserve which
        // horizontal edge is being dragged while normalizing the corners.
        std::swap(selection.tl.x, selection.br.x);
        selectUpdatesLeft = true;
    }
    redraw();
}

void VtermImpl::pasteSelection(const std::string& utf8_selection) {
    std::ostringstream oss;

    if (bracketedPasteMode) {
        oss << "\x1b[200~";
    }

    for (const auto ch : utf8_selection) {
        oss << (ch == '\n' ? '\r' : ch);
    }

    if (bracketedPasteMode) {
        oss << "\x1b[201~";
    }

    if (oss.str().size()) {
        writePty(oss.str().c_str(), true);
    }
}

Vterm* Vterm::create(Composer& composer, VtermHost& host, Pty& pty, u16 glyphPx, u16 glyphPy, u16 winPx, u16 winPy) {
    Output* dump = nullptr;
    if (opts.dump != nullptr) {
        const int rawFd = ::open(opts.dump, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (rawFd < 0) {
            Errno().raise(StringBuilder()
                          << StringView(u8"can not open dump file ")
                          << StringView(opts.dump));
        }
        auto* fd = composer.pool->make<ScopedFD>(rawFd);
        dump = createOutBuf(composer.pool, *createFDRegular(composer.pool, *fd));
    }
    return composer.pool->make<VtermImpl>(host, pty, dump, glyphPx, glyphPy, winPx, winPy);
}
