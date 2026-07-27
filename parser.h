/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "base64.h"
#include "color.h"
#include "terminal_types.h"
#include "vterm.h"

#include <std/lib/buffer.h>
#include <std/str/view.h>
#include <std/sys/types.h>

#include <cstddef>

namespace stl {
    struct ObjPool;
}

struct VtermTrace;

enum class CompatibilityLevel : u8 {
    VT52,
    VT100,
    VT200,
    VT300,
    VT400,
    VT500,
};

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
    NrcTurkish,
};

struct ParserParameters {
    const u32* values;
    const unsigned char* separators;
    const bool* present;
    size_t count;
    bool hadAny;
};

struct ParserUdkDefinition {
    size_t valueOffset;
    size_t valueLength;
    VtKey key;
};

struct ParserIface {
    virtual void parserResetGraphemeInput() = 0;
    virtual bool parserExecuteC0(u8 byte) = 0;
    virtual void parserBell() = 0;
    virtual bool parserAutoNewlineMode() const = 0;
    virtual bool parserPrinterControllerMode() const = 0;
    virtual void parserSetPrinterControllerMode(bool enabled) = 0;
    virtual CompatibilityLevel parserCompatibilityLevel() const = 0;
    virtual void parserSetCompatibilityLevel(CompatibilityLevel level) = 0;
    virtual void parserSet8BitControls(bool enabled) = 0;
    virtual void parserSetApplicationKeypad(bool enabled) = 0;
    virtual void parserMoveCursorBackward(u32 count) = 0;
    virtual bool parserHexTitleInput() const = 0;
    virtual Base64Decoder parserNotificationDecoder(stl::StringView id, bool body) const = 0;
    virtual void parserSingleShift(u8 index) = 0;
    virtual void parserLockingShiftGl(u8 index) = 0;
    virtual void parserLockingShiftGr(u8 index) = 0;
    virtual void parserResetCharsets(bool isoLatin1) = 0;
    virtual void parserDesignateCharset(u8 index, Charset charset) = 0;
    virtual bool parserHighlightMouseTracking() const = 0;
    virtual bool parserHandlesPrinter() const = 0;
    virtual void parserPrint(stl::StringView bytes) = 0;
    virtual void parserWritePty(stl::StringView bytes) = 0;
    virtual bool parserGroundContinuation(u8 byte) = 0;
    virtual void parserGroundHigh(u8 byte) = 0;
    virtual void parserGroundAscii(u8 byte) = 0;
    virtual bool parserAsciiBulkEligible() const = 0;
    virtual bool parserUtf8BulkEligible() const = 0;
    virtual size_t parserPlaceAsciiLines(stl::StringView bytes) = 0;
    virtual void parserPlaceAsciiRun(stl::StringView bytes) = 0;
    virtual size_t parserPlaceUtf8Run(stl::StringView bytes) = 0;

    virtual void unhandledInput(unsigned char byte) = 0;
    virtual void inp_CR() = 0;
    virtual void inp_HT() = 0;
    virtual bool esc_IND() = 0;
    virtual void esc_RI() = 0;
    virtual void esc_NEL() = 0;
    virtual void esc_BI() = 0;
    virtual void esc_FI() = 0;
    virtual void esc_HTS() = 0;
    virtual void esc_SPA() = 0;
    virtual void esc_EPA() = 0;
    virtual void esc_DECSC() = 0;
    virtual void esc_DECRC() = 0;
    virtual void esc_RIS() = 0;
    virtual void csi_DECSTR() = 0;
    virtual void csi_SCOSC_SLRM(const ParserParameters& parameters) = 0;
    virtual void csi_SCORC() = 0;

    virtual void csi_CUU(const ParserParameters& parameters) = 0;
    virtual void csi_CUD(const ParserParameters& parameters) = 0;
    virtual void csi_CUF(const ParserParameters& parameters) = 0;
    virtual void csi_CUB(const ParserParameters& parameters) = 0;
    virtual void csi_CNL(const ParserParameters& parameters) = 0;
    virtual void csi_CPL(const ParserParameters& parameters) = 0;
    virtual void csi_CHA(const ParserParameters& parameters) = 0;
    virtual void csi_HPA(const ParserParameters& parameters) = 0;
    virtual void csi_HPR(const ParserParameters& parameters) = 0;
    virtual void csi_VPA(const ParserParameters& parameters) = 0;
    virtual void csi_VPR(const ParserParameters& parameters) = 0;
    virtual void csi_CUP(const ParserParameters& parameters) = 0;
    virtual void csi_SU(const ParserParameters& parameters) = 0;
    virtual void csi_SD(u32 count) = 0;
    virtual void csi_CHT(const ParserParameters& parameters) = 0;
    virtual void csi_CBT(const ParserParameters& parameters) = 0;
    virtual void csi_REP(const ParserParameters& parameters) = 0;
    virtual void csi_ICH(u32 count) = 0;

    virtual void csi_ED(const ParserParameters& parameters) = 0;
    virtual void csi_EL(const ParserParameters& parameters) = 0;
    virtual void csi_DECSED(const ParserParameters& parameters) = 0;
    virtual void csi_DECSEL(const ParserParameters& parameters) = 0;
    virtual void csi_DECSCA(const ParserParameters& parameters) = 0;
    virtual void csi_DECFRA(const ParserParameters& parameters) = 0;
    virtual void csi_DECCRA(const ParserParameters& parameters) = 0;
    virtual void csi_DECERA(const ParserParameters& parameters, bool selective) = 0;
    virtual void csi_DECCARA(const ParserParameters& parameters, bool reverse) = 0;
    virtual void csi_DECRQCRA(const ParserParameters& parameters) = 0;
    virtual void csi_IL(const ParserParameters& parameters) = 0;
    virtual void csi_DL(const ParserParameters& parameters) = 0;
    virtual void csi_DCH(const ParserParameters& parameters) = 0;
    virtual void csi_ECH(const ParserParameters& parameters) = 0;
    virtual void csi_DECIC(const ParserParameters& parameters) = 0;
    virtual void csi_DECDC(const ParserParameters& parameters) = 0;
    virtual void csi_STBM(const ParserParameters& parameters) = 0;
    virtual void csi_TBC(const ParserParameters& parameters) = 0;
    virtual void csi_SM(const ParserParameters& parameters) = 0;
    virtual void csi_RM(const ParserParameters& parameters) = 0;
    virtual void csi_privSM(const ParserParameters& parameters) = 0;
    virtual void csi_privRM(const ParserParameters& parameters) = 0;
    virtual void csi_privSave(const ParserParameters& parameters) = 0;
    virtual void csi_privRestore(const ParserParameters& parameters) = 0;
    virtual void csi_ecma48_SL(const ParserParameters& parameters) = 0;
    virtual void csi_ecma48_SR(const ParserParameters& parameters) = 0;
    virtual void csi_DECSCUSR(const ParserParameters& parameters) = 0;

    virtual void csi_priDA() = 0;
    virtual void csi_secDA() = 0;
    virtual void csi_terDA() = 0;
    virtual void csi_DSR(const ParserParameters& parameters, bool privateMode) = 0;
    virtual void csi_SGR(const ParserParameters& parameters) = 0;
    virtual void esch_DECALN() = 0;
    virtual void setLineAttribute(u8 attribute) = 0;

    virtual void osc_TITLE_0(stl::StringView payload) = 0;
    virtual void osc_TITLE_1(stl::StringView payload) = 0;
    virtual void osc_TITLE_2(stl::StringView payload) = 0;
    virtual void osc_PALETTE(u32 index, Color color, bool query) = 0;
    virtual void osc_SPECIAL_COLOR(u32 index, Color color, bool query) = 0;
    virtual void osc_SPECIAL_COLOR_MODE(u32 index, u32 mode) = 0;
    virtual void osc_CWD(stl::StringView uri, stl::StringView path, bool valid) = 0;
    virtual void osc_HYPERLINK(stl::StringView id, bool hasId, stl::StringView uri) = 0;
    virtual void osc_NOTIFY(stl::StringView payload) = 0;
    virtual void osc_PROGRESS(u32 state, u32 percent) = 0;
    virtual void osc_DEFAULT_FOREGROUND(Color color, bool query) = 0;
    virtual void osc_DEFAULT_BACKGROUND(Color color, bool query) = 0;
    virtual void osc_CURSOR_COLOR(Color color, bool query) = 0;
    virtual void osc_SELECTION_BACKGROUND(Color color, bool query) = 0;
    virtual void osc_SELECTION_FOREGROUND(Color color, bool query) = 0;
    virtual void osc_CLIPBOARD_QUERY(stl::StringView selectors, bool primary, bool clipboard, u8 replySelector, bool valid) = 0;
    virtual void osc_CLIPBOARD_WRITE(stl::StringView selectors, stl::StringView content, bool primary, bool clipboard, bool valid) = 0;
    virtual void osc_CLIPBOARD_MALFORMED(stl::StringView selectors) = 0;
    virtual void osc_NOTIFICATION_CAPABILITIES(stl::StringView payload) = 0;
    virtual void osc_NOTIFICATION_CLOSE(stl::StringView id) = 0;
    virtual void osc_NOTIFICATION_TITLE(stl::StringView id, stl::StringView content, const Base64Decoder& decoder, bool encoded, bool final) = 0;
    virtual void osc_NOTIFICATION_BODY(stl::StringView id, stl::StringView content, const Base64Decoder& decoder, bool encoded, bool final) = 0;
    virtual void osc_RESET_PALETTE() = 0;
    virtual void osc_RESET_PALETTE(u32 index) = 0;
    virtual void osc_RESET_SPECIAL_COLOR() = 0;
    virtual void osc_RESET_SPECIAL_COLOR(u32 index) = 0;
    virtual void osc_RESET_DEFAULT_FOREGROUND() = 0;
    virtual void osc_RESET_DEFAULT_BACKGROUND() = 0;
    virtual void osc_RESET_CURSOR_COLOR() = 0;
    virtual void osc_RESET_SELECTION_BACKGROUND() = 0;
    virtual void osc_RESET_SELECTION_FOREGROUND() = 0;
    virtual void osc_SHELL_A(stl::StringView payload) = 0;
    virtual void osc_SHELL_B(stl::StringView payload) = 0;
    virtual void osc_SHELL_C(stl::StringView payload) = 0;
    virtual void osc_SHELL_D(stl::StringView payload) = 0;
    virtual void osc_SHELL_UNKNOWN(stl::StringView payload) = 0;
    virtual void osc_UNKNOWN(u32 command, stl::StringView payload) = 0;

    virtual void csiq_DECSCL(const ParserParameters& parameters) = 0;
    virtual void csi_XTWINOPS(const ParserParameters& parameters) = 0;
    virtual void csi_XTTITLEMODE(const ParserParameters& parameters, bool set) = 0;
    virtual void csi_XTHIMOUSE(const ParserParameters& parameters) = 0;
    virtual void csi_DECELR(const ParserParameters& parameters) = 0;
    virtual void csi_DECSLE(const ParserParameters& parameters) = 0;
    virtual void csi_DECRQLP() = 0;
    virtual void csi_DECEFR(const ParserParameters& parameters) = 0;
    virtual void csi_XTMODKEYS(const ParserParameters& parameters) = 0;
    virtual void csi_XTQMODKEYS(const ParserParameters& parameters) = 0;
    virtual void csi_kittyKeyboardPush(const ParserParameters& parameters) = 0;
    virtual void csi_kittyKeyboardPop(const ParserParameters& parameters) = 0;
    virtual void csi_kittyKeyboardSet(const ParserParameters& parameters) = 0;
    virtual void csi_kittyKeyboardQuery() = 0;
    virtual void csi_DECRQM(const ParserParameters& parameters, bool privateMode) = 0;
    virtual void csi_XTVERSION() = 0;
    virtual void csi_MC(const ParserParameters& parameters, bool privateMode) = 0;
    virtual void csi_DECLL(const ParserParameters& parameters) = 0;

    virtual void dcs_DECRQSS_DECSCL() = 0;
    virtual void dcs_DECRQSS_SGR() = 0;
    virtual void dcs_DECRQSS_DECSTBM() = 0;
    virtual void dcs_DECRQSS_DECSLRM() = 0;
    virtual void dcs_DECRQSS_DECSLPP() = 0;
    virtual void dcs_DECRQSS_DECSCUSR() = 0;
    virtual void dcs_DECRQSS_DECSCA() = 0;
    virtual void dcs_DECRQSS_UNKNOWN() = 0;
    virtual void dcs_XTGETTCAP(stl::Buffer& replies, stl::StringView encoded, stl::StringView value) = 0;
    virtual void dcs_XTGETTCAP_COMMIT(stl::StringView replies) = 0;
    virtual void dcs_DECUDK(bool clearDefinitions, bool lockDefinitions, const ParserUdkDefinition* definitions, size_t definitionCount, stl::StringView values) = 0;
};

struct Parser {
    static Parser* create(stl::ObjPool* pool, ParserIface& iface, VtermTrace* trace);

    virtual void feed(stl::StringView bytes) = 0;
};
