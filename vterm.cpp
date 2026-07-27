/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* part of this file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#include "vterm.h"
#include "vterm_trace.h"
#include "vterm_test.h"
#include "base64.h"
#include "cell_extra_store.h"
#include "clipboard.h"
#include "color_spec.h"
#include "composer.h"
#include "desktop_actions.h"
#include "input_sink.h"
#include "keyboard.h"
#include "mouse_frontend.h"
#include "mouse_protocol.h"
#include "screen.h"
#include "unicode_map.h"
#include "grapheme.h"
#include "hex.h"
#include "listener.h"
#include "options.h"
#include "utf8.h"
#include "vterm_host.h"

#include <std/alg/minmax.h>
#include <std/dbg/assert.h>
#include <std/mem/obj_pool.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/sys/types.h>
#include <std/ios/out.h>
#include <std/ios/output.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/fd.h>
#include <std/sys/throw.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <map>
#include <set>
#include <sys/types.h>

#if defined(__SSE2__)
    #include <emmintrin.h>
#endif

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
    constexpr size_t ptyProtocolHighWater = 1024 * 1024;

    StringView stringView(const std::string& value) {
        return StringView((const u8*)(value.data()), value.size());
    }

    struct GraphemeBuffer {
        void clear();
        void push_back(u32 codepoint);
        bool empty() const;
        size_t size() const;
        const u32* data() const;

        constexpr static size_t inlineCapacity = 4;
        std::array<u32, inlineCapacity> inlineValues = {};
        std::vector<u32> overflowValues;
        size_t size_ = 0;
    };

    struct VtermInputSpec {
        VtKey key;
        const char* input;
        size_t length = 0;

        size_t getLength() const;
    };

    template <bool traced>
    struct VtermImpl;

    template <bool traced>
    struct VtermInput {
        explicit VtermInput(VtermImpl<traced>* terminal);

        bool key(const KeyInput& input);
        bool text(const TextInput& input);
        bool pointerMotion(const PointerMotionInput& input);
        bool pointerButton(const PointerButtonInput& input);
        bool scroll(const ScrollInput& input);
        void focus(bool focused);
        void pointerPresence(bool present);
        void flush();

        VtModifier legacyModifiers(u16 modifiers) const;
        u16 kittyModifiers(u16 modifiers) const;
        VtKey keypadKey(InputKey key, bool numLock) const;
        VtKey specialKey(InputKey key, u16 modifiers) const;
        void mouseProtocolCoordinates(MouseTrackingEnc encoding, int pixelX, int pixelY, u16& column, u16& row) const;
        void sendMouseProtocol(MouseTrackingEnc encoding, MouseEventType type, u16 modifiers, int button, int column, int row);
        void sendMouseButtonProtocol(MouseEventType type, int button, int pixelX, int pixelY, u16 modifiers, const MouseTrackingState& tracking);
        bool refreshHyperlink();
        void refreshHyperlinkAndRedraw();
        void updatePointer(int pixelX, int pixelY, u16 modifiers);
        void updatePointerModifiers(const KeyInput& input);
        ScreenHyperlink resolveLink(int pixelX, int pixelY);
        bool paste(bool primary);

        struct PendingTextKey {
            bool active = false;
            u32 primary = 0;
            u32 base = 0;
            u16 modifiers = 0;
            VtermKeyEventType event = VtermKeyEventType::Press;
        };

        VtermImpl<traced>* terminal;
        MouseFrontendState mouse;
        PendingTextKey pendingTextKey;
        unsigned suppressedTextInputs = 0;
        bool suppressRepeatedTextInput = false;
        bool hyperlinkClick = false;
        int pointerX = 0;
        int pointerY = 0;
        u16 pointerModifiers = 0;
        u32 hoveredHyperlink = 0;
        u32 hoveredLinkBegin = 0;
        u32 hoveredLinkEnd = 0;
        bool pointerPresent = false;
        bool pointerPositionKnown = false;
        bool pointerFocused = true;
        Buffer schemeScratch;
        bool locallyConsumedKeys[(unsigned)(InputKey::Count) + 128]{};
    };

    template <bool traced>
    struct CallVtermResize: Listener {
        explicit CallVtermResize(VtermImpl<traced>* parent);

        void onListen(void*) override;

        VtermImpl<traced>* parent;
    };

    template <bool traced>
    struct CallVtermFontChanged: Listener {
        explicit CallVtermFontChanged(VtermImpl<traced>* parent);

        void onListen(void*) override;

        VtermImpl<traced>* parent;
    };

    template <bool traced>
    struct VtermImpl final: public Vterm, public InputSink {
        VtermImpl(Composer& composer, VtermHost& host, VtermTrace* trace, Output* dump);

        ~VtermImpl();

        void feedPty(StringView bytes) override;
        void expose() override;
        void focus(bool focused) override;
        bool key(const KeyInput& input) override;
        bool text(const TextInput& input) override;
        bool pointerMotion(const PointerMotionInput& input) override;
        bool pointerButton(const PointerButtonInput& input) override;
        bool scroll(const ScrollInput& input) override;
        void pointerPresence(bool present) override;
        void flush() override;
        void key(VtKey key, VtModifier modifiers);
        void character(u8 byte, VtModifier modifiers);
        void sendBytes(StringView bytes, bool userInput) override;
        void kittyKey(VtKey key, u16 modifiers, VtermKeyEventType event);
        void kittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, VtermKeyEventType event);
        bool mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY);
        void locatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons);
        void locatorButton(u8 button, bool pressed);
        void scrollUp(u16 count);
        void scrollDown(u16 count);
        void pageUp();
        void pageDown();
        void selectionStart(int pixelX, int pixelY, bool cycleSnapTo);
        void selectionExtend(int pixelX, int pixelY, bool cycleSnapTo);
        void selectionUpdate(int pixelX, int pixelY);
        VtermTextResult selectionFinish();
        void selectionClear();
        void selectionRectangular();
        void paste(StringView text);
        ScreenHyperlink resolveHyperlink(int pixelX, int pixelY) const;
        StringView hyperlinkAt(int pixelX, int pixelY);
        bool expireSynchronizedOutput(bool force) override;
        bool advanceAnimation(bool force) override;
        VtermOutput output() override;
        void consume(const VtermConsume& consumed) override;
        VtermState state() const override;
        TestApi* testApi() override;

        bool getPrivateMode(u32 mode) const;

        void resizeGrid();
        void fontChanged();
        void createFreshScreen(Screen*& frame, ObjPool*& pool, u16 saveLines);
        void createInactiveScreen(Screen*& frame, ObjPool*& pool);
        void resizeScreen(Screen*& frame, ObjPool*& pool, bool reflow, Screen::Cursor* cursor);

        void redraw();
        bool animationActive() const;

        using InputSpec = VtermInputSpec;

        int writePty(VtKey key, VtModifier modifiers = VtModifier::none, bool userInput = true);
        int writePty(u8 ch, VtModifier modifiers = VtModifier::none, bool userInput = true);
        int writePty(const char* cstr, bool userInput = false);
        int writePty(const char* data, size_t size, bool userInput);
        int writePty(const u8* ucstr, size_t len, bool userInput = false);
        void writeProtocolResponse(StringView prefix, StringView payload, StringView suffix = {});
        void compactPtyOutput();
        int writeKittyKey(VtKey key, u16 modifiers, VtermKeyEventType event);
        int writeKittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, VtermKeyEventType event);
        u8 getKittyKeyboardFlags() const;

        void setLocatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons = 0);
        void reportLocatorButton(u8 button, bool pressed);

        void setHasFocus(bool);
        std::string getHyperlink(int pX, int pY) const;
        void mouseWheelUp(u16 count = 1);
        void mouseWheelDown(u16 count = 1);
        void selectStart(int pX, int pY, bool cycleSnapTo);
        void selectExtend(int pX, int pY, bool cycleSnapTo);
        void selectUpdate(int pX, int pY);
        bool selectFinish(std::string& utf8_selection);
        void selectClear();
        void selectRectangularModeToggle();

        void pasteSelection(const std::string& utf8_selection);

        Point selectionPoint(int pX, int pY) const;
        std::string getLocalEcho(const u8* const begin, const u8* const end);
        bool processInput(const u8* input, int size, bool refresh = true);
        [[gnu::noinline]] bool processInputImpl(const u8* input, int size, bool refresh);
        void parseWithRagel(const u8* data, size_t len);
        bool ragelGroundContinuation(u8 ch);
        void ragelGroundHigh(u8 ch);
        void ragelGroundAscii(u8 ch);
        void ragelBeginString(VtermTraceString type, bool buffered);
        void ragelBeginDcs();
        void ragelBeginOsc();
        void resetOscColor();
        bool ragelStringContinuation(u8 ch);
        void ragelAppendString(u8 ch, size_t limit);
        void ragelAppendEscapedString(u8 ch, size_t limit);
        void ragelFinishDcs();
        void ragelFinishOsc();
        StringView ragelOscPayload() const noexcept;

        struct PresentationState {
            Screen* frame;
            TerminalCursor cursor;
            Rect selection;
            u16 columns;
            u16 rows;
            u32 viewOffset;
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
        void fillTerminalUpdate(TerminalUpdate& update, Screen& frame, const TerminalCellSpan* spans, size_t spanCount);

        void writeCsiResponse(StringView payload);
        void writeDcsResponse(StringView payload);
        void writeOscResponse(StringView payload);

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
        void resetTerminal();
        void resetAttrs();
        void resetScreen(bool resetTabStops = true);
        void clearScreen();
        void fillScreen(u16 ch);
        void collectCellExtrasIfNeeded(bool force = false);
        void collectCellExtras();
        void updateExtraCellCount();

        bool stringUtf8Continuation(u8 ch);
        void beginCsi();
        void traceCsi(u8 finalByte);
        [[gnu::always_inline]] bool executeC0InSequence(unsigned char ch, bool stringData = false);
        void normalizeCursorPos();
        bool isCursorInsideMargins();
        void eraseRow(u16 pY);
        void eraseRows(u16 startY, u16 count);
        void copyRow(u16 dstY, u16 srcY);
        void insertRows(u16 startY, u16 count);
        void deleteRows(u16 startY, u16 count);
        void insertCols(u16 startX, u16 count);
        void deleteCols(u16 startX, u16 count);
        void eraseRangeInRow(u16 row, u16 start, u16 count);
        void selectiveEraseRangeInRow(u16 row, u16 start, u16 count);
        void eraseEcmaRangeInRow(u16 row, u16 start, u16 count);
        void eraseEcmaRow(u16 row);

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
        void placeGraphicChar(bool graphemeBoundary);
        void placeGraphicChar(bool graphemeBoundary, u8 width);
        template <bool insert>
        void placeAsciiRun(const u8* input, size_t size);
        size_t placeAsciiLines(const u8* input, size_t size);
        int placeUtf8Run(const u8* input, int size);
        template <bool hasWide>
        void placePreparedRun(const u32* input, const u8* widths, size_t size);
        u8 codepointData(u32 codepoint);
        void resetGraphemeInput();
        void jumpToNextTabStop();
        void setFgFromPalIx();
        void setBgFromPalIx();

        CellColor attrForeground() const noexcept {
            return attrs.foreground();
        }

        CellColor attrBackground() const noexcept {
            return attrs.background();
        }

        CellColor attrUnderlineColor() const noexcept {
            return attrs.inlineUnderlineColor();
        }

        void setAttrForeground(CellColor color) noexcept {
            attrs.setForeground(color);
            eraseAttrs.setForeground(color);
            eraseAttrs.setInlineUnderlineColor(color);
        }

        void setAttrBackground(CellColor color) noexcept {
            attrs.setBackground(color);
            eraseAttrs.setBackground(color);
        }

        void setAttrUnderlineColor(CellColor color) noexcept {
            attrs.setInlineUnderlineColor(color);
        }

        void inp_LF();
        void inp_CR();
        void inp_HT();
        bool performIndex();
        void moveCursorBackward(u32 count);
        void scrollRegionUp(u16 count);
        void scrollRegionDown(u16 count);

        void esc_DCS(unsigned char fin);
        bool esc_IND();
        void esc_RI();
        void esc_NEL();
        void esc_BI();
        void esc_FI();
        void esc_HTS();
        void esc_SPA();
        void esc_EPA();
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
        void osc_TITLE_0(StringView);
        void osc_TITLE_1(StringView);
        void osc_TITLE_2(StringView);
        void osc_PALETTE(u32, Color, bool);
        void osc_SPECIAL_COLOR(u32, Color, bool);
        void osc_SPECIAL_COLOR_MODE(u32, u32);
        void osc_CWD(StringView, StringView, bool);
        void osc_HYPERLINK(StringView, bool, StringView);
        void osc_NOTIFY(StringView);
        void osc_PROGRESS(u32, u32);
        void osc_DYNAMIC_COLOR(u32, Color, bool);
        void osc_CLIPBOARD_QUERY(StringView, bool, bool, u8, bool);
        void osc_CLIPBOARD_WRITE(StringView, StringView, bool, bool, bool);
        void osc_CLIPBOARD_MALFORMED(StringView);
        void osc_NOTIFICATION_CAPABILITIES(StringView);
        void osc_NOTIFICATION_CLOSE(StringView);
        Base64Decoder notificationDecoder(StringView, bool) const;
        void osc_NOTIFICATION_TITLE(StringView, StringView, const Base64Decoder&, bool, bool);
        void osc_NOTIFICATION_BODY(StringView, StringView, const Base64Decoder&, bool, bool);
        void applyNotificationPart(StringView, StringView, const Base64Decoder&, bool, bool, bool);
        void osc_RESET_PALETTE();
        void osc_RESET_PALETTE(u32);
        void osc_RESET_SPECIAL_COLOR();
        void osc_RESET_SPECIAL_COLOR(u32);
        void osc_RESET_DEFAULT_FOREGROUND();
        void osc_RESET_DEFAULT_BACKGROUND();
        void osc_RESET_CURSOR_COLOR();
        void osc_RESET_SELECTION_BACKGROUND();
        void osc_RESET_SELECTION_FOREGROUND();
        void osc_SHELL_A(StringView);
        void osc_SHELL_B(StringView);
        void osc_SHELL_C(StringView);
        void osc_SHELL_D(StringView);
        void osc_SHELL_UNKNOWN(StringView);
        void osc_UNKNOWN(u32, StringView);
        void csiq_DECSCL();
        void csi_XTWINOPS();
        void csi_XTTITLEMODE(bool set);
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
        void dcs_DECRQSS_DECSCL();
        void dcs_DECRQSS_SGR();
        void dcs_DECRQSS_DECSTBM();
        void dcs_DECRQSS_DECSLRM();
        void dcs_DECRQSS_DECSLPP();
        void dcs_DECRQSS_DECSCUSR();
        void dcs_DECRQSS_DECSCA();
        void dcs_DECRQSS_UNKNOWN();
        void writeDecrqssResponse(StringView);
        void dcs_XTGETTCAP(StringView encoded, StringView value);
        void dcs_XTGETTCAP_COMMIT();
        void dcs_DECUDK();

        void reportInBandResize();
        void reportColorScheme();
        void writeTitleResponse(char, StringView);
        void applyPaletteColor(u16 index, Color color);

        VtermInput<traced> input;
        Composer& composer;
        VtermHost& host;
        Output* dump;
        VtermTrace* const parserTrace;
        UnicodeMap<u8>* const unicodeProperties;
        Buffer ptyOutput;
        Buffer protocolResponseScratch;
        size_t ptyOutputOffset = 0;
        u64 droppedPtyResponses = 0;
        Vector<TerminalCellSpan> outputSpans;
        TerminalUpdate terminalUpdate;

        std::string inputResult;
        bool outputPending = false;
        Screen* updateScreen = nullptr;

        Vector<TerminalCell*> extraCells;
        u32 processInputDepth = 0;
        bool presentedSinceGcSafePoint = false;

        TerminalColors colors;
        Color originalPalette256[256];
        Screen* frame_pri = nullptr;
        ObjPool* framePriPool = nullptr;
        Screen* frame_alt = nullptr;
        ObjPool* frameAltPool = nullptr;
        Screen* cf = nullptr;
        u16 posX = 0;
        u16 posY = 0;
        u16 marginTop = 0;
        u16 marginBottom = 0;
        bool lastCol = false;

        TerminalCell attrs{};
        TerminalCell eraseAttrs{};
        Color cursorColor;
        Color selectionFgColor;
        Color selectionBgColor;
        u32 activeHyperlink = 0;
        u32 nextHyperlink = 1;
        u32 currentSemantic = 0;
        std::string windowTitle;
        std::string iconTitle;
        u8 titleModes = 0;

        struct SavedTitles {
            bool hasIcon = false;
            bool hasWindow = false;
            std::string icon;
            std::string window;
        };

        std::vector<SavedTitles> titleStack;

        struct NotificationPart {
            std::string text;
            Base64Decoder decoder;
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

        int ragelState = 0;
        bool ragelInitialized = false;
        // Whether a private/intermediate CSI prefix may still occur.  This is
        // parser state, rather than an input-buffer offset: PTY reads may split an
        // escape sequence at any byte.
        u8 csiPrefix = 0;
        u8 csiIntermediates[4] = {};
        u8 csiIntermediateCount = 0;
        constexpr const static size_t maxEscOps = 32;
        constexpr const static size_t maxDcsBytes = 4095;
        constexpr const static size_t maxOscBytes = 1024 * 1024;
        u32 inputOps[maxEscOps];
        unsigned char inputSeparators[maxEscOps] = {};
        bool inputPresent[maxEscOps] = {};
        size_t nInputOps = 0;
        Utf8Decoder utf8dec;
        GraphemeBuffer inputGrapheme;
        u32 inputGraphemeBase = 0;
        GraphemeBreaker inputGraphemeBreaker;
        Screen* inputGraphemeScreen = nullptr;
        u16 inputGraphemeX = 0;
        u16 inputGraphemeY = 0;
        bool inputGraphemeWide = false;
        TerminalCell inputGraphemeAttrs{};
        u32 inputGraphemeHyperlink = 0;
        u32 inputGraphemeSemantic = 0;
        Buffer argBuf;
        bool argBufOverflowed = false;
        size_t ragelStringLimit = 0;
        u8 stringUtf8Remaining = 0;

        struct DcsUdkDefinition {
            size_t valueOffset;
            size_t valueLength;
            VtKey key;
        };

        u8 dcsIntermediates[4] = {};
        u8 dcsIntermediateCount = 0;
        size_t dcsCapabilityOffset = 0;
        size_t dcsCapabilityDecodedLength = 0;
        u8 dcsCapabilityCandidates = 0;
        u8 dcsCapabilityHighNibble = 0;
        bool dcsCapabilityHasHighNibble = false;
        bool dcsCapabilityValid = false;
        bool dcsCapabilityComplete = false;
        Vector<DcsUdkDefinition> dcsUdkDefinitions;
        Buffer dcsDecoded;
        size_t dcsUdkValueOffset = 0;
        u32 dcsUdkCode = 0;
        VtKey dcsUdkKey = VtKey::NONE;
        u8 dcsUdkHighNibble = 0;
        bool dcsUdkHasCode = false;
        bool dcsUdkHasHighNibble = false;
        bool dcsUdkValid = false;
        bool dcsUdkInValue = false;
        u32 oscCommand = 0;
        size_t oscPayloadOffset = 0;
        bool oscCommandValid = false;
        bool oscTerminated = false;
        Buffer oscDecoded;
        u8 oscTitleHighNibble = 0;
        bool oscTitleHex = false;
        bool oscTitleHasHighNibble = false;
        bool oscTitleValid = false;
        bool oscTitleStopped = false;
        u8 oscCwdPercentHigh = 0;
        bool oscCwdValid = false;
        bool oscCwdDecode = false;
        size_t oscHyperlinkIdOffset = 0;
        size_t oscHyperlinkIdLength = 0;
        size_t oscHyperlinkUriOffset = 0;
        bool oscHyperlinkHasId = false;
        u32 oscProgressState = 0;
        u32 oscProgressPercent = 0;
        bool oscProgressStatePresent = false;
        bool oscProgressPercentPresent = false;
        bool oscProgressValid = false;
        Base64Decoder oscBase64;
        u8 osc52ReplySelector = 0;
        bool osc52Primary = false;
        bool osc52Clipboard = false;
        bool osc52SelectorSeen = false;
        bool osc52PayloadSeen = false;
        bool osc52Query = false;
        size_t oscNotificationFieldOffset = 0;
        size_t oscNotificationIdOffset = 0;
        size_t oscNotificationIdLength = 0;
        size_t oscNotificationPayloadOffset = 0;
        u32 oscNotificationPayloadBytes = 0;
        u8 oscNotificationKey = 0;
        bool oscNotificationValid = false;
        bool oscNotificationEncoded = false;
        bool oscNotificationFinal = false;
        bool oscNotificationQuery = false;
        bool oscNotificationClose = false;
        bool oscNotificationBody = false;
        Color oscColor{};
        double oscColorComponents[3]{};
        double oscColorMantissa = 0.0;
        double oscColorFraction = 0.1;
        u64 oscColorHex = 0;
        u32 oscColorExponent = 0;
        u8 oscColorComponent = 0;
        u8 oscColorDigits = 0;
        bool oscColorNegative = false;
        bool oscColorExponentNegative = false;
        bool oscColorValid = false;
        bool oscColorQuery = false;

        u32 oscFieldNumber = 0;
        u32 oscFieldFirst = 0;
        bool oscFieldNumeric = false;
        bool oscFieldPresent = false;
        bool oscFieldFirstValid = false;
        bool oscFieldHaveFirst = false;
        unsigned char scsDst;
        unsigned char scsMod;

        VtModifier modifiers = VtModifier::none;

        bool showCursorMode = true;
        bool noClearColumnMode = false;
        TerminalCursor::Style cursorShape = TerminalCursor::Style::filled_block;
        u8 cursorStyleParam = 2;
        bool cursorBlinkMode = false;
        bool haveBlinkingText = false;
        bool blinkVisible = true;
        std::chrono::steady_clock::time_point nextBlink;
        bool altScreenBufferMode = false;
        bool altScreenInitialized = false;
        bool autoWrapMode = true;
        bool autoRepeatMode = true;
        bool smoothScrollMode = false;
        bool allowColumnMode = false;
        bool moreFixMode = false;
        bool autoNewlineMode = false;
        bool keyboardLocked = false;
        bool insertMode = false;
        bool eraseModeAll = false;
        bool bkspSendsDel = true;
        bool localEcho = false;
        bool bracketedPasteMode = false;
        bool synchronizedOutputMode = false;
        bool colorSchemeUpdateMode = false;
        bool inBandResizeMode = false;
        bool printerControllerMode = false;
        bool autoPrintMode = false;
        bool printFormFeedMode = false;
        bool printExtentMode = false;

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
            VT200,
            VT300,
            VT400,
            VT500,
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

        struct SavedCursor {
            bool isSet = false;
            u16 posX = 0;
            u16 posY = 0;
            bool lastCol = false;
            TerminalCell attrs{};
            TerminalCell eraseAttrs{};
            OriginMode originMode = OriginMode::Absolute;
            CharsetState charsetState = CharsetState{};
        };

        SavedCursor savedCursorPri;
        SavedCursor savedCursorAlt;
        SavedCursor* savedCursor = &savedCursorPri;

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
    };

    template <bool traced>
    struct TestApiImpl final: public TestApi {
        explicit TestApiImpl(VtermImpl<traced>* vterm);

        VtermTestState inspect() const override;
        bool ansiMode(u32 mode) const override;
        bool privateMode(u32 mode) const override;
        VtermTestCell cell(u16 row, u16 column) const override;
        void key(VtKey key, VtModifier modifiers) override;
        void character(u8 byte, VtModifier modifiers) override;
        void kittyKey(VtKey key, u16 modifiers, VtermKeyEventType event) override;
        void kittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, VtermKeyEventType event) override;
        bool mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY) override;
        void locatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons) override;
        void locatorButton(u8 button, bool pressed) override;
        void scrollUp(u16 count) override;
        void scrollDown(u16 count) override;
        void pageUp() override;
        void pageDown() override;
        void selectionStart(int pixelX, int pixelY, bool cycleSnapTo) override;
        void selectionExtend(int pixelX, int pixelY, bool cycleSnapTo) override;
        void selectionUpdate(int pixelX, int pixelY) override;
        VtermTextResult selectionFinish() override;
        void selectionRectangular() override;
        void paste(StringView text) override;
        StringView hyperlinkAt(int pixelX, int pixelY) override;

        VtermImpl<traced>* vterm;
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

template <bool traced>
VtermInput<traced>::VtermInput(VtermImpl<traced>* terminal_)
    : terminal(terminal_)
{
}

template <bool traced>
VtModifier VtermInput<traced>::legacyModifiers(u16 modifiers) const {
    VtModifier result = VtModifier::none;
    if (modifiers & InputShift) {
        result = result | VtModifier::shift;
    }
    if (modifiers & InputControl) {
        result = result | VtModifier::control;
    }
    if (modifiers & InputAlt) {
        result = result | VtModifier::alt;
    }
    if ((modifiers & InputSuper) && terminal->eightBitInput) {
        result = result | VtModifier::alt;
    }
    return result;
}

template <bool traced>
u16 VtermInput<traced>::kittyModifiers(u16 modifiers) const {
    u16 result = 0;
    if (modifiers & InputShift) {
        result |= 1;
    }
    if (modifiers & InputAlt) {
        result |= 2;
    }
    if (modifiers & InputControl) {
        result |= 4;
    }
    if (modifiers & InputSuper) {
        result |= 8;
    }
    if (modifiers & InputCapsLock) {
        result |= 64;
    }
    if (modifiers & InputNumLock) {
        result |= 128;
    }
    return result;
}

template <bool traced>
VtKey VtermInput<traced>::keypadKey(InputKey key, bool numLock) const {
    using Key = VtKey;
    if (!numLock) {
        switch (key) {
            case InputKey::Keypad0:
                return Key::KP_Insert;
            case InputKey::Keypad1:
                return Key::KP_End;
            case InputKey::Keypad2:
                return Key::KP_Down;
            case InputKey::Keypad3:
                return Key::KP_PageDown;
            case InputKey::Keypad4:
                return Key::KP_Left;
            case InputKey::Keypad5:
                return Key::KP_Begin;
            case InputKey::Keypad6:
                return Key::KP_Right;
            case InputKey::Keypad7:
                return Key::KP_Home;
            case InputKey::Keypad8:
                return Key::KP_Up;
            case InputKey::Keypad9:
                return Key::KP_PageUp;
            case InputKey::KeypadDecimal:
                return Key::KP_Delete;
            default:
                break;
        }
    }
    switch (key) {
        case InputKey::Keypad0:
            return Key::KP_0;
        case InputKey::Keypad1:
            return Key::KP_1;
        case InputKey::Keypad2:
            return Key::KP_2;
        case InputKey::Keypad3:
            return Key::KP_3;
        case InputKey::Keypad4:
            return Key::KP_4;
        case InputKey::Keypad5:
            return Key::KP_5;
        case InputKey::Keypad6:
            return Key::KP_6;
        case InputKey::Keypad7:
            return Key::KP_7;
        case InputKey::Keypad8:
            return Key::KP_8;
        case InputKey::Keypad9:
            return Key::KP_9;
        case InputKey::KeypadDecimal:
            return Key::KP_Dot;
        case InputKey::KeypadDivide:
            return Key::KP_Slash;
        case InputKey::KeypadMultiply:
            return Key::KP_Star;
        case InputKey::KeypadSubtract:
            return Key::KP_Minus;
        case InputKey::KeypadAdd:
            return Key::KP_Plus;
        case InputKey::KeypadEnter:
            return Key::KP_Enter;
        case InputKey::KeypadEqual:
            return Key::KP_Equal;
        default:
            return Key::NONE;
    }
}

template <bool traced>
VtKey VtermInput<traced>::specialKey(InputKey key, u16 modifiers) const {
    using Key = VtKey;
    const Key keypad = keypadKey(key, (modifiers & InputNumLock) != 0);
    if (keypad != Key::NONE) {
        return keypad;
    }
    switch (key) {
        case InputKey::Enter:
            return Key::Return;
        case InputKey::Backspace:
            return Key::Backspace;
        case InputKey::Tab:
            return Key::Tab;
        case InputKey::Insert:
            return Key::Insert;
        case InputKey::Delete:
            return Key::Delete;
        case InputKey::Home:
            return Key::Home;
        case InputKey::End:
            return Key::End;
        case InputKey::Up:
            return Key::Up;
        case InputKey::Down:
            return Key::Down;
        case InputKey::Left:
            return Key::Left;
        case InputKey::Right:
            return Key::Right;
        case InputKey::PageUp:
            return Key::PageUp;
        case InputKey::PageDown:
            return Key::PageDown;
        case InputKey::F1:
            return Key::F1;
        case InputKey::F2:
            return Key::F2;
        case InputKey::F3:
            return Key::F3;
        case InputKey::F4:
            return Key::F4;
        case InputKey::F5:
            return Key::F5;
        case InputKey::F6:
            return Key::F6;
        case InputKey::F7:
            return Key::F7;
        case InputKey::F8:
            return Key::F8;
        case InputKey::F9:
            return Key::F9;
        case InputKey::F10:
            return Key::F10;
        case InputKey::F11:
            return Key::F11;
        case InputKey::F12:
            return Key::F12;
        case InputKey::F13:
            return Key::F13;
        case InputKey::F14:
            return Key::F14;
        case InputKey::F15:
            return Key::F15;
        case InputKey::F16:
            return Key::F16;
        case InputKey::F17:
            return Key::F17;
        case InputKey::F18:
            return Key::F18;
        case InputKey::F19:
            return Key::F19;
        case InputKey::F20:
            return Key::F20;
        case InputKey::CapsLock:
            return Key::CapsLock;
        case InputKey::ScrollLock:
            return Key::ScrollLock;
        case InputKey::NumLock:
            return Key::NumLock;
        case InputKey::PrintScreen:
            return Key::Print;
        case InputKey::Pause:
            return Key::Pause;
        case InputKey::Menu:
            return Key::Menu;
        case InputKey::LeftShift:
            return Key::LeftShift;
        case InputKey::LeftControl:
            return Key::LeftControl;
        case InputKey::LeftAlt:
            return Key::LeftAlt;
        case InputKey::LeftSuper:
            return Key::LeftSuper;
        case InputKey::RightShift:
            return Key::RightShift;
        case InputKey::RightControl:
            return Key::RightControl;
        case InputKey::RightAlt:
            return Key::RightAlt;
        case InputKey::RightSuper:
            return Key::RightSuper;
        default:
            return Key::NONE;
    }
}

template <bool traced>
bool VtermInput<traced>::paste(bool primary) {
    if (terminal->composer.clipboard == nullptr) {
        return false;
    }
    const StringView text = primary ? terminal->composer.clipboard->readPrimary() : terminal->composer.clipboard->readClipboard();
    if (text.empty()) {
        return false;
    }
    terminal->paste(text);
    return true;
}

template <bool traced>
ScreenHyperlink VtermInput<traced>::resolveLink(int pixelX, int pixelY) {
    const ScreenHyperlink link = terminal->resolveHyperlink(pixelX, pixelY);
    if (link.payload.empty() || link.displayId != 0) {
        return link;
    }
    DesktopActions* const desktop = terminal->composer.desktopActions;
    if (desktop == nullptr || link.scheme.empty()) {
        return {};
    }
    const StringView scheme = link.scheme.lower(schemeScratch);
    return desktop->handlesUriScheme(scheme) ? link : ScreenHyperlink{};
}

template <bool traced>
bool VtermInput<traced>::refreshHyperlink() {
    ScreenHyperlink next;
    if (pointerFocused && pointerPresent && pointerPositionKnown && (pointerModifiers & InputControl)) {
        next = resolveLink(pointerX, pointerY);
    }
    if (next.displayId == hoveredHyperlink && next.begin == hoveredLinkBegin && next.end == hoveredLinkEnd) {
        return false;
    }
    const bool wasActive = hoveredHyperlink != 0 || hoveredLinkBegin < hoveredLinkEnd;
    hoveredHyperlink = next.displayId;
    hoveredLinkBegin = next.begin;
    hoveredLinkEnd = next.end;
    const bool active = hoveredHyperlink != 0 || hoveredLinkBegin < hoveredLinkEnd;
    if (active != wasActive && terminal->composer.desktopActions != nullptr) {
        terminal->composer.desktopActions->pointerIcon(active ? PointerIcon::Link : PointerIcon::Text);
    }
    return true;
}

template <bool traced>
void VtermInput<traced>::refreshHyperlinkAndRedraw() {
    if (refreshHyperlink()) {
        terminal->redraw();
    }
}

template <bool traced>
void VtermInput<traced>::updatePointer(int pixelX, int pixelY, u16 modifiers) {
    pointerX = pixelX;
    pointerY = pixelY;
    pointerModifiers = modifiers;
    pointerPresent = true;
    pointerPositionKnown = true;
    refreshHyperlinkAndRedraw();
}

template <bool traced>
void VtermInput<traced>::updatePointerModifiers(const KeyInput& input) {
    pointerModifiers = input.modifiers;
    if (input.key == InputKey::LeftControl || input.key == InputKey::RightControl) {
        if (input.action == InputAction::Release) {
            pointerModifiers &= ~InputControl;
        } else {
            pointerModifiers |= InputControl;
        }
    }
    refreshHyperlinkAndRedraw();
}

template <bool traced>
void VtermInput<traced>::flush() {
    if (!pendingTextKey.active) {
        return;
    }
    const PendingTextKey pending = pendingTextKey;
    pendingTextKey.active = false;
    terminal->writeKittyKey(pending.primary, 0, pending.base, pending.modifiers, pending.event);
}

template <bool traced>
bool VtermInput<traced>::key(const KeyInput& input) {
    flush();
    updatePointerModifiers(input);
    suppressRepeatedTextInput = input.action == InputAction::Repeat && !terminal->autoRepeatMode;
    const VtModifier modifiers = legacyModifiers(input.modifiers);
    const bool pressed = input.action != InputAction::Release;
    const unsigned keyIndex = input.key == InputKey::Printable && input.baseCodepoint < 128 ? (unsigned)(InputKey::Count) + input.baseCodepoint : (unsigned)(input.key);
    if (!pressed && keyIndex < sizeof(locallyConsumedKeys) && locallyConsumedKeys[keyIndex]) {
        locallyConsumedKeys[keyIndex] = false;
        return true;
    }
    const auto runLocal = [&](const auto& operation) {
        if (!pressed) {
            return;
        }
        if (keyIndex < sizeof(locallyConsumedKeys)) {
            locallyConsumedKeys[keyIndex] = true;
        }
        operation();
    };

    if (input.key == InputKey::PageUp && modifiers == VtModifier::shift) {
        runLocal([&]() {
            terminal->pageUp();
        });
        return true;
    }
    if (input.key == InputKey::PageDown && modifiers == VtModifier::shift) {
        runLocal([&]() {
            terminal->pageDown();
        });
        return true;
    }
    if (input.baseCodepoint == 'c' && modifiers == VtModifier::shift_control) {
        runLocal([&]() {
            if (terminal->composer.clipboard != nullptr) {
                const StringView content = terminal->composer.clipboard->readPrimary();
                if (!content.empty()) {
                    terminal->composer.clipboard->writeClipboard(content);
                }
            }
        });
        return true;
    }
    if (input.baseCodepoint == 'v' && modifiers == VtModifier::shift_control) {
        runLocal([&]() {
            paste(false);
        });
        return true;
    }
    if ((input.key == InputKey::Insert || input.key == InputKey::Keypad0) && modifiers == VtModifier::shift) {
        runLocal([&]() {
            paste(true);
        });
        return true;
    }
    if (input.baseCodepoint == ' ' && mouse.selectionOngoing()) {
        runLocal([&]() {
            terminal->selectionRectangular();
        });
        return true;
    }
    if (suppressRepeatedTextInput) {
        return true;
    }

    const u8 kittyFlags = terminal->getKittyKeyboardFlags();
    const u16 kittyMods = kittyModifiers(input.modifiers);
    const VtermKeyEventType event = input.action == InputAction::Release ? VtermKeyEventType::Release : input.action == InputAction::Repeat ? VtermKeyEventType::Repeat : VtermKeyEventType::Press;
    if (kittyFlags) {
        if (input.key == InputKey::Escape) {
            terminal->writeKittyKey(27, 0, 0, kittyMods, event);
            return true;
        }
        const VtKey special = specialKey(input.key, input.modifiers);
        if (special != VtKey::NONE) {
            terminal->writeKittyKey(special, kittyMods, event);
            return true;
        }
        const u32 primaryKey = input.layoutCodepoint != 0 ? input.layoutCodepoint : input.baseCodepoint;
        const u16 textMods = kittyMods & ~(64 | 128);
        if (primaryKey && ((textMods & (2 | 4 | 8)) || (kittyFlags & 0x08))) {
            if (pressed && !(textMods & (2 | 4 | 8))) {
                pendingTextKey = {true, primaryKey, input.baseCodepoint, textMods, event};
                return true;
            }
            terminal->writeKittyKey(primaryKey, 0, input.baseCodepoint, textMods, event);
            if (pressed && (((textMods & (2 | 8)) && !(textMods & 4)) || (kittyFlags & 0x08))) {
                ++suppressedTextInputs;
            }
            return true;
        }
    }
    if (!pressed) {
        return true;
    }
    if (input.key == InputKey::Escape) {
        terminal->writePty((u8)('\x1b'), modifiers, true);
        return true;
    }
    const VtKey special = specialKey(input.key, input.modifiers);
    if (special != VtKey::NONE) {
        terminal->writePty(special, modifiers, true);
        return true;
    }
    if (input.modifiers & InputControl) {
        int controlKey = (int)(input.baseCodepoint);
        if (controlKey >= 'a' && controlKey <= 'z') {
            controlKey -= 'a' - 'A';
        }
        u8 character = 0;
        if (controlCharacter(controlKey, input.modifiers & InputShift, character)) {
            terminal->writePty(character, modifiers, true);
        }
    }
    return true;
}

template <bool traced>
bool VtermInput<traced>::text(const TextInput& input) {
    if (suppressRepeatedTextInput) {
        return true;
    }
    if (pendingTextKey.active) {
        const PendingTextKey pending = pendingTextKey;
        pendingTextKey.active = false;
        const u32 alternate = input.codepoint != pending.primary ? input.codepoint : 0;
        terminal->writeKittyKey(pending.primary, alternate, pending.base, pending.modifiers, pending.event);
        return true;
    }
    if (suppressedTextInputs) {
        --suppressedTextInputs;
        return true;
    }
    if (input.codepoint == 0) {
        return false;
    }
    const VtModifier modifiers = legacyModifiers(input.modifiers);
    if (input.codepoint < 0x80) {
        terminal->writePty((u8)(input.codepoint), modifiers, true);
        return true;
    }
    u8 encoded[4];
    size_t size = 0;
    Utf8Encoder::pushUnicode(input.codepoint, [&](u8 byte) {
        encoded[size++] = byte;
    });
    if ((input.modifiers & InputAlt) && terminal->altSendsEscape) {
        terminal->writePty((const u8*)("\x1b"), 1, true);
    }
    terminal->writePty(encoded, size, true);
    return true;
}

template <bool traced>
void VtermInput<traced>::mouseProtocolCoordinates(MouseTrackingEnc encoding, int pixelX, int pixelY, u16& column, u16& row) const {
    const MouseGeometry geometry = {terminal->composer.pixelWidth, terminal->composer.pixelHeight, opts.border, terminal->composer.glyphWidth, terminal->composer.glyphHeight};
    const MouseProtocolPoint point = mouseProtocolPoint(encoding, pixelX, pixelY, geometry);
    column = point.column;
    row = point.row;
}

template <bool traced>
void VtermInput<traced>::sendMouseProtocol(MouseTrackingEnc encoding, MouseEventType type, u16 modifiers, int button, int column, int row) {
    const unsigned protocolModifiers = mouseProtocolModifiers(modifiers);
    StringBuilder report;
    if (encodeMouseProtocol(report, encoding, type, protocolModifiers, mouse.motionButton(), button, column, row)) {
        terminal->writePty((const u8*)(report.data()), report.used(), false);
    }
}

template <bool traced>
void VtermInput<traced>::sendMouseButtonProtocol(MouseEventType type, int button, int pixelX, int pixelY, u16 modifiers, const MouseTrackingState& tracking) {
    if (!mouseButtonReportAllowed(tracking.mode, type, button)) {
        return;
    }
    u16 column = 0;
    u16 row = 0;
    mouseProtocolCoordinates(tracking.enc, pixelX, pixelY, column, row);
    if (tracking.mode == MouseTrackingMode::VT200_Highlight && type == MouseEventType::Release) {
        terminal->mouseHighlightRelease(column, row, column, row);
        return;
    }
    sendMouseProtocol(tracking.enc, type, tracking.mode == MouseTrackingMode::X10_Compat ? 0 : modifiers, button, column, row);
}

template <bool traced>
bool VtermInput<traced>::pointerButton(const PointerButtonInput& input) {
    updatePointer(input.pixelX, input.pixelY, input.modifiers);
    const int button = (int)(input.button);
    mouse.updateButton(button, input.pressed);
    const MouseTrackingState tracking = terminal->mouseTrk;
    const int protocolButton = mouseTerminalButton(button);
    u16 locatorColumn = 1;
    u16 locatorRow = 1;
    mouseProtocolCoordinates(MouseTrackingEnc::Default, input.pixelX, input.pixelY, locatorColumn, locatorRow);
    terminal->setLocatorPosition(locatorColumn, locatorRow, std::max(1, input.pixelX + 1), std::max(1, input.pixelY + 1), 0);
    if (protocolButton >= 1 && protocolButton <= 4) {
        terminal->reportLocatorButton(protocolButton, input.pressed);
    }
    if (!input.pressed && input.button == PointerButton::Primary && hyperlinkClick) {
        hyperlinkClick = false;
        return true;
    }
    if (input.pressed && input.button == PointerButton::Primary) {
        hyperlinkClick = false;
        if (input.modifiers & InputControl) {
            const ScreenHyperlink link = resolveLink(input.pixelX, input.pixelY);
            if (!link.payload.empty() && terminal->composer.desktopActions != nullptr) {
                hyperlinkClick = true;
                terminal->composer.desktopActions->openUri(link.payload);
                return true;
            }
        }
    }
    if (mouse.protocolActive(input.modifiers, tracking.mode)) {
        sendMouseButtonProtocol(input.pressed ? MouseEventType::Press : MouseEventType::Release, protocolButton, input.pixelX, input.pixelY, input.modifiers, tracking);
        return true;
    }
    if (input.pressed) {
        const bool cycleSnapTo = mouse.registerClick(button, input.pixelX, input.pixelY, input.time) > 1;
        if (input.button == PointerButton::Primary) {
            terminal->selectionStart(input.pixelX, input.pixelY, cycleSnapTo);
            mouse.beginSelection();
        } else if (input.button == PointerButton::Secondary) {
            terminal->selectionExtend(input.pixelX, input.pixelY, cycleSnapTo);
            mouse.beginSelection();
        }
        return true;
    }
    if (input.button == PointerButton::Primary || input.button == PointerButton::Secondary) {
        mouse.endSelection();
        const VtermTextResult selected = terminal->selectionFinish();
        if (selected.status && terminal->composer.clipboard != nullptr) {
            terminal->composer.clipboard->writePrimary(selected.text);
            if (opts.autoCopyMode) {
                terminal->composer.clipboard->writeClipboard(selected.text);
            }
        }
    } else if (input.button == PointerButton::Middle) {
        paste(true);
    }
    return true;
}

template <bool traced>
bool VtermInput<traced>::pointerMotion(const PointerMotionInput& input) {
    updatePointer(input.pixelX, input.pixelY, input.modifiers);
    u16 locatorColumn = 1;
    u16 locatorRow = 1;
    mouseProtocolCoordinates(MouseTrackingEnc::Default, input.pixelX, input.pixelY, locatorColumn, locatorRow);
    terminal->setLocatorPosition(locatorColumn, locatorRow, std::max(1, input.pixelX + 1), std::max(1, input.pixelY + 1), 0);
    const MouseTrackingState tracking = terminal->mouseTrk;
    if (mouse.protocolActive(input.modifiers, tracking.mode)) {
        if (tracking.mode == MouseTrackingMode::VT200_ButtonEvent && !mouse.primaryButtonPressed()) {
            return true;
        }
        if (tracking.mode != MouseTrackingMode::VT200_ButtonEvent && tracking.mode != MouseTrackingMode::VT200_AnyEvent) {
            return true;
        }
        u16 column = 0;
        u16 row = 0;
        mouseProtocolCoordinates(tracking.enc, input.pixelX, input.pixelY, column, row);
        if (mouse.reportMotion(column, row, tracking.mode, tracking.enc, tracking.generation)) {
            sendMouseProtocol(tracking.enc, MouseEventType::Motion, input.modifiers, 0, column, row);
        }
    } else if (mouse.buttons() & ((1u << (unsigned)(PointerButton::Primary)) | (1u << (unsigned)(PointerButton::Secondary)))) {
        terminal->selectionUpdate(input.pixelX, input.pixelY);
    }
    return true;
}

template <bool traced>
bool VtermInput<traced>::scroll(const ScrollInput& input) {
    updatePointer(input.pixelX, input.pixelY, input.modifiers);
    const MouseTrackingState tracking = terminal->mouseTrk;
    const bool reporting = mouse.protocolActive(input.modifiers, tracking.mode);
    const MouseWheelSteps steps = mouse.consumeWheel(input.x, input.y, reporting);
    if (reporting) {
        for (int k = 0; k < steps.y; ++k) {
            sendMouseButtonProtocol(MouseEventType::Press, 4, input.pixelX, input.pixelY, input.modifiers, tracking);
        }
        for (int k = 0; k < -steps.y; ++k) {
            sendMouseButtonProtocol(MouseEventType::Press, 5, input.pixelX, input.pixelY, input.modifiers, tracking);
        }
        for (int k = 0; k < -steps.x; ++k) {
            sendMouseButtonProtocol(MouseEventType::Press, 6, input.pixelX, input.pixelY, input.modifiers, tracking);
        }
        for (int k = 0; k < steps.x; ++k) {
            sendMouseButtonProtocol(MouseEventType::Press, 7, input.pixelX, input.pixelY, input.modifiers, tracking);
        }
    } else if (steps.y > 0) {
        terminal->mouseWheelUp(steps.y);
    } else if (steps.y < 0) {
        terminal->mouseWheelDown(-steps.y);
    }
    return true;
}

template <bool traced>
void VtermInput<traced>::focus(bool focused) {
    pointerFocused = focused;
    if (!focused) {
        pointerModifiers = 0;
        hyperlinkClick = false;
        mouse.clearButtons();
        mouse.endSelection();
        suppressedTextInputs = 0;
        suppressRepeatedTextInput = false;
        pendingTextKey.active = false;
        std::fill(std::begin(locallyConsumedKeys), std::end(locallyConsumedKeys), false);
    }
    refreshHyperlinkAndRedraw();
    terminal->setHasFocus(focused);
}

template <bool traced>
void VtermInput<traced>::pointerPresence(bool present) {
    mouse.resetMotion();
    pointerPresent = present;
    if (!present) {
        pointerPositionKnown = false;
    }
    refreshHyperlinkAndRedraw();
}

template <bool traced>
VtermImpl<traced>::~VtermImpl() {
    delete framePriPool;
    delete frameAltPool;
    composer.vterm = nullptr;
}

template <bool traced>
void VtermImpl<traced>::createFreshScreen(Screen*& frame, ObjPool*& pool, u16 saveLines) {
    ObjPool* const next = ObjPool::fromMemoryRaw();
    Screen* screen;
    try {
        screen = Screen::create(composer, *next, composer.columns, composer.rows, &colors, saveLines);
    } catch (...) {
        delete next;
        throw;
    }
    delete pool;
    pool = next;
    frame = screen;
}

template <bool traced>
void VtermImpl<traced>::createInactiveScreen(Screen*& frame, ObjPool*& pool) {
    ObjPool* const next = ObjPool::fromMemoryRaw();
    Screen* screen;
    try {
        screen = Screen::create(composer, *next);
    } catch (...) {
        delete next;
        throw;
    }
    delete pool;
    pool = next;
    frame = screen;
}

template <bool traced>
void VtermImpl<traced>::resizeScreen(Screen*& frame, ObjPool*& pool, bool reflow, Screen::Cursor* cursor) {
    // The state handle lives in the screen's own pool, so the old pool must
    // survive until the replacement has been laid out from it.
    ResizeState* const state = frame->moveInto();
    ObjPool* const next = ObjPool::fromMemoryRaw();
    Screen* screen;
    try {
        screen = Screen::create(composer, *next, *state, composer.columns, composer.rows, &colors, reflow, cursor);
    } catch (...) {
        delete next;
        throw;
    }
    delete pool;
    pool = next;
    frame = screen;
}

template <bool traced>
void VtermImpl<traced>::feedPty(StringView bytes) {
    if (bytes.empty()) {
        return;
    }
    if (dump != nullptr) {
        dump->write(bytes.data(), bytes.length());
    }
    processInput(bytes.data(), (int)(bytes.length()));
}

template <bool traced>
void VtermImpl<traced>::expose() {
    redraw();
}

template <bool traced>
void VtermImpl<traced>::focus(bool focused) {
    input.focus(focused);
}

template <bool traced>
bool VtermImpl<traced>::key(const KeyInput& value) {
    return input.key(value);
}

template <bool traced>
bool VtermImpl<traced>::text(const TextInput& value) {
    return input.text(value);
}

template <bool traced>
bool VtermImpl<traced>::pointerMotion(const PointerMotionInput& value) {
    return input.pointerMotion(value);
}

template <bool traced>
bool VtermImpl<traced>::pointerButton(const PointerButtonInput& value) {
    return input.pointerButton(value);
}

template <bool traced>
bool VtermImpl<traced>::scroll(const ScrollInput& value) {
    return input.scroll(value);
}

template <bool traced>
void VtermImpl<traced>::pointerPresence(bool present) {
    input.pointerPresence(present);
}

template <bool traced>
void VtermImpl<traced>::flush() {
    input.flush();
}

template <bool traced>
void VtermImpl<traced>::key(VtKey key_, VtModifier modifiers_) {
    writePty(key_, modifiers_, true);
}

template <bool traced>
void VtermImpl<traced>::character(u8 byte, VtModifier modifiers_) {
    writePty(byte, modifiers_, true);
}

template <bool traced>
void VtermImpl<traced>::sendBytes(StringView bytes, bool userInput) {
    writePty(bytes.data(), bytes.length(), userInput);
}

template <bool traced>
void VtermImpl<traced>::kittyKey(VtKey key_, u16 modifiers_, VtermKeyEventType event) {
    writeKittyKey(key_, modifiers_, event);
}

template <bool traced>
void VtermImpl<traced>::kittyKey(u32 key_, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers_, VtermKeyEventType event) {
    writeKittyKey(key_, shiftedKey, baseLayoutKey, modifiers_, event);
}

template <bool traced>
void VtermImpl<traced>::locatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons) {
    setLocatorPosition(column, row, pixelX, pixelY, buttons);
}

template <bool traced>
void VtermImpl<traced>::locatorButton(u8 button, bool pressed) {
    reportLocatorButton(button, pressed);
}

template <bool traced>
void VtermImpl<traced>::scrollUp(u16 count) {
    mouseWheelUp(count);
}

template <bool traced>
void VtermImpl<traced>::scrollDown(u16 count) {
    mouseWheelDown(count);
}

template <bool traced>
void VtermImpl<traced>::selectionStart(int pixelX, int pixelY, bool cycleSnapTo) {
    selectStart(pixelX, pixelY, cycleSnapTo);
}

template <bool traced>
void VtermImpl<traced>::selectionExtend(int pixelX, int pixelY, bool cycleSnapTo) {
    selectExtend(pixelX, pixelY, cycleSnapTo);
}

template <bool traced>
void VtermImpl<traced>::selectionUpdate(int pixelX, int pixelY) {
    selectUpdate(pixelX, pixelY);
}

template <bool traced>
VtermTextResult VtermImpl<traced>::selectionFinish() {
    inputResult.clear();
    const bool selected = selectFinish(inputResult);
    return {stringView(inputResult), selected};
}

template <bool traced>
void VtermImpl<traced>::selectionClear() {
    selectClear();
}

template <bool traced>
void VtermImpl<traced>::selectionRectangular() {
    selectRectangularModeToggle();
}

template <bool traced>
void VtermImpl<traced>::paste(StringView text) {
    pasteSelection(std::string((const char*)(text.data()), text.length()));
}

template <bool traced>
ScreenHyperlink VtermImpl<traced>::resolveHyperlink(int pixelX, int pixelY) const {
    if (pixelX < opts.border || pixelY < opts.border || pixelX >= composer.pixelWidth - opts.border || pixelY >= composer.pixelHeight - opts.border) {
        return {};
    }
    const u16 column = (pixelX - opts.border) / composer.glyphWidth;
    const u16 row = (pixelY - opts.border) / composer.glyphHeight;
    if (column >= cf->columns() || row >= cf->rows()) {
        return {};
    }
    return cf->hyperlinkAt(row, column);
}

template <bool traced>
StringView VtermImpl<traced>::hyperlinkAt(int pixelX, int pixelY) {
    const StringView payload = resolveHyperlink(pixelX, pixelY).payload;
    if (payload.empty()) {
        inputResult.clear();
    } else {
        inputResult.assign((const char*)(payload.data()), payload.length());
    }
    return stringView(inputResult);
}

template <bool traced>
void VtermImpl<traced>::fillTerminalUpdate(TerminalUpdate& update, Screen& frame, const TerminalCellSpan* spans, size_t spanCount) {
    update = {};
    update.spans = spans;
    update.spanCount = spanCount;
    update.colors = &colors;
    update.viewOffset = frame.getViewOffset();
    update.historyRows = frame.getHistoryRows();
    update.cursor = frame.getCursor();
    update.selection = frame.getSelectionForView();
    update.snappedSelection = frame.getSnappedSelection();
    update.selectionForeground = frame.getSelectionForeground();
    update.selectionBackground = frame.getSelectionBackground();
    update.selectionColorMask = frame.getSelectionColorMask();
    update.hoveredHyperlink = input.hoveredHyperlink;
    update.hoveredLinkBegin = input.hoveredLinkBegin;
    update.hoveredLinkEnd = input.hoveredLinkEnd;
    update.screenReverse = frame.getScreenReverseVideo();
    update.blinkVisible = frame.getBlinkVisible();
    update.cursorBlink = frame.getCursorBlink();
}

template <bool traced>
VtermOutput VtermImpl<traced>::output() {
    VtermOutput result;
    if (ptyOutputOffset < ptyOutput.used()) {
        result.pty = StringView((const u8*)(ptyOutput.data()) + ptyOutputOffset, ptyOutput.used() - ptyOutputOffset);
    }
    if (!outputPending) {
        return result;
    }

    Screen* const frame = cf;
    const TerminalCellBatch output = frame->copyDamage(outputSpans.mutData());

    updateScreen = frame;
    fillTerminalUpdate(terminalUpdate, *frame, outputSpans.data(), output.spanCount);
    result.terminal = &terminalUpdate;
    return result;
}

template <bool traced>
void VtermImpl<traced>::consume(const VtermConsume& consumed) {
    const size_t pending = ptyOutput.used() - ptyOutputOffset;
    STD_ASSERT(consumed.ptyBytes <= pending);
    ptyOutputOffset += consumed.ptyBytes;
    if (ptyOutputOffset == ptyOutput.used()) {
        ptyOutput.reset();
        ptyOutputOffset = 0;
    }
    if (!consumed.terminal) {
        return;
    }
    updateScreen->resetDamage();
    updateScreen = nullptr;
    outputPending = false;
    presentedSinceGcSafePoint = true;
    collectCellExtrasIfNeeded();
}

template <bool traced>
VtermState VtermImpl<traced>::state() const {
    VtermState result;
    result.synchronizedOutput = synchronizedOutputMode;
    result.animation = haveBlinkingText || cursorBlinkMode;
    return result;
}

template <bool traced>
TestApi* VtermImpl<traced>::testApi() {
#ifdef SHITTY_FOR_TESTS
    return composer.pool->make<TestApiImpl<traced>>(this);
#else
    return nullptr;
#endif
}

template <bool traced>
TestApiImpl<traced>::TestApiImpl(VtermImpl<traced>* vterm_)
    : vterm(vterm_)
{
}

template <bool traced>
VtermTestState TestApiImpl<traced>::inspect() const {
    VtermTestState result;
    result.mouse = vterm->mouseTrk;
    result.droppedPtyResponses = vterm->droppedPtyResponses;
    result.kittyKeyboardFlags = vterm->getKittyKeyboardFlags();
    result.screenReverseVideo = vterm->screenReverseVideo;
    result.ledState = vterm->ledState;
    result.reverseWrapMode = vterm->reverseWrapMode;
    result.nationalReplacementMode = vterm->nationalReplacementMode;
    result.cursorStyle = vterm->cursorShape;
    result.pen.cell = vterm->attrs;
    result.pen.fg = vterm->colors.resolve(vterm->attrs.foreground());
    result.pen.bg = vterm->colors.resolve(vterm->attrs.background());
    if (vterm->originMode == VtermImpl<traced>::OriginMode::ScrollingRegion) {
        result.rectangleOrigin = {vterm->marginTop, vterm->hMargin, vterm->marginBottom, vterm->nColsEff};
    } else {
        result.rectangleOrigin = {0, 0, vterm->composer.rows, vterm->composer.columns};
    }
    result.hyperlinkCount = vterm->composer.cellExtras->hyperlinkCount();
    return result;
}

template <bool traced>
bool TestApiImpl<traced>::ansiMode(u32 mode) const {
    switch (mode) {
        case 4:
            return vterm->insertMode;
        case 6:
            return vterm->eraseModeAll;
        case 12:
            return !vterm->localEcho;
        case 20:
            return vterm->autoNewlineMode;
        default:
            return false;
    }
}

template <bool traced>
bool TestApiImpl<traced>::privateMode(u32 mode) const {
    using CursorKeyMode = typename VtermImpl<traced>::CursorKeyMode;
    using ColMode = typename VtermImpl<traced>::ColMode;
    using KeypadMode = typename VtermImpl<traced>::KeypadMode;
    using OriginMode = typename VtermImpl<traced>::OriginMode;
    switch (mode) {
        case 1:
            return vterm->cursorKeyMode == CursorKeyMode::Application;
        case 3:
            return vterm->colMode == ColMode::C132;
        case 4:
            return vterm->smoothScrollMode;
        case 5:
            return vterm->screenReverseVideo;
        case 6:
            return vterm->originMode == OriginMode::ScrollingRegion;
        case 7:
            return vterm->autoWrapMode;
        case 8:
            return vterm->autoRepeatMode;
        case 12:
            return vterm->cursorBlinkMode;
        case 18:
            return vterm->printFormFeedMode;
        case 19:
            return vterm->printExtentMode;
        case 40:
            return vterm->allowColumnMode;
        case 41:
            return vterm->moreFixMode;
        case 42:
            return vterm->nationalReplacementMode;
        case 45:
            return vterm->reverseWrapMode;
        case 9:
            return vterm->mouseTrk.mode == MouseTrackingMode::X10_Compat;
        case 25:
            return vterm->showCursorMode;
        case 47:
        case 1047:
            return vterm->altScreenBufferMode;
        case 66:
            return vterm->keypadMode == KeypadMode::Application;
        case 67:
            return !vterm->bkspSendsDel;
        case 69:
            return vterm->horizMarginMode;
        case 95:
            return vterm->noClearColumnMode;
        case 1000:
            return vterm->mouseTrk.mode == MouseTrackingMode::VT200;
        case 1001:
            return vterm->mouseTrk.mode == MouseTrackingMode::VT200_Highlight;
        case 1002:
            return vterm->mouseTrk.mode == MouseTrackingMode::VT200_ButtonEvent;
        case 1003:
            return vterm->mouseTrk.mode == MouseTrackingMode::VT200_AnyEvent;
        case 1004:
            return vterm->mouseTrk.focusEventMode;
        case 1005:
            return vterm->mouseTrk.enc == MouseTrackingEnc::UTF8;
        case 1006:
            return vterm->mouseTrk.enc == MouseTrackingEnc::SGR;
        case 1007:
            return vterm->altScrollMode;
        case 1015:
            return vterm->mouseTrk.enc == MouseTrackingEnc::URXVT;
        case 1016:
            return vterm->mouseTrk.enc == MouseTrackingEnc::SGRPixels;
        case 1034:
            return vterm->eightBitInput;
        case 1036:
        case 1039:
            return vterm->altSendsEscape;
        case 1045:
            return vterm->extendedReverseWrapMode;
        case 2004:
            return vterm->bracketedPasteMode;
        case 2026:
            return vterm->synchronizedOutputMode;
        case 2031:
            return vterm->colorSchemeUpdateMode;
        case 2048:
            return vterm->inBandResizeMode;
        default:
            return false;
    }
}

template <bool traced>
VtermTestCell TestApiImpl<traced>::cell(u16 row, u16 column) const {
    if (row >= vterm->cf->rows() || column >= vterm->cf->columns()) {
        return {};
    }
    VtermTestCell result;
    result.cell = vterm->cf->testCell(row, column);
    CellExtraStore& extras = *vterm->composer.cellExtras;
    const GraphemeView grapheme = extras.grapheme(result.cell.extraRef());
    result.grapheme = grapheme.data();
    result.graphemeSize = grapheme.size();
    result.underlineColor = extras.underlineColor(result.cell);
    result.lineAttribute = vterm->cf->lineAttribute(row);
    return result;
}

template <bool traced>
void TestApiImpl<traced>::key(VtKey key_, VtModifier modifiers) {
    vterm->key(key_, modifiers);
}

template <bool traced>
void TestApiImpl<traced>::character(u8 byte, VtModifier modifiers) {
    vterm->character(byte, modifiers);
}

template <bool traced>
void TestApiImpl<traced>::kittyKey(VtKey key_, u16 modifiers, VtermKeyEventType event) {
    vterm->kittyKey(key_, modifiers, event);
}

template <bool traced>
void TestApiImpl<traced>::kittyKey(u32 key_, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, VtermKeyEventType event) {
    vterm->kittyKey(key_, shiftedKey, baseLayoutKey, modifiers, event);
}

template <bool traced>
bool TestApiImpl<traced>::mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY) {
    return vterm->mouseHighlightRelease(endX, endY, mouseX, mouseY);
}

template <bool traced>
void TestApiImpl<traced>::locatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons) {
    vterm->locatorPosition(column, row, pixelX, pixelY, buttons);
}

template <bool traced>
void TestApiImpl<traced>::locatorButton(u8 button, bool pressed) {
    vterm->locatorButton(button, pressed);
}

template <bool traced>
void TestApiImpl<traced>::scrollUp(u16 count) {
    vterm->scrollUp(count);
}

template <bool traced>
void TestApiImpl<traced>::scrollDown(u16 count) {
    vterm->scrollDown(count);
}

template <bool traced>
void TestApiImpl<traced>::pageUp() {
    vterm->pageUp();
}

template <bool traced>
void TestApiImpl<traced>::pageDown() {
    vterm->pageDown();
}

template <bool traced>
void TestApiImpl<traced>::selectionStart(int pixelX, int pixelY, bool cycleSnapTo) {
    vterm->selectionStart(pixelX, pixelY, cycleSnapTo);
}

template <bool traced>
void TestApiImpl<traced>::selectionExtend(int pixelX, int pixelY, bool cycleSnapTo) {
    vterm->selectionExtend(pixelX, pixelY, cycleSnapTo);
}

template <bool traced>
void TestApiImpl<traced>::selectionUpdate(int pixelX, int pixelY) {
    vterm->selectionUpdate(pixelX, pixelY);
}

template <bool traced>
VtermTextResult TestApiImpl<traced>::selectionFinish() {
    return vterm->selectionFinish();
}

template <bool traced>
void TestApiImpl<traced>::selectionRectangular() {
    vterm->selectionRectangular();
}

template <bool traced>
void TestApiImpl<traced>::paste(StringView text) {
    vterm->paste(text);
}

template <bool traced>
StringView TestApiImpl<traced>::hyperlinkAt(int pixelX, int pixelY) {
    return vterm->hyperlinkAt(pixelX, pixelY);
}

template <bool traced>
bool VtermImpl<traced>::animationActive() const {
    return haveBlinkingText || cursorBlinkMode;
}

size_t VtermInputSpec::getLength() const {
    return length ? length : strlen(input);
}

template <bool traced>
void VtermImpl<traced>::unhandledInput(unsigned char ch) {
    (void)ch;
}

template <bool traced>
void VtermImpl<traced>::redraw() {
    input.refreshHyperlink();
    if (synchronizedOutputMode) {
        return;
    }
    outputPending = true;
}

template <bool traced>
void VtermImpl<traced>::updateExtraCellCount() {
    size_t count = frame_pri->active() ? frame_pri->cellCapacity() : 0;
    count += frame_alt->active() ? frame_alt->cellCapacity() : 0;
    composer.cellExtras->setCellCount(count);
}

template <bool traced>
void VtermImpl<traced>::collectCellExtrasIfNeeded(bool force) {
    CellExtraStore& extras = *composer.cellExtras;
    const bool hardLimit = extras.hardLimitExceeded();
    if (processInputDepth != 0) {
        return;
    }
    if (!extras.shouldCollect() && !force) {
        presentedSinceGcSafePoint = false;
        return;
    }
    if (!presentedSinceGcSafePoint && !hardLimit && !force) {
        return;
    }
    collectCellExtras();
    presentedSinceGcSafePoint = false;
}

template <bool traced>
void VtermImpl<traced>::collectCellExtras() {
    extraCells.clear();
    u32* roots[2];
    size_t rootCount = 0;
    if (activeHyperlink != 0) {
        roots[rootCount++] = &activeHyperlink;
    }
    if (inputGraphemeHyperlink != 0) {
        roots[rootCount++] = &inputGraphemeHyperlink;
    }
    if (frame_pri->active()) {
        frame_pri->collectExtraCells(extraCells);
    }
    if (frame_alt->active()) {
        frame_alt->collectExtraCells(extraCells);
    }

    CellExtraStore& extras = *composer.cellExtras;
    extras.collect(extraCells, roots, rootCount);
    if (frame_pri->active()) {
        frame_pri->damageExtraCells();
    }
    if (frame_alt->active()) {
        frame_alt->damageExtraCells();
    }
    extraCells.clear();
}

template <bool traced>
bool VtermImpl<traced>::advanceAnimation(bool force) {
    if (!animationActive()) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < nextBlink) {
        return false;
    }
    blinkVisible = !blinkVisible;
    nextBlink = now + std::chrono::milliseconds(500);
    frame_pri->setBlinkState(blinkVisible, cursorBlinkMode);
    frame_alt->setBlinkState(blinkVisible, cursorBlinkMode);
    return true;
}

template <bool traced>
bool VtermImpl<traced>::expireSynchronizedOutput(bool force) {
    if (!synchronizedOutputMode || (!force && std::chrono::steady_clock::now() < synchronizedOutputDeadline)) {
        return false;
    }
    synchronizedOutputMode = false;
    redraw();
    return true;
}

template <bool traced>
bool VtermImpl<traced>::mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY) {
    if (mouseTrk.mode != MouseTrackingMode::VT200_Highlight || !mouseHighlight.active) {
        return false;
    }
    mouseHighlight.active = false;
    endY = std::clamp(endY, mouseHighlight.firstRow, mouseHighlight.lastRow);
    StringBuilder response;
    if (mouseTrk.enc == MouseTrackingEnc::SGR || mouseTrk.enc == MouseTrackingEnc::SGRPixels) {
        response << StringView(u8"<") << mouseHighlight.startX << StringView(u8";") << mouseHighlight.startY << StringView(u8";") << endX << StringView(u8";") << endY << StringView(u8";") << mouseX << StringView(u8";") << mouseY << StringView(u8"T");
    } else {
        const auto coordinate = [](u16 value) {
            return (u8)(32 + std::clamp<u16>(value, 1, 223));
        };
        const u8 kind = mouseHighlight.startX == endX && mouseHighlight.startY == endY ? u8't' : u8'T';
        const u8 startX = coordinate(mouseHighlight.startX);
        const u8 startY = coordinate(mouseHighlight.startY);
        response.append(&kind, 1);
        response.append(&startX, 1);
        response.append(&startY, 1);
        if (mouseHighlight.startX != endX || mouseHighlight.startY != endY) {
            const u8 coordinates[] = {coordinate(endX), coordinate(endY), coordinate(mouseX), coordinate(mouseY)};
            response.append(coordinates, sizeof(coordinates));
        }
    }
    writeCsiResponse(StringView(response));
    return true;
}

template <bool traced>
void VtermImpl<traced>::setLocatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons) {
    locator.column = std::max<u16>(1, column);
    locator.row = std::max<u16>(1, row);
    locator.pixelX = std::max<u16>(1, pixelX);
    locator.pixelY = std::max<u16>(1, pixelY);
    locator.buttons = buttons & 15;
    if (locator.enabled && locator.filter) {
        const u16 x = locator.pixels ? locator.pixelX : locator.column;
        const u16 y = locator.pixels ? locator.pixelY : locator.row;
        if (y < locator.filterTop || y > locator.filterBottom || x < locator.filterLeft || x > locator.filterRight) {
            StringBuilder response;
            response << StringView(u8"10;") << (unsigned)(locator.buttons) << StringView(u8";") << y << StringView(u8";") << x << StringView(u8";0&w");
            writeCsiResponse(StringView(response));
            locator.filter = false;
            if (locator.enabled == 2) {
                locator.enabled = 0;
            }
        }
    }
}

template <bool traced>
void VtermImpl<traced>::reportLocatorButton(u8 button, bool pressed) {
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
    StringBuilder response;
    response << event << StringView(u8";") << (unsigned)(locator.buttons) << StringView(u8";") << y << StringView(u8";") << x << StringView(u8";0&w");
    writeCsiResponse(StringView(response));
    if (locator.enabled == 2) {
        locator.enabled = 0;
    }
}

template <bool traced>
VtermImpl<traced>::KittyKeyboardState& VtermImpl<traced>::kittyKeyboardState() {
    return altScreenBufferMode ? kittyKeyboardAlt : kittyKeyboardPri;
}

template <bool traced>
const VtermImpl<traced>::KittyKeyboardState& VtermImpl<traced>::kittyKeyboardState() const {
    return altScreenBufferMode ? kittyKeyboardAlt : kittyKeyboardPri;
}

template <bool traced>
u8 VtermImpl<traced>::getKittyKeyboardFlags() const {
    return kittyKeyboardState().flags;
}

template <bool traced>
void VtermImpl<traced>::setHasFocus(bool hasFocus_) {
    hasFocus = hasFocus_;
    if (mouseTrk.focusEventMode) {
        writeCsiResponse(hasFocus ? "I" : "O");
    }
    showCursor();
    redraw();
}

template <bool traced>
void VtermImpl<traced>::pageUp() {
    if (altScrollMode && altScreenBufferMode) {
        inputOps[0] = 1;
        nInputOps = 1;
        for (int k = 0; k < (marginBottom - marginTop) / 2; ++k) {
            writePty(VtKey::Up);
        }
    } else {
        cf->pageUp(composer.rows / 2);
        redraw();
    }
}

template <bool traced>
void VtermImpl<traced>::pageDown() {
    if (altScrollMode && altScreenBufferMode) {
        inputOps[0] = 1;
        nInputOps = 1;
        for (int k = 0; k < (marginBottom - marginTop) / 2; ++k) {
            writePty(VtKey::Down);
        }
    } else {
        cf->pageDown(composer.rows / 2);
        redraw();
    }
}

template <bool traced>
void VtermImpl<traced>::mouseWheelUp(u16 count) {
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

template <bool traced>
void VtermImpl<traced>::mouseWheelDown(u16 count) {
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

template <bool traced>
void VtermImpl<traced>::resetTerminal() {
    switchScreenBufferMode(false, true);
    resetScreen();
    resetAttrs();

    noClearColumnMode = false;
    switchColMode(ColMode::C80);

    cf->dropScrollbackHistory();
    marginTop = 0;
    marginBottom = composer.rows;
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
    savedCursorPri.isSet = false;
    savedCursorAlt.isSet = false;
    activeHyperlink = 0;
    nextHyperlink = 1;
    currentSemantic = 0;
    titleModes = 0;
    titleStack.clear();
    notifications.clear();
    activeNotificationIds.clear();

    horizMarginMode = false;
    hMargin = 0;
    nColsEff = composer.columns;

    if (host.handlesOsc()) {
        osc_TITLE_0(StringView((const u8*)(opts.title), std::strlen(opts.title)));
    }
}

template <bool traced>
void VtermImpl<traced>::resetScreen(bool resetTabStops) {
    utf8dec.reset();
    showCursorMode = true;
    cursorShape = TerminalCursor::Style::filled_block;
    cursorStyleParam = 2;
    cursorBlinkMode = false;
    haveBlinkingText = false;
    blinkVisible = true;
    nextBlink = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    frame_pri->setBlinkState(true, false);
    frame_alt->setBlinkState(true, false);
    autoWrapMode = true;
    autoRepeatMode = true;
    smoothScrollMode = false;
    autoNewlineMode = false;
    keyboardLocked = false;
    insertMode = false;
    eraseModeAll = false;
    attrs.protected_char = 0;
    bkspSendsDel = true;
    localEcho = false;
    bracketedPasteMode = false;
    synchronizedOutputMode = false;
    colorSchemeUpdateMode = false;
    inBandResizeMode = false;
    printerControllerMode = false;
    autoPrintMode = false;
    printFormFeedMode = false;
    printExtentMode = false;
    screenReverseVideo = false;
    frame_pri->setScreenReverseVideo(false);
    frame_alt->setScreenReverseVideo(false);
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

    savedCursor->isSet = false;

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

template <bool traced>
void VtermImpl<traced>::resetAttrs() {
    reverseVideo = false;

    inputOps[0] = 0;
    nInputOps = 1;
    csi_SGR();
}

template <bool traced>
void VtermImpl<traced>::clearScreen() {
    posX = 0;
    posY = 0;
    lastCol = false;
    fillScreen(' ');
}

template <bool traced>
void VtermImpl<traced>::fillScreen(u16 ch) {
    cf->fillCells(ch, attrs);
}

template <bool traced>
void VtermImpl<traced>::switchColMode(ColMode colMode_) {
    if (colMode == colMode_) {
        return;
    }

    const u16 columns = colMode_ == ColMode::C80 ? 80 : 132;
    if (composer.columns != columns) {
        host.windowOperation(8, composer.rows, columns);
    }
    marginTop = 0;
    marginBottom = composer.rows;
    horizMarginMode = false;
    hMargin = 0;
    nColsEff = composer.columns;
    posX = 0;
    posY = 0;
    lastCol = false;
    if (!noClearColumnMode) {
        fillScreen(' ');
    }

    colMode = colMode_;
}

template <bool traced>
void VtermImpl<traced>::switchScreenBufferMode(bool altScreenBufferMode_, bool clearAlternate) {
    if (altScreenBufferMode == altScreenBufferMode_) {
        if (clearAlternate) {
            if (altScreenBufferMode_) {
                kittyKeyboardAlt = {};
                createFreshScreen(frame_alt, frameAltPool, 0);
                marginTop = 0;
                marginBottom = composer.rows;
                altScreenInitialized = true;
                cf = frame_alt;
                cf->expose();
            } else if (altScreenInitialized) {
                createInactiveScreen(frame_alt, frameAltPool);
                altScreenInitialized = false;
            }
            updateExtraCellCount();
        }
        return;
    }

    if (altScreenBufferMode_) {
        if (clearAlternate || !altScreenInitialized) {
            kittyKeyboardAlt = {};
            createFreshScreen(frame_alt, frameAltPool, 0);
            marginTop = 0;
            marginBottom = composer.rows;
            altScreenInitialized = true;
        } else if (frame_alt->columns() != composer.columns || frame_alt->rows() != composer.rows) {
            resizeScreen(frame_alt, frameAltPool, false, nullptr);
            marginTop = 0;
            marginBottom = composer.rows;
        }
        cf = frame_alt;
        cf->expose();

        savedCursor = &savedCursorAlt;
        altScreenBufferMode = true;
    } else {
        if (frame_pri->columns() != composer.columns || frame_pri->rows() != composer.rows) {
            const bool reflow = frame_pri->columns() != composer.columns;
            Screen::Cursor cursorState{Point(posX, posY), lastCol};
            resizeScreen(frame_pri, framePriPool, reflow, reflow ? &cursorState : nullptr);
            if (reflow) {
                posX = cursorState.position.x;
                posY = cursorState.position.y;
                lastCol = cursorState.pendingWrap;
            }
            marginTop = 0;
            marginBottom = composer.rows;
        }
        cf = frame_pri;
        cf->expose();
        if (clearAlternate) {
            createInactiveScreen(frame_alt, frameAltPool);
            altScreenInitialized = false;
        }
        savedCursor = &savedCursorPri;
        altScreenBufferMode = false;
    }
    updateExtraCellCount();
}

template <bool traced>
bool VtermImpl<traced>::stringUtf8Continuation(u8 ch) {
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

template <bool traced>
void VtermImpl<traced>::normalizeCursorPos() {
    if (nColsEff < posX + 1) {
        posX = nColsEff - 1;
    }

    if (composer.rows < posY + 1) {
        posY = composer.rows - 1;
    }

    lastCol = false;
}

template <bool traced>
bool VtermImpl<traced>::isCursorInsideMargins() {
    return posX >= hMargin && posX < nColsEff && posY >= marginTop && posY < marginBottom;
}

template <bool traced>
void VtermImpl<traced>::eraseRow(u16 pY) {
    eraseRangeInRow(pY, hMargin, nColsEff - hMargin);
    if (hMargin == 0 && nColsEff == composer.columns) {
        cf->setLineAttribute(pY, 0);
    }
}

template <bool traced>
void VtermImpl<traced>::eraseRows(u16 startY, u16 count) {
    for (u16 pY = startY; pY < startY + count; ++pY) {
        eraseRow(pY);
    }
}

template <bool traced>
void VtermImpl<traced>::copyRow(u16 dstY, u16 srcY) {
    if (dstY == srcY) {
        return;
    }

    cf->copyRow(dstY, srcY, hMargin, nColsEff - hMargin, eraseAttrs);
}

template <bool traced>
void VtermImpl<traced>::insertRows(u16 startY, u16 count) {
    if (hMargin == 0 && nColsEff == composer.columns) {
        cf->rotateRowsDown(startY, marginBottom, count);
    } else {
        cf->scrollRectangleDown(startY, hMargin, marginBottom, nColsEff, count, eraseAttrs);
        return;
    }

    for (u16 pY = startY; pY < startY + count; ++pY) {
        eraseRow(pY);
    }
}

template <bool traced>
void VtermImpl<traced>::deleteRows(u16 startY, u16 count) {
    if (hMargin == 0 && nColsEff == composer.columns) {
        cf->rotateRowsUp(startY, marginBottom, count);
    } else {
        cf->scrollRectangleUp(startY, hMargin, marginBottom, nColsEff, count, eraseAttrs);
        return;
    }

    for (u16 pY = marginBottom - count; pY < marginBottom; ++pY) {
        eraseRow(pY);
    }
}

template <bool traced>
void VtermImpl<traced>::insertCols(u16 startX, u16 count) {
    for (u16 r = marginTop; r < marginBottom; ++r) {
        cf->insertCells(r, startX, nColsEff, count, eraseAttrs);
    }
}

template <bool traced>
void VtermImpl<traced>::deleteCols(u16 startX, u16 count) {
    for (u16 r = marginTop; r < marginBottom; ++r) {
        cf->deleteCells(r, startX, nColsEff, count, eraseAttrs);
    }
}

template <bool traced>
void VtermImpl<traced>::eraseRangeInRow(u16 row, u16 start, u16 count) {
    if (!count) {
        return;
    }
    cf->eraseCells(row, start, count, eraseAttrs);
}

template <bool traced>
void VtermImpl<traced>::eraseEcmaRangeInRow(u16 row, u16 start, u16 count) {
    if (eraseModeAll) {
        eraseRangeInRow(row, start, count);
        return;
    }
    if (!count) {
        return;
    }
    cf->selectiveEraseCells(row, start, count, eraseAttrs, TerminalCell::isoProtection);
}

template <bool traced>
void VtermImpl<traced>::eraseEcmaRow(u16 row) {
    if (eraseModeAll) {
        eraseRow(row);
        return;
    }
    const bool retained = cf->hasProtection(row, TerminalCell::isoProtection);
    eraseEcmaRangeInRow(row, 0, composer.columns);
    if (!retained) {
        cf->setLineAttribute(row, 0);
    }
}

template <bool traced>
void VtermImpl<traced>::selectiveEraseRangeInRow(u16 row, u16 start, u16 count) {
    if (!count) {
        return;
    }
    cf->selectiveEraseCells(row, start, count, eraseAttrs, TerminalCell::decProtection);
}

template <bool traced>
void VtermImpl<traced>::rectangleOrigin(u16& rowBase, u16& columnBase, u16& rowLimit, u16& columnLimit) const {
    if (originMode == OriginMode::ScrollingRegion) {
        rowBase = marginTop;
        columnBase = hMargin;
        rowLimit = marginBottom;
        columnLimit = nColsEff;
    } else {
        rowBase = 0;
        columnBase = 0;
        rowLimit = composer.rows;
        columnLimit = composer.columns;
    }
}

template <bool traced>
bool VtermImpl<traced>::rectangleFromParams(size_t offset, Rectangle& rectangle) const {
    u16 rowBase, columnBase, rowLimit, columnLimit;
    rectangleOrigin(rowBase, columnBase, rowLimit, columnLimit);
    const u32 rows = rowLimit - rowBase;
    const u32 columns = columnLimit - columnBase;
    const u32 topParam = offset < nInputOps ? inputOps[offset] : 0;
    const u32 leftParam = offset + 1 < nInputOps ? inputOps[offset + 1] : 0;
    const u32 bottomParam = offset + 2 < nInputOps ? inputOps[offset + 2] : 0;
    const u32 rightParam = offset + 3 < nInputOps ? inputOps[offset + 3] : 0;
    const u32 rawTop = topParam ? topParam : 1;
    const u32 rawLeft = leftParam ? leftParam : 1;
    const u32 rawBottom = bottomParam ? bottomParam : rows;
    const u32 rawRight = rightParam ? rightParam : columns;
    if (rawTop > rawBottom || rawLeft > rawRight) {
        return false;
    }

    rectangle.top = rowBase + std::min(rawTop, rows) - 1;
    rectangle.left = columnBase + std::min(rawLeft, columns) - 1;
    rectangle.bottom = rowBase + std::min(rawBottom, rows);
    rectangle.right = columnBase + std::min(rawRight, columns);
    return true;
}

template <bool traced>
void VtermImpl<traced>::inputGraphicChar(unsigned char ch) {
    if ((ch & 0x80) == 0) {
        if (utf8dec.checkPrematureEOS()) {
            placeGraphicChar();
        }

        Charset cs;
        if (charsetState.ss) {
            cs = charsetState.g[charsetState.ss];
            charsetState.ss = 0;
        } else {
            cs = charsetState.g[charsetState.gl];
        }

        if (cs == Charset::UTF8) {
            if (utf8dec.onUnicode(ch < 127 ? ch : 0)) {
                placeGraphicChar();
            }
        } else if (ch >= 32 && (cs == Charset::IsoLatin1 || ch < 127)) {
            if (utf8dec.onUnicode(translateCharset(cs, ch))) {
                placeGraphicChar();
            }
        }
    } else {
        Charset cs = charsetState.g[charsetState.gr];
        if (cs == Charset::UTF8) {
            for (int completed = utf8dec.pushByte(ch); completed > 0; --completed) {
                placeGraphicChar();
            }
        } else if (ch >= 160 && (cs == Charset::IsoLatin1 || ch < 255)) {
            // ISO 2022 invokes a 94/96-character G-set in GR by moving its
            // 7-bit code positions into the right half.  Strip that bit and
            // use the same translation path as GL; NRC sets are deliberately
            // outside charCodes, so indexing the table directly was both
            // incorrect and out of bounds for LS1R/LS2R/LS3R.
            if (utf8dec.onUnicode(translateCharset(cs, ch & 0x7f))) {
                placeGraphicChar();
            }
        }
    }
}

template <bool traced>
void VtermImpl<traced>::resetGraphemeInput() {
    if (inputGraphemeScreen == nullptr) {
        return;
    }
    inputGrapheme.clear();
    inputGraphemeBase = 0;
    inputGraphemeBreaker.reset();
    inputGraphemeScreen = nullptr;
    inputGraphemeWide = false;
    inputGraphemeAttrs = {};
    inputGraphemeHyperlink = 0;
    inputGraphemeSemantic = 0;
}

template <bool traced>
u8 VtermImpl<traced>::codepointData(u32 codepoint) {
    constexpr u8 valid = 0x80;
    constexpr u8 simple = 0x04;
    u8& cached = (*unicodeProperties)[codepoint];
    if ((cached & valid) == 0) {
        const CodepointProperties properties = codepointProperties(codepoint);
        cached = valid | properties.width | (properties.simpleGrapheme ? simple : 0);
    }
    return cached;
}

template <bool traced>
void VtermImpl<traced>::placeGraphicChar() {
    const u32 pt = utf8dec.getUnicode();
    if (inputGraphemeScreen != cf) {
        inputGraphemeBreaker.reset();
    }
    const u8 data = codepointData(pt);
    placeGraphicChar(inputGraphemeBreaker.breakBefore(pt, (data & 0x04) != 0), data & 0x03);
}

template <bool traced>
void VtermImpl<traced>::placeGraphicChar(bool graphemeBoundary) {
    placeGraphicChar(graphemeBoundary, codepointData(utf8dec.getUnicode()) & 0x03);
}

template <bool traced>
void VtermImpl<traced>::placeGraphicChar(bool graphemeBoundary, u8 width) {
    u32 pt = utf8dec.getUnicode();
    u8 w = width;
    const u8 lineAttribute = cf->lineAttribute(posY);
    const u16 lineCols = lineAttribute ? hMargin + std::max<u16>(1, (nColsEff - hMargin) / 2) : nColsEff;

    if (inputGraphemeScreen == cf && !graphemeBoundary) {
        const u32 previous = inputGrapheme.empty() ? inputGraphemeBase : inputGrapheme.data()[inputGrapheme.size() - 1];
        if (inputGrapheme.empty()) {
            inputGrapheme.push_back(inputGraphemeBase);
        }
        inputGrapheme.push_back(pt);
        u16 targetX = inputGraphemeX;
        u16 targetY = inputGraphemeY;
        bool wide = inputGraphemeWide;
        switch (graphemeWidthEffect(previous, pt)) {
            case GraphemeWidthEffect::Wide:
                if (!wide && lineCols - hMargin >= 2) {
                    if (targetX == lineCols - 1) {
                        if (autoWrapMode) {
                            cf->eraseCells(targetY, targetX, 1, eraseAttrs);
                            const u16 wrapColumn = targetX > hMargin ? targetX - 1 : targetX;
                            cf->setWrapped(targetY, wrapColumn);
                            inp_CR();
                            inp_LF();
                            targetX = posX;
                            targetY = posY;
                            wide = true;
                        }
                    } else {
                        wide = true;
                    }
                    if (wide) {
                        if (targetX + 1 == lineCols - 1) {
                            posX = targetX + 1;
                            lastCol = true;
                        } else {
                            posX = targetX + 2;
                            lastCol = false;
                        }
                    }
                }
                break;
            case GraphemeWidthEffect::Narrow:
                if (wide) {
                    wide = false;
                    posX = targetX + 1;
                    lastCol = false;
                }
                break;
            case GraphemeWidthEffect::Unchanged:
                break;
        }
        cf->writeGrapheme(targetY, targetX, inputGrapheme.data(), inputGrapheme.size(), wide, inputGraphemeAttrs, inputGraphemeHyperlink, inputGraphemeSemantic, eraseAttrs);
        inputGraphemeX = targetX;
        inputGraphemeY = targetY;
        inputGraphemeWide = wide;
        return;
    }

    if (posX >= lineCols) {
        posX = lineCols - 1;
        lastCol = false;
    }
    if (autoWrapMode && lastCol) {
        cf->setWrapped(posY, posX);
        inp_CR();
        inp_LF();
    }

    if (w == 2 && posX == lineCols - 1 && autoWrapMode) {
        // The wide glyph belongs wholly to the next row.  Mark the last
        // occupied cell as the soft-wrap boundary, not the unused final
        // column: otherwise copying the logical line invents a space.
        const u16 wrapColumn = posX > hMargin ? posX - 1 : posX;
        cf->setWrapped(posY, wrapColumn);
        inp_CR();
        inp_LF();
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
    const bool wide = w == 2 && posX < lineCols - 1;
    cf->writeCodepoint(posY, posX, pt, wide, attrs, activeHyperlink, currentSemantic, eraseAttrs);
    if (attrs.blink) {
        haveBlinkingText = true;
    }

    inputGrapheme.clear();
    inputGraphemeBase = pt;
    inputGraphemeScreen = cf;
    inputGraphemeX = clusterX;
    inputGraphemeY = clusterY;
    inputGraphemeWide = wide;
    inputGraphemeAttrs = attrs;
    inputGraphemeHyperlink = activeHyperlink;
    inputGraphemeSemantic = currentSemantic;

    if (wide) {
        ++posX;
    }

    if (posX == lineCols - 1) {
        lastCol = true;
    } else {
        ++posX;
    }
}

template <bool traced>
template <bool insert>
void VtermImpl<traced>::placeAsciiRun(const u8* input, size_t size) {
    bool checkBoundary = true;
    while (size > 0) {
        bool graphemeBoundary = true;
        if (checkBoundary) {
            if (inputGraphemeScreen != cf) {
                inputGraphemeBreaker.reset();
            }
            graphemeBoundary = inputGraphemeBreaker.breakBefore(*input);
            checkBoundary = false;
        }
        if (!graphemeBoundary) {
            utf8dec.setUnicode(*input++);
            placeGraphicChar(false);
            --size;
            continue;
        }
        if (autoWrapMode && lastCol) {
            cf->setWrapped(posY, posX);
            inp_CR();
            inp_LF();
        }

        const u8 lineAttribute = cf->lineAttribute(posY);
        const u16 lineCols = lineAttribute ? hMargin + std::max<u16>(1, (nColsEff - hMargin) / 2) : nColsEff;
        if (posX >= lineCols) {
            inputGraphemeBreaker.setBoundaryAfter(*input);
            utf8dec.setUnicode(*input++);
            placeGraphicChar(true);
            --size;
            continue;
        }
        const u16 count = std::min<size_t>(size, lineCols - posX);
        const u16 startX = posX;
        const u16 endX = startX + count;
        if constexpr (insert) {
            cf->writeAsciiRunInsert(posY, startX, nColsEff, input, count, attrs, activeHyperlink, currentSemantic, eraseAttrs);
        } else {
            cf->writeAsciiRun(posY, startX, input, count, attrs, activeHyperlink, currentSemantic, eraseAttrs);
        }
        if (attrs.blink) {
            haveBlinkingText = true;
        }

        const u16 clusterX = endX - 1;
        const u32 codepoint = input[count - 1];
        inputGrapheme.clear();
        inputGraphemeBase = codepoint;
        inputGraphemeScreen = cf;
        inputGraphemeX = clusterX;
        inputGraphemeY = posY;
        inputGraphemeWide = false;
        inputGraphemeAttrs = attrs;
        inputGraphemeHyperlink = activeHyperlink;
        inputGraphemeSemantic = currentSemantic;
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

// Decodes complete UTF-8 sequences ahead and batches independent glyphs into
// span writes.  Joining codepoints fall back to the standard cluster path;
// invalid or split sequences stop before the streaming decoder takes over.
template <bool traced>
int VtermImpl<traced>::placeUtf8Run(const u8* input, int size) {
    constexpr size_t batchLimit = 64;
    u32 batch[batchLimit];
    u8 widths[batchLimit];
    size_t batchCount = 0;
    bool batchWide = false;
    int consumed = 0;

    if (inputGraphemeScreen != cf) {
        inputGraphemeBreaker.reset();
    }
    const auto flush = [&]() {
        if (batchCount != 0) {
            if (batchWide) {
                placePreparedRun<true>(batch, widths, batchCount);
            } else {
                placePreparedRun<false>(batch, widths, batchCount);
            }
            batchCount = 0;
            batchWide = false;
        }
    };

    while (consumed < size) {
        const u8 lead = input[consumed];
        u32 codepoint;
        u32 minimum;
        int length;
        if (lead < 0x80) {
            if (lead < 0x20 || lead == 0x7f) {
                break;
            }
            codepoint = lead;
            minimum = 0;
            length = 1;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            codepoint = lead & 0x1f;
            minimum = 0x80;
            length = 2;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            codepoint = lead & 0x0f;
            minimum = 0x800;
            length = 3;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            codepoint = lead & 0x07;
            minimum = 0x10000;
            length = 4;
        } else {
            break;
        }
        if (consumed + length > size) {
            break;
        }
        bool valid = true;
        for (int k = 1; k < length; ++k) {
            const u8 continuation = input[consumed + k];
            if ((continuation & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }
        if (!valid || codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            break;
        }

        const u8 data = codepointData(codepoint);
        const bool boundary = inputGraphemeBreaker.breakBefore(codepoint, (data & 0x04) != 0);
        const u8 width = data & 0x03;
        if (!boundary || width == 0) {
            flush();
            utf8dec.setUnicode(codepoint);
            placeGraphicChar(boundary, width);
            consumed += length;
            continue;
        }
        if (batchCount == batchLimit) {
            flush();
        }
        batch[batchCount] = codepoint;
        widths[batchCount++] = width;
        batchWide |= width == 2;
        consumed += length;
    }
    flush();
    return consumed;
}

template <bool traced>
template <bool hasWide>
void VtermImpl<traced>::placePreparedRun(const u32* input, const u8* widths, size_t size) {
    while (size > 0) {
        if (autoWrapMode && lastCol) {
            cf->setWrapped(posY, posX);
            inp_CR();
            inp_LF();
        }

        const u8 lineAttribute = cf->lineAttribute(posY);
        const u16 lineCols = lineAttribute ? hMargin + std::max<u16>(1, (nColsEff - hMargin) / 2) : nColsEff;
        if (posX >= lineCols) {
            utf8dec.setUnicode(*input++);
            placeGraphicChar(true, *widths++);
            --size;
            continue;
        }
        const u16 available = lineCols - posX;
        u16 count = 0;
        u16 cellCount = 0;
        if constexpr (hasWide) {
            while (count < size && cellCount + widths[count] <= available) {
                cellCount += widths[count++];
            }
            if (count == 0) {
                utf8dec.setUnicode(*input++);
                placeGraphicChar(true, *widths++);
                --size;
                continue;
            }
        } else {
            count = std::min<size_t>(size, available);
            cellCount = count;
        }
        const u16 startX = posX;
        const u16 endX = startX + cellCount;
        if constexpr (hasWide) {
            cf->writeGlyphRun(posY, startX, input, widths, count, cellCount, attrs, activeHyperlink, currentSemantic, eraseAttrs);
        } else {
            cf->writeRun(posY, startX, input, count, attrs, activeHyperlink, currentSemantic, eraseAttrs);
        }
        if (attrs.blink) {
            haveBlinkingText = true;
        }

        const bool lastWide = widths[count - 1] == 2;
        const u16 clusterX = endX - widths[count - 1];
        const u32 codepoint = input[count - 1];
        inputGrapheme.clear();
        inputGraphemeBase = codepoint;
        inputGraphemeScreen = cf;
        inputGraphemeX = clusterX;
        inputGraphemeY = posY;
        inputGraphemeWide = lastWide;
        inputGraphemeAttrs = attrs;
        inputGraphemeHyperlink = activeHyperlink;
        inputGraphemeSemantic = currentSemantic;
        // The grapheme breaker already advanced through every batched
        // codepoint; only the decoder mirror needs the last one.
        utf8dec.setUnicode(codepoint);

        if (endX == lineCols) {
            posX = lineCols - 1;
            lastCol = true;
        } else {
            posX = endX;
            lastCol = false;
        }
        input += count;
        widths += count;
        size -= count;
    }
}

template <bool traced>
void VtermImpl<traced>::inp_LF() {
    if (esc_IND()) {
        eraseRangeInRow(posY, posX, nColsEff - posX);
    }
}

template <bool traced>
void VtermImpl<traced>::inp_CR() {
    if (originMode == OriginMode::Absolute && posX < hMargin) {
        posX = 0;
    } else {
        posX = hMargin;
    }
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::jumpToNextTabStop() {
    const u16 previous = posX;
    const bool insideMargins = isCursorInsideMargins();
    const u16 left = insideMargins ? hMargin : 0;
    const u16 right = insideMargins ? nColsEff : composer.columns;
    if (!tabStopsCustomized) {
        do {
            posX = ((posX / 8) + 1) * 8;
        } while (posX < left);
        posX = std::min<int>(posX, right - 1);
    } else {
        auto ts = std::upper_bound(tabStops.begin(), tabStops.end(), posX);
        posX = ts == tabStops.end() || *ts >= right ? right - 1 : *ts;
    }
    if (posX != previous) {
        lastCol = false;
    }
}

template <bool traced>
void VtermImpl<traced>::inp_HT() {
    if (moreFixMode && lastCol && autoWrapMode) {
        esc_IND();
        posX = 0;
        lastCol = false;
    }
    jumpToNextTabStop();
}

template <bool traced>
void VtermImpl<traced>::showCursor() {
    if (showCursorMode) {
        cf->setCursorPos(posY, posX);
        using CS = TerminalCursor::Style;
        cf->setCursorStyle(hasFocus ? cursorShape : CS::hollow_block);
    }
}

template <bool traced>
void VtermImpl<traced>::hideCursor() {
    using CS = TerminalCursor::Style;
    cf->setCursorStyle(CS::hidden);
}

template <bool traced>
void VtermImpl<traced>::esc_DCS(unsigned char fin) {
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
}

template <bool traced>
bool VtermImpl<traced>::esc_IND() {
    const bool scrolled = performIndex();
    return scrolled;
}

template <bool traced>
bool VtermImpl<traced>::performIndex() {
    if (autoPrintMode) {
        printLine(posY);
    }
    bool scrolled = false;
    if (posY == marginBottom - 1) {
        if (posX >= hMargin && posX < nColsEff) {
            scrollRegionUp(1);
            scrolled = true;
        }
    } else if (posY < composer.rows - 1) {
        ++posY;
        lastCol = false;
    }
    return scrolled;
}

template <bool traced>
std::string VtermImpl<traced>::printableLine(u16 row) const {
    std::string result;
    cf->appendPrintableLine(row, result);
    return result;
}

template <bool traced>
void VtermImpl<traced>::printLine(u16 row) {
    const std::string line = printableLine(std::min<u16>(row, composer.rows - 1));
    host.print(stringView(line));
}

template <bool traced>
void VtermImpl<traced>::csi_MC(bool privateMode) {
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
            const u16 firstRow = printExtentMode ? 0 : marginTop;
            const u16 lastRow = printExtentMode ? composer.rows : marginBottom;
            for (u16 row = firstRow; row < lastRow; ++row) {
                screen += printableLine(row);
            }
            if (printFormFeedMode) {
                screen.push_back('\f');
            }
            host.print(stringView(screen));
        } else if (operation == 4) {
            printerControllerMode = false;
        } else if (operation == 5) {
            printerControllerMode = true;
        }
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECLL() {
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
}

template <bool traced>
void VtermImpl<traced>::esc_RI() {
    if (posY == marginTop) {
        if (posX >= hMargin && posX < nColsEff) {
            nInputOps = 1;
            inputOps[0] = 1;
            csi_SD();
        }
    } else if (posY > 0) {
        --posY;
        lastCol = false;
    }
}

template <bool traced>
void VtermImpl<traced>::csi_ecma48_SL() {
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1u;
        arg = std::min<u32>(arg, nColsEff - hMargin);
        deleteCols(hMargin, (u16)(arg));
    }
}

template <bool traced>
void VtermImpl<traced>::csi_ecma48_SR() {
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1u;
        arg = std::min<u32>(arg, nColsEff - hMargin);
        insertCols(hMargin, (u16)(arg));
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECSCUSR() {
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
            break;
    }
    cursorBlinkMode = (cursorStyleParam & 1) != 0;
    blinkVisible = true;
    nextBlink = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    frame_pri->setBlinkState(true, cursorBlinkMode);
    frame_alt->setBlinkState(true, cursorBlinkMode);
}

template <bool traced>
void VtermImpl<traced>::csi_DECIC() {
    u32 arg = inputOps[0] ? inputOps[0] : 1u;
    if (isCursorInsideMargins()) {
        arg = min<u32>(arg, nColsEff - posX);
        insertCols(posX, (u16)(arg));
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECDC() {
    u32 arg = inputOps[0] ? inputOps[0] : 1u;
    if (isCursorInsideMargins()) {
        arg = min<u32>(arg, nColsEff - posX);
        deleteCols(posX, (u16)(arg));
    }
}

template <bool traced>
void VtermImpl<traced>::esc_FI() {
    if (posX >= hMargin && posX == nColsEff - 1) {
        deleteCols(hMargin, 1);
        lastCol = false;
    } else if (posX < composer.columns - 1) {
        ++posX;
        lastCol = false;
    }
}

template <bool traced>
void VtermImpl<traced>::esc_BI() {
    if (posX == hMargin && posX < nColsEff) {
        insertCols(hMargin, 1);
        lastCol = false;
    } else if (posX > 0) {
        --posX;
        lastCol = false;
    }
}

template <bool traced>
void VtermImpl<traced>::esc_NEL() {
    esc_IND();
    inp_CR();
}

template <bool traced>
void VtermImpl<traced>::esc_HTS() {
    if (!tabStopsCustomized) {
        for (unsigned column = 8; column < composer.columns; column += 8) {
            tabStops.push_back((u16)(column));
        }
        tabStopsCustomized = true;
    }
    if (std::find(tabStops.begin(), tabStops.end(), posX) == tabStops.end()) {
        tabStops.push_back(posX);
        std::sort(tabStops.begin(), tabStops.end());
    }
}

template <bool traced>
void VtermImpl<traced>::esc_SPA() {
    attrs.protected_char |= TerminalCell::isoProtection;
}

template <bool traced>
void VtermImpl<traced>::esc_EPA() {
    attrs.protected_char &= ~TerminalCell::isoProtection;
}

template <bool traced>
void VtermImpl<traced>::csi_SCOSC_SLRM() {
    if (horizMarginMode) {
        csi_SLRM();
    } else {
        csi_SCOSC();
    }
}

template <bool traced>
void VtermImpl<traced>::csi_SCOSC() {
    esc_DECSC();
}

template <bool traced>
void VtermImpl<traced>::csi_SCORC() {
    esc_DECRC();
}

template <bool traced>
void VtermImpl<traced>::esc_DECSC() {
    savedCursor->posX = posX;
    savedCursor->posY = posY;
    savedCursor->lastCol = lastCol;
    savedCursor->attrs = attrs;
    savedCursor->eraseAttrs = eraseAttrs;
    savedCursor->originMode = originMode;
    savedCursor->charsetState = charsetState;
    savedCursor->isSet = true;
}

template <bool traced>
void VtermImpl<traced>::esc_DECRC() {
    if (savedCursor->isSet) {
        posX = savedCursor->posX;
        posY = savedCursor->posY;
        normalizeCursorPos();
        lastCol = savedCursor->lastCol;
        attrs = savedCursor->attrs;
        eraseAttrs = savedCursor->eraseAttrs;
        reverseVideo = attrs.inverse;
        originMode = savedCursor->originMode;
        charsetState = savedCursor->charsetState;
    }
}

template <bool traced>
void VtermImpl<traced>::csi_CUU() {
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u16 top = posY >= marginTop ? marginTop : 0;
    arg = std::min<u32>(arg, posY - top);
    posY -= arg;
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_CUD() {
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u16 bottom = posY < marginBottom ? marginBottom : composer.rows;
    arg = std::min<u32>(arg, bottom - posY - 1);
    posY += arg;
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_CUF() {
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const bool insideMargins = posX >= hMargin && posX < nColsEff;
    const u16 right = insideMargins ? nColsEff : composer.columns;
    arg = std::min<u32>(arg, right - posX - 1);
    posX += arg;
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_CUB() {
    moveCursorBackward(inputOps[0] ? inputOps[0] : 1);
}

template <bool traced>
void VtermImpl<traced>::moveCursorBackward(u32 count) {
    const bool insideMargins = posX >= hMargin && posX < nColsEff;
    if (count && lastCol && autoWrapMode && (reverseWrapMode || extendedReverseWrapMode)) {
        lastCol = false;
        if (--count == 0) {
            return;
        }
    }
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
        if (!reverseWrapMode || posY == 0 || (!extendedReverseWrapMode && !cf->wrapped(posY - 1, nColsEff - 1))) {
            break;
        }
        --posY;
        posX = insideMargins ? nColsEff : composer.columns;
    }
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_CNL() {
    csi_CUD();
    inp_CR();
}

template <bool traced>
void VtermImpl<traced>::csi_CPL() {
    csi_CUU();
    inp_CR();
}

template <bool traced>
void VtermImpl<traced>::csi_CHA() {
    u32 col = inputOps[0] ? inputOps[0] : 1;
    if (originMode == OriginMode::ScrollingRegion) {
        col = std::max<u32>(1, std::min<u32>(col, nColsEff - hMargin));
        posX = hMargin + col - 1;
    } else {
        col = std::max<u32>(1, std::min<u32>(col, composer.columns));
        posX = col - 1;
    }
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_HPA() {
    csi_CHA();
}

template <bool traced>
void VtermImpl<traced>::csi_HPR() {
    const u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u16 right = originMode == OriginMode::ScrollingRegion ? nColsEff : composer.columns;
    posX = (u16)(std::min<u64>((u64)(posX) + arg, right - 1));
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_VPA() {
    u32 row = inputOps[0] ? inputOps[0] : 1;
    if (originMode == OriginMode::ScrollingRegion) {
        row = std::max<u32>(1, std::min<u32>(row, marginBottom - marginTop));
        posY = marginTop + row - 1;
    } else {
        row = std::max<u32>(1, std::min<u32>(row, composer.rows));
        posY = row - 1;
    }
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_VPR() {
    const u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u16 bottom = originMode == OriginMode::ScrollingRegion ? marginBottom : composer.rows;
    posY = (u16)(std::min<u64>((u64)(posY) + arg, bottom - 1));
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_CUP() {
    u32 row = inputOps[0] ? inputOps[0] : 1;
    u32 col = (nInputOps > 1 && inputOps[1]) ? inputOps[1] : 1;
    switch (originMode) {
        case OriginMode::Absolute:
            row = std::max<u32>(1, std::min<u32>(row, composer.rows)) - 1;
            col = std::max<u32>(1, std::min<u32>(col, composer.columns)) - 1;
            break;
        case OriginMode::ScrollingRegion:
            row = marginTop + std::max<u32>(1, std::min<u32>(row, marginBottom - marginTop)) - 1;
            col = hMargin + std::max<u32>(1, std::min<u32>(col, nColsEff - hMargin)) - 1;
            break;
    }

    posX = col;
    posY = row;
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_SU() {
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    arg = std::min<u32>(arg, marginBottom - marginTop);
    const bool pendingWrap = lastCol;
    scrollRegionUp((u16)(arg));
    lastCol = pendingWrap;
}

template <bool traced>
void VtermImpl<traced>::scrollRegionUp(u16 count) {
    if (horizMarginMode) {
        deleteRows(marginTop, count);
    } else {
        cf->scrollUp(marginTop, marginBottom, count, eraseAttrs);
        lastCol = false;
    }
}

template <bool traced>
void VtermImpl<traced>::scrollRegionDown(u16 count) {
    if (horizMarginMode) {
        insertRows(marginTop, count);
    } else {
        cf->scrollDown(marginTop, marginBottom, count, eraseAttrs);
        lastCol = false;
    }
}

template <bool traced>
void VtermImpl<traced>::csi_SD() {
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    arg = std::min<u32>(arg, marginBottom - marginTop);
    const bool pendingWrap = lastCol;
    scrollRegionDown((u16)(arg));
    lastCol = pendingWrap;
}

template <bool traced>
void VtermImpl<traced>::csi_CHT() {
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    arg = std::min<u32>(arg, composer.columns);
    if (arg == 1) {
        inp_HT();
    } else {
        for (int k = 0; k < arg; ++k) {
            jumpToNextTabStop();
        }
    }
}

template <bool traced>
void VtermImpl<traced>::csi_CBT() {
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    arg = std::min<u32>(arg, composer.columns);
    for (u32 k = 0; k < arg; ++k) {
        const u16 left = originMode == OriginMode::ScrollingRegion ? hMargin : 0;
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
}

template <bool traced>
void VtermImpl<traced>::csi_REP() {
    const u32 preceding = utf8dec.getUnicode();
    if (!preceding || codepointWidth(preceding) == 0) {
        return;
    }
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u64 observableCells = ((u64)(opts.saveLines) + composer.rows + 1) * composer.columns;
    if (arg > observableCells) {
        arg = (u32)(observableCells + (arg - observableCells) % composer.columns);
    }
    for (u32 k = 0; k < arg; ++k) {
        placeGraphicChar();
    }
}

template <bool traced>
void VtermImpl<traced>::csi_ED() {
    normalizeCursorPos();
    switch (inputOps[0]) {
        case 0:
            eraseEcmaRangeInRow(posY, posX, composer.columns - posX);
            for (u16 pY = posY + 1; pY < composer.rows; ++pY) {
                eraseEcmaRow(pY);
            }
            break;
        case 1:
            for (u16 pY = 0; pY < posY; ++pY) {
                eraseEcmaRow(pY);
            }
            eraseEcmaRangeInRow(posY, 0, posX + 1);
            break;
        case 3:
            cf->dropScrollbackHistory();
            break;

        case 2:
            for (u16 pY = 0; pY < composer.rows; ++pY) {
                eraseEcmaRow(pY);
            }
            break;
        default:
            break;
    }
}

template <bool traced>
void VtermImpl<traced>::csi_EL() {
    normalizeCursorPos();
    switch (inputOps[0]) {
        case 0:
            eraseEcmaRangeInRow(posY, posX, composer.columns - posX);
            break;
        case 1:
            eraseEcmaRangeInRow(posY, 0, posX + 1);
            break;
        case 2:
            eraseEcmaRangeInRow(posY, 0, composer.columns);
            break;
        default:
            break;
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECSED() {
    normalizeCursorPos();
    if (inputOps[0] == 0 || inputOps[0] == 2) {
        const u16 firstRow = inputOps[0] == 2 ? 0 : posY;
        const u16 lastRow = composer.rows - 1;
        for (u16 row = firstRow; row <= lastRow; ++row) {
            const u16 first = row == posY && inputOps[0] == 0 ? posX : 0;
            selectiveEraseRangeInRow(row, first, composer.columns - first);
        }
    } else if (inputOps[0] == 1) {
        for (u16 row = 0; row <= posY; ++row) {
            const u16 count = row == posY ? posX + 1 : composer.columns;
            selectiveEraseRangeInRow(row, 0, count);
        }
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECSEL() {
    normalizeCursorPos();
    if (inputOps[0] == 0) {
        selectiveEraseRangeInRow(posY, posX, composer.columns - posX);
    } else if (inputOps[0] == 1) {
        selectiveEraseRangeInRow(posY, 0, posX + 1);
    } else if (inputOps[0] == 2) {
        selectiveEraseRangeInRow(posY, 0, composer.columns);
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECSCA() {
    if (inputOps[0] == 1) {
        attrs.protected_char |= TerminalCell::decProtection;
    } else {
        attrs.protected_char &= ~TerminalCell::decProtection;
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECFRA() {
    if (inputOps[0] >= 32 && inputOps[0] <= 0x10ffff) {
        Rectangle rectangle;
        if (!rectangleFromParams(1, rectangle)) {
            return;
        }
        cf->fillRectangle(rectangle.top, rectangle.left, rectangle.bottom, rectangle.right, inputOps[0], attrs, eraseAttrs);
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECERA(bool selective) {
    Rectangle rectangle;
    if (!rectangleFromParams(0, rectangle)) {
        return;
    }
    for (u16 y = rectangle.top; y < rectangle.bottom; ++y) {
        if (selective) {
            selectiveEraseRangeInRow(y, rectangle.left, rectangle.right - rectangle.left);
        } else {
            eraseRangeInRow(y, rectangle.left, rectangle.right - rectangle.left);
        }
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECCRA() {
    Rectangle source;
    if (!rectangleFromParams(0, source)) {
        return;
    }
    u16 rowBase, columnBase, rowLimit, columnLimit;
    rectangleOrigin(rowBase, columnBase, rowLimit, columnLimit);
    const u32 targetRowParam = 5 < nInputOps ? inputOps[5] : 0;
    const u32 targetColumnParam = 6 < nInputOps ? inputOps[6] : 0;
    const u32 targetRow = targetRowParam ? targetRowParam : 1;
    const u32 targetColumn = targetColumnParam ? targetColumnParam : 1;
    const u16 targetTop = rowBase + std::min<u32>(targetRow, rowLimit - rowBase) - 1;
    const u16 targetLeft = columnBase + std::min<u32>(targetColumn, columnLimit - columnBase) - 1;
    const u16 height = std::min<u16>(source.bottom - source.top, rowLimit - targetTop);
    const u16 width = std::min<u16>(source.right - source.left, columnLimit - targetLeft);
    cf->copyRectangle(source.top, source.left, targetTop, targetLeft, height, width, eraseAttrs);
}

template <bool traced>
void VtermImpl<traced>::csi_DECCARA(bool reverse) {
    if (nInputOps >= 5) {
        Rectangle rectangle;
        if (!rectangleFromParams(0, rectangle)) {
            return;
        }
        cf->changeRectangleAttributes(rectangle.top, rectangle.left, rectangle.bottom, rectangle.right, inputOps + 4, nInputOps - 4, reverse);
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECRQCRA() {
    if (nInputOps >= 6) {
        Rectangle rectangle;
        if (!rectangleFromParams(2, rectangle)) {
            return;
        }
        const u16 checksum = cf->checksum(rectangle.top, rectangle.left, rectangle.bottom, rectangle.right);
        StringBuilder response;
        response << inputOps[0] << StringView(u8"!~") << Hex{checksum, 4, true};
        writeDcsResponse(StringView(response));
    }
}

template <bool traced>
void VtermImpl<traced>::csi_IL() {
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1;
        arg = std::min<u32>(arg, marginBottom - posY);
        insertRows(posY, (u16)(arg));
        inp_CR();
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DL() {
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1;
        arg = std::min<u32>(arg, marginBottom - posY);
        deleteRows(posY, (u16)(arg));
        inp_CR();
    }
}

template <bool traced>
void VtermImpl<traced>::csi_ICH() {
    if (isCursorInsideMargins()) {
        u32 arg = inputOps[0] ? inputOps[0] : 1;
        arg = min<u32>(arg, nColsEff - posX);
        cf->insertCells(posY, posX, nColsEff, (u16)(arg), eraseAttrs);
    }
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_DCH() {
    if (posX >= hMargin && posX < nColsEff) {
        u32 arg = inputOps[0] ? inputOps[0] : 1;
        arg = min<u32>(arg, nColsEff - posX);
        cf->deleteCells(posY, posX, nColsEff, (u16)(arg), eraseAttrs);
    }
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_ECH() {
    u32 arg = inputOps[0] ? inputOps[0] : 1;
    const u32 len = composer.columns - posX;
    arg = std::min(arg, len);
    eraseEcmaRangeInRow(posY, posX, arg);
    lastCol = false;
}

template <bool traced>
void VtermImpl<traced>::csi_STBM() {
    if (nInputOps <= 2) {
        u32 newMarginTop = inputOps[0] > 0 ? inputOps[0] - 1 : 0;
        u32 newMarginBottom = nInputOps < 2 || inputOps[1] == 0 ? composer.rows : inputOps[1];

        const bool illegal = newMarginTop >= composer.rows || newMarginBottom > composer.rows || newMarginBottom <= newMarginTop + 1;
        if (!illegal && (newMarginTop != marginTop || newMarginBottom != marginBottom)) {
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
}

template <bool traced>
void VtermImpl<traced>::csi_SLRM() {
    if (nInputOps <= 2) {
        u32 newMarginLeft = inputOps[0] > 0 ? inputOps[0] - 1 : 0;
        u32 newMarginRight = nInputOps < 2 || inputOps[1] == 0 ? composer.columns : inputOps[1];

        const bool illegal = newMarginLeft >= composer.columns || newMarginRight > composer.columns || newMarginRight <= newMarginLeft + 1;
        if (!illegal && (newMarginLeft != hMargin || newMarginRight != nColsEff)) {
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
}

template <bool traced>
void VtermImpl<traced>::csi_TBC() {
    switch (inputOps[0]) {
        case 0: {
            if (!tabStopsCustomized) {
                for (unsigned column = 8; column < composer.columns; column += 8) {
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
}

template <bool traced>
void VtermImpl<traced>::csi_SM() {
    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& arg = inputOps[k];

        switch (arg) {
            case 2:
                keyboardLocked = true;
                break;
            case 4:
                insertMode = true;
                break;
            case 6:
                eraseModeAll = true;
                break;
            case 12:
                localEcho = false;
                break;
            case 20:
                autoNewlineMode = true;
                break;
            default:
                break;
        }
    }
}

template <bool traced>
void VtermImpl<traced>::csi_RM() {
    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& arg = inputOps[k];

        switch (arg) {
            case 2:
                keyboardLocked = false;
                break;
            case 4:
                insertMode = false;
                break;
            case 6:
                eraseModeAll = false;
                break;
            case 12:
                localEcho = true;
                break;
            case 20:
                autoNewlineMode = false;
                break;
            default:
                break;
        }
    }
}

template <bool traced>
void VtermImpl<traced>::setPrivMode(u32 arg, bool set) {
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
                if (allowColumnMode) {
                    switchColMode(ColMode::C132);
                }
                break;
            case 4:
                smoothScrollMode = true;
                break;
            case 5:
                screenReverseVideo = true;
                frame_pri->setScreenReverseVideo(true);
                frame_alt->setScreenReverseVideo(true);
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
                break;
            case 18:
                if (compatLevel >= CompatibilityLevel::VT200) {
                    printFormFeedMode = true;
                }
                break;
            case 19:
                if (compatLevel >= CompatibilityLevel::VT200) {
                    printExtentMode = true;
                }
                break;
            case 40:
                allowColumnMode = true;
                break;
            case 41:
                moreFixMode = true;
                break;
            case 42:
                nationalReplacementMode = true;
                break;
            case 66:
                keypadMode = KeypadMode::Application;
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
                frame_pri->setBlinkState(blinkVisible, true);
                frame_alt->setBlinkState(blinkVisible, true);
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
                if (compatLevel >= CompatibilityLevel::VT400) {
                    horizMarginMode = true;
                    hMargin = 0;
                    nColsEff = composer.columns;
                }
                break;
            case 95:
                if (compatLevel >= CompatibilityLevel::VT500) {
                    noClearColumnMode = true;
                }
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
                if (allowColumnMode) {
                    switchColMode(ColMode::C80);
                }
                break;
            case 4:
                smoothScrollMode = false;
                break;
            case 5:
                screenReverseVideo = false;
                frame_pri->setScreenReverseVideo(false);
                frame_alt->setScreenReverseVideo(false);
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
                break;
            case 18:
                if (compatLevel >= CompatibilityLevel::VT200) {
                    printFormFeedMode = false;
                }
                break;
            case 19:
                if (compatLevel >= CompatibilityLevel::VT200) {
                    printExtentMode = false;
                }
                break;
            case 40:
                allowColumnMode = false;
                break;
            case 41:
                moreFixMode = false;
                break;
            case 42:
                nationalReplacementMode = false;
                break;
            case 66:
                keypadMode = KeypadMode::Normal;
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
                frame_pri->setBlinkState(true, false);
                frame_alt->setBlinkState(true, false);
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
                if (compatLevel >= CompatibilityLevel::VT400) {
                    horizMarginMode = false;
                    hMargin = 0;
                    nColsEff = composer.columns;
                }
                break;
            case 95:
                if (compatLevel >= CompatibilityLevel::VT500) {
                    noClearColumnMode = false;
                }
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
                savedCursor = &savedCursorPri;
                esc_DECRC();
                switchScreenBufferMode(false, true);
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
                break;
        }
    }
}

template <bool traced>
bool VtermImpl<traced>::getPrivateMode(u32 arg) const {
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
        case 18:
            return printFormFeedMode;
        case 19:
            return printExtentMode;
        case 40:
            return allowColumnMode;
        case 41:
            return moreFixMode;
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
        case 95:
            return noClearColumnMode;
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

template <bool traced>
void VtermImpl<traced>::csi_privSM() {
    for (size_t k = 0; k < nInputOps; ++k) {
        setPrivMode(inputOps[k], true);
    }
}

template <bool traced>
void VtermImpl<traced>::csi_privRM() {
    for (size_t k = 0; k < nInputOps; ++k) {
        setPrivMode(inputOps[k], false);
    }
}

template <bool traced>
void VtermImpl<traced>::csi_privSave() {
    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& arg = inputOps[k];
        switch (arg) {
            case 2:
            case 1048:
            case 1049:
                break;
            default:
                savedPrivModes[arg] = getPrivateMode(arg);
                break;
        }
    }
}

template <bool traced>
void VtermImpl<traced>::csi_privRestore() {
    for (size_t k = 0; k < nInputOps; ++k) {
        const auto& arg = inputOps[k];
        const auto it = savedPrivModes.find(arg);
        if (it != savedPrivModes.end()) {
            setPrivMode(arg, it->second);
        }
    }
}

template <bool traced>
void VtermImpl<traced>::setFgFromPalIx() {
    if (fgPalIx < 0) {
        setAttrForeground(CellColor::defaultForeground());
    } else if (fgPalIx > 255) {
        return;
    } else {
        setAttrForeground(CellColor::indexed(fgPalIx));
    }
    if (opts.boldColors && attrs.bold && fgPalIx >= 0 && fgPalIx <= 7) {
        attrs.setForeground(CellColor::indexed(fgPalIx + 8));
    }
    if (underlineColorDefault) {
        setAttrUnderlineColor(attrForeground());
    }
}

template <bool traced>
void VtermImpl<traced>::setBgFromPalIx() {
    if (bgPalIx < 0) {
        setAttrBackground(CellColor::defaultBackground());
    } else if (bgPalIx > 255) {
        return;
    } else {
        setAttrBackground(CellColor::indexed(bgPalIx));
    }
}

template <bool traced>
void VtermImpl<traced>::csi_SGR() {
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
            // Xterm accepts both 2:Pr:Pg:Pb and ISO 8613-6's
            // 2:Pi:Pr:Pg:Pb form.  Pi and any later optional fields are
            // ignored.
            const size_t count = end - first + 1;
            const size_t rgbFirst = first + (count >= 4);
            if (mode != 2 || count < 3 || (count == 3 && !inputPresent[first]) || !inputPresent[rgbFirst] || !inputPresent[rgbFirst + 1] || !inputPresent[rgbFirst + 2] || inputOps[rgbFirst] > 255 || inputOps[rgbFirst + 1] > 255 || inputOps[rgbFirst + 2] > 255) {
                return false;
            }
            color = CellColor::direct({
                (u8)(inputOps[rgbFirst]),
                (u8)(inputOps[rgbFirst + 1]),
                (u8)(inputOps[rgbFirst + 2]),
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
                attrs.uc_pt = 0;
                attrs.bold = 0;
                attrs.faint = 0;
                attrs.italic = 0;
                attrs.underline_style = 0;
                attrs.blink = 0;
                attrs.conceal = 0;
                attrs.strike = 0;
                attrs.overline = 0;
                attrs.inverse = 0;
                reverseVideo = false;
                fgPalIx = defaultFgPalIx;
                setFgFromPalIx();
                bgPalIx = defaultBgPalIx;
                setBgFromPalIx();
                underlineColorDefault = true;
                setAttrUnderlineColor(attrForeground());
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
                    }
                } else {
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

            case 38: {
                CellColor color = attrForeground();
                if (parseColor(k, color, &fgPalIx)) {
                    setAttrForeground(color);
                }
            }
                if (underlineColorDefault) {
                    setAttrUnderlineColor(attrForeground());
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

            case 48: {
                CellColor color = attrBackground();
                if (parseColor(k, color, &bgPalIx)) {
                    setAttrBackground(color);
                }
            } break;
            case 49:
                bgPalIx = defaultBgPalIx;
                setBgFromPalIx();
                break;

            case 58:
                underlinePalIx = -1;
                {
                    CellColor color = attrUnderlineColor();
                    if (parseColor(k, color, &underlinePalIx)) {
                        setAttrUnderlineColor(color);
                        underlineColorDefault = false;
                    }
                }
                break;
            case 59:
                underlineColorDefault = true;
                setAttrUnderlineColor(attrForeground());
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
                break;
        }
    }
    if (underlineColorDefault) {
        setAttrUnderlineColor(reverseVideo ? attrBackground() : attrForeground());
    }
}

/* 64 - VT420 family
    *  9 - National Replacement Character-sets
    * 15 - DEC technical set
    * 21 - horizontal scrolling
    * 22 - color
    */
#define DEVICE_ID "64;1;2;6;8;9;15;21;22;28;29c"

template <bool traced>
void VtermImpl<traced>::csi_priDA() {
    writeCsiResponse("?" DEVICE_ID);
}

template <bool traced>
void VtermImpl<traced>::csi_secDA() {
    writeCsiResponse(">41;14;0c");
}

template <bool traced>
void VtermImpl<traced>::csi_terDA() {
    writeDcsResponse("!|00000000");
}

template <bool traced>
void VtermImpl<traced>::csi_XTVERSION() {
    writeDcsResponse(">|Shitty " SHITTY_VERSION);
}

template <bool traced>
void VtermImpl<traced>::csi_DECRQM(bool privateMode) {
    if (compatLevel < CompatibilityLevel::VT300) {
        return;
    }
    const u32 mode = inputOps[0];
    u8 state = 0;

    if (privateMode) {
        switch (mode) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 12:
            case 18:
            case 19:
            case 25:
            case 40:
            case 41:
            case 42:
            case 45:
            case 47:
            case 66:
            case 67:
                state = (mode == 2 ? compatLevel != CompatibilityLevel::VT52 : getPrivateMode(mode)) ? 1 : 2;
                break;
            case 60:
            case 61:
            case 64:
            case 68:
            case 73:
                state = 4;
                break;
            case 69:
                if (compatLevel < CompatibilityLevel::VT400) {
                    break;
                }
                state = getPrivateMode(mode) ? 1 : 2;
                break;
            case 81:
                if (compatLevel < CompatibilityLevel::VT400) {
                    break;
                }
                state = 4;
                break;
            case 95:
                if (compatLevel < CompatibilityLevel::VT500) {
                    break;
                }
                state = getPrivateMode(mode) ? 1 : 2;
                break;
            case 34:
            case 35:
            case 36:
            case 57:
            case 96:
            case 97:
            case 98:
            case 99:
            case 100:
            case 101:
            case 102:
            case 103:
            case 104:
            case 106:
                if (compatLevel < CompatibilityLevel::VT500) {
                    break;
                }
                state = 4;
                break;
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
                state = getPrivateMode(mode) ? 1 : 2;
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
            case 6:
                state = eraseModeAll ? 1 : 2;
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

    StringBuilder response;
    if (privateMode) {
        response << StringView(u8"?");
    }
    response << mode << StringView(u8";") << (unsigned)(state) << StringView(u8"$y");
    writeCsiResponse(StringView(response));
}

template <bool traced>
void VtermImpl<traced>::csi_DSR(bool privateMode) {
    if (privateMode) {
        switch (inputOps[0]) {
            case 6: {
                StringBuilder response;
                response << StringView(u8"?");
                if (originMode == OriginMode::Absolute) {
                    response << (posY + 1) << StringView(u8";") << (posX + 1);
                } else {
                    response << (posY - marginTop + 1) << StringView(u8";") << (posX - hMargin + 1);
                }
                response << StringView(u8";1R");
                writeCsiResponse(StringView(response));
            } break;
            case 15:
                writeCsiResponse("?10n");
                break;
            case 25:
                writeCsiResponse(userDefinedKeysLocked ? "?21n" : "?20n");
                break;
            case 26:
                writeCsiResponse("?27;1;0;0n");
                break;
            case 55:
                writeCsiResponse("?50n");
                break;
            case 56:
                writeCsiResponse("?57;1n");
                break;
            case 62:
                writeCsiResponse("0*{");
                break;
            case 63: {
                StringBuilder response;
                response << (nInputOps > 1 ? inputOps[1] : 0) << StringView(u8"!~0000");
                writeDcsResponse(StringView(response));
            } break;
            case 75:
                writeCsiResponse("?70n");
                break;
            case 85:
                writeCsiResponse("?83n");
                break;
            case 996:
                reportColorScheme();
                break;
            default:
                break;
        }
        return;
    }
    switch (inputOps[0]) {
        case 5:
            writeCsiResponse("0n");
            break;
        case 6: {
            StringBuilder response;
            if (originMode == OriginMode::Absolute) {
                response << (posY + 1) << StringView(u8";") << (posX + 1) << StringView(u8"R");
            } else {
                response << (posY - marginTop + 1) << StringView(u8";") << (posX - hMargin + 1) << StringView(u8"R");
            }
            writeCsiResponse(StringView(response));
        } break;
        default:
            break;
    }
}

template <bool traced>
void VtermImpl<traced>::esch_DECALN() {
    originMode = OriginMode::Absolute;
    marginTop = 0;
    marginBottom = composer.rows;
    hMargin = 0;
    nColsEff = composer.columns;
    posX = 0;
    posY = 0;
    lastCol = false;

    TerminalCell origAttrs = attrs;
    TerminalCell origEraseAttrs = eraseAttrs;

    resetAttrs();
    fillScreen('E');

    attrs = origAttrs;
    eraseAttrs = origEraseAttrs;
    reverseVideo = attrs.inverse;
}

template <bool traced>
void VtermImpl<traced>::setLineAttribute(u8 attribute) {
    cf->setLineAttribute(posY, attribute);
    if (attribute) {
        posX = std::min<u16>(posX, std::max(1, composer.columns / 2) - 1);
    }
}

template <bool traced>
void VtermImpl<traced>::esc_RIS() {
    resetTerminal();
}

template <bool traced>
void VtermImpl<traced>::csi_DECSTR() {
    resetScreen(false);
    resetAttrs();
    marginTop = 0;
    marginBottom = composer.rows;
    hMargin = 0;
    nColsEff = composer.columns;
    savedCursor->posX = 0;
    savedCursor->posY = 0;
    savedCursor->lastCol = false;
    savedCursor->attrs = attrs;
    savedCursor->eraseAttrs = eraseAttrs;
    savedCursor->originMode = OriginMode::Absolute;
    savedCursor->charsetState = CharsetState{};
    savedCursor->isSet = true;
}

template <bool traced>
void VtermImpl<traced>::dcs_DECUDK() {
    if (userDefinedKeysLocked || nInputOps > 2) {
        return;
    }
    const u32 clear = inputPresent[0] ? inputOps[0] : 0;
    const u32 lock = nInputOps > 1 && inputPresent[1] ? inputOps[1] : 0;
    if (clear > 1 || lock > 1 || (nInputOps > 1 && inputSeparators[1] != ';')) {
        return;
    }
    if (clear == 0) {
        userDefinedKeys.clear();
    }

    for (const DcsUdkDefinition& definition : dcsUdkDefinitions) {
        const auto* value = (const char*)(dcsDecoded.data()) + definition.valueOffset;
        userDefinedKeys[definition.key] = std::string(value, definition.valueLength);
    }
    userDefinedKeysLocked = lock == 0;
}

template <bool traced>
void VtermImpl<traced>::writeDecrqssResponse(StringView value) {
    StringBuilder response;
    response << StringView(u8"1$r") << value;
    writeDcsResponse(StringView(response));
}

template <bool traced>
void VtermImpl<traced>::dcs_DECRQSS_DECSCL() {
    StringBuilder value;
    value << 60 + (u8)(compatLevel) << StringView(u8";") << (send8BitControls ? 0 : 1) << StringView(u8"\"p");
    writeDecrqssResponse(StringView(value));
}

template <bool traced>
void VtermImpl<traced>::dcs_DECRQSS_SGR() {
    StringBuilder value;
    value << StringView(u8"0");
    if (attrs.bold) {
        value << StringView(u8";1");
    }
    if (attrs.faint) {
        value << StringView(u8";2");
    }
    if (attrs.italic) {
        value << StringView(u8";3");
    }
    if (attrs.underlined()) {
        value << StringView(u8";4");
        if (attrs.underline_style > 1) {
            value << StringView(u8":") << (unsigned)(attrs.underline_style);
        }
    }
    if (attrs.blink) {
        value << StringView(u8";5");
    }
    if (reverseVideo) {
        value << StringView(u8";7");
    }
    if (attrs.conceal) {
        value << StringView(u8";8");
    }
    if (attrs.strike) {
        value << StringView(u8";9");
    }
    if (attrs.overline) {
        value << StringView(u8";53");
    }
    if (fgPalIx >= 0 && fgPalIx < 8) {
        value << StringView(u8";") << 30 + fgPalIx;
    } else if (fgPalIx >= 8 && fgPalIx < 16) {
        value << StringView(u8";") << 90 + fgPalIx - 8;
    } else if (fgPalIx >= 0) {
        value << StringView(u8";38:5:") << fgPalIx;
    } else if (attrForeground().source() == CellColor::Source::Direct) {
        const Color color = attrForeground().color();
        value << StringView(u8";38:2::") << (unsigned)(color.red) << StringView(u8":") << (unsigned)(color.green) << StringView(u8":") << (unsigned)(color.blue);
    }
    if (bgPalIx >= 0 && bgPalIx < 8) {
        value << StringView(u8";") << 40 + bgPalIx;
    } else if (bgPalIx >= 8 && bgPalIx < 16) {
        value << StringView(u8";") << 100 + bgPalIx - 8;
    } else if (bgPalIx >= 0) {
        value << StringView(u8";48:5:") << bgPalIx;
    } else if (attrBackground().source() == CellColor::Source::Direct) {
        const Color color = attrBackground().color();
        value << StringView(u8";48:2::") << (unsigned)(color.red) << StringView(u8":") << (unsigned)(color.green) << StringView(u8":") << (unsigned)(color.blue);
    }
    if (!underlineColorDefault) {
        if (underlinePalIx >= 0) {
            value << StringView(u8";58:5:") << underlinePalIx;
        } else {
            const Color color = attrUnderlineColor().color();
            value << StringView(u8";58:2::") << (unsigned)(color.red) << StringView(u8":") << (unsigned)(color.green) << StringView(u8":") << (unsigned)(color.blue);
        }
    }
    value << StringView(u8"m");
    writeDecrqssResponse(StringView(value));
}

template <bool traced>
void VtermImpl<traced>::dcs_DECRQSS_DECSTBM() {
    StringBuilder value;
    value << marginTop + 1 << StringView(u8";") << marginBottom << StringView(u8"r");
    writeDecrqssResponse(StringView(value));
}

template <bool traced>
void VtermImpl<traced>::dcs_DECRQSS_DECSLRM() {
    StringBuilder value;
    value << hMargin + 1 << StringView(u8";") << nColsEff << StringView(u8"s");
    writeDecrqssResponse(StringView(value));
}

template <bool traced>
void VtermImpl<traced>::dcs_DECRQSS_DECSLPP() {
    StringBuilder value;
    value << composer.rows << StringView(u8"t");
    writeDecrqssResponse(StringView(value));
}

template <bool traced>
void VtermImpl<traced>::dcs_DECRQSS_DECSCUSR() {
    StringBuilder value;
    value << (unsigned)(cursorStyleParam) << StringView(u8" q");
    writeDecrqssResponse(StringView(value));
}

template <bool traced>
void VtermImpl<traced>::dcs_DECRQSS_DECSCA() {
    StringBuilder value;
    value << ((attrs.protected_char & TerminalCell::decProtection) ? 1 : 0) << StringView(u8"\"q");
    writeDecrqssResponse(StringView(value));
}

template <bool traced>
void VtermImpl<traced>::dcs_DECRQSS_UNKNOWN() {
    writeDcsResponse("0$r");
}

template <bool traced>
void VtermImpl<traced>::dcs_XTGETTCAP(StringView encoded, StringView value) {
    StringBuilder replies(static_cast<Buffer&&>(dcsDecoded));
    replies << (send8BitControls ? StringView(u8"\x90") : StringView(u8"\x1bP"));
    replies << (value.empty() ? StringView(u8"0+r") : StringView(u8"1+r"));
    replies << encoded;
    if (!value.empty()) {
        static constexpr u8 hex[] = u8"0123456789abcdef";
        replies << StringView(u8"=");
        for (u8 ch : value) {
            const u8 pair[] = {hex[ch >> 4], hex[ch & 15]};
            replies.append(pair, sizeof(pair));
        }
    }
    replies << (send8BitControls ? StringView(u8"\x9c") : StringView(u8"\x1b\\"));
    dcsDecoded = static_cast<Buffer&&>(replies);
}

template <bool traced>
void VtermImpl<traced>::dcs_XTGETTCAP_COMMIT() {
    writePty((const u8*)(dcsDecoded.data()), dcsDecoded.used(), false);
}

template <bool traced>
void VtermImpl<traced>::osc_TITLE_0(StringView payload) {
    iconTitle.assign((const char*)(payload.data()), payload.length());
    windowTitle = iconTitle;
    host.title(payload);
    host.osc(0, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_TITLE_1(StringView payload) {
    iconTitle.assign((const char*)(payload.data()), payload.length());
    host.title(payload);
    host.osc(1, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_TITLE_2(StringView payload) {
    windowTitle.assign((const char*)(payload.data()), payload.length());
    host.title(payload);
    host.osc(2, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_PALETTE(u32 index, Color color, bool query) {
    if (index >= 256 + TerminalColors::specialCount) {
        return;
    }
    const bool special = index >= 256;
    const u16 colorIndex = (u16)(special ? index - 256 : index);
    if (query) {
        StringBuilder reply;
        reply << StringView(u8"4;") << index << StringView(u8";") << (special ? colors.special[colorIndex] : colors.palette[colorIndex]);
        writeOscResponse(StringView(reply));
        return;
    }
    if (special) {
        colors.special[colorIndex] = color;
        colors.changed();
        frame_pri->expose();
        frame_alt->expose();
    } else {
        applyPaletteColor(colorIndex, color);
    }
}

template <bool traced>
void VtermImpl<traced>::osc_SPECIAL_COLOR(u32 index, Color color, bool query) {
    if (index >= TerminalColors::specialCount) {
        return;
    }
    if (query) {
        StringBuilder reply;
        reply << StringView(u8"5;") << index << StringView(u8";") << colors.special[index];
        writeOscResponse(StringView(reply));
        return;
    }
    colors.special[index] = color;
    colors.changed();
    frame_pri->expose();
    frame_alt->expose();
}

template <bool traced>
void VtermImpl<traced>::osc_SPECIAL_COLOR_MODE(u32 index, u32 value) {
    if (index > TerminalColors::specialCount) {
        return;
    }
    const u8 bit = (u8)(1u << index);
    const u8 modes = value == 0 ? (u8)(colors.specialModes & ~bit) : (u8)(colors.specialModes | bit);
    if (modes == colors.specialModes) {
        return;
    }
    colors.specialModes = modes;
    colors.changed();
    frame_pri->expose();
    frame_alt->expose();
}

template <bool traced>
void VtermImpl<traced>::osc_CWD(StringView raw, StringView path, bool valid) {
    host.osc(7, raw);
    if (valid) {
        host.cwd(path);
    }
}

template <bool traced>
void VtermImpl<traced>::osc_HYPERLINK(StringView id, bool hasId, StringView uri) {
    if (uri.empty()) {
        activeHyperlink = 0;
        return;
    }

    StringBuilder identity;
    if (hasId) {
        identity << StringView(u8"id=") << id << StringView(u8";uri=") << uri;
    } else {
        identity << StringView(u8"uri=") << uri;
    }
    const StringView identityView(identity);

    CellExtraStore& extras = *composer.cellExtras;
    if (const u32 known = extras.findHyperlink(identityView); known != 0) {
        activeHyperlink = known;
        return;
    }
    if (nextHyperlink == 0) {
        activeHyperlink = 0;
        return;
    }
    activeHyperlink = extras.getOrCreateHyperlink(identityView, uri, nextHyperlink++);
}

template <bool traced>
void VtermImpl<traced>::osc_NOTIFY(StringView payload) {
    host.notify({}, stringView(windowTitle), payload, false);
}

template <bool traced>
void VtermImpl<traced>::osc_PROGRESS(u32 state, u32 percent) {
    host.progress(state, percent);
}

template <bool traced>
void VtermImpl<traced>::osc_DYNAMIC_COLOR(u32 command, Color color, bool query) {
    if (query) {
        switch (command) {
            case 10:
                color = colors.defaultForeground;
                break;
            case 11:
                color = colors.defaultBackground;
                break;
            case 12:
                color = cursorColor;
                break;
            case 17:
                color = selectionBgColor;
                break;
            case 19:
                color = selectionFgColor;
                break;
            default:
                return;
        }
        StringBuilder response;
        response << command << StringView(u8";") << color;
        writeOscResponse(StringView(response));
        return;
    }
    switch (command) {
        case 10:
            colors.defaultForeground = color;
            colors.changed();
            defaultFgPalIx = -1;
            frame_pri->expose();
            frame_alt->expose();
            break;
        case 11:
            colors.defaultBackground = color;
            colors.changed();
            defaultBgPalIx = -1;
            frame_pri->expose();
            frame_alt->expose();
            break;
        case 12:
            cursorColor = color;
            frame_pri->setCursorColor(color);
            frame_alt->setCursorColor(color);
            break;
        case 17:
            selectionBgColor = color;
            frame_pri->setSelectionColor(false, color, true);
            frame_alt->setSelectionColor(false, color, true);
            break;
        case 19:
            selectionFgColor = color;
            frame_pri->setSelectionColor(true, color, true);
            frame_alt->setSelectionColor(true, color, true);
            break;
    }
}

template <bool traced>
void VtermImpl<traced>::osc_CLIPBOARD_QUERY(StringView raw, bool primary, bool clipboard, u8 replySelector, bool selectorsEmpty) {
    host.osc(52, raw);

    StringView content;
    if (opts.allowOsc52Read) {
        if (primary) {
            content = composer.clipboard->readPrimary();
        }
        if (content.empty() && clipboard) {
            content = composer.clipboard->readClipboard();
        }
    }

    Buffer encoded;
    base64Encode(content, encoded);
    StringBuilder reply;
    reply << StringView(u8"52;");
    if (selectorsEmpty) {
        reply << StringView(u8"s0");
    } else if (replySelector != 0) {
        reply.append(&replySelector, 1);
    }
    reply << StringView(u8";") << StringView(encoded);
    writeOscResponse(StringView(reply));
}

template <bool traced>
void VtermImpl<traced>::osc_CLIPBOARD_WRITE(StringView raw, StringView decoded, bool valid, bool primary, bool clipboard) {
    host.osc(52, raw);

    if (!valid) {
        return;
    }
    if (primary) {
        composer.clipboard->writePrimary(decoded);
    }
    if (clipboard) {
        composer.clipboard->writeClipboard(decoded);
    }
}

template <bool traced>
void VtermImpl<traced>::osc_CLIPBOARD_MALFORMED(StringView raw) {
    host.osc(52, raw);
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_PALETTE() {
    std::copy(std::begin(originalPalette256), std::end(originalPalette256), std::begin(colors.palette));
    colors.changed();
    frame_pri->expose();
    frame_alt->expose();
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_PALETTE(u32 index) {
    if (index > 255) {
        return;
    }
    applyPaletteColor((u16)(index), originalPalette256[index]);
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_SPECIAL_COLOR() {
    std::copy(std::begin(colors.originalSpecial), std::end(colors.originalSpecial), std::begin(colors.special));
    colors.changed();
    frame_pri->expose();
    frame_alt->expose();
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_SPECIAL_COLOR(u32 index) {
    if (index >= TerminalColors::specialCount) {
        return;
    }
    colors.special[index] = colors.originalSpecial[index];
    colors.changed();
    frame_pri->expose();
    frame_alt->expose();
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_DEFAULT_FOREGROUND() {
    colors.defaultForeground = opts.fg;
    colors.changed();
    defaultFgPalIx = -1;
    frame_pri->expose();
    frame_alt->expose();
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_DEFAULT_BACKGROUND() {
    colors.defaultBackground = opts.bg;
    colors.changed();
    defaultBgPalIx = -1;
    frame_pri->expose();
    frame_alt->expose();
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_CURSOR_COLOR() {
    cursorColor = opts.cr;
    frame_pri->setCursorColor(cursorColor);
    frame_alt->setCursorColor(cursorColor);
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_SELECTION_BACKGROUND() {
    selectionBgColor = opts.bg;
    frame_pri->setSelectionColor(false, selectionBgColor, false);
    frame_alt->setSelectionColor(false, selectionBgColor, false);
}

template <bool traced>
void VtermImpl<traced>::osc_RESET_SELECTION_FOREGROUND() {
    selectionFgColor = opts.fg;
    frame_pri->setSelectionColor(true, selectionFgColor, false);
    frame_alt->setSelectionColor(true, selectionFgColor, false);
}

template <bool traced>
void VtermImpl<traced>::osc_SHELL_A(StringView payload) {
    currentSemantic = 1;
    host.osc(133, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_SHELL_B(StringView payload) {
    if (currentSemantic == 1) {
        currentSemantic = 2;
    }
    host.osc(133, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_SHELL_C(StringView payload) {
    if (currentSemantic == 2) {
        currentSemantic = 3;
    }
    host.osc(133, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_SHELL_D(StringView payload) {
    if (currentSemantic == 3) {
        currentSemantic = 0;
    }
    host.osc(133, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_SHELL_UNKNOWN(StringView payload) {
    host.osc(133, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_UNKNOWN(u32 command, StringView payload) {
    host.osc(command, payload);
}

template <bool traced>
void VtermImpl<traced>::osc_NOTIFICATION_CAPABILITIES(StringView id) {
    StringBuilder response;
    response << StringView(u8"99;i=") << id << StringView(u8":p=?;p=title,body,close");
    writeOscResponse(StringView(response));
}

template <bool traced>
void VtermImpl<traced>::osc_NOTIFICATION_CLOSE(StringView id) {
    const std::string key((const char*)(id.data()), id.length());
    if (key.empty() || !activeNotificationIds.erase(key)) {
        return;
    }
    host.notify(stringView(key), {}, {}, true);
    notifications.erase(key);
}

template <bool traced>
Base64Decoder VtermImpl<traced>::notificationDecoder(StringView id, bool body) const {
    const std::string key((const char*)(id.data()), id.length());
    const auto found = notifications.find(key);
    if (found == notifications.end()) {
        return {};
    }
    return body ? found->second.body.decoder : found->second.title.decoder;
}

template <bool traced>
void VtermImpl<traced>::osc_NOTIFICATION_TITLE(StringView id, StringView payload, const Base64Decoder& decoder, bool encoded, bool finalChunk) {
    applyNotificationPart(id, payload, decoder, encoded, finalChunk, false);
}

template <bool traced>
void VtermImpl<traced>::osc_NOTIFICATION_BODY(StringView id, StringView payload, const Base64Decoder& decoder, bool encoded, bool finalChunk) {
    applyNotificationPart(id, payload, decoder, encoded, finalChunk, true);
}

template <bool traced>
void VtermImpl<traced>::applyNotificationPart(StringView id, StringView payload, const Base64Decoder& decoder, bool encoded, bool finalChunk, bool body) {
    const std::string key((const char*)(id.data()), id.length());
    auto& notification = notifications[key];
    NotificationPart& destination = body ? notification.body : notification.title;
    const auto flushDecoder = [](NotificationPart& part) {
        Buffer decoded;
        if (!part.decoder.finish(decoded) || part.text.size() + decoded.used() > 8192) {
            return false;
        }
        part.text.append((const char*)(decoded.data()), decoded.used());
        part.decoder.reset();
        return true;
    };

    if (encoded) {
        if (!decoder.valid || destination.text.size() + payload.length() > 8192) {
            notifications.erase(key);
            return;
        }
        destination.text.append((const char*)(payload.data()), payload.length());
        destination.decoder = decoder;
        if (destination.decoder.complete) {
            destination.decoder.reset();
        }
    } else {
        if (!flushDecoder(destination) || destination.text.size() + payload.length() > 8192) {
            notifications.erase(key);
            return;
        }
        destination.text.append((const char*)(payload.data()), payload.length());
    }

    if (!finalChunk) {
        return;
    }
    if (!flushDecoder(notification.title) || !flushDecoder(notification.body)) {
        notifications.erase(key);
        return;
    }

    const auto escapeSafeUtf8 = [](const std::string& value) {
        for (size_t k = 0; k < value.size();) {
            const u8 first = value[k++];
            if (first <= 0x1f || first == 0x7f) {
                return false;
            }
            if (first < 0x80) {
                continue;
            }
            u32 codepoint;
            u32 minimum;
            size_t continuation;
            if ((first & 0xe0) == 0xc0) {
                codepoint = first & 0x1f;
                minimum = 0x80;
                continuation = 1;
            } else if ((first & 0xf0) == 0xe0) {
                codepoint = first & 0x0f;
                minimum = 0x800;
                continuation = 2;
            } else if ((first & 0xf8) == 0xf0) {
                codepoint = first & 0x07;
                minimum = 0x10000;
                continuation = 3;
            } else {
                return false;
            }
            if (k + continuation > value.size()) {
                return false;
            }
            for (size_t n = 0; n < continuation; ++n) {
                const u8 ch = value[k++];
                if ((ch & 0xc0) != 0x80) {
                    return false;
                }
                codepoint = (codepoint << 6) | (ch & 0x3f);
            }
            if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff) || (codepoint >= 0x7f && codepoint <= 0x9f)) {
                return false;
            }
        }
        return true;
    };
    if (!escapeSafeUtf8(notification.title.text) || !escapeSafeUtf8(notification.body.text)) {
        notifications.erase(key);
        return;
    }
    if (notification.title.text.empty()) {
        notification.title.text = std::move(notification.body.text);
        notification.body.text.clear();
    }
    if (notification.title.text.empty()) {
        notifications.erase(key);
        return;
    }
    host.notify(stringView(key), stringView(notification.title.text), stringView(notification.body.text), false);
    if (!key.empty()) {
        activeNotificationIds.insert(key);
    }
    notifications.erase(key);
}

template <bool traced>
void VtermImpl<traced>::reportInBandResize() {
    StringBuilder response;
    response << StringView(u8"48;") << composer.rows << StringView(u8";") << composer.columns << StringView(u8";") << composer.rows * composer.glyphHeight << StringView(u8";") << composer.columns * composer.glyphWidth << StringView(u8"t");
    writeCsiResponse(StringView(response));
}

template <bool traced>
void VtermImpl<traced>::reportColorScheme() {
    // Shitty has no runtime profile or operating-system theme switching.  Its
    // configured background therefore remains the authoritative preference;
    // application-originated OSC color changes must not affect this report.
    const u32 brightness = 299 * opts.bg.red + 587 * opts.bg.green + 114 * opts.bg.blue;
    const u8 scheme = brightness >= 128000 ? 2 : 1;
    StringBuilder response;
    response << StringView(u8"?997;") << (unsigned)(scheme) << StringView(u8"n");
    writeCsiResponse(StringView(response));
}

template <bool traced>
void VtermImpl<traced>::writeTitleResponse(char kind, StringView title) {
    StringBuilder response;
    response << StringView(u8"\x1b]");
    response.append(&kind, 1);
    if (titleModes & 2) {
        for (u8 byte : title) {
            response << Hex{byte, 2, true};
        }
    } else {
        response << title;
    }
    response << StringView(u8"\x1b\\");
    writePty((const u8*)(response.data()), response.used());
}

template <bool traced>
void VtermImpl<traced>::csi_XTTITLEMODE(bool set) {
    if (!csiHadParams) {
        titleModes = 0;
    } else {
        for (size_t k = 0; k < nInputOps; ++k) {
            if (inputOps[k] > 3) {
                continue;
            }
            const u8 bit = (u8)(1 << inputOps[k]);
            if (set) {
                titleModes |= bit;
            } else {
                titleModes &= (u8)~bit;
            }
        }
    }
}

template <bool traced>
void VtermImpl<traced>::applyPaletteColor(u16 index, Color color) {
    colors.palette[index] = color;
    colors.changed();
    frame_pri->expose();
    frame_alt->expose();
}

template <bool traced>
void VtermImpl<traced>::csiq_DECSCL() {
    CompatibilityLevel level;
    switch (inputOps[0]) {
        case 61:
            level = CompatibilityLevel::VT100;
            break;
        case 62:
            level = CompatibilityLevel::VT200;
            break;
        case 63:
            level = CompatibilityLevel::VT300;
            break;
        case 64:
            level = CompatibilityLevel::VT400;
            break;
        case 65:
            level = CompatibilityLevel::VT500;
            break;
        default:
            return;
    }

    const u32 controlMode = nInputOps > 1 ? inputOps[1] : 0;
    if (controlMode > 2) {
        return;
    }

    resetTerminal();
    compatLevel = level;
    send8BitControls = level != CompatibilityLevel::VT100 && controlMode != 1;
}

template <bool traced>
void VtermImpl<traced>::csi_XTWINOPS() {
    if (!opts.allowWindowOps) {
        return;
    }
    const u32 operation = inputOps[0];
    StringBuilder response;
    switch (operation) {
        case 4:
        case 8: {
            const auto info = host.windowInfo();
            const u32 currentHeight = operation == 4 ? composer.pixelHeight : composer.rows;
            const u32 currentWidth = operation == 4 ? composer.pixelWidth : composer.columns;
            const u32 maximumHeight = operation == 4 ? info.screenPixelHeight : info.screenPixelHeight / composer.glyphHeight;
            const u32 maximumWidth = operation == 4 ? info.screenPixelWidth : info.screenPixelWidth / composer.glyphWidth;
            const auto dimension = [&](size_t index, u32 current, u32 maximum) {
                if (index >= nInputOps || !inputPresent[index]) {
                    return current;
                }
                return inputOps[index] ? inputOps[index] : maximum;
            };
            host.windowOperation(operation, dimension(1, currentHeight, maximumHeight), dimension(2, currentWidth, maximumWidth));
        } break;
        case 1:
        case 2:
        case 3:
        case 5:
        case 6:
        case 7:
        case 9:
        case 10:
            host.windowOperation(operation, nInputOps > 1 ? inputOps[1] : 0, nInputOps > 2 ? inputOps[2] : 0);
            break;
        case 11:
            writeCsiResponse(host.windowInfo().iconified ? "2t" : "1t");
            break;
        case 13: {
            const auto info = host.windowInfo();
            response << StringView(u8"3;") << (u16)(info.x) << StringView(u8";") << (u16)(info.y) << StringView(u8"t");
            writeCsiResponse(StringView(response));
        } break;
        case 14:
            if (nInputOps > 1 && inputOps[1] == 2) {
                response << StringView(u8"4;") << composer.pixelHeight << StringView(u8";") << composer.pixelWidth << StringView(u8"t");
            } else {
                response << StringView(u8"4;") << composer.rows * composer.glyphHeight << StringView(u8";") << composer.columns * composer.glyphWidth << StringView(u8"t");
            }
            writeCsiResponse(StringView(response));
            break;
        case 15: {
            const auto info = host.windowInfo();
            response << StringView(u8"5;") << info.screenPixelHeight << StringView(u8";") << info.screenPixelWidth << StringView(u8"t");
            writeCsiResponse(StringView(response));
        } break;
        case 16:
            response << StringView(u8"6;") << composer.glyphHeight << StringView(u8";") << composer.glyphWidth << StringView(u8"t");
            writeCsiResponse(StringView(response));
            break;
        case 18:
            response << StringView(u8"8;") << composer.rows << StringView(u8";") << composer.columns << StringView(u8"t");
            writeCsiResponse(StringView(response));
            break;
        case 19: {
            const auto info = host.windowInfo();
            response << StringView(u8"9;") << info.screenPixelHeight / composer.glyphHeight << StringView(u8";") << info.screenPixelWidth / composer.glyphWidth << StringView(u8"t");
            writeCsiResponse(StringView(response));
        } break;
        case 20:
            writeTitleResponse('L', stringView(iconTitle));
            break;
        case 21:
            writeTitleResponse('l', stringView(windowTitle));
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
                for (auto it = titleStack.rbegin(); it != titleStack.rend() && (!saved.hasIcon || !saved.hasWindow); ++it) {
                    if (!saved.hasIcon && it->hasIcon) {
                        saved.hasIcon = true;
                        saved.icon = it->icon;
                    }
                    if (!saved.hasWindow && it->hasWindow) {
                        saved.hasWindow = true;
                        saved.window = it->window;
                    }
                }
                if ((which == 0 || which == 1) && saved.hasIcon) {
                    iconTitle = saved.icon;
                    host.osc(1, stringView(iconTitle));
                }
                if ((which == 0 || which == 2) && saved.hasWindow) {
                    windowTitle = saved.window;
                    host.osc(2, stringView(windowTitle));
                }
            }
        } break;
        default:
            if (operation >= 24) {
                host.windowOperation(8, operation, composer.columns);
            }
            break;
    }
}

template <bool traced>
void VtermImpl<traced>::csi_XTHIMOUSE() {
    if (mouseTrk.mode == MouseTrackingMode::VT200_Highlight && nInputOps == 5 && inputOps[0] != 0) {
        mouseHighlight.active = true;
        mouseHighlight.startX = std::max<u32>(1, inputOps[1]);
        mouseHighlight.startY = std::max<u32>(1, inputOps[2]);
        mouseHighlight.firstRow = std::max<u32>(1, inputOps[3]);
        mouseHighlight.lastRow = std::max<u32>(mouseHighlight.firstRow, inputOps[4]);
    } else {
        mouseHighlight.active = false;
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECELR() {
    const u32 mode = inputOps[0];
    locator.enabled = mode <= 2 ? mode : 0;
    locator.pixels = nInputOps > 1 && inputOps[1] == 1;
    locator.filter = false;
}

template <bool traced>
void VtermImpl<traced>::csi_DECSLE() {
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
}

template <bool traced>
void VtermImpl<traced>::csi_DECRQLP() {
    if (locator.enabled) {
        const u16 x = locator.pixels ? locator.pixelX : locator.column;
        const u16 y = locator.pixels ? locator.pixelY : locator.row;
        StringBuilder response;
        response << StringView(u8"1;") << (unsigned)(locator.buttons) << StringView(u8";") << y << StringView(u8";") << x << StringView(u8";0&w");
        writeCsiResponse(StringView(response));
        if (locator.enabled == 2) {
            locator.enabled = 0;
        }
    } else {
        writeCsiResponse("0&w");
    }
}

template <bool traced>
void VtermImpl<traced>::csi_DECEFR() {
    const auto value = [this](size_t k, u16 current) {
        return k < nInputOps && inputOps[k] ? (u16)(inputOps[k]) : current;
    };
    locator.filterTop = value(0, locator.pixels ? locator.pixelY : locator.row);
    locator.filterLeft = value(1, locator.pixels ? locator.pixelX : locator.column);
    locator.filterBottom = value(2, locator.filterTop);
    locator.filterRight = value(3, locator.filterLeft);
    locator.filter = locator.enabled != 0;
}

template <bool traced>
void VtermImpl<traced>::csi_XTMODKEYS() {
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
}

template <bool traced>
void VtermImpl<traced>::csi_XTQMODKEYS() {
    const u32 resource = inputOps[0];
    if (resource <= 4 || resource == 6 || resource == 7) {
        StringBuilder response;
        response << StringView(u8">") << resource << StringView(u8";") << (unsigned)(modifyKeyResources[resource]) << StringView(u8"m");
        writeCsiResponse(StringView(response));
    }
}

template <bool traced>
void VtermImpl<traced>::csi_kittyKeyboardPush() {
    constexpr size_t maxStackDepth = 16;
    auto& state = kittyKeyboardState();
    if (state.stack.size() == maxStackDepth) {
        state.stack.erase(state.stack.begin());
    }
    state.stack.push_back(state.flags);
    state.flags = inputOps[0] & 0x1f;
}

template <bool traced>
void VtermImpl<traced>::csi_kittyKeyboardPop() {
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
}

template <bool traced>
void VtermImpl<traced>::csi_kittyKeyboardSet() {
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
}

template <bool traced>
void VtermImpl<traced>::csi_kittyKeyboardQuery() {
    StringBuilder response;
    response << StringView(u8"?") << (unsigned)(getKittyKeyboardFlags()) << StringView(u8"u");
    writeCsiResponse(StringView(response));
}

namespace {
    using Key = VtKey;
    using InputSpec = VtermInputSpec;

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
    * These tables are referenced by VtermImpl<traced>::charCodes (see below).
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
        0x0020,

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

template <bool traced>
const u16* VtermImpl<traced>::charCodes[] = {

    nullptr,
    uc_DecSpec,
    uc_DecSuppl,
    uc_DecSuppl,
    uc_DecTechn,
    uc_IsoLatin1,
    uc_IsoUK
};

template <bool traced>
u32 VtermImpl<traced>::translateCharset(Charset charset, unsigned char ch) const {
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

template <bool traced>
CallVtermResize<traced>::CallVtermResize(VtermImpl<traced>* parent_)
    : parent(parent_)
{
}

template <bool traced>
void CallVtermResize<traced>::onListen(void*) {
    parent->resizeGrid();
    parent->redraw();
}

template <bool traced>
CallVtermFontChanged<traced>::CallVtermFontChanged(VtermImpl<traced>* parent_)
    : parent(parent_)
{
}

template <bool traced>
void CallVtermFontChanged<traced>::onListen(void*) {
    parent->fontChanged();
}

template <bool traced>
VtermImpl<traced>::VtermImpl(Composer& composer_, VtermHost& host_, VtermTrace* trace, Output* dump_)
    : input(this)
    , composer(composer_)
    , host(host_)
    , dump(dump_)
    , parserTrace(trace)
    , unicodeProperties(UnicodeMap<u8>::create(*composer.pool))
    , nColsEff(composer.columns)
    , hMargin(0)
{
    try {
        createFreshScreen(frame_pri, framePriPool, opts.saveLines);
        createInactiveScreen(frame_alt, frameAltPool);
    } catch (...) {
        delete framePriPool;
        delete frameAltPool;
        throw;
    }
    cf = frame_pri;
    outputSpans.grow((size_t)(composer.rows) * ((composer.columns + 1u) / 2u));
    makePalette256(colors.palette);
    std::copy(std::begin(colors.palette), std::end(colors.palette), std::begin(originalPalette256));
    colors.defaultForeground = opts.fg;
    colors.defaultBackground = opts.bg;
    std::fill(std::begin(colors.special), std::end(colors.special), opts.fg);
    std::fill(std::begin(colors.originalSpecial), std::end(colors.originalSpecial), opts.fg);
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
    composer.resizedListeners.pushBack(composer.pool->make<CallVtermResize<traced>>(this));
    composer.fontChangedListeners.pushBack(composer.pool->make<CallVtermFontChanged<traced>>(this));
    composer.inputSinks.pushBack(this);
}

template <bool traced>
void VtermImpl<traced>::fontChanged() {
    const u32 width = 2u * opts.border + (u32)(composer.columns) * composer.glyphWidth;
    const u32 height = 2u * opts.border + (u32)(composer.rows) * composer.glyphHeight;
    if (composer.pixelWidth == width && composer.pixelHeight == height) {
        redraw();
    }
}

template <bool traced>
void VtermImpl<traced>::resizeGrid() {
    const u16 previousColumns = cf->columns();
    const u16 previousRows = cf->rows();
    if (previousColumns == composer.columns && previousRows == composer.rows) {
        if (inBandResizeMode) {
            reportInBandResize();
        }
        return;
    }

    hideCursor();
    resetGraphemeInput();

    const bool reflow = cf == frame_pri && previousColumns != composer.columns;
    Screen::Cursor cursorState{Point(posX, posY), lastCol};
    if (cf == frame_pri) {
        resizeScreen(frame_pri, framePriPool, reflow, &cursorState);
        cf = frame_pri;
    } else {
        resizeScreen(frame_alt, frameAltPool, false, &cursorState);
        cf = frame_alt;
    }
    posX = cursorState.position.x;
    posY = cursorState.position.y;
    lastCol = cursorState.pendingWrap;

    // The rebuild resets the vertical scrolling region.  Reset the
    // horizontal region to the resized page as well; retaining a clipped
    // right edge made subsequent growth keep a stale narrow region.
    marginTop = 0;
    marginBottom = composer.rows;
    nColsEff = composer.columns;
    hMargin = 0;
    if (!reflow) {
        const bool pendingWrap = lastCol;
        normalizeCursorPos();
        lastCol = pendingWrap;
    }
    showCursor();

    outputSpans.grow((size_t)(composer.rows) * ((composer.columns + 1u) / 2u));
    updateExtraCellCount();
    if (inBandResizeMode) {
        reportInBandResize();
    }
}

template <bool traced>
std::string VtermImpl<traced>::getLocalEcho(const u8* const begin, const u8* const end) {
    StringBuilder output((end - begin) * 2);
    for (const u8* p = begin; p < end; ++p) {
        if (*p == '\r' || *p >= ' ') {
            output.append(p, 1);
        } else {
            const u8 bytes[] = {u8'^', (u8)(*p + 0x40)};
            output.append(bytes, sizeof(bytes));
        }
    }
    return std::string((const char*)(output.data()), output.used());
}

template <bool traced>
int VtermImpl<traced>::writePty(VtKey key, VtModifier modifiers_, bool userInput) {
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

template <bool traced>
int VtermImpl<traced>::writePty(u8 ch, VtModifier modifiers, bool userInput) {
    using VM = VtModifier;

    auto uch = &ch;

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

template <bool traced>
int VtermImpl<traced>::writeKittyKey(VtKey key, u16 modifiers, VtermKeyEventType event) {
    const KittyKeySpec spec = kittyKeySpec(key);
    if (!spec.code) {
        return 0;
    }

    if (isKittyRecoveryKey(key) && !(getKittyKeyboardFlags() & 0x08)) {
        if (event == VtermKeyEventType::Release) {
            return 0;
        }
        return writePty(key, kittyToLegacyModifiers(modifiers), true);
    }

    if (isKittyModifierKey(key) && !(getKittyKeyboardFlags() & 0x08)) {
        return 0;
    }

    if (event == VtermKeyEventType::Release && !(getKittyKeyboardFlags() & 0x02)) {
        return 0;
    }

    StringBuilder sequence;
    sequence << StringView(u8"\x1b[") << spec.code << StringView(u8";") << modifiers + 1;
    if (getKittyKeyboardFlags() & 0x02) {
        sequence << StringView(u8":") << (unsigned)(event);
    }
    if ((getKittyKeyboardFlags() & 0x10) && event != VtermKeyEventType::Release && isKittyRecoveryKey(key) && validKittyAssociatedText(spec.code)) {
        sequence << StringView(u8";") << spec.code;
    }
    sequence.append(&spec.final, 1);
    return writePty((const u8*)(sequence.data()), sequence.used(), true);
}

template <bool traced>
int VtermImpl<traced>::writeKittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, VtermKeyEventType event) {
    if (!key || (event == VtermKeyEventType::Release && !(getKittyKeyboardFlags() & 0x02))) {
        return 0;
    }

    StringBuilder sequence;
    sequence << StringView(u8"\x1b[") << key;
    if (getKittyKeyboardFlags() & 0x04) {
        const u32 alternateShifted = shiftedKey != key ? shiftedKey : 0;
        const u32 alternateBase = baseLayoutKey != key ? baseLayoutKey : 0;
        if (alternateShifted) {
            sequence << StringView(u8":") << alternateShifted;
            if (alternateBase) {
                sequence << StringView(u8":") << alternateBase;
            }
        } else if (alternateBase) {
            sequence << StringView(u8"::") << alternateBase;
        }
    }
    sequence << StringView(u8";") << modifiers + 1;
    if (getKittyKeyboardFlags() & 0x02) {
        sequence << StringView(u8":") << (unsigned)(event);
    }
    if ((getKittyKeyboardFlags() & 0x10) && event != VtermKeyEventType::Release) {
        const u32 text = (modifiers & 1) && shiftedKey ? shiftedKey : key;
        if (validKittyAssociatedText(text)) {
            sequence << StringView(u8";") << text;
        }
    }
    sequence << StringView(u8"u");
    return writePty((const u8*)(sequence.data()), sequence.used(), true);
}

template <bool traced>
int VtermImpl<traced>::writePty(const char* cstr, bool userInput) {
    return writePty(cstr, strlen(cstr), userInput);
}

template <bool traced>
int VtermImpl<traced>::writePty(const char* data, size_t size, bool userInput) {
    return writePty((const u8*)(data), size, userInput);
}

template <bool traced>
void VtermImpl<traced>::writeCsiResponse(StringView payload) {
    const StringView prefix = send8BitControls ? StringView(u8"\x9b") : StringView(u8"\x1b[");
    writeProtocolResponse(prefix, payload);
}

template <bool traced>
void VtermImpl<traced>::writeDcsResponse(StringView payload) {
    const StringView prefix = send8BitControls ? StringView(u8"\x90") : StringView(u8"\x1bP");
    const StringView suffix = send8BitControls ? StringView(u8"\x9c") : StringView(u8"\x1b\\");
    writeProtocolResponse(prefix, payload, suffix);
}

template <bool traced>
void VtermImpl<traced>::writeOscResponse(StringView payload) {
    const StringView prefix = send8BitControls ? StringView(u8"\x9d") : StringView(u8"\x1b]");
    const StringView suffix = send8BitControls ? StringView(u8"\x9c") : StringView(u8"\x1b\\");
    writeProtocolResponse(prefix, payload, suffix);
}

template <bool traced>
void VtermImpl<traced>::writeProtocolResponse(StringView prefix, StringView payload, StringView suffix) {
    StringBuilder response(static_cast<Buffer&&>(protocolResponseScratch));
    response.reset();
    response << prefix << payload << suffix;
    writePty((const u8*)(response.data()), response.used(), false);
    protocolResponseScratch = static_cast<Buffer&&>(response);
}

template <bool traced>
void VtermImpl<traced>::compactPtyOutput() {
    const size_t pending = ptyOutput.used() - ptyOutputOffset;
    if (pending != 0) {
        memmove(ptyOutput.mutData(), (const u8*)(ptyOutput.data()) + ptyOutputOffset, pending);
    }
    ptyOutput.seekAbsolute(pending);
    ptyOutputOffset = 0;
}

template <bool traced>
int VtermImpl<traced>::writePty(const u8* ucstr, size_t len, bool userInput) {
    if (userInput && keyboardLocked) {
        return len;
    }

    if (userInput && cf->pageToBottom()) {
        redraw();
    }

    if (userInput && localEcho) {
        const std::string localEcho = getLocalEcho(ucstr, ucstr + len);
        processInput((const u8*)localEcho.data(), (int)localEcho.size());
    }
    if (ptyOutputOffset == ptyOutput.used()) {
        ptyOutput.reset();
        ptyOutputOffset = 0;
    }
    const size_t pending = ptyOutput.used() - ptyOutputOffset;
    if (!userInput && len != 0 && (pending > ptyProtocolHighWater || len > ptyProtocolHighWater - pending)) {
        ++droppedPtyResponses;
        return 0;
    }
    if (!userInput && ptyOutputOffset != 0 && ptyOutput.used() + len > ptyProtocolHighWater) {
        compactPtyOutput();
    } else if (userInput && ptyOutputOffset != 0 && ptyOutput.used() + len > ptyOutput.capacity()) {
        compactPtyOutput();
    }
    ptyOutput.append(ucstr, len);
    return len;
}

using Key = VtKey;
using Mod = VtModifier;

template <bool traced>
VtermImpl<traced>::InputSpecTable* VtermImpl<traced>::getInputSpecTable() {
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

template <bool traced>
void VtermImpl<traced>::resetInputSpecTable() {
    for (InputSpecTable* e = getInputSpecTable(); e->specs != nullptr; ++e) {
        e->visited = false;
    }
}

template <bool traced>
const VtermImpl<traced>::InputSpec* VtermImpl<traced>::selectInputSpecs() {
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

template <bool traced>
const VtermImpl<traced>::InputSpec& VtermImpl<traced>::getInputSpec(Key key) {
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

template <bool traced>
VtermImpl<traced>::PresentationState VtermImpl<traced>::capturePresentationState() const {
    return {
        cf,
        cf->getCursor(),
        cf->getSelectionForView(),
        cf->columns(),
        cf->rows(),
        cf->getViewOffset(),
        cf->getScreenReverseVideo(),
        cf->getBlinkVisible(),
        cf->getCursorBlink(),
        cf->getSelectionForeground(),
        cf->getSelectionBackground(),
        cf->getSelectionColorMask(),
    };
}

template <bool traced>
bool VtermImpl<traced>::presentationChanged(const PresentationState& before) const {
    if (before.frame != cf || cf->hasDamage() || before.columns != cf->columns() || before.rows != cf->rows() || before.viewOffset != cf->getViewOffset() || before.screenReverse != cf->getScreenReverseVideo() || before.blinkVisible != cf->getBlinkVisible() || before.cursorBlink != cf->getCursorBlink() || !(before.selectionForeground == cf->getSelectionForeground()) || !(before.selectionBackground == cf->getSelectionBackground()) || before.selectionColorMask != cf->getSelectionColorMask()) {
        return true;
    }
    const auto cursor = cf->getCursor();
    if (before.cursor.posX != cursor.posX || before.cursor.posY != cursor.posY || before.cursor.style != cursor.style || !(before.cursor.color == cursor.color)) {
        return true;
    }
    const Rect selection = cf->getSelectionForView();
    return !(before.selection.tl == selection.tl) || !(before.selection.br == selection.br) || before.selection.rectangular != selection.rectangular;
}

template <bool traced>
void VtermImpl<traced>::syncPresentationCursor() {
    cf->setCursorPos(posY, posX);
    using CS = TerminalCursor::Style;
    cf->setCursorStyle(showCursorMode ? (hasFocus ? cursorShape : CS::hollow_block) : CS::hidden);
}

template <bool traced>
void VtermImpl<traced>::beginCsi() {
    stringUtf8Remaining = 0;
    ragelStringLimit = 0;
    resetGraphemeInput();
    inputOps[0] = 0;
    inputSeparators[0] = 0;
    inputPresent[0] = false;
    nInputOps = 1;
    csiHadParams = false;
    csiPrefix = 0;
    csiIntermediateCount = 0;
}

template <bool traced>
void VtermImpl<traced>::traceCsi(u8 finalByte) {
    if constexpr (traced) {
        parserTrace->csi(finalByte, StringView(&csiPrefix, csiPrefix == 0 ? 0 : 1), StringView(csiIntermediates, csiIntermediateCount), inputOps, inputSeparators, nInputOps, csiHadParams);
    }
}

template <bool traced>
bool VtermImpl<traced>::executeC0InSequence(unsigned char ch, bool stringData) {
    if (ch >= 0x20 || ch == '\x18' || ch == '\x1a' || ch == '\x1b') {
        return false;
    }

    if constexpr (traced) {
        if (!stringData) {
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

namespace {
    [[gnu::always_inline]] size_t printableAsciiPrefix(const u8* input, size_t size) {
        using Bytes = u8 __attribute__((vector_size(16)));
#if !defined(__SSE2__)
        using Bits = unsigned __int128;
#endif
        constexpr Bytes spaces = {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
        constexpr Bytes deletes = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f};
        size_t offset = 0;
        while (size - offset >= sizeof(Bytes)) {
            Bytes word;
            memcpy(&word, input + offset, sizeof(word));
            const Bytes invalidBytes = (word < spaces) | (word >= deletes);
#if defined(__SSE2__)
            const u32 invalid = _mm_movemask_epi8(__builtin_bit_cast(__m128i, invalidBytes));
            if (invalid != 0) {
                return offset + __builtin_ctz(invalid);
            }
#else
            const Bits invalid = __builtin_bit_cast(Bits, invalidBytes);
            const u64 low = invalid;
            if (low != 0) {
                return offset + __builtin_ctzll(low) / 8;
            }
            const u64 high = invalid >> 64;
            if (high != 0) {
                return offset + 8 + __builtin_ctzll(high) / 8;
            }
#endif
            offset += sizeof(word);
        }
        while (offset < size && input[offset] >= 0x20 && input[offset] < 0x7f) {
            ++offset;
        }
        return offset;
    }
}

template <bool traced>
size_t VtermImpl<traced>::placeAsciiLines(const u8* input, size_t size) {
    if (insertMode || posX != 0 || lastCol || inputGraphemeScreen != nullptr || horizMarginMode || hMargin != 0 || nColsEff != composer.columns || marginTop != 0 || marginBottom != composer.rows || autoPrintMode) {
        return 0;
    }

    const u8* cursor = input;
    const u8* const end = input + size;
    constexpr u16 maxLines = 256;
    u16 lengths[maxLines];
    u16 lineCount = 0;
    u32 preceding = 0;
    bool havePreceding = false;
    while (cursor != end && lineCount != maxLines) {
        const size_t length = printableAsciiPrefix(cursor, end - cursor);
        if (length > nColsEff || (size_t)(end - cursor) - length < 2 || cursor[length] != '\r' || cursor[length + 1] != '\n') {
            break;
        }
        const u32 row = (u32)(posY) + lineCount;
        if (row < composer.rows && cf->lineAttribute(row) != 0) {
            break;
        }
        if (length != 0) {
            preceding = cursor[length - 1];
            havePreceding = true;
        }
        lengths[lineCount] = (u16)(length);
        ++lineCount;
        cursor += length + 2;
    }
    if (lineCount < 2) {
        return 0;
    }

    if constexpr (traced) {
        const u8* tracedInput = input;
        for (u16 line = 0; line < lineCount; ++line) {
            const u16 length = lengths[line];
            if (length != 0) {
                parserTrace->text(tracedInput, length);
            }
            parserTrace->control('\r');
            parserTrace->control('\n');
            tracedInput += length + 2;
        }
    }
    cf->writeAsciiLines(posY, input, lengths, lineCount, attrs, activeHyperlink, currentSemantic, eraseAttrs);
    if (havePreceding) {
        utf8dec.setUnicode(preceding);
    }
    posY = min<u32>((u32)(posY) + lineCount, composer.rows - 1);
    posX = 0;
    lastCol = false;
    if (attrs.blink) {
        haveBlinkingText = true;
    }
    return cursor - input;
}

template <bool traced>
bool VtermImpl<traced>::processInput(const u8* input, int inputSize, bool refresh) {
    ++processInputDepth;
    bool changed;
    try {
        changed = processInputImpl(input, inputSize, refresh);
    } catch (...) {
        --processInputDepth;
        throw;
    }
    --processInputDepth;
    if (processInputDepth == 0 && refresh) {
        collectCellExtrasIfNeeded();
    }
    return changed;
}

template <bool traced>
[[gnu::noinline]] bool VtermImpl<traced>::processInputImpl(const u8* input, int inputSize, bool refresh) {
    const PresentationState presentationBefore = capturePresentationState();
    hideCursor();
    parseWithRagel(input, inputSize);
    syncPresentationCursor();
    const bool changed = presentationChanged(presentationBefore);
    if (refresh && changed) {
        redraw();
    }
    return changed;
}

template <bool traced>
void VtermImpl<traced>::parseWithRagel(const u8* data, size_t len) {
    const u8* p = data;
    const u8* const pe = data + len;
    const u8* const eof = nullptr;
    int& cs = ragelState;
    const bool printerHandled = host.handlesPrinter();
    const auto appendPrinter = [&](const void* bytes, size_t size) {
        if (printerHandled && size != 0) {
            host.print(StringView((const u8*)(bytes), size));
        }
    };

#include "vterm_parser.rl.h"
}

template <bool traced>
bool VtermImpl<traced>::ragelGroundContinuation(u8 ch) {
    if (!utf8dec.expectsContinuation() || ch < 0x80) {
        return false;
    }
    if constexpr (traced) {
        parserTrace->text(&ch, 1);
    }
    if (charsetState.g[charsetState.gr] == Charset::UTF8) {
        for (int completed = utf8dec.pushByte(ch); completed > 0; --completed) {
            placeGraphicChar();
        }
    } else {
        inputGraphicChar(ch);
    }
    return true;
}

template <bool traced>
void VtermImpl<traced>::ragelGroundHigh(u8 ch) {
    if (ragelGroundContinuation(ch)) {
        return;
    }
    if constexpr (traced) {
        if (ch >= 0xa0) {
            parserTrace->text(&ch, 1);
        } else {
            parserTrace->control(ch);
        }
    }
    if (ch <= 0x9f) {
        resetGraphemeInput();
    }
    if (charsetState.g[charsetState.gr] == Charset::UTF8) {
        for (int completed = utf8dec.pushByte(ch); completed > 0; --completed) {
            placeGraphicChar();
        }
    } else {
        inputGraphicChar(ch);
    }
}

template <bool traced>
void VtermImpl<traced>::ragelGroundAscii(u8 ch) {
    if constexpr (traced) {
        parserTrace->text(&ch, 1);
    }
    inputGraphicChar(ch);
}

template <bool traced>
void VtermImpl<traced>::ragelBeginString(VtermTraceString type, bool buffered) {
    resetGraphemeInput();
    stringUtf8Remaining = 0;
    ragelStringLimit = type == VtermTraceString::Dcs ? maxDcsBytes : type == VtermTraceString::Osc ? maxOscBytes : 0;
    oscCwdDecode = false;
    if (buffered) {
        argBuf.reset();
        argBufOverflowed = false;
    }
    if constexpr (traced) {
        parserTrace->stringBegin(type);
    }
}

template <bool traced>
void VtermImpl<traced>::ragelBeginDcs() {
    ragelBeginString(VtermTraceString::Dcs, true);
    inputOps[0] = 0;
    inputSeparators[0] = 0;
    inputPresent[0] = false;
    nInputOps = 1;
    dcsIntermediateCount = 0;
    dcsCapabilityOffset = 0;
    dcsCapabilityDecodedLength = 0;
    dcsCapabilityCandidates = 0;
    dcsCapabilityHasHighNibble = false;
    dcsCapabilityValid = false;
    dcsCapabilityComplete = false;
    dcsUdkDefinitions.clear();
    dcsDecoded.reset();
    dcsUdkValueOffset = 0;
    dcsUdkCode = 0;
    dcsUdkKey = VtKey::NONE;
    dcsUdkHasCode = false;
    dcsUdkHasHighNibble = false;
    dcsUdkValid = false;
    dcsUdkInValue = false;
}

template <bool traced>
void VtermImpl<traced>::ragelBeginOsc() {
    ragelBeginString(VtermTraceString::Osc, true);
    oscCommand = 0;
    oscPayloadOffset = 0;
    oscCommandValid = false;
    oscTerminated = false;
    oscDecoded.reset();
    oscTitleHighNibble = 0;
    oscTitleHex = false;
    oscTitleHasHighNibble = false;
    oscTitleValid = false;
    oscTitleStopped = false;
    oscCwdPercentHigh = 0;
    oscCwdValid = false;
    oscCwdDecode = false;
    oscHyperlinkIdOffset = 0;
    oscHyperlinkIdLength = 0;
    oscHyperlinkUriOffset = 0;
    oscHyperlinkHasId = false;
    oscProgressState = 0;
    oscProgressPercent = 0;
    oscProgressStatePresent = false;
    oscProgressPercentPresent = false;
    oscProgressValid = false;
    oscBase64.reset();
    osc52ReplySelector = 0;
    osc52Primary = false;
    osc52Clipboard = false;
    osc52SelectorSeen = false;
    osc52PayloadSeen = false;
    osc52Query = false;
}

template <bool traced>
void VtermImpl<traced>::resetOscColor() {
    oscColor = {};
    oscColorComponents[0] = 0.0;
    oscColorComponents[1] = 0.0;
    oscColorComponents[2] = 0.0;
    oscColorHex = 0;
    oscColorComponent = 0;
    oscColorDigits = 0;
    oscColorValid = true;
    oscColorQuery = false;
}

template <bool traced>
bool VtermImpl<traced>::ragelStringContinuation(u8 ch) {
    if (!stringUtf8Continuation(ch)) {
        return false;
    }
    if (ragelStringLimit != 0) {
        ragelAppendString(ch, ragelStringLimit);
    } else if constexpr (traced) {
        parserTrace->stringData(&ch, 1);
    }
    if (oscCwdDecode && !argBufOverflowed) {
        oscDecoded.append(&ch, 1);
    }
    return true;
}

template <bool traced>
void VtermImpl<traced>::ragelAppendString(u8 ch, size_t limit) {
    if constexpr (traced) {
        parserTrace->stringData(&ch, 1);
    }
    if (argBuf.used() < limit) {
        argBuf.append(&ch, 1);
    } else {
        argBufOverflowed = true;
    }
}

template <bool traced>
void VtermImpl<traced>::ragelAppendEscapedString(u8 ch, size_t limit) {
    if constexpr (traced) {
        const u8 bytes[] = {'\x1b', ch};
        parserTrace->stringData(bytes, sizeof(bytes));
    }
    if (argBuf.used() <= limit - 2) {
        const u8 bytes[] = {'\x1b', ch};
        argBuf.append(bytes, sizeof(bytes));
    } else {
        argBufOverflowed = true;
    }
}

template <bool traced>
void VtermImpl<traced>::ragelFinishDcs() {
    stringUtf8Remaining = 0;
    ragelStringLimit = 0;
    if constexpr (traced) {
        parserTrace->stringEnd();
    }
}

template <bool traced>
void VtermImpl<traced>::ragelFinishOsc() {
    stringUtf8Remaining = 0;
    ragelStringLimit = 0;
    if constexpr (traced) {
        parserTrace->stringEnd();
    }
}

template <bool traced>
StringView VtermImpl<traced>::ragelOscPayload() const noexcept {
    const auto* data = (const u8*)(argBuf.data());
    return StringView(data + oscPayloadOffset, argBuf.used() - oscPayloadOffset);
}

template <bool traced>
std::string VtermImpl<traced>::getHyperlink(int pX, int pY) const {
    const StringView link = resolveHyperlink(pX, pY).payload;
    return link.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(link.data()), link.length());
}

template <bool traced>
Point VtermImpl<traced>::selectionPoint(int pX, int pY) const {
    const int contentWidth = std::max(0, (int)composer.pixelWidth - 2 * opts.border);
    const int contentHeight = std::max(1, (int)composer.pixelHeight - 2 * opts.border);
    pX = std::min(std::max(0, pX - opts.border), contentWidth);
    pY = std::min(std::max(0, pY - opts.border), contentHeight - 1);
    return cf->getLogicalPoint(Point(std::min(pX / composer.glyphWidth, (int)composer.columns), std::min(pY / composer.glyphHeight, (int)composer.rows - 1)));
}

template <bool traced>
void VtermImpl<traced>::selectStart(int pX, int pY, bool cycleSnapTo) {
    if (cycleSnapTo) {
        selectExtend(pX, pY, true);
        return;
    }

    Point pt = selectionPoint(pX, pY);

    Rect& selection = cf->getSelection();
    cf->setSelectSnapTo(Screen::SelectSnapTo::Char);
    selection.tl = pt;
    selection.br = pt;
    selectUpdatesTop = false;
    selectUpdatesLeft = false;

    hideCursor();
    redraw();
}

template <bool traced>
void VtermImpl<traced>::selectExtend(int pX, int pY, bool cycleSnapTo) {
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

template <bool traced>
void VtermImpl<traced>::selectUpdate(int pX, int pY) {
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

template <bool traced>
bool VtermImpl<traced>::selectFinish(std::string& utf8_selection) {
    showCursor();
    redraw();

    return cf->getSelectedUtf8(utf8_selection);
}

template <bool traced>
void VtermImpl<traced>::selectClear() {
    cf->getSelection().clear();
    redraw();
}

template <bool traced>
void VtermImpl<traced>::selectRectangularModeToggle() {
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

template <bool traced>
void VtermImpl<traced>::pasteSelection(const std::string& utf8_selection) {
    StringBuilder output(utf8_selection.size() + 12);

    if (bracketedPasteMode) {
        output << StringView(u8"\x1b[200~");
    }

    for (const auto ch : utf8_selection) {
        const char outputByte = ch == '\n' ? '\r' : ch;
        output.append(&outputByte, 1);
    }

    if (bracketedPasteMode) {
        output << StringView(u8"\x1b[201~");
    }

    if (!output.empty()) {
        writePty((const u8*)(output.data()), output.used(), true);
    }
}

Vterm* Vterm::create(Composer& composer, VtermHost& host, VtermTrace* trace) {
    Output* dump = nullptr;
    if (opts.dump != nullptr) {
        const int rawFd = ::open(opts.dump, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (rawFd < 0) {
            Errno().raise(StringBuilder() << StringView(u8"can not open dump file ") << StringView(opts.dump));
        }
        auto* fd = composer.pool->make<ScopedFD>(rawFd);
        dump = createOutBuf(composer.pool, *createFDRegular(composer.pool, *fd));
    }

    CellExtraStore::create(composer, (size_t)(composer.columns) * (composer.rows + opts.saveLines));
    try {
        if (trace != nullptr) {
            return composer.pool->make<VtermImpl<true>>(composer, host, trace, dump);
        }
        return composer.pool->make<VtermImpl<false>>(composer, host, trace, dump);
    } catch (...) {
        composer.setCellExtras(nullptr);
        throw;
    }
}
