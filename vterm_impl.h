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
#include <std/sys/types.h>

#include "vterm.h"
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
class VtermImpl final : public Vterm {
public:
    VtermImpl(VtermHost& host, Pty& pty,
              u16 glyphPx, u16 glyphPy,
              u16 winPx, u16 winPy);

    ~VtermImpl() = default;

    bool getScreenReverseVideo() const { return screenReverseVideo; }
    u8 getLedState() const { return ledState; }
    bool getReverseWrapMode() const { return reverseWrapMode; }
    bool getNationalReplacementMode() const { return nationalReplacementMode; }
    bool getMetaMode() const { return eightBitInput; }

    void resize(u16 winPx, u16 winPy);

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
    int writePty(u8 ch, VtModifier modifiers = VtModifier::none,
                 bool userInput = false);
    int writePty(const char* cstr, bool userInput = false);
    int writePty(const char* data, size_t size, bool userInput);
    int writePty(const u8* ucstr, size_t len,
                 bool userInput = false);
    bool flushPtyOutput();
    bool hasPendingPtyOutput() const {
        return ptyOutputOffset < ptyOutput.size();
    }
    size_t pendingPtyOutputBytes() const {
        return ptyOutput.size() - ptyOutputOffset;
    }
    int writeKittyKey(VtKey key, u16 modifiers,
                      KeyEventType event);
    int writeKittyKey(u32 key, u32 shiftedKey,
                      u32 baseLayoutKey, u16 modifiers,
                      KeyEventType event);
    u8 getKittyKeyboardFlags() const;

    bool readPty();
    bool servicePty(bool readable, bool writable);
    void feedPtyOutput(const std::string& output);

    const MouseTrackingState& getMouseTrackingState() const;
    bool mouseHighlightRelease(u16 endX, u16 endY,
                               u16 mouseX, u16 mouseY);
    void setLocatorPosition(u16 column, u16 row,
                            u16 pixelX, u16 pixelY,
                            u8 buttons = 0);
    void reportLocatorButton(u8 button, bool pressed);

    void setHasFocus(bool);
    void setHyperlink(const std::string& parametersAndUri);
    std::string getHyperlink(int pX, int pY) const;
    size_t getHyperlinkCount() const {
        return hyperlinks.size();
    }
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
    std::string getLocalEcho(const u8* const begin,
                             const u8* const end);
    bool processInput(const u8* input, int size, bool refresh = true);
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
    void eraseRow(u16 pY);
    void eraseRows(u16 startY, u16 count);
    void copyRow(u16 dstY, u16 srcY);
    void insertRows(u16 startY, u16 count);
    void deleteRows(u16 startY, u16 count);
    void insertCols(u16 startX, u16 count);
    void deleteCols(u16 startX, u16 count);
    void clearWideCellAt(u16 row, u16 column);
    void normalizeWideCells(u16 row);

    struct Rectangle {
        u16 top;
        u16 left;
        u16 bottom;
        u16 right;
    };
    bool rectangleFromParams(size_t offset, Rectangle& rectangle) const;
    void rectangleOrigin(u16& rowBase, u16& columnBase,
                         u16& rowLimit, u16& columnLimit) const;

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
    void setPrivMode(u32 mode, bool set);
    bool getPrivMode(u32 mode) const;
    void csi_SGR();

    void csi_ecma48_SL();
    void csi_ecma48_SR();
    void csi_DECSCUSR();

    void csi_priDA();
    void csi_secDA();
    void csi_terDA();
    void csi_DSR();
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
    void applyPaletteColor(u16 index, Color color);

    VtermHost& host;
    Pty& pty;
    u16 winPx;
    u16 winPy;
    u16 nCols;
    u16 nRows;
    u16 glyphPx;
    u16 glyphPy;
    bool ptyReceivedInput = false;
    std::u8string ptyOutput;
    size_t ptyOutputOffset = 0;

    Frame frame_pri;
    Frame frame_alt;
    Frame* cf;
    u16 posX = 0;
    u16 posY = 0;
    u16 marginTop;
    u16 marginBottom;
    bool lastCol = false;

    TerminalCell attrs;
    Color* fg = &attrs.fg;
    Color* bg = &attrs.bg;
    i32* fgIndex = &attrs.fg_index;
    i32* bgIndex = &attrs.bg_index;
    Color palette256[256];
    Color originalPalette256[256];
    Color defaultFgColor;
    Color defaultBgColor;
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
    Frame::Grapheme inputGrapheme;
    GraphemeBreaker inputGraphemeBreaker;
    Frame* inputGraphemeFrame = nullptr;
    u16 inputGraphemeX = 0;
    u16 inputGraphemeY = 0;
    std::vector<unsigned char> argBuf;
    bool argBufOverflowed = false;
    unsigned char scsDst;
    unsigned char scsMod;

    VtModifier modifiers = VtModifier::none;

    bool showCursorMode = true;
    TerminalCursor::Style cursorShape =
        TerminalCursor::Style::filled_block;
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
    void switchScreenBufferMode(bool altScreenBufferMode,
                                bool clearAlternate = false);

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
        Charset g[4] =
            {Charset::UTF8, Charset::UTF8, Charset::UTF8, Charset::UTF8};

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

#include "vterm.icc"
