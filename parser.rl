/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

%%{
    machine parser;
    alphtype unsigned char;

    action groundDone {
        fbreak;
    }

    action returnGround {
        fnext main;
        fbreak;
    }

    action cancel {
        parser.stringUtf8Remaining = 0;
        parser.stringLimit = 0;
        if constexpr (traced) {
            parserTrace->control(fc);
            parserTrace->stringCancel();
            parserTrace->escapeCancel();
        }
        fnext main;
        fbreak;
    }

    action beginEscape {
        iface.parserResetGraphemeInput();
        parser.stringUtf8Remaining = 0;
        parser.stringLimit = 0;
        if constexpr (traced) {
            parserTrace->stringCancel();
            parserTrace->escapeCancel();
            parserTrace->escapeBegin();
        }
        parser.parameters[0] = 0;
        parser.separators[0] = 0;
        parser.parameterCount = 1;
        if (iface.parserCompatibilityLevel() == CompatibilityLevel::VT52) {
            fgoto escapeVt52;
        }
        fgoto escape;
    }

    action repeatEscape {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->escapeBegin();
        }
        parser.parameters[0] = 0;
        parser.separators[0] = 0;
        parser.parameterCount = 1;
    }

    action beginCsi {
        beginCsi();
        fgoto csiEntry;
    }

    action beginDcs {
        ragelBeginDcs();
        fgoto dcsEntry;
    }

    action beginOsc {
        ragelBeginOsc();
        fgoto oscCommand;
    }

    action beginSos {
        ragelBeginString(VtermTraceString::Sos, false);
        fgoto string;
    }

    action beginPm {
        ragelBeginString(VtermTraceString::Pm, false);
        fgoto string;
    }

    action beginApc {
        ragelBeginString(VtermTraceString::Apc, false);
        fgoto string;
    }

    action c1Ind {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.esc_IND();
        fnext main;
        fbreak;
    }

    action c1Nel {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.esc_NEL();
        fnext main;
        fbreak;
    }

    action c1Hts {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.esc_HTS();
        fnext main;
        fbreak;
    }

    action c1Ri {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.esc_RI();
        fnext main;
        fbreak;
    }

    action c1Ss2 {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.parserSingleShift(2);
        fnext main;
        fbreak;
    }

    action c1Ss3 {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.parserSingleShift(3);
        fnext main;
        fbreak;
    }

    action c1Spa {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.esc_SPA();
        fnext main;
        fbreak;
    }

    action c1Epa {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.esc_EPA();
        fnext main;
        fbreak;
    }

    action c1Da {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        iface.csi_priDA();
        fnext main;
        fbreak;
    }

    action c1St {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        fnext main;
        fbreak;
    }

    action sequenceC0 {
        executeC0(fc);
    }

    action highToGround {
        if constexpr (traced) {
            parserTrace->escapeCancel();
        }
        fhold;
        fgoto main;
    }

    action groundIgnored {
        if constexpr (traced) {
            if (fc != 0) {
                parserTrace->control(fc);
            }
        }
        iface.parserResetGraphemeInput();
        fbreak;
    }

    action groundBell {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserResetGraphemeInput();
        iface.parserBell();
        fbreak;
    }

    action groundBackspace {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserResetGraphemeInput();
        iface.parserMoveCursorBackward(1);
        fbreak;
    }

    action groundTab {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserResetGraphemeInput();
        iface.inp_HT();
        fbreak;
    }

    action groundLineFeed {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserResetGraphemeInput();
        if (iface.parserAutoNewlineMode()) {
            iface.inp_CR();
        }
        iface.esc_IND();
        fbreak;
    }

    action groundCarriageReturn {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserResetGraphemeInput();
        iface.inp_CR();
        fbreak;
    }

    action groundShiftOut {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserResetGraphemeInput();
        iface.parserLockingShiftGl(1);
        fbreak;
    }

    action groundShiftIn {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserResetGraphemeInput();
        iface.parserLockingShiftGl(0);
        fbreak;
    }

    action groundAscii {
        ragelGroundAscii(fc);
        fbreak;
    }

    action groundHigh {
        ragelGroundHigh(fc);
        fbreak;
    }

    action groundC1Ind {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.esc_IND();
        fbreak;
    }

    action groundC1Nel {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.esc_NEL();
        fbreak;
    }

    action groundC1Hts {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.esc_HTS();
        fbreak;
    }

    action groundC1Ri {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.esc_RI();
        fbreak;
    }

    action groundC1Ss2 {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserSingleShift(2);
        fbreak;
    }

    action groundC1Ss3 {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.parserSingleShift(3);
        fbreak;
    }

    action groundC1Dcs {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        ragelBeginDcs();
        fgoto dcsEntry;
    }

    action groundC1Spa {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.esc_SPA();
        fbreak;
    }

    action groundC1Epa {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.esc_EPA();
        fbreak;
    }

    action groundC1Sos {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        ragelBeginString(VtermTraceString::Sos, false);
        fgoto string;
    }

    action groundC1Da {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.csi_priDA();
        fbreak;
    }

    action groundC1Csi {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        beginCsi();
        fgoto csiEntry;
    }

    action groundC1St {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        fbreak;
    }

    action groundC1Osc {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        ragelBeginOsc();
        fgoto oscCommand;
    }

    action groundC1Pm {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        ragelBeginString(VtermTraceString::Pm, false);
        fgoto string;
    }

    action groundC1Apc {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        iface.parserResetGraphemeInput();
        ragelBeginString(VtermTraceString::Apc, false);
        fgoto string;
    }

    action escapeIntermediate {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        fgoto escapeIntermediate;
    }

    action escapeFinal {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.unhandledInput(fc);
        fnext main;
        fbreak;
    }

    action escapeC0 {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        iface.unhandledInput(fc);
        fnext main;
        fbreak;
    }

    action escapeSpace {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        fgoto escapeSpace;
    }

    action escapeHash {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        fgoto escapeHash;
    }

    action escapePercent {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        fgoto escapePercent;
    }

    action charsetG0 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        parser.scsIndex = 0;
        parser.scsMod = 0;
        parser.scs96 = false;
        fgoto selectCharset;
    }

    action charsetG1 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        parser.scsIndex = 1;
        parser.scsMod = 0;
        parser.scs96 = false;
        fgoto selectCharset;
    }

    action charsetG2 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        parser.scsIndex = 2;
        parser.scsMod = 0;
        parser.scs96 = false;
        fgoto selectCharset;
    }

    action charsetG3 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        parser.scsIndex = 3;
        parser.scsMod = 0;
        parser.scs96 = false;
        fgoto selectCharset;
    }

    action charsetG1_96 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        parser.scsIndex = 1;
        parser.scsMod = 0;
        parser.scs96 = true;
        fgoto selectCharset;
    }

    action charsetG2_96 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        parser.scsIndex = 2;
        parser.scsMod = 0;
        parser.scs96 = true;
        fgoto selectCharset;
    }

    action charsetG3_96 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        parser.scsIndex = 3;
        parser.scsMod = 0;
        parser.scs96 = true;
        fgoto selectCharset;
    }

    action escapeStringTerminator {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        fnext main;
        fbreak;
    }

    action escapeInd {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_IND();
        fnext main;
        fbreak;
    }

    action escapeRi {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_RI();
        fnext main;
        fbreak;
    }

    action escapeNel {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_NEL();
        fnext main;
        fbreak;
    }

    action escapeHts {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_HTS();
        fnext main;
        fbreak;
    }

    action escapeSs2 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserSingleShift(2);
        fnext main;
        fbreak;
    }

    action escapeSs3 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserSingleShift(3);
        fnext main;
        fbreak;
    }

    action escapeSpa {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_SPA();
        fnext main;
        fbreak;
    }

    action escapeEpa {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_EPA();
        fnext main;
        fbreak;
    }

    action escapeDa {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.csi_priDA();
        fnext main;
        fbreak;
    }

    action escapeRis {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_RIS();
        fnext main;
        fbreak;
    }

    action escapeBi {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_BI();
        fnext main;
        fbreak;
    }

    action escapeDecsc {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_DECSC();
        fnext main;
        fbreak;
    }

    action escapeDecrc {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_DECRC();
        fnext main;
        fbreak;
    }

    action escapeFi {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.esc_FI();
        fnext main;
        fbreak;
    }

    action escapeAppKeypad {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserSetApplicationKeypad(true);
        fnext main;
        fbreak;
    }

    action escapeNormalKeypad {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserSetApplicationKeypad(false);
        fnext main;
        fbreak;
    }

    action escapeAnsi {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserSetCompatibilityLevel(CompatibilityLevel::VT400);
        fnext main;
        fbreak;
    }

    action escapeLs1r {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserLockingShiftGr(1);
        fnext main;
        fbreak;
    }

    action escapeLs2 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserLockingShiftGl(2);
        fnext main;
        fbreak;
    }

    action escapeLs2r {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserLockingShiftGr(2);
        fnext main;
        fbreak;
    }

    action escapeLs3 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserLockingShiftGl(3);
        fnext main;
        fbreak;
    }

    action escapeLs3r {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        iface.parserLockingShiftGr(3);
        fnext main;
        fbreak;
    }

    action intermediateByte {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
    }

    action intermediateFinal {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        fnext main;
        fbreak;
    }

    action specialIntermediate {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        fgoto escapeIntermediate;
    }

    action specialFinal {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
    }

    action charsetModifier {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        parser.scsMod = fc;
    }

    action charsetFinal {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        fnext main;
        fbreak;
    }

    action csiDigit {
        parser.csiHadParameters = true;
        parser.present[parser.parameterCount - 1] = true;
        if (parser.parameters[parser.parameterCount - 1] > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            parser.parameters[parser.parameterCount - 1] = UINT32_MAX;
        } else {
            parser.parameters[parser.parameterCount - 1] = parser.parameters[parser.parameterCount - 1] * 10 + fc - '0';
        }
    }

    action csiSeparator {
        if (parser.parameterCount >= parser.maxParameters) {
            fgoto csiIgnore;
        }
        parser.csiHadParameters = true;
        parser.separators[parser.parameterCount] = fc;
        parser.parameters[parser.parameterCount] = 0;
        parser.present[parser.parameterCount] = false;
        ++parser.parameterCount;
    }

    action csiPrefix {
        if (parser.csiPrefix != 0) {
            fgoto csiIgnore;
        }
        parser.csiPrefix = fc;
    }

    action csiIntermediate {
        if (parser.csiIntermediateCount >= sizeof(parser.csiIntermediates)) {
            fgoto csiIgnore;
        }
        parser.csiIntermediates[parser.csiIntermediateCount++] = fc;
    }

    action csiTrace {
        traceCsi(fc);
    }

    action csiSgr {
        dispatchSgr();
    }

    action csiDone {
        if (parser.enterPrinter) {
            fnext printer;
        } else {
            fnext main;
        }
        fbreak;
    }

    action printerByte {
        appendPrinter(&fc, 1);
    }

    action printerEscapePrefix {
        appendPrinter("\x1b", 1);
    }

    action printerEscapeBracketPrefix {
        appendPrinter("\x1b[", 2);
    }

    action printerEscapeBracket4Prefix {
        appendPrinter("\x1b[4", 3);
    }

    action printerCsiPrefix {
        appendPrinter("\x9b", 1);
    }

    action printerCsi4Prefix {
        appendPrinter("\x9b"
                      "4", 2);
    }

    action printerEnd {
        fnext main;
        fbreak;
    }

    action csiFinalSelect {
        fhold;
        if (parser.csiPrefix == '>') {
            fgoto csiGreaterDispatch;
        }
        if (parser.csiPrefix == '<') {
            fgoto csiLessDispatch;
        }
        if (parser.csiPrefix == '=') {
            fgoto csiEqualDispatch;
        }
        if (parser.csiPrefix == '?') {
            fgoto csiQuestionDispatch;
        }
        fgoto csiPlainDispatch;
    }

    action csiIntermediateFinalSelect {
        fhold;
        if (parser.csiIntermediateCount != 1) {
            fgoto csiUnknownDispatch;
        }
        if (parser.csiPrefix == '?' && parser.csiIntermediates[0] == '$') {
            fgoto csiQuestionDollarDispatch;
        }
        if (parser.csiPrefix != 0) {
            fgoto csiUnknownDispatch;
        }
        if (parser.csiIntermediates[0] == '!') {
            fgoto csiBangDispatch;
        }
        if (parser.csiIntermediates[0] == '"') {
            fgoto csiQuoteDispatch;
        }
        if (parser.csiIntermediates[0] == ' ') {
            fgoto csiSpaceDispatch;
        }
        if (parser.csiIntermediates[0] == '\'') {
            fgoto csiApostropheDispatch;
        }
        if (parser.csiIntermediates[0] == '$') {
            fgoto csiDollarDispatch;
        }
        if (parser.csiIntermediates[0] == '*') {
            fgoto csiStarDispatch;
        }
        fgoto csiUnknownDispatch;
    }

    action csiInvalid {
        fgoto csiIgnore;
    }

    action csiIgnoredFinal {
        if constexpr (traced) {
            parserTrace->escapeCancel();
        }
        fnext main;
        fbreak;
    }

    action vt52AppKeypad {
        iface.parserSetApplicationKeypad(true);
        fnext main;
        fbreak;
    }

    action vt52NormalKeypad {
        iface.parserSetApplicationKeypad(false);
        fnext main;
        fbreak;
    }

    action vt52Ansi {
        iface.parserSetCompatibilityLevel(CompatibilityLevel::VT100);
        fnext main;
        fbreak;
    }

    action vt52Cuu {
        iface.csi_CUU(1);
        fnext main;
        fbreak;
    }

    action vt52Cud {
        iface.csi_CUD(1);
        fnext main;
        fbreak;
    }

    action vt52Cuf {
        iface.csi_CUF(1);
        fnext main;
        fbreak;
    }

    action vt52Cub {
        iface.csi_CUB(1);
        fnext main;
        fbreak;
    }

    action vt52Graphics {
        iface.parserResetCharsets(false);
        iface.parserDesignateCharset(0, Charset::DecSpec);
        fnext main;
        fbreak;
    }

    action vt52Ascii {
        iface.parserResetCharsets(false);
        fnext main;
        fbreak;
    }

    action vt52Cup {
        iface.csi_CUP(parameter(0), parameter(1));
        fnext main;
        fbreak;
    }

    action vt52Ri {
        iface.esc_RI();
        fnext main;
        fbreak;
    }

    action vt52Ed {
        iface.eraseDisplayAfter();
        fnext main;
        fbreak;
    }

    action vt52El {
        iface.eraseLineAfter();
        fnext main;
        fbreak;
    }

    action vt52CupBegin {
        fgoto vt52CupRow;
    }

    action vt52Identify {
        iface.parserWritePty(StringView(u8"\x1b/Z"));
        fnext main;
        fbreak;
    }

    action vt52Ris {
        iface.esc_RIS();
        fnext main;
        fbreak;
    }

    action vt52Unhandled {
        iface.unhandledInput(fc);
        fnext main;
        fbreak;
    }

    action vt52Row {
        parser.parameters[0] = fc - 31;
        fgoto vt52CupColumn;
    }

    action vt52Column {
        parser.parameters[1] = fc - 31;
        parser.parameterCount = 2;
        iface.csi_CUP(parameter(0), parameter(1));
        fnext main;
        fbreak;
    }

    action dcsHeaderByte {
        ragelAppendString(fc, parser.maxDcsBytes);
    }

    action dcsDigit {
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.present[parser.parameterCount - 1] = true;
        if (parser.parameters[parser.parameterCount - 1] > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            parser.parameters[parser.parameterCount - 1] = UINT32_MAX;
        } else {
            parser.parameters[parser.parameterCount - 1] = parser.parameters[parser.parameterCount - 1] * 10 + fc - '0';
        }
    }

    action dcsSeparator {
        ragelAppendString(fc, parser.maxDcsBytes);
        if (parser.parameterCount >= parser.maxParameters) {
            if constexpr (traced) {
                parserTrace->stringCancel();
            }
            fgoto dcsIgnore;
        }
        parser.separators[parser.parameterCount] = fc;
        parser.parameters[parser.parameterCount] = 0;
        parser.present[parser.parameterCount] = false;
        ++parser.parameterCount;
    }

    action dcsIntermediate {
        ragelAppendString(fc, parser.maxDcsBytes);
        if (parser.dcsIntermediateCount >= sizeof(parser.dcsIntermediates)) {
            if constexpr (traced) {
                parserTrace->stringCancel();
            }
            fgoto dcsIgnore;
        }
        parser.dcsIntermediates[parser.dcsIntermediateCount++] = fc;
    }

    action dcsFinal {
        ragelAppendString(fc, parser.maxDcsBytes);
        if (parser.dcsIntermediateCount == 1 && parser.dcsIntermediates[0] == '$' && fc == 'q') {
            fgoto dcsDecrqssEntry;
        } else if (parser.dcsIntermediateCount == 1 && parser.dcsIntermediates[0] == '+' && fc == 'q') {
            parser.dcsCapabilityOffset = parser.scratch.used();
            parser.dcsCapabilityDecodedLength = 0;
            parser.dcsCapabilityCandidates = 0x0f;
            parser.dcsCapabilityHasHighNibble = false;
            parser.dcsCapabilityValid = true;
            parser.dcsCapabilityComplete = false;
            fgoto dcsXtgettcap;
        } else if (parser.dcsIntermediateCount == 0 && fc == '|') {
            const bool secondParameterValid =
                parser.parameterCount < 2 || !parser.present[1] || parser.parameters[1] <= 1;
            parser.dcsUdkHeaderValid =
                parser.parameterCount <= 2 &&
                (!parser.present[0] || parser.parameters[0] <= 1) &&
                secondParameterValid &&
                (parser.parameterCount < 2 || parser.separators[1] == ';');
            parser.dcsUdkClearDefinitions =
                !parser.present[0] || parser.parameters[0] == 0;
            parser.dcsUdkLockDefinitions =
                parser.parameterCount < 2 || !parser.present[1] || parser.parameters[1] == 0;
            parser.dcsUdkValueOffset = parser.dcsDecoded.used();
            parser.dcsUdkCode = 0;
            parser.dcsUdkKey = VtKey::NONE;
            parser.dcsUdkHasCode = false;
            parser.dcsUdkHasHighNibble = false;
            parser.dcsUdkValid = true;
            parser.dcsUdkInValue = false;
            fgoto dcsUdkCode;
        } else {
            fgoto dcsPayload;
        }
    }

    action dcsHeaderInvalid {
        if constexpr (traced) {
            parserTrace->stringCancel();
        }
        fgoto dcsIgnore;
    }

    action dcsHeaderTerminated {
        parser.stringUtf8Remaining = 0;
        parser.stringLimit = 0;
        if constexpr (traced) {
            parserTrace->stringCancel();
        }
        fnext main;
        fbreak;
    }

    action dcsIgnoreSt {
        parser.stringUtf8Remaining = 0;
        parser.stringLimit = 0;
        fnext main;
        fbreak;
    }

    action dcsPayloadData {
        if (fc >= 0x20 && fc < 0x7f) {
            const size_t count = printableAsciiPrefix(p, pe - p);
            parser.stringUtf8Remaining = 0;
            ragelAppendStringSpan(p, count, parser.maxDcsBytes);
            p += count - 1;
        } else {
            consumeStringUtf8Byte(fc);
            if (!executeC0(fc)) {
                ragelAppendString(fc, parser.maxDcsBytes);
            }
        }
    }

    action dcsIgnoreData {
        if (fc >= 0x20 && fc < 0x7f) {
            const size_t count = printableAsciiPrefix(p, pe - p);
            p += count - 1;
        }
    }

    action dcsSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxDcsBytes);
        } else {
            ragelFinishDcs();
            fnext main;
            fbreak;
        }
    }

    action dcsHeaderEscape {
        fgoto dcsHeaderEscape;
    }

    action dcsPayloadEscape {
        fgoto dcsPayloadEscape;
    }

    action dcsEscapedEscape {
        if constexpr (traced) {
            parserTrace->stringData((const u8*)("\x1b"), 1);
        }
        if (parser.scratch.used() < parser.maxDcsBytes) {
            const u8 ch = '\x1b';
            parser.scratch.append(&ch, 1);
        } else {
            parser.overflow = true;
        }
    }

    action dcsEscapedData {
        ragelAppendEscapedString(fc, parser.maxDcsBytes);
        fgoto dcsPayload;
    }

    action dcsDecrqssInvalidStart {
        fhold;
        fgoto dcsDecrqssInvalid;
    }

    action dcsDecrqssInvalid {
        consumeStringUtf8Byte(fc);
        ragelAppendString(fc, parser.maxDcsBytes);
        fgoto dcsDecrqssInvalid;
    }

    action dcsDecrqssEscape {
        fgoto dcsDecrqssEscape;
    }

    action dcsDecrqssEscapedEscape {
        if constexpr (traced) {
            parserTrace->stringData((const u8*)("\x1b"), 1);
        }
        if (parser.scratch.used() < parser.maxDcsBytes) {
            const u8 ch = '\x1b';
            parser.scratch.append(&ch, 1);
        } else {
            parser.overflow = true;
        }
        fgoto dcsDecrqssEscape;
    }

    action dcsDecrqssEscapedData {
        ragelAppendEscapedString(fc, parser.maxDcsBytes);
        fgoto dcsDecrqssInvalid;
    }

    action dcsDecrqssUnknownSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxDcsBytes);
            fgoto dcsDecrqssInvalid;
        } else {
            ragelFinishDcs();
            if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT400) {
                iface.dcs_DECRQSS_UNKNOWN();
            }
            fnext main;
            fbreak;
        }
    }

    action dcsDecrqssDecsclSt {
        ragelFinishDcs();
        if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT400) {
            iface.dcs_DECRQSS_DECSCL();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssSgrSt {
        ragelFinishDcs();
        if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT400) {
            iface.dcs_DECRQSS_SGR();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecstbmSt {
        ragelFinishDcs();
        if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT400) {
            iface.dcs_DECRQSS_DECSTBM();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecslrmSt {
        ragelFinishDcs();
        if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT400) {
            iface.dcs_DECRQSS_DECSLRM();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecslppSt {
        ragelFinishDcs();
        if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT400) {
            iface.dcs_DECRQSS_DECSLPP();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecscusrSt {
        ragelFinishDcs();
        if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT400) {
            iface.dcs_DECRQSS_DECSCUSR();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecscaSt {
        ragelFinishDcs();
        if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT400) {
            iface.dcs_DECRQSS_DECSCA();
        }
        fnext main;
        fbreak;
    }

    action dcsXtHex {
        ragelAppendString(fc, parser.maxDcsBytes);
        const u8 nibble = fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10;
        if (!parser.dcsCapabilityHasHighNibble) {
            parser.dcsCapabilityHighNibble = nibble;
            parser.dcsCapabilityHasHighNibble = true;
        } else {
            const u8 decoded = (parser.dcsCapabilityHighNibble << 4) | nibble;
            static constexpr u8 terminalName[] = {'T', 'N'};
            static constexpr u8 colorCount[] = {'C', 'o'};
            static constexpr u8 colors[] = {'c', 'o', 'l', 'o', 'r', 's'};
            static constexpr u8 rgb[] = {'R', 'G', 'B'};
            const size_t index = parser.dcsCapabilityDecodedLength++;
            if (index >= sizeof(terminalName) || terminalName[index] != decoded) {
                parser.dcsCapabilityCandidates &= ~0x01;
            }
            if (index >= sizeof(colorCount) || colorCount[index] != decoded) {
                parser.dcsCapabilityCandidates &= ~0x02;
            }
            if (index >= sizeof(colors) || colors[index] != decoded) {
                parser.dcsCapabilityCandidates &= ~0x04;
            }
            if (index >= sizeof(rgb) || rgb[index] != decoded) {
                parser.dcsCapabilityCandidates &= ~0x08;
            }
            parser.dcsCapabilityHasHighNibble = false;
        }
    }

    action dcsXtInvalid {
        consumeStringUtf8Byte(fc);
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.dcsCapabilityValid = false;
    }

    action dcsXtField {
        if (parser.dcsCapabilityComplete && !parser.overflow) {
            const auto* data = (const u8*)(parser.scratch.data());
            const StringView encoded(
                data + parser.dcsCapabilityOffset,
                parser.scratch.used() - parser.dcsCapabilityOffset
            );
            if (parser.dcsCapabilityValid && !parser.dcsCapabilityHasHighNibble &&
                (parser.dcsCapabilityCandidates & 0x01) &&
                parser.dcsCapabilityDecodedLength == 2) {
                iface.dcs_XTGETTCAP(parser.dcsDecoded, encoded, StringView(u8"xterm-256color"));
            } else if (parser.dcsCapabilityValid && !parser.dcsCapabilityHasHighNibble &&
                       (((parser.dcsCapabilityCandidates & 0x02) &&
                         parser.dcsCapabilityDecodedLength == 2) ||
                        ((parser.dcsCapabilityCandidates & 0x04) &&
                         parser.dcsCapabilityDecodedLength == 6))) {
                iface.dcs_XTGETTCAP(parser.dcsDecoded, encoded, StringView(u8"256"));
            } else if (parser.dcsCapabilityValid && !parser.dcsCapabilityHasHighNibble &&
                       (parser.dcsCapabilityCandidates & 0x08) &&
                       parser.dcsCapabilityDecodedLength == 3) {
                iface.dcs_XTGETTCAP(parser.dcsDecoded, encoded, StringView(u8"8"));
            } else {
                iface.dcs_XTGETTCAP(parser.dcsDecoded, encoded, {});
            }
        }
    }

    action dcsXtSeparator {
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.dcsCapabilityOffset = parser.scratch.used();
        parser.dcsCapabilityDecodedLength = 0;
        parser.dcsCapabilityCandidates = 0x0f;
        parser.dcsCapabilityHasHighNibble = false;
        parser.dcsCapabilityValid = true;
        parser.dcsCapabilityComplete = false;
    }

    action dcsXtSt {
        parser.dcsCapabilityComplete = false;
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxDcsBytes);
            parser.dcsCapabilityValid = false;
        } else {
            ragelFinishDcs();
            parser.dcsCapabilityComplete = true;
        }
    }

    action dcsXtDone {
        if (parser.dcsCapabilityComplete) {
            if (!parser.overflow && iface.parserCompatibilityLevel() >= CompatibilityLevel::VT200) {
                iface.dcs_XTGETTCAP_COMMIT(StringView(parser.dcsDecoded));
            }
            fnext main;
            fbreak;
        }
    }

    action dcsXtEscape {
        fgoto dcsXtEscape;
    }

    action dcsXtEscapedEscape {
        if constexpr (traced) {
            parserTrace->stringData((const u8*)("\x1b"), 1);
        }
        if (parser.scratch.used() < parser.maxDcsBytes) {
            const u8 ch = '\x1b';
            parser.scratch.append(&ch, 1);
        } else {
            parser.overflow = true;
        }
        parser.dcsCapabilityValid = false;
    }

    action dcsXtEscapedData {
        ragelAppendEscapedString(fc, parser.maxDcsBytes);
        parser.dcsCapabilityValid = false;
        fgoto dcsXtgettcap;
    }

    action dcsUdkDigit {
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.dcsUdkHasCode = true;
        if (parser.dcsUdkCode > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            parser.dcsUdkValid = false;
        } else {
            parser.dcsUdkCode = parser.dcsUdkCode * 10 + fc - '0';
        }
    }

    action dcsUdkSlash {
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.dcsUdkInValue = true;
        parser.dcsUdkValid = parser.dcsUdkValid && parser.dcsUdkHasCode;
        if (parser.dcsUdkCode >= 17 && parser.dcsUdkCode <= 21) {
            parser.dcsUdkKey = (VtKey)(
                (int)(VtKey::F6) + parser.dcsUdkCode - 17
            );
        } else if (parser.dcsUdkCode >= 23 && parser.dcsUdkCode <= 26) {
            parser.dcsUdkKey = (VtKey)(
                (int)(VtKey::F11) + parser.dcsUdkCode - 23
            );
        } else if (parser.dcsUdkCode >= 28 && parser.dcsUdkCode <= 29) {
            parser.dcsUdkKey = (VtKey)(
                (int)(VtKey::F15) + parser.dcsUdkCode - 28
            );
        } else if (parser.dcsUdkCode >= 31 && parser.dcsUdkCode <= 34) {
            parser.dcsUdkKey = (VtKey)(
                (int)(VtKey::F17) + parser.dcsUdkCode - 31
            );
        } else {
            parser.dcsUdkKey = VtKey::NONE;
            parser.dcsUdkValid = false;
        }
        parser.dcsUdkValueOffset = parser.dcsDecoded.used();
        parser.dcsUdkHasHighNibble = false;
        fgoto dcsUdkValue;
    }

    action dcsUdkHex {
        ragelAppendString(fc, parser.maxDcsBytes);
        const u8 nibble = fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10;
        if (!parser.dcsUdkHasHighNibble) {
            parser.dcsUdkHighNibble = nibble;
            parser.dcsUdkHasHighNibble = true;
        } else {
            if (!parser.overflow &&
                parser.dcsDecoded.used() - parser.dcsUdkValueOffset < 255) {
                const u8 decoded = (parser.dcsUdkHighNibble << 4) | nibble;
                parser.dcsDecoded.append(&decoded, 1);
            } else {
                parser.dcsUdkValid = false;
            }
            parser.dcsUdkHasHighNibble = false;
        }
    }

    action dcsUdkCodeSeparator {
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.dcsUdkCode = 0;
        parser.dcsUdkKey = VtKey::NONE;
        parser.dcsUdkHasCode = false;
        parser.dcsUdkHasHighNibble = false;
        parser.dcsUdkValid = true;
        parser.dcsUdkInValue = false;
    }

    action dcsUdkValueSeparator {
        if (!parser.overflow && parser.dcsUdkValid && !parser.dcsUdkHasHighNibble) {
            parser.dcsUdkDefinitions.pushBack({
                parser.dcsUdkValueOffset,
                parser.dcsDecoded.used() - parser.dcsUdkValueOffset,
                parser.dcsUdkKey,
            });
        }
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.dcsUdkCode = 0;
        parser.dcsUdkKey = VtKey::NONE;
        parser.dcsUdkHasCode = false;
        parser.dcsUdkHasHighNibble = false;
        parser.dcsUdkValid = true;
        parser.dcsUdkInValue = false;
        fgoto dcsUdkCode;
    }

    action dcsUdkInvalidSeparator {
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.dcsUdkCode = 0;
        parser.dcsUdkKey = VtKey::NONE;
        parser.dcsUdkHasCode = false;
        parser.dcsUdkHasHighNibble = false;
        parser.dcsUdkValid = true;
        parser.dcsUdkInValue = false;
        fgoto dcsUdkCode;
    }

    action dcsUdkInvalid {
        consumeStringUtf8Byte(fc);
        ragelAppendString(fc, parser.maxDcsBytes);
        parser.dcsUdkValid = false;
        fgoto dcsUdkInvalid;
    }

    action dcsUdkSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxDcsBytes);
            parser.dcsUdkValid = false;
            fgoto dcsUdkInvalid;
        } else {
            if (!parser.overflow && parser.dcsUdkInValue && parser.dcsUdkValid &&
                !parser.dcsUdkHasHighNibble) {
                parser.dcsUdkDefinitions.pushBack({
                    parser.dcsUdkValueOffset,
                    parser.dcsDecoded.used() - parser.dcsUdkValueOffset,
                    parser.dcsUdkKey,
                });
            }
            ragelFinishDcs();
            if (!parser.overflow && parser.dcsUdkHeaderValid &&
                iface.parserCompatibilityLevel() >= CompatibilityLevel::VT200) {
                iface.dcs_DECUDK(
                    parser.dcsUdkClearDefinitions,
                    parser.dcsUdkLockDefinitions,
                    parser.dcsUdkDefinitions.data(),
                    parser.dcsUdkDefinitions.length(),
                    StringView(parser.dcsDecoded)
                );
            }
            fnext main;
            fbreak;
        }
    }

    action dcsUdkEscape {
        fgoto dcsUdkEscape;
    }

    action dcsUdkEscapedEscape {
        if constexpr (traced) {
            parserTrace->stringData((const u8*)("\x1b"), 1);
        }
        if (parser.scratch.used() < parser.maxDcsBytes) {
            const u8 ch = '\x1b';
            parser.scratch.append(&ch, 1);
        } else {
            parser.overflow = true;
        }
        parser.dcsUdkValid = false;
    }

    action dcsUdkEscapedData {
        ragelAppendEscapedString(fc, parser.maxDcsBytes);
        parser.dcsUdkValid = false;
        fgoto dcsUdkInvalid;
    }

    action oscCommandDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscCommandValid = true;
        if (parser.oscCommand > (2147483647u - (u32)(fc - '0')) / 10) {
            parser.oscCommandValid = false;
        } else {
            parser.oscCommand = parser.oscCommand * 10 + fc - '0';
        }
    }

    action oscCommandSeparator {
        ragelAppendString(fc, parser.maxOscBytes);
        if (!parser.oscCommandValid) {
            fgoto oscInvalid;
        }
        parser.oscPayloadOffset = parser.scratch.used();
        if (parser.oscCommand == 0 || parser.oscCommand == 1 || parser.oscCommand == 2) {
            parser.oscTitleHex = iface.parserHexTitleInput();
            parser.oscTitleHasHighNibble = false;
            parser.oscTitleValid = true;
            parser.oscTitleStopped = false;
            parser.oscDecoded.reset();
            if (parser.oscCommand == 0) {
                fgoto oscTitle0;
            }
            if (parser.oscCommand == 1) {
                fgoto oscTitle1;
            }
            fgoto oscTitle2;
        } else if (parser.oscCommand == 7) {
            parser.oscDecoded.reset();
            parser.oscCwdPercentHigh = 0;
            parser.oscCwdValid = false;
            parser.oscCwdDecode = false;
            fgoto oscCwdEntry;
        } else if (parser.oscCommand >= 10 && parser.oscCommand <= 19) {
            resetOscColor();
            fgoto oscDynamicColor;
        } else if (parser.oscCommand == 4 || parser.oscCommand == 5) {
            parser.oscFieldNumber = 0;
            parser.oscFieldNumeric = true;
            parser.oscFieldPresent = false;
            fgoto oscIndexedColorIndex;
        } else if (parser.oscCommand == 6 || parser.oscCommand == 106) {
            parser.oscFieldNumber = 0;
            parser.oscFieldNumeric = true;
            parser.oscFieldPresent = false;
            parser.oscFieldFirst = 0;
            parser.oscFieldFirstValid = false;
            parser.oscFieldHaveFirst = false;
            fgoto oscNumericFields;
        } else if (parser.oscCommand == 104 || parser.oscCommand == 105) {
            parser.oscFieldNumber = 0;
            parser.oscFieldNumeric = true;
            parser.oscFieldPresent = false;
            fgoto oscNumericFields;
        } else if (parser.oscCommand == 8) {
            parser.oscHyperlinkIdOffset = 0;
            parser.oscHyperlinkIdLength = 0;
            parser.oscHyperlinkUriOffset = 0;
            parser.oscHyperlinkHasId = false;
            fgoto oscHyperlinkParamStart;
        } else if (parser.oscCommand == 9) {
            parser.oscProgressState = 0;
            parser.oscProgressPercent = 0;
            parser.oscProgressStatePresent = false;
            parser.oscProgressPercentPresent = false;
            parser.oscProgressValid = true;
            fgoto oscProgressEntry;
        } else if (parser.oscCommand == 52) {
            parser.oscBase64.reset();
            parser.osc52ReplySelector = 0;
            parser.osc52Primary = false;
            parser.osc52Clipboard = false;
            parser.osc52SelectorSeen = false;
            parser.osc52PayloadSeen = false;
            parser.osc52Query = false;
            fgoto osc52Selectors;
        } else if (parser.oscCommand == 99) {
            parser.oscNotificationFieldOffset = 0;
            parser.oscNotificationIdOffset = 0;
            parser.oscNotificationIdLength = 0;
            parser.oscNotificationPayloadOffset = 0;
            parser.oscNotificationPayloadBytes = 0;
            parser.oscNotificationKey = 0;
            parser.oscNotificationValid = true;
            parser.oscNotificationEncoded = false;
            parser.oscNotificationFinal = true;
            parser.oscNotificationQuery = false;
            parser.oscNotificationClose = false;
            parser.oscNotificationBody = false;
            fgoto oscNotificationField;
        } else if (parser.oscCommand == 133) {
            fgoto oscShellEntry;
        }
        fgoto oscPayload;
    }

    action oscCommandInvalid {
        consumeStringUtf8Byte(fc);
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscInvalid;
    }

    action oscCommandSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscTerminated = false;
            fgoto oscInvalid;
        } else {
            parser.oscPayloadOffset = parser.scratch.used();
            ragelFinishOsc();
            parser.oscTerminated = true;
        }
    }

    action oscCommandBell {
        parser.oscPayloadOffset = parser.scratch.used();
        ragelFinishOsc();
        parser.oscTerminated = true;
    }

    action oscData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
    }

    action oscRawData {
        if (fc >= 0x20 && fc < 0x7f) {
            const size_t count = printableAsciiPrefix(p, pe - p);
            parser.stringUtf8Remaining = 0;
            ragelAppendStringSpan(p, count, parser.maxOscBytes);
            p += count - 1;
        } else {
            consumeStringUtf8Byte(fc);
            if (!executeC0(fc)) {
                ragelAppendString(fc, parser.maxOscBytes);
            }
        }
    }

    action oscSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscTerminated = false;
        } else {
            ragelFinishOsc();
            parser.oscTerminated = true;
        }
    }

    action oscBell {
        ragelFinishOsc();
        parser.oscTerminated = true;
    }

    action oscDispatch {
        if (parser.oscTerminated && parser.oscCommandValid && !parser.overflow) {
            const StringView payload = ragelOscPayload();
            if (parser.oscCommand == 0) {
                iface.osc_TITLE_0(payload);
            } else if (parser.oscCommand == 1) {
                iface.osc_TITLE_1(payload);
            } else if (parser.oscCommand == 2) {
                iface.osc_TITLE_2(payload);
            } else if (parser.oscCommand == 8) {
                (void)payload;
            } else if (parser.oscCommand == 9) {
                iface.osc_NOTIFY(payload);
            } else if (parser.oscCommand == 52) {
                iface.osc_CLIPBOARD_MALFORMED(payload);
            } else if (parser.oscCommand == 104 && payload.empty()) {
                iface.osc_RESET_PALETTE();
            } else if (parser.oscCommand == 105 && payload.empty()) {
                iface.osc_RESET_SPECIAL_COLOR();
            } else if (parser.oscCommand == 110) {
                iface.osc_RESET_DEFAULT_FOREGROUND();
            } else if (parser.oscCommand == 111) {
                iface.osc_RESET_DEFAULT_BACKGROUND();
            } else if (parser.oscCommand == 112) {
                iface.osc_RESET_CURSOR_COLOR();
            } else if (parser.oscCommand == 117) {
                iface.osc_RESET_SELECTION_BACKGROUND();
            } else if (parser.oscCommand == 119) {
                iface.osc_RESET_SELECTION_FOREGROUND();
            } else if (parser.oscCommand == 133) {
                iface.osc_SHELL_UNKNOWN(payload);
            } else {
                iface.osc_UNKNOWN(parser.oscCommand, payload);
            }
        }
    }

    action oscDone {
        if (parser.oscTerminated) {
            fnext main;
            fbreak;
        }
    }

    action oscEscape {
        fgoto oscEscape;
    }

    action oscEscapedEscape {
        if constexpr (traced) {
            parserTrace->stringData((const u8*)("\x1b"), 1);
        }
        if (parser.scratch.used() < parser.maxOscBytes) {
            const u8 ch = '\x1b';
            parser.scratch.append(&ch, 1);
        } else {
            parser.overflow = true;
        }
    }

    action oscEscapedData {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscPayload;
    }

    action oscInvalidSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        } else {
            parser.stringUtf8Remaining = 0;
            parser.stringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringEnd();
            }
            fnext main;
            fbreak;
        }
    }

    action oscInvalidBell {
        parser.stringUtf8Remaining = 0;
        parser.stringLimit = 0;
        if constexpr (traced) {
            parserTrace->stringEnd();
        }
        fnext main;
        fbreak;
    }

    action oscInvalidEscapedData {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscInvalid;
    }

    action oscCwdRaw {
        ragelAppendString(fc, parser.maxOscBytes);
    }

    action oscCwdInvalidData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
        parser.oscCwdValid = false;
        parser.oscCwdDecode = false;
        fgoto oscCwdInvalid;
    }

    action oscCwdPrefixError {
        fhold;
        fgoto oscCwdInvalid;
    }

    action oscCwdPathStart {
        ragelAppendString(fc, parser.maxOscBytes);
        if (!parser.overflow) {
            parser.oscDecoded.append(&fc, 1);
        }
        parser.oscCwdValid = true;
        parser.oscCwdDecode = true;
        fgoto oscCwdPath;
    }

    action oscCwdAuthorityData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
    }

    action oscCwdPathData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            if (!parser.overflow) {
                parser.oscDecoded.append(&fc, 1);
            }
        }
    }

    action oscCwdPercentStart {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscCwdValid = false;
        parser.oscCwdDecode = false;
        fgoto oscCwdPercentHigh;
    }

    action oscCwdPercentHigh {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscCwdPercentHigh =
            fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10;
        fgoto oscCwdPercentLow;
    }

    action oscCwdPercentLow {
        ragelAppendString(fc, parser.maxOscBytes);
        if (!parser.overflow) {
            const u8 decoded =
                (parser.oscCwdPercentHigh << 4) |
                (fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10);
            parser.oscDecoded.append(&decoded, 1);
        }
        parser.oscCwdValid = true;
        parser.oscCwdDecode = true;
        fgoto oscCwdPath;
    }

    action oscCwdSt {
        if (ragelStringContinuation(fc)) {
            parser.oscTerminated = false;
        } else {
            ragelFinishOsc();
            parser.oscTerminated = true;
        }
    }

    action oscCwdBell {
        ragelFinishOsc();
        parser.oscTerminated = true;
    }

    action oscCwdDispatch {
        if (parser.oscTerminated && !parser.overflow) {
            iface.osc_CWD(
                ragelOscPayload(), StringView(parser.oscDecoded), parser.oscCwdValid
            );
        }
    }

    action oscCwdInvalidEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        parser.oscCwdValid = false;
        parser.oscCwdDecode = false;
        fgoto oscCwdInvalid;
    }

    action oscCwdAuthorityEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscCwdAuthority;
    }

    action oscCwdPathEscapedEscape {
        if (!parser.overflow) {
            const u8 escape = '\x1b';
            parser.oscDecoded.append(&escape, 1);
        }
    }

    action oscCwdPathEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        if (!parser.overflow) {
            const u8 bytes[] = {'\x1b', (u8)(fc)};
            parser.oscDecoded.append(bytes, sizeof(bytes));
        }
        fgoto oscCwdPath;
    }

    action oscShellA {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscShellAComplete;
    }

    action oscShellB {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscShellBComplete;
    }

    action oscShellC {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscShellCComplete;
    }

    action oscShellD {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscShellDComplete;
    }

    action oscShellInvalid {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
        fgoto oscShellUnknown;
    }

    action oscShellASt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            fgoto oscShellUnknown;
        } else {
            ragelFinishOsc();
            if (!parser.overflow) {
                iface.osc_SHELL_A(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellBSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            fgoto oscShellUnknown;
        } else {
            ragelFinishOsc();
            if (!parser.overflow) {
                iface.osc_SHELL_B(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellCSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            fgoto oscShellUnknown;
        } else {
            ragelFinishOsc();
            if (!parser.overflow) {
                iface.osc_SHELL_C(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellDSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            fgoto oscShellUnknown;
        } else {
            ragelFinishOsc();
            if (!parser.overflow) {
                iface.osc_SHELL_D(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellUnknownSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        } else {
            ragelFinishOsc();
            if (!parser.overflow) {
                iface.osc_SHELL_UNKNOWN(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellEscapedUnknown {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscShellUnknown;
    }

    action oscShellEscapedA {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscShellATail;
    }

    action oscShellEscapedB {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscShellBTail;
    }

    action oscShellEscapedC {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscShellCTail;
    }

    action oscShellEscapedD {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscShellDTail;
    }

    action oscTitleData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            if (parser.oscTitleHex) {
                u8 nibble = 0;
                if (fc >= '0' && fc <= '9') {
                    nibble = fc - '0';
                } else if ((fc | 0x20) >= 'a' && (fc | 0x20) <= 'f') {
                    nibble = (fc | 0x20) - 'a' + 10;
                } else {
                    parser.oscTitleValid = false;
                }
                if (parser.oscTitleValid) {
                    if (!parser.oscTitleHasHighNibble) {
                        parser.oscTitleHighNibble = nibble;
                        parser.oscTitleHasHighNibble = true;
                    } else {
                        const u8 decoded = (parser.oscTitleHighNibble << 4) | nibble;
                        if (decoded < 32) {
                            parser.oscTitleStopped = true;
                        } else if (!parser.oscTitleStopped) {
                        if (!parser.overflow) {
                            parser.oscDecoded.append(&decoded, 1);
                        }
                        }
                        parser.oscTitleHasHighNibble = false;
                    }
                }
            }
        }
    }

    action oscTitleEscapedEscape {
        if constexpr (traced) {
            parserTrace->stringData((const u8*)("\x1b"), 1);
        }
        if (parser.scratch.used() < parser.maxOscBytes) {
            const u8 ch = '\x1b';
            parser.scratch.append(&ch, 1);
        } else {
            parser.overflow = true;
        }
        if (parser.oscTitleHex) {
            parser.oscTitleValid = false;
        }
    }

    action oscTitleEscaped0 {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        if (parser.oscTitleHex) {
            parser.oscTitleValid = false;
        }
        fgoto oscTitle0;
    }

    action oscTitleEscaped1 {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        if (parser.oscTitleHex) {
            parser.oscTitleValid = false;
        }
        fgoto oscTitle1;
    }

    action oscTitleEscaped2 {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        if (parser.oscTitleHex) {
            parser.oscTitleValid = false;
        }
        fgoto oscTitle2;
    }

    action oscTitle0St {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            if (parser.oscTitleHex) {
                parser.oscTitleValid = false;
            }
        } else {
            ragelFinishOsc();
            if (!parser.overflow && parser.oscTitleValid && (!parser.oscTitleHex || !parser.oscTitleHasHighNibble)) {
                iface.osc_TITLE_0(parser.oscTitleHex ? StringView(parser.oscDecoded) : ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscTitle1St {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            if (parser.oscTitleHex) {
                parser.oscTitleValid = false;
            }
        } else {
            ragelFinishOsc();
            if (!parser.overflow && parser.oscTitleValid && (!parser.oscTitleHex || !parser.oscTitleHasHighNibble)) {
                iface.osc_TITLE_1(parser.oscTitleHex ? StringView(parser.oscDecoded) : ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscTitle2St {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            if (parser.oscTitleHex) {
                parser.oscTitleValid = false;
            }
        } else {
            ragelFinishOsc();
            if (!parser.overflow && parser.oscTitleValid && (!parser.oscTitleHex || !parser.oscTitleHasHighNibble)) {
                iface.osc_TITLE_2(parser.oscTitleHex ? StringView(parser.oscDecoded) : ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscHyperlinkParamData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
        fgoto oscHyperlinkParamSkip;
    }

    action oscHyperlinkI {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscHyperlinkParamI;
    }

    action oscHyperlinkD {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscHyperlinkParamId;
    }

    action oscHyperlinkEqual {
        ragelAppendString(fc, parser.maxOscBytes);
        if (!parser.oscHyperlinkHasId) {
            parser.oscHyperlinkIdOffset = parser.scratch.used();
        }
        fgoto oscHyperlinkIdValue;
    }

    action oscHyperlinkColon {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscHyperlinkParamStart;
    }

    action oscHyperlinkIdColon {
        if (!parser.oscHyperlinkHasId) {
            parser.oscHyperlinkIdLength = parser.scratch.used() - parser.oscHyperlinkIdOffset;
            parser.oscHyperlinkHasId = true;
        }
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscHyperlinkParamStart;
    }

    action oscHyperlinkUri {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscHyperlinkUriOffset = parser.scratch.used();
        fgoto oscHyperlinkUri;
    }

    action oscHyperlinkIdUri {
        if (!parser.oscHyperlinkHasId) {
            parser.oscHyperlinkIdLength = parser.scratch.used() - parser.oscHyperlinkIdOffset;
            parser.oscHyperlinkHasId = true;
        }
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscHyperlinkUriOffset = parser.scratch.used();
        fgoto oscHyperlinkUri;
    }

    action oscHyperlinkMalformedSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            fgoto oscHyperlinkParamSkip;
        } else {
            ragelFinishOsc();
            fnext main;
            fbreak;
        }
    }

    action oscHyperlinkSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        } else {
            ragelFinishOsc();
            if (!parser.overflow) {
                const auto* data = (const u8*)(parser.scratch.data());
                iface.osc_HYPERLINK(
                    StringView(data + parser.oscHyperlinkIdOffset, parser.oscHyperlinkHasId ? parser.oscHyperlinkIdLength : 0),
                    parser.oscHyperlinkHasId,
                    StringView(data + parser.oscHyperlinkUriOffset, parser.scratch.used() - parser.oscHyperlinkUriOffset)
                );
            }
            fnext main;
            fbreak;
        }
    }

    action oscHyperlinkParamEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscHyperlinkParamSkip;
    }

    action oscHyperlinkIdEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscHyperlinkIdValue;
    }

    action oscHyperlinkUriEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscHyperlinkUri;
    }

    action oscProgressFour {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscProgressFour;
    }

    action oscProgressBeginState {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscProgressState;
    }

    action oscProgressNotifyData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
        fgoto oscProgressNotify;
    }

    action oscProgressStateDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscProgressStatePresent = true;
        if (parser.oscProgressState > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            parser.oscProgressValid = false;
        } else {
            parser.oscProgressState = parser.oscProgressState * 10 + fc - '0';
        }
    }

    action oscProgressBeginPercent {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscProgressValid = parser.oscProgressValid && parser.oscProgressStatePresent;
        fgoto oscProgressPercent;
    }

    action oscProgressPercentDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscProgressPercentPresent = true;
        if (parser.oscProgressPercent > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            parser.oscProgressValid = false;
        } else {
            parser.oscProgressPercent = parser.oscProgressPercent * 10 + fc - '0';
        }
    }

    action oscProgressDiscardData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
        fgoto oscProgressDiscard;
    }

    action oscProgressNotifySt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        } else {
            ragelFinishOsc();
            if (!parser.overflow) {
                iface.osc_NOTIFY(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscProgressSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscProgressValid = false;
            fgoto oscProgressDiscard;
        } else {
            ragelFinishOsc();
            if (!parser.overflow && parser.oscProgressValid && parser.oscProgressPercentPresent &&
                parser.oscProgressState <= 4 && parser.oscProgressPercent <= 100) {
                iface.osc_PROGRESS(parser.oscProgressState, parser.oscProgressPercent);
            }
            fnext main;
            fbreak;
        }
    }

    action oscProgressDiscardSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        } else {
            ragelFinishOsc();
            fnext main;
            fbreak;
        }
    }

    action oscProgressNotifyEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscProgressNotify;
    }

    action oscProgressDiscardEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscProgressDiscard;
    }

    action osc52Selector {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.osc52SelectorSeen = true;
        if (parser.osc52ReplySelector == 0 && (fc == 's' || fc == 'p' || fc == 'c')) {
            parser.osc52ReplySelector = fc;
        }
        if (fc == 'p' || (fc == 's' && !opts.osc52SelectClipboard)) {
            parser.osc52Primary = true;
        }
        if (fc == 'c' || (fc == 's' && opts.osc52SelectClipboard)) {
            parser.osc52Clipboard = true;
        }
    }

    action osc52BeginPayload {
        ragelAppendString(fc, parser.maxOscBytes);
        if (!parser.osc52SelectorSeen) {
            parser.osc52Primary = true;
            parser.osc52Clipboard = true;
        }
        parser.oscDecoded.reset();
        parser.oscBase64.reset();
        parser.osc52PayloadSeen = false;
        parser.osc52Query = false;
        fgoto osc52Payload;
    }

    action osc52Data {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            if (!parser.osc52PayloadSeen) {
                parser.osc52PayloadSeen = true;
                parser.osc52Query = fc == '?';
                if (!parser.osc52Query && !parser.overflow) {
                    parser.oscBase64.push(fc, parser.oscDecoded);
                }
            } else if (parser.osc52Query) {
                parser.osc52Query = false;
                parser.oscBase64.valid = false;
            } else if (!parser.overflow) {
                parser.oscBase64.push(fc, parser.oscDecoded);
            }
        }
    }

    action osc52MalformedSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            fgoto oscInvalid;
        } else {
            ragelFinishOsc();
            if (!parser.overflow) {
                iface.osc_CLIPBOARD_MALFORMED(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action osc52MalformedBell {
        ragelFinishOsc();
        if (!parser.overflow) {
            iface.osc_CLIPBOARD_MALFORMED(ragelOscPayload());
        }
        fnext main;
        fbreak;
    }

    action osc52St {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscTerminated = false;
            fgoto oscInvalid;
        } else {
            ragelFinishOsc();
            parser.oscTerminated = true;
        }
    }

    action osc52Bell {
        ragelFinishOsc();
        parser.oscTerminated = true;
    }

    action osc52Dispatch {
        if (parser.oscTerminated && !parser.overflow) {
            const StringView raw = ragelOscPayload();
            if (parser.osc52PayloadSeen && parser.osc52Query) {
                iface.osc_CLIPBOARD_QUERY(
                    raw, parser.osc52Primary, parser.osc52Clipboard, parser.osc52ReplySelector,
                    !parser.osc52SelectorSeen
                );
            } else {
                const bool valid = parser.oscBase64.finish(parser.oscDecoded);
                iface.osc_CLIPBOARD_WRITE(
                    raw, StringView(parser.oscDecoded), valid, parser.osc52Primary,
                    parser.osc52Clipboard
                );
            }
        }
    }

    action osc52SelectorEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscInvalid;
    }

    action osc52PayloadEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        parser.osc52PayloadSeen = true;
        parser.osc52Query = false;
        parser.oscBase64.valid = false;
        fgoto osc52Payload;
    }

    action oscNotificationKey {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscNotificationKey = fc;
        fgoto oscNotificationEqual;
    }

    action oscNotificationBeginValue {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscNotificationFieldOffset = parser.scratch.used();
        fgoto oscNotificationValue;
    }

    action oscNotificationValueData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            if (parser.oscNotificationKey == 'i' &&
                !((fc >= 'a' && fc <= 'z') || (fc >= 'A' && fc <= 'Z') ||
                  (fc >= '0' && fc <= '9') || fc == '_' || fc == '-' ||
                  fc == '+' || fc == '.')) {
                parser.oscNotificationValid = false;
            }
        }
    }

    action oscNotificationFinishField {
        const auto* data = (const u8*)(parser.scratch.data());
        const StringView value(
            data + parser.oscNotificationFieldOffset,
            parser.scratch.used() - parser.oscNotificationFieldOffset
        );
        if (parser.oscNotificationKey == 'i') {
            parser.oscNotificationIdOffset = parser.oscNotificationFieldOffset;
            parser.oscNotificationIdLength = value.length();
            if (value.length() > 256) {
                parser.oscNotificationValid = false;
            }
        } else if (parser.oscNotificationKey == 'p') {
            parser.oscNotificationQuery = value == StringView(u8"?");
            parser.oscNotificationClose = value == StringView(u8"close");
            parser.oscNotificationBody = value == StringView(u8"body");
            if (!parser.oscNotificationQuery && !parser.oscNotificationClose &&
                !parser.oscNotificationBody && value != StringView(u8"title")) {
                parser.oscNotificationValid = false;
            }
        } else if (parser.oscNotificationKey == 'e') {
            if (value == StringView(u8"0")) {
                parser.oscNotificationEncoded = false;
            } else if (value == StringView(u8"1")) {
                parser.oscNotificationEncoded = true;
            } else {
                parser.oscNotificationValid = false;
            }
        } else if (parser.oscNotificationKey == 'd') {
            if (value == StringView(u8"0")) {
                parser.oscNotificationFinal = false;
            } else if (value == StringView(u8"1")) {
                parser.oscNotificationFinal = true;
            } else {
                parser.oscNotificationValid = false;
            }
        }
    }

    action oscNotificationNextField {
        ragelAppendString(fc, parser.maxOscBytes);
        fgoto oscNotificationField;
    }

    action oscNotificationBeginPayload {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscNotificationPayloadOffset = parser.scratch.used();
        parser.oscNotificationPayloadBytes = 0;
        parser.oscDecoded.reset();
        parser.oscBase64.reset();
        if (parser.oscNotificationEncoded && parser.oscNotificationValid &&
            !parser.oscNotificationQuery && !parser.oscNotificationClose) {
            const auto* data = (const u8*)(parser.scratch.data());
            const StringView id(
                data + parser.oscNotificationIdOffset, parser.oscNotificationIdLength
            );
            parser.oscBase64 = iface.parserNotificationDecoder(id, parser.oscNotificationBody);
        }
        fgoto oscNotificationPayload;
    }

    action oscNotificationPayloadData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            ++parser.oscNotificationPayloadBytes;
            const u32 limit = parser.oscNotificationEncoded ? 4096 : 2048;
            if (parser.oscNotificationPayloadBytes > limit) {
                parser.oscNotificationValid = false;
            } else if (parser.oscNotificationEncoded && !parser.overflow) {
                parser.oscBase64.push(fc, parser.oscDecoded);
            }
        }
    }

    action oscNotificationInvalidData {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
        parser.oscNotificationValid = false;
        fgoto oscNotificationInvalid;
    }

    action oscNotificationSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscNotificationValid = false;
            parser.oscTerminated = false;
            fgoto oscNotificationInvalid;
        } else {
            ragelFinishOsc();
            parser.oscTerminated = true;
        }
    }

    action oscNotificationBell {
        ragelFinishOsc();
        parser.oscTerminated = true;
    }

    action oscNotificationDispatch {
        if (parser.oscTerminated && !parser.overflow && parser.oscNotificationValid) {
            const auto* data = (const u8*)(parser.scratch.data());
            const StringView id(
                data + parser.oscNotificationIdOffset, parser.oscNotificationIdLength
            );
            const StringView payload(
                data + parser.oscNotificationPayloadOffset,
                parser.scratch.used() - parser.oscNotificationPayloadOffset
            );
            if (parser.oscNotificationEncoded && parser.oscNotificationFinal) {
                parser.oscBase64.finish(parser.oscDecoded);
            }
            if (parser.oscNotificationQuery) {
                iface.osc_NOTIFICATION_CAPABILITIES(id);
            } else if (parser.oscNotificationClose) {
                iface.osc_NOTIFICATION_CLOSE(id);
            } else if (parser.oscNotificationBody) {
                iface.osc_NOTIFICATION_BODY(
                    id,
                    parser.oscNotificationEncoded ? StringView(parser.oscDecoded) : payload,
                    parser.oscBase64, parser.oscNotificationEncoded, parser.oscNotificationFinal
                );
            } else {
                iface.osc_NOTIFICATION_TITLE(
                    id,
                    parser.oscNotificationEncoded ? StringView(parser.oscDecoded) : payload,
                    parser.oscBase64, parser.oscNotificationEncoded, parser.oscNotificationFinal
                );
            }
        }
    }

    action oscNotificationInvalidSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        } else {
            ragelFinishOsc();
            fnext main;
            fbreak;
        }
    }

    action oscNotificationInvalidBell {
        ragelFinishOsc();
        fnext main;
        fbreak;
    }

    action oscNotificationPayloadEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        parser.oscNotificationPayloadBytes += 2;
        const u32 limit = parser.oscNotificationEncoded ? 4096 : 2048;
        if (parser.oscNotificationPayloadBytes > limit) {
            parser.oscNotificationValid = false;
        } else if (parser.oscNotificationEncoded && !parser.overflow) {
            parser.oscBase64.push('\x1b', parser.oscDecoded);
            parser.oscBase64.push(fc, parser.oscDecoded);
        }
        fgoto oscNotificationPayload;
    }

    action oscNotificationInvalidEscaped {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscNotificationInvalid;
    }

    action oscColorByte {
        ragelAppendString(fc, parser.maxOscBytes);
    }

    action oscColorQuery {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscColorQuery = true;
    }

    action oscColorHashDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        const u8 digit =
            fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10;
        parser.oscColorHex = (parser.oscColorHex << 4) | digit;
        ++parser.oscColorDigits;
    }

    action oscColorHashDone {
        const u8 width = parser.oscColorDigits / 3;
        const u8 bits = width * 4;
        const u32 mask = (1u << bits) - 1;
        const u32 red = (parser.oscColorHex >> (bits * 2)) & mask;
        const u32 green = (parser.oscColorHex >> bits) & mask;
        const u32 blue = parser.oscColorHex & mask;
        const u8 shift = width < 2 ? 4 : (width - 2) * 4;
        parser.oscColor.red =
            width < 2 ? (u8)(red << shift) : (u8)(red >> shift);
        parser.oscColor.green =
            width < 2 ? (u8)(green << shift) : (u8)(green >> shift);
        parser.oscColor.blue =
            width < 2 ? (u8)(blue << shift) : (u8)(blue >> shift);
    }

    action oscColorRgbComponentStart {
        parser.oscColorHex = 0;
        parser.oscColorDigits = 0;
    }

    action oscColorRgbComponentDone {
        const u32 maximum = (1u << (4 * parser.oscColorDigits)) - 1;
        const u8 value =
            (u8)((parser.oscColorHex * 255 + maximum / 2) / maximum);
        if (parser.oscColorComponent == 0) {
            parser.oscColor.red = value;
        } else if (parser.oscColorComponent == 1) {
            parser.oscColor.green = value;
        } else {
            parser.oscColor.blue = value;
        }
    }

    action oscColorNextComponent {
        ragelAppendString(fc, parser.maxOscBytes);
        ++parser.oscColorComponent;
    }

    action oscColorNumberStart {
        parser.oscColorMantissa = 0.0;
        parser.oscColorFraction = 0.1;
        parser.oscColorExponent = 0;
        parser.oscColorNegative = false;
        parser.oscColorExponentNegative = false;
    }

    action oscColorNegative {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscColorNegative = true;
    }

    action oscColorNumberSign {
        ragelAppendString(fc, parser.maxOscBytes);
    }

    action oscColorIntegerDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscColorMantissa =
            parser.oscColorMantissa * 10.0 + (double)(fc - '0');
    }

    action oscColorPoint {
        ragelAppendString(fc, parser.maxOscBytes);
    }

    action oscColorFractionDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscColorMantissa += (double)(fc - '0') * parser.oscColorFraction;
        parser.oscColorFraction *= 0.1;
    }

    action oscColorExponentStart {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscColorExponent = 0;
        parser.oscColorExponentNegative = false;
    }

    action oscColorExponentNegative {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscColorExponentNegative = true;
    }

    action oscColorExponentSign {
        ragelAppendString(fc, parser.maxOscBytes);
    }

    action oscColorExponentDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        if (parser.oscColorExponent < 10000) {
            parser.oscColorExponent = parser.oscColorExponent * 10 + fc - '0';
            if (parser.oscColorExponent > 10000) {
                parser.oscColorExponent = 10000;
            }
        }
    }

    action oscColorNumberDone {
        parser.oscColorValid =
            finishColorNumber(
                parser.oscColorMantissa,
                parser.oscColorNegative,
                parser.oscColorExponent,
                parser.oscColorExponentNegative,
                parser.oscColorComponents[parser.oscColorComponent]
            ) &&
            parser.oscColorValid;
    }

    action oscColorRgbIntensity {
        parser.oscColorValid = parser.oscColorValid && colorFromRgbIntensity(
            parser.oscColorComponents[0], parser.oscColorComponents[1],
            parser.oscColorComponents[2], parser.oscColor
        );
    }

    action oscColorCieXyz {
        parser.oscColorValid = parser.oscColorValid && colorFromCieXyz(
            parser.oscColorComponents[0], parser.oscColorComponents[1],
            parser.oscColorComponents[2], parser.oscColor
        );
    }

    action oscColorCieUvY {
        parser.oscColorValid = parser.oscColorValid && colorFromCieUvY(
            parser.oscColorComponents[0], parser.oscColorComponents[1],
            parser.oscColorComponents[2], parser.oscColor
        );
    }

    action oscColorCieXyY {
        parser.oscColorValid = parser.oscColorValid && colorFromCieXyY(
            parser.oscColorComponents[0], parser.oscColorComponents[1],
            parser.oscColorComponents[2], parser.oscColor
        );
    }

    action oscColorCieLab {
        parser.oscColorValid = parser.oscColorValid && colorFromCieLab(
            parser.oscColorComponents[0], parser.oscColorComponents[1],
            parser.oscColorComponents[2], parser.oscColor
        );
    }

    action oscColorCieLuv {
        parser.oscColorValid = parser.oscColorValid && colorFromCieLuv(
            parser.oscColorComponents[0], parser.oscColorComponents[1],
            parser.oscColorComponents[2], parser.oscColor
        );
    }

    action oscColorTekHvc {
        parser.oscColorValid = parser.oscColorValid && colorFromTekHvc(
            parser.oscColorComponents[0], parser.oscColorComponents[1],
            parser.oscColorComponents[2], parser.oscColor
        );
    }

    action oscDynamicColorNext {
        ragelAppendString(fc, parser.maxOscBytes);
        ++parser.oscCommand;
        if (parser.oscCommand > 19) {
            fgoto oscInvalid;
        }
        resetOscColor();
        fgoto oscDynamicColor;
    }

    action oscDynamicColorCommit {
        if (!parser.overflow && parser.oscColorValid) {
            if (parser.oscCommand == 10) {
                iface.osc_DEFAULT_FOREGROUND(parser.oscColor, parser.oscColorQuery);
            } else if (parser.oscCommand == 11) {
                iface.osc_DEFAULT_BACKGROUND(parser.oscColor, parser.oscColorQuery);
            } else if (parser.oscCommand == 12) {
                iface.osc_CURSOR_COLOR(parser.oscColor, parser.oscColorQuery);
            } else if (parser.oscCommand == 17) {
                iface.osc_SELECTION_BACKGROUND(parser.oscColor, parser.oscColorQuery);
            } else if (parser.oscCommand == 19) {
                iface.osc_SELECTION_FOREGROUND(parser.oscColor, parser.oscColorQuery);
            }
        }
    }

    action oscDynamicColorSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscTerminated = false;
            fgoto oscInvalid;
        } else {
            ragelFinishOsc();
            parser.oscTerminated = true;
        }
    }

    action oscDynamicColorBell {
        ragelFinishOsc();
        parser.oscTerminated = true;
    }

    action oscDynamicColorInvalid {
        fhold;
        fgoto oscDynamicColorDiscard;
    }

    action oscDynamicColorDiscardNext {
        ragelAppendString(fc, parser.maxOscBytes);
        ++parser.oscCommand;
        if (parser.oscCommand > 19) {
            fgoto oscInvalid;
        } else {
            resetOscColor();
            fgoto oscDynamicColor;
        }
    }

    action oscDynamicColorEscapedInvalid {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscDynamicColorDiscard;
    }

    action oscIndexedColorIndexDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscFieldPresent = true;
        if (parser.oscFieldNumber > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            parser.oscFieldNumeric = false;
        } else {
            parser.oscFieldNumber = parser.oscFieldNumber * 10 + fc - '0';
        }
    }

    action oscIndexedColorIndexInvalid {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
        }
        parser.oscFieldNumeric = false;
    }

    action oscIndexedColorIndexEscapedInvalid {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        parser.oscFieldNumeric = false;
        fgoto oscIndexedColorIndex;
    }

    action oscIndexedColorBegin {
        ragelAppendString(fc, parser.maxOscBytes);
        if (!parser.oscFieldPresent || !parser.oscFieldNumeric) {
            fgoto oscIndexedColorDiscard;
        }
        resetOscColor();
        fgoto oscIndexedColor;
    }

    action oscIndexedColorCommit {
        if (!parser.overflow && parser.oscColorValid) {
            if (parser.oscCommand == 4) {
                iface.osc_PALETTE(
                    parser.oscFieldNumber, parser.oscColor, parser.oscColorQuery
                );
            } else {
                iface.osc_SPECIAL_COLOR(
                    parser.oscFieldNumber, parser.oscColor, parser.oscColorQuery
                );
            }
        }
    }

    action oscIndexedColorNext {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscFieldNumber = 0;
        parser.oscFieldNumeric = true;
        parser.oscFieldPresent = false;
        fgoto oscIndexedColorIndex;
    }

    action oscIndexedColorSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscTerminated = false;
            fgoto oscInvalid;
        } else {
            ragelFinishOsc();
            parser.oscTerminated = true;
        }
    }

    action oscIndexedColorBell {
        ragelFinishOsc();
        parser.oscTerminated = true;
    }

    action oscIndexedColorInvalid {
        fhold;
        fgoto oscIndexedColorDiscard;
    }

    action oscIndexedColorDiscardNext {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscFieldNumber = 0;
        parser.oscFieldNumeric = true;
        parser.oscFieldPresent = false;
        fgoto oscIndexedColorIndex;
    }

    action oscIndexedColorEscapedInvalid {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        fgoto oscIndexedColorDiscard;
    }

    action oscNumericDigit {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscFieldPresent = true;
        if (parser.oscFieldNumber > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            parser.oscFieldNumeric = false;
        } else {
            parser.oscFieldNumber = parser.oscFieldNumber * 10 + fc - '0';
        }
    }

    action oscNumericInvalid {
        consumeStringUtf8Byte(fc);
        if (!executeC0(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscFieldPresent = true;
            parser.oscFieldNumeric = false;
        }
    }

    action oscNumericField {
        const bool valid = parser.oscFieldPresent && parser.oscFieldNumeric;
        if (parser.oscCommand == 6 || parser.oscCommand == 106) {
            if (!parser.oscFieldHaveFirst) {
                parser.oscFieldFirst = parser.oscFieldNumber;
                parser.oscFieldFirstValid = valid;
                parser.oscFieldHaveFirst = true;
            } else {
                if (!parser.overflow && parser.oscFieldFirstValid && valid) {
                    iface.osc_SPECIAL_COLOR_MODE(
                        parser.oscFieldFirst, parser.oscFieldNumber
                    );
                }
                parser.oscFieldHaveFirst = false;
            }
        } else if (!parser.overflow && valid) {
            if (parser.oscCommand == 104) {
                iface.osc_RESET_PALETTE(parser.oscFieldNumber);
            } else {
                iface.osc_RESET_SPECIAL_COLOR(parser.oscFieldNumber);
            }
        }
    }

    action oscNumericFinalField {
        if (parser.oscFieldPresent) {
            const bool valid = parser.oscFieldNumeric;
            if (parser.oscCommand == 6 || parser.oscCommand == 106) {
                if (!parser.oscFieldHaveFirst) {
                    parser.oscFieldFirst = parser.oscFieldNumber;
                    parser.oscFieldFirstValid = valid;
                    parser.oscFieldHaveFirst = true;
                } else if (!parser.overflow &&
                           parser.oscFieldFirstValid && valid) {
                    iface.osc_SPECIAL_COLOR_MODE(
                        parser.oscFieldFirst, parser.oscFieldNumber
                    );
                }
            } else if (!parser.overflow && valid) {
                if (parser.oscCommand == 104) {
                    iface.osc_RESET_PALETTE(parser.oscFieldNumber);
                } else {
                    iface.osc_RESET_SPECIAL_COLOR(parser.oscFieldNumber);
                }
            }
        } else if (!parser.overflow &&
                   parser.scratch.used() == parser.oscPayloadOffset) {
            if (parser.oscCommand == 104) {
                iface.osc_RESET_PALETTE();
            } else if (parser.oscCommand == 105) {
                iface.osc_RESET_SPECIAL_COLOR();
            }
        }
    }

    action oscNumericSeparator {
        ragelAppendString(fc, parser.maxOscBytes);
        parser.oscFieldNumber = 0;
        parser.oscFieldNumeric = true;
        parser.oscFieldPresent = false;
    }

    action oscNumericSt {
        if (consumeStringUtf8Byte(fc)) {
            ragelAppendString(fc, parser.maxOscBytes);
            parser.oscFieldPresent = true;
            parser.oscFieldNumeric = false;
            fgoto oscNumericFields;
        } else {
            ragelFinishOsc();
            parser.oscTerminated = true;
        }
    }

    action oscNumericBell {
        ragelFinishOsc();
        parser.oscTerminated = true;
    }

    action oscNumericEscapedInvalid {
        ragelAppendEscapedString(fc, parser.maxOscBytes);
        parser.oscFieldPresent = true;
        parser.oscFieldNumeric = false;
        fgoto oscNumericFields;
    }

    action ignoredData {
        if (fc >= 0x20 && fc < 0x7f) {
            const size_t count = printableAsciiPrefix(p, pe - p);
            parser.stringUtf8Remaining = 0;
            if constexpr (traced) {
                parserTrace->stringData(p, count);
            }
            p += count - 1;
        } else {
            consumeStringUtf8Byte(fc);
            executeC0(fc);
            if constexpr (traced) {
                parserTrace->stringData(&fc, 1);
            }
        }
    }

    action ignoredSt {
        if (consumeStringUtf8Byte(fc)) {
            if constexpr (traced) {
                parserTrace->stringData(&fc, 1);
            }
        } else {
            parser.stringUtf8Remaining = 0;
            parser.stringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringEnd();
            }
            fnext main;
            fbreak;
        }
    }

    action ignoredEscape {
        fgoto stringEscape;
    }

    action ignoredEscapedData {
        if constexpr (traced) {
            const u8 bytes[] = {'\x1b', (u8)(fc)};
            parserTrace->stringData(bytes, sizeof(bytes));
        }
        fgoto string;
    }

    action stringRestartDcs {
        if (!ragelStringContinuation(fc)) {
            ragelBeginDcs();
            fgoto dcsEntry;
        }
    }

    action stringRestartOsc {
        if (!ragelStringContinuation(fc)) {
            ragelBeginOsc();
            fgoto oscCommand;
        }
    }

    action stringRestartSos {
        if (!ragelStringContinuation(fc)) {
            ragelBeginString(VtermTraceString::Sos, false);
            fgoto string;
        }
    }

    action stringRestartPm {
        if (!ragelStringContinuation(fc)) {
            ragelBeginString(VtermTraceString::Pm, false);
            fgoto string;
        }
    }

    action stringRestartApc {
        if (!ragelStringContinuation(fc)) {
            ragelBeginString(VtermTraceString::Apc, false);
            fgoto string;
        }
    }

    action stringRestartCsi {
        if (!ragelStringContinuation(fc)) {
            beginCsi();
            fgoto csiEntry;
        }
    }

    action stringControlSpa {
        if (!ragelStringContinuation(fc)) {
            parser.stringUtf8Remaining = 0;
            parser.stringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringCancel();
                parserTrace->control(fc);
            }
            iface.esc_SPA();
            fnext main;
            fbreak;
        }
    }

    action stringControlEpa {
        if (!ragelStringContinuation(fc)) {
            parser.stringUtf8Remaining = 0;
            parser.stringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringCancel();
                parserTrace->control(fc);
            }
            iface.esc_EPA();
            fnext main;
            fbreak;
        }
    }

    action stringControlDa {
        if (!ragelStringContinuation(fc)) {
            parser.stringUtf8Remaining = 0;
            parser.stringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringCancel();
            }
            iface.csi_priDA();
            fnext main;
            fbreak;
        }
    }

    csiPlainKnown = [@ABCDEFGHIJKLMPSTXZ`abcdefghijklmnqrstu];
    csiPlainFinal = (
        'T' @csiTrace @{ if (parser.parameterCount == 5 && iface.parserHighlightMouseTracking()) { iface.csi_XTHIMOUSE(parameter(0), parameter(1), parameter(2), parameter(3), parameter(4)); } else { iface.csi_SD(countParameter(0)); } } |
        'A' @csiTrace @{ iface.csi_CUU(countParameter(0)); } |
        'B' @csiTrace @{ iface.csi_CUD(countParameter(0)); } |
        'C' @csiTrace @{ iface.csi_CUF(countParameter(0)); } |
        'D' @csiTrace @{ iface.csi_CUB(countParameter(0)); } |
        'E' @csiTrace @{ iface.csi_CNL(countParameter(0)); } |
        'F' @csiTrace @{ iface.csi_CPL(countParameter(0)); } |
        'G' @csiTrace @{ iface.csi_CHA(countParameter(0)); } |
        ('H' | 'f') @csiTrace @{ iface.csi_CUP(countParameter(0), countParameter(1)); } |
        'I' @csiTrace @{ iface.csi_CHT(countParameter(0)); } |
        'J' @csiTrace @{ dispatchEraseDisplay(false); } |
        'K' @csiTrace @{ dispatchEraseLine(false); } |
        'L' @csiTrace @{ iface.csi_IL(countParameter(0)); } |
        'M' @csiTrace @{ iface.csi_DL(countParameter(0)); } |
        'P' @csiTrace @{ iface.csi_DCH(countParameter(0)); } |
        'S' @csiTrace @{ iface.csi_SU(countParameter(0)); } |
        'X' @csiTrace @{ iface.csi_ECH(countParameter(0)); } |
        'Z' @csiTrace @{ iface.csi_CBT(countParameter(0)); } |
        '@' @csiTrace @{ iface.csi_ICH(parser.parameters[0] ? parser.parameters[0] : 1); } |
        '`' @csiTrace @{ iface.csi_HPA(countParameter(0)); } |
        'a' @csiTrace @{ iface.csi_HPR(countParameter(0)); } |
        'b' @csiTrace @{ iface.csi_REP(countParameter(0)); } |
        'c' @csiTrace @{ iface.csi_priDA(); } |
        'd' @csiTrace @{ iface.csi_VPA(countParameter(0)); } |
        'e' @csiTrace @{ iface.csi_VPR(countParameter(0)); } |
        'g' @csiTrace @{ dispatchTabClear(); } |
        'h' @csiTrace @{ dispatchStandardModes(true); } |
        'i' @csiTrace @{ dispatchMediaCopy(false); } |
        'j' @csiTrace @{ iface.csi_CUB(countParameter(0)); } |
        'k' @csiTrace @{ iface.csi_CUU(countParameter(0)); } |
        'l' @csiTrace @{ dispatchStandardModes(false); } |
        'm' @csiTrace @csiSgr |
        'n' @csiTrace @{ dispatchDsr(false); } |
        'q' @csiTrace @{ dispatchDecll(); } |
        'r' @csiTrace @{ iface.csi_STBM(parameter(0), parameter(1), parser.parameterCount <= 2); } |
        's' @csiTrace @{ dispatchScoscSlrm(); } |
        't' @csiTrace @{ dispatchWindowOps(); } |
        'u' @csiTrace @{ iface.csi_SCORC(); } |
        (0x40..0x7e - csiPlainKnown) @csiTrace
    ) @csiDone;

    csiGreaterKnown = [Tcmqtu];
    csiGreaterFinal = (
        'T' @csiTrace @{ dispatchTitleMode(false); } |
        'c' @csiTrace @{ iface.csi_secDA(); } |
        'm' @csiTrace @{ dispatchXtmodkeys(); } |
        'q' @csiTrace @{ iface.csi_XTVERSION(); } |
        't' @csiTrace @{ dispatchTitleMode(true); } |
        'u' @csiTrace @{ iface.csi_kittyKeyboardPush(parameter(0)); } |
        (0x40..0x7e - csiGreaterKnown) @csiTrace
    ) @csiDone;

    csiLessFinal = (
        'u' @csiTrace @{ iface.csi_kittyKeyboardPop(countParameter(0)); } |
        (0x40..0x7e - 'u') @csiTrace
    ) @csiDone;

    csiEqualKnown = [cu];
    csiEqualFinal = (
        'c' @csiTrace @{ iface.csi_terDA(); } |
        'u' @csiTrace @{ dispatchKittyKeyboardSet(); } |
        (0x40..0x7e - csiEqualKnown) @csiTrace
    ) @csiDone;

    csiQuestionKnown = [JKhilmnrsu];
    csiQuestionFinal = (
        'J' @csiTrace @{ dispatchEraseDisplay(true); } |
        'K' @csiTrace @{ dispatchEraseLine(true); } |
        'h' @csiTrace @{ dispatchPrivateModes(true); } |
        'i' @csiTrace @{ dispatchMediaCopy(true); } |
        'l' @csiTrace @{ dispatchPrivateModes(false); } |
        'm' @csiTrace @{ dispatchXtqmodkeys(); } |
        'n' @csiTrace @{ dispatchDsr(true); } |
        'r' @csiTrace @{ dispatchPrivateRestore(); } |
        's' @csiTrace @{ dispatchPrivateSave(); } |
        'u' @csiTrace @{ iface.csi_kittyKeyboardQuery(); } |
        (0x40..0x7e - csiQuestionKnown) @csiTrace
    ) @csiDone;

    csiBangFinal = (
        'p' @csiTrace @{ iface.csi_DECSTR(); } |
        (0x40..0x7e - 'p') @csiTrace
    ) @csiDone;

    csiQuoteKnown = [pq];
    csiQuoteFinal = (
        'p' @csiTrace @{ dispatchDecscl(); } |
        'q' @csiTrace @{ iface.setDecProtection(parameter(0) == 1); } |
        (0x40..0x7e - csiQuoteKnown) @csiTrace
    ) @csiDone;

    csiSpaceKnown = [@Aq];
    csiSpaceFinal = (
        '@' @csiTrace @{ iface.csi_ecma48_SL(countParameter(0)); } |
        'A' @csiTrace @{ iface.csi_ecma48_SR(countParameter(0)); } |
        'q' @csiTrace @{ dispatchCursorStyle(); } |
        (0x40..0x7e - csiSpaceKnown) @csiTrace
    ) @csiDone;

    csiApostropheKnown = [wz-~];
    csiApostropheFinal = (
        'w' @csiTrace @{ iface.csi_DECEFR(parameter(0), parameter(1), parameter(2), parameter(3)); } |
        'z' @csiTrace @{ dispatchLocatorReporting(); } |
        '{' @csiTrace @{ dispatchDecsle(); } |
        '|' @csiTrace @{ iface.csi_DECRQLP(); } |
        '}' @csiTrace @{ iface.csi_DECIC(countParameter(0)); } |
        '~' @csiTrace @{ iface.csi_DECDC(countParameter(0)); } |
        (0x40..0x7e - csiApostropheKnown) @csiTrace
    ) @csiDone;

    csiDollarKnown = [prtvxz{];
    csiDollarFinal = (
        'p' @csiTrace @{ dispatchModeReport(false); } |
        'r' @csiTrace @{ dispatchDeccara(false); } |
        't' @csiTrace @{ dispatchDeccara(true); } |
        'v' @csiTrace @{ dispatchDeccra(); } |
        'x' @csiTrace @{ dispatchDecfra(); } |
        'z' @csiTrace @{ dispatchDecera(false); } |
        '{' @csiTrace @{ dispatchDecera(true); } |
        (0x40..0x7e - csiDollarKnown) @csiTrace
    ) @csiDone;

    csiStarFinal = (
        'y' @csiTrace @{ dispatchDecrqcra(); } |
        (0x40..0x7e - 'y') @csiTrace
    ) @csiDone;

    csiQuestionDollarFinal = (
        'p' @csiTrace @{ dispatchModeReport(true); } |
        (0x40..0x7e - 'p') @csiTrace
    ) @csiDone;

    csiUnknownFinal = 0x40..0x7e @csiTrace @csiDone;

    printer := (
        0x1b @{ fnext printerEscape; fbreak; } |
        0x9b @{ fnext printerCsi; fbreak; } |
        any @printerByte
    )*;

    printerEscape := (
        '[' @{ fnext printerEscapeBracket; fbreak; } |
        0x1b @printerEscapePrefix
            @{ fnext printerEscape; fbreak; } |
        0x9b @printerEscapePrefix
            @{ fnext printerCsi; fbreak; } |
        (any - ('[' | 0x1b | 0x9b))
            @printerEscapePrefix @printerByte
            @{ fnext printer; fbreak; }
    )*;

    printerEscapeBracket := (
        '4' @{ fnext printerEscapeBracket4; fbreak; } |
        0x1b @printerEscapeBracketPrefix
            @{ fnext printerEscape; fbreak; } |
        0x9b @printerEscapeBracketPrefix
            @{ fnext printerCsi; fbreak; } |
        (any - ('4' | 0x1b | 0x9b))
            @printerEscapeBracketPrefix @printerByte
            @{ fnext printer; fbreak; }
    )*;

    printerEscapeBracket4 := (
        'i' @printerEnd |
        0x1b @printerEscapeBracket4Prefix
            @{ fnext printerEscape; fbreak; } |
        0x9b @printerEscapeBracket4Prefix
            @{ fnext printerCsi; fbreak; } |
        (any - ('i' | 0x1b | 0x9b))
            @printerEscapeBracket4Prefix @printerByte
            @{ fnext printer; fbreak; }
    )*;

    printerCsi := (
        '4' @{ fnext printerCsi4; fbreak; } |
        0x1b @printerCsiPrefix
            @{ fnext printerEscape; fbreak; } |
        0x9b @printerCsiPrefix
            @{ fnext printerCsi; fbreak; } |
        (any - ('4' | 0x1b | 0x9b))
            @printerCsiPrefix @printerByte
            @{ fnext printer; fbreak; }
    )*;

    printerCsi4 := (
        'i' @printerEnd |
        0x1b @printerCsi4Prefix
            @{ fnext printerEscape; fbreak; } |
        0x9b @printerCsi4Prefix
            @{ fnext printerCsi; fbreak; } |
        (any - ('i' | 0x1b | 0x9b))
            @printerCsi4Prefix @printerByte
            @{ fnext printer; fbreak; }
    )*;

    cancel = (0x18 | 0x1a) @cancel;
    restartEscape = 0x1b @beginEscape;
    sequenceC0 = (0x00..0x17 | 0x19 | 0x1c..0x1f) @sequenceC0;
    highToGround = 0xa0..0xff @highToGround;

    c1Dispatch = (
        0x84 @c1Ind |
        0x85 @c1Nel |
        0x88 @c1Hts |
        0x8d @c1Ri |
        0x8e @c1Ss2 |
        0x8f @c1Ss3 |
        0x90 @beginDcs |
        0x96 @c1Spa |
        0x97 @c1Epa |
        0x98 @beginSos |
        0x9a @c1Da |
        0x9b @beginCsi |
        0x9c @c1St |
        0x9d @beginOsc |
        0x9e @beginPm |
        0x9f @beginApc
    );

    stringC1 = (
        0x90 @stringRestartDcs |
        0x96 @stringControlSpa |
        0x97 @stringControlEpa |
        0x98 @stringRestartSos |
        0x9a @stringControlDa |
        0x9b @stringRestartCsi |
        0x9d @stringRestartOsc |
        0x9e @stringRestartPm |
        0x9f @stringRestartApc
    );

    oscColorGap = (
        cancel |
        stringC1 |
        0x7f |
        sequenceC0
    );

    oscColorA = [aA] @oscColorByte oscColorGap*;
    oscColorB = [bB] @oscColorByte oscColorGap*;
    oscColorC = [cC] @oscColorByte oscColorGap*;
    oscColorE = [eE] @oscColorByte oscColorGap*;
    oscColorG = [gG] @oscColorByte oscColorGap*;
    oscColorH = [hH] @oscColorByte oscColorGap*;
    oscColorI = [iI] @oscColorByte oscColorGap*;
    oscColorK = [kK] @oscColorByte oscColorGap*;
    oscColorL = [lL] @oscColorByte oscColorGap*;
    oscColorR = [rR] @oscColorByte oscColorGap*;
    oscColorT = [tT] @oscColorByte oscColorGap*;
    oscColorU = [uU] @oscColorByte oscColorGap*;
    oscColorV = [vV] @oscColorByte oscColorGap*;
    oscColorX = [xX] @oscColorByte oscColorGap*;
    oscColorY = [yY] @oscColorByte oscColorGap*;
    oscColorZ = [zZ] @oscColorByte oscColorGap*;
    oscColorColon = ':' @oscColorByte;

    oscColorHexDigit =
        oscColorGap* xdigit @oscColorHashDigit;

    oscColorIntegerDigit =
        oscColorGap* digit @oscColorIntegerDigit;

    oscColorFractionDigit =
        oscColorGap* digit @oscColorFractionDigit;

    oscColorExponentDigit =
        oscColorGap* digit @oscColorExponentDigit;

    oscColorNumber = (
        oscColorGap*
        (
            '+' @oscColorNumberSign |
            '-' @oscColorNegative
        )?
        (
            oscColorIntegerDigit+
            (
                oscColorGap* '.' @oscColorPoint
                oscColorFractionDigit*
            )? |
            oscColorGap* '.' @oscColorPoint
            oscColorFractionDigit+
        )
        (
            oscColorGap* [eE] @oscColorExponentStart
            (
                oscColorGap* '+' @oscColorExponentSign |
                oscColorGap* '-' @oscColorExponentNegative
            )?
            oscColorExponentDigit+
        )?
    ) >oscColorNumberStart %oscColorNumberDone;

    oscColorSlash =
        oscColorGap* '/' @oscColorNextComponent;

    oscColorTriple =
        oscColorNumber
        oscColorSlash
        oscColorNumber
        oscColorSlash
        oscColorNumber;

    oscColorHash = (
        '#' @oscColorByte
        (
            oscColorHexDigit{3} |
            oscColorHexDigit{6} |
            oscColorHexDigit{9} |
            oscColorHexDigit{12}
        )
    ) %oscColorHashDone;

    oscColorRgbComponent = (
        oscColorHexDigit{1,4}
    ) >oscColorRgbComponentStart %oscColorRgbComponentDone;

    oscColorRgb = (
        oscColorR oscColorG oscColorB oscColorColon
        oscColorRgbComponent
        oscColorSlash
        oscColorRgbComponent
        oscColorSlash
        oscColorRgbComponent
    );

    oscColorRgbIntensity = (
        oscColorR oscColorG oscColorB oscColorI oscColorColon
        oscColorTriple
    ) %oscColorRgbIntensity;

    oscColorCieXyz = (
        oscColorC oscColorI oscColorE
        oscColorX oscColorY oscColorZ oscColorColon
        oscColorTriple
    ) %oscColorCieXyz;

    oscColorCieUvY = (
        oscColorC oscColorI oscColorE
        oscColorU oscColorV oscColorY oscColorColon
        oscColorTriple
    ) %oscColorCieUvY;

    oscColorCieXyY = (
        oscColorC oscColorI oscColorE
        oscColorX oscColorY oscColorY oscColorColon
        oscColorTriple
    ) %oscColorCieXyY;

    oscColorCieLab = (
        oscColorC oscColorI oscColorE
        oscColorL oscColorA oscColorB oscColorColon
        oscColorTriple
    ) %oscColorCieLab;

    oscColorCieLuv = (
        oscColorC oscColorI oscColorE
        oscColorL oscColorU oscColorV oscColorColon
        oscColorTriple
    ) %oscColorCieLuv;

    oscColorTekHvc = (
        oscColorT oscColorE oscColorK
        oscColorH oscColorV oscColorC oscColorColon
        oscColorTriple
    ) %oscColorTekHvc;

    oscColorValue = (
        oscColorGap*
        (
            '?' @oscColorQuery |
            oscColorHash |
            oscColorRgb |
            oscColorRgbIntensity |
            oscColorCieXyz |
            oscColorCieUvY |
            oscColorCieXyY |
            oscColorCieLab |
            oscColorCieLuv |
            oscColorTekHvc
        )
        oscColorGap*
    );

    main := (
        0x00 @groundIgnored |
        0x01..0x06 @groundIgnored |
        0x07 @groundBell |
        0x08 @groundBackspace |
        0x09 @groundTab |
        0x0a..0x0c @groundLineFeed |
        0x0d @groundCarriageReturn |
        0x0e @groundShiftOut |
        0x0f @groundShiftIn |
        0x10..0x17 @groundIgnored |
        0x18 @groundIgnored |
        0x19 @groundIgnored |
        0x1a @groundIgnored |
        0x1b @beginEscape |
        0x1c..0x1f @groundIgnored |
        0x20..0x7e @groundAscii |
        0x7f @groundDone |
        0x80..0x83 @groundHigh |
        0x84 @groundC1Ind |
        0x85 @groundC1Nel |
        0x86..0x87 @groundHigh |
        0x88 @groundC1Hts |
        0x89..0x8c @groundHigh |
        0x8d @groundC1Ri |
        0x8e @groundC1Ss2 |
        0x8f @groundC1Ss3 |
        0x90 @groundC1Dcs |
        0x91..0x95 @groundHigh |
        0x96 @groundC1Spa |
        0x97 @groundC1Epa |
        0x98 @groundC1Sos |
        0x99 @groundHigh |
        0x9a @groundC1Da |
        0x9b @groundC1Csi |
        0x9c @groundC1St |
        0x9d @groundC1Osc |
        0x9e @groundC1Pm |
        0x9f @groundC1Apc |
        0xa0..0xff @groundHigh
    )*;

    escape := (
        cancel |
        0x1b @repeatEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        0x00..0x17 @escapeC0 |
        0x19 @escapeC0 |
        0x1c..0x1f @escapeC0 |
        ' ' @escapeSpace |
        '#' @escapeHash |
        '%' @escapePercent |
        ('(' | ',' | '$') @charsetG0 |
        ')' @charsetG1 |
        '*' @charsetG2 |
        '+' @charsetG3 |
        '-' @charsetG1_96 |
        '.' @charsetG2_96 |
        '/' @charsetG3_96 |
        '[' @beginCsi |
        ']' @beginOsc |
        'X' @beginSos |
        '^' @beginPm |
        '_' @beginApc |
        'P' @beginDcs |
        '\\' @escapeStringTerminator |
        'D' @escapeInd |
        'M' @escapeRi |
        'E' @escapeNel |
        'H' @escapeHts |
        'N' @escapeSs2 |
        'O' @escapeSs3 |
        'V' @escapeSpa |
        'W' @escapeEpa |
        'Z' @escapeDa |
        'c' @escapeRis |
        '6' @escapeBi |
        '7' @escapeDecsc |
        '8' @escapeDecrc |
        '9' @escapeFi |
        '=' @escapeAppKeypad |
        '>' @escapeNormalKeypad |
        '<' @escapeAnsi |
        '~' @escapeLs1r |
        'n' @escapeLs2 |
        '}' @escapeLs2r |
        'o' @escapeLs3 |
        '|' @escapeLs3r |
        0x20..0x2f @escapeIntermediate |
        0x30..0x7e @escapeFinal |
        0x80..0x9f @escapeFinal
    )*;

    escapeIntermediate := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        sequenceC0 |
        0x20..0x2f @intermediateByte |
        0x30..0x7e @intermediateFinal |
        0x80..0x9f @escapeFinal
    )*;

    escapeSpace := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        0x20..0x2f @specialIntermediate |
        'F' @specialFinal @{ if (iface.parserCompatibilityLevel() >= CompatibilityLevel::VT200) { iface.parserSet8BitControls(false); } fnext main; fbreak; } |
        'G' @specialFinal @{ if (iface.parserCompatibilityLevel() >= CompatibilityLevel::VT200) { iface.parserSet8BitControls(true); } fnext main; fbreak; } |
        ('L' | 'M' | 'N') @specialFinal @{ fnext main; fbreak; } |
        any @specialFinal @vt52Unhandled
    )*;

    escapeHash := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        0x20..0x2f @specialIntermediate |
        '3' @specialFinal @{ iface.setLineAttribute(1); fnext main; fbreak; } |
        '4' @specialFinal @{ iface.setLineAttribute(2); fnext main; fbreak; } |
        '5' @specialFinal @{ iface.setLineAttribute(0); fnext main; fbreak; } |
        '6' @specialFinal @{ iface.setLineAttribute(3); fnext main; fbreak; } |
        '8' @specialFinal @{ iface.esch_DECALN(); fnext main; fbreak; } |
        any @specialFinal @vt52Unhandled
    )*;

    escapePercent := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        0x20..0x2f @specialIntermediate |
        '@' @specialFinal @{ iface.parserResetCharsets(true); fnext main; fbreak; } |
        'G' @specialFinal @{ iface.parserResetCharsets(false); fnext main; fbreak; } |
        any @specialFinal @vt52Unhandled
    )*;

    charsetKnown = [AB05<>4CRf9QKY`E6Z7H=32];

    selectCharset := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        0x00..0x2f @charsetModifier |
        'A' @{ iface.parserDesignateCharset(parser.scsIndex, parser.scs96 ? Charset::IsoLatin1 : Charset::IsoUK); } @charsetFinal |
        'B' @{ iface.parserDesignateCharset(parser.scsIndex, Charset::UTF8); } @charsetFinal |
        '0' @{ iface.parserDesignateCharset(parser.scsIndex, Charset::DecSpec); } @charsetFinal |
        '5' @{
            iface.parserDesignateCharset(
                parser.scsIndex,
                parser.scsMod == '%' ? Charset::DecSuppl :
                parser.scsMod == '&' ? Charset::NrcRussian : Charset::NrcFinnish
            );
        } @charsetFinal |
        '<' @{ iface.parserDesignateCharset(parser.scsIndex, Charset::DecUserPref); } @charsetFinal |
        '>' @{
            iface.parserDesignateCharset(
                parser.scsIndex,
                parser.scsMod == '"' ? Charset::NrcGreek : Charset::DecTechn
            );
        } @charsetFinal |
        '4' @{ iface.parserDesignateCharset(parser.scsIndex, Charset::NrcDutch); } @charsetFinal |
        'C' @{ iface.parserDesignateCharset(parser.scsIndex, Charset::NrcFinnish); } @charsetFinal |
        ('R' | 'f') @{ iface.parserDesignateCharset(parser.scsIndex, Charset::NrcFrench); } @charsetFinal |
        ('9' | 'Q') @{ iface.parserDesignateCharset(parser.scsIndex, Charset::NrcFrenchCanadian); } @charsetFinal |
        'K' @{ iface.parserDesignateCharset(parser.scsIndex, Charset::NrcGerman); } @charsetFinal |
        'Y' @{ iface.parserDesignateCharset(parser.scsIndex, Charset::NrcItalian); } @charsetFinal |
        ('`' | 'E' | '6') @{
            iface.parserDesignateCharset(
                parser.scsIndex,
                parser.scsMod == '%' ? Charset::NrcPortuguese : Charset::NrcNorwegianDanish
            );
        } @charsetFinal |
        'Z' @{ iface.parserDesignateCharset(parser.scsIndex, Charset::NrcSpanish); } @charsetFinal |
        ('7' | 'H') @{ iface.parserDesignateCharset(parser.scsIndex, Charset::NrcSwedish); } @charsetFinal |
        '=' @{
            iface.parserDesignateCharset(
                parser.scsIndex,
                parser.scsMod == '%' ? Charset::NrcHebrew : Charset::NrcSwiss
            );
        } @charsetFinal |
        '3' @{
            iface.parserDesignateCharset(
                parser.scsIndex,
                parser.scsMod == '%' ? Charset::NrcSerboCroatian : Charset::UTF8
            );
        } @charsetFinal |
        '2' @{
            iface.parserDesignateCharset(
                parser.scsIndex,
                parser.scsMod == '%' ? Charset::NrcTurkish : Charset::UTF8
            );
        } @charsetFinal |
        (0x30..0x7e - charsetKnown) @{
            iface.parserDesignateCharset(parser.scsIndex, Charset::UTF8);
        } @charsetFinal |
        0x80..0x9f @escapeFinal
    )*;

    csiEntry := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        sequenceC0 |
        '0'..'9' @csiDigit @{ fgoto csiParameter; } |
        (';' | ':') @csiSeparator @{ fgoto csiParameter; } |
        0x3c..0x3f @csiPrefix |
        0x20..0x2f @csiIntermediate @{ fgoto csiIntermediate; } |
        0x40..0x7e @csiFinalSelect |
        0x80..0x9f @csiInvalid
    )*;

    csiParameter := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        sequenceC0 |
        '0'..'9' @csiDigit |
        (';' | ':') @csiSeparator |
        0x20..0x2f @csiIntermediate @{ fgoto csiIntermediate; } |
        0x40..0x7e @csiFinalSelect |
        0x3c..0x3f @csiInvalid |
        0x80..0x9f @csiInvalid
    )*;

    csiIntermediate := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        sequenceC0 |
        0x20..0x2f @csiIntermediate |
        0x40..0x7e @csiIntermediateFinalSelect |
        0x30..0x3f @csiInvalid |
        0x80..0x9f @csiInvalid
    )*;

    csiIgnore := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        sequenceC0 |
        0x40..0x7e @csiIgnoredFinal |
        (0x20..0x3f | 0x80..0x9f)
    )*;

    csiPlainDispatch := csiPlainFinal;
    csiGreaterDispatch := csiGreaterFinal;
    csiLessDispatch := csiLessFinal;
    csiEqualDispatch := csiEqualFinal;
    csiQuestionDispatch := csiQuestionFinal;
    csiBangDispatch := csiBangFinal;
    csiQuoteDispatch := csiQuoteFinal;
    csiSpaceDispatch := csiSpaceFinal;
    csiApostropheDispatch := csiApostropheFinal;
    csiDollarDispatch := csiDollarFinal;
    csiStarDispatch := csiStarFinal;
    csiQuestionDollarDispatch := csiQuestionDollarFinal;
    csiUnknownDispatch := csiUnknownFinal;

    escapeVt52 := (
        cancel |
        0x1b @repeatEscape |
        c1Dispatch |
        (0x80..0x83 | 0x86..0x87 | 0x89..0x8c |
         0x91..0x95 | 0x99) @escapeFinal |
        0x7f |
        highToGround |
        '=' @vt52AppKeypad |
        '>' @vt52NormalKeypad |
        '<' @vt52Ansi |
        'A' @vt52Cuu |
        'B' @vt52Cud |
        'C' @vt52Cuf |
        'D' @vt52Cub |
        'F' @vt52Graphics |
        'G' @vt52Ascii |
        'H' @vt52Cup |
        'I' @vt52Ri |
        'J' @vt52Ed |
        'K' @vt52El |
        'Y' @vt52CupBegin |
        'Z' @vt52Identify |
        'c' @vt52Ris |
        (any - (0x18 | 0x1a | 0x1b | 0x7f | 0x80..0xff |
                '=' | '>' | '<' | 'A' | 'B' | 'C' | 'D' | 'F' | 'G' |
                'H' | 'I' | 'J' | 'K' | 'Y' | 'Z' | 'c')) @vt52Unhandled
    )*;

    vt52CupRow := any @vt52Row;
    vt52CupColumn := any @vt52Column;

    dcsEntry := (
        cancel |
        stringC1 |
        0x9c @dcsHeaderTerminated |
        0x1b @dcsHeaderEscape |
        0x7f |
        sequenceC0 |
        '0'..'9' @dcsDigit @{ fgoto dcsParameter; } |
        (';' | ':') @dcsSeparator @{ fgoto dcsParameter; } |
        0x3c..0x3f @dcsHeaderByte |
        0x20..0x2f @dcsIntermediate @{ fgoto dcsIntermediate; } |
        0x40..0x7e @dcsFinal |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsHeaderInvalid
    )*;

    dcsParameter := (
        cancel |
        stringC1 |
        0x9c @dcsHeaderTerminated |
        0x1b @dcsHeaderEscape |
        0x7f |
        sequenceC0 |
        '0'..'9' @dcsDigit |
        (';' | ':') @dcsSeparator |
        0x20..0x2f @dcsIntermediate @{ fgoto dcsIntermediate; } |
        0x40..0x7e @dcsFinal |
        (0x3c..0x3f | 0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsHeaderInvalid
    )*;

    dcsIntermediate := (
        cancel |
        stringC1 |
        0x9c @dcsHeaderTerminated |
        0x1b @dcsHeaderEscape |
        0x7f |
        sequenceC0 |
        0x20..0x2f @dcsIntermediate |
        0x40..0x7e @dcsFinal |
        (0x30..0x3f | 0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsHeaderInvalid
    )*;

    dcsHeaderEscape := (
        cancel |
        stringC1 |
        0x9c @dcsHeaderTerminated |
        '\\' @dcsHeaderTerminated |
        0x1b |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsHeaderInvalid
    )*;

    dcsDecrqssGap = (
        cancel |
        stringC1 |
        0x7f |
        sequenceC0
    );

    dcsDecrqssTerminator = (
        0x9c |
        0x1b (
            '\\' |
            0x9c |
            0x1b @dcsDecrqssEscapedEscape |
            (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                    0x9a..0x9f)) @dcsDecrqssEscapedData |
            cancel |
            stringC1
        )
    );

    dcsDecrqssEntry := (
        dcsDecrqssGap*
        (
            '"' @dcsHeaderByte dcsDecrqssGap* (
                'p' @dcsHeaderByte dcsDecrqssGap*
                    dcsDecrqssTerminator @dcsDecrqssDecsclSt |
                'q' @dcsHeaderByte dcsDecrqssGap*
                    dcsDecrqssTerminator @dcsDecrqssDecscaSt
            ) |
            'm' @dcsHeaderByte dcsDecrqssGap*
                dcsDecrqssTerminator @dcsDecrqssSgrSt |
            'r' @dcsHeaderByte dcsDecrqssGap*
                dcsDecrqssTerminator @dcsDecrqssDecstbmSt |
            's' @dcsHeaderByte dcsDecrqssGap*
                dcsDecrqssTerminator @dcsDecrqssDecslrmSt |
            't' @dcsHeaderByte dcsDecrqssGap*
                dcsDecrqssTerminator @dcsDecrqssDecslppSt |
            ' ' @dcsHeaderByte dcsDecrqssGap*
                'q' @dcsHeaderByte dcsDecrqssGap*
                dcsDecrqssTerminator @dcsDecrqssDecscusrSt
        )
    ) $err(dcsDecrqssInvalidStart);

    dcsDecrqssInvalid := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssUnknownSt |
        0x1b @dcsDecrqssEscape |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssUnknownSt |
        '\\' @dcsDecrqssUnknownSt |
        0x1b @dcsDecrqssEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsDecrqssEscapedData
    )*;

    dcsXtgettcap := (
        cancel |
        stringC1 |
        0x9c @dcsXtSt @dcsXtField @dcsXtDone |
        0x1b @dcsXtEscape |
        0x7f |
        sequenceC0 |
        xdigit @dcsXtHex |
        ';' @{ parser.dcsCapabilityComplete = true; } @dcsXtField @dcsXtSeparator |
        (0x20..0x7e - (xdigit | ';')) @dcsXtInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsXtInvalid
    )*;

    dcsXtEscape := (
        cancel |
        stringC1 |
        0x9c @dcsXtSt @dcsXtField @dcsXtDone |
        '\\' @dcsXtSt @dcsXtField @dcsXtDone |
        0x1b @dcsXtEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsXtEscapedData
    )*;

    dcsUdkCode := (
        cancel |
        stringC1 |
        0x9c @dcsUdkSt |
        0x1b @dcsUdkEscape |
        0x7f |
        sequenceC0 |
        digit @dcsUdkDigit |
        '/' @dcsUdkSlash |
        ';' @dcsUdkCodeSeparator |
        (0x20..0x7e - (digit | '/' | ';')) @dcsUdkInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsUdkInvalid
    )*;

    dcsUdkValue := (
        cancel |
        stringC1 |
        0x9c @dcsUdkSt |
        0x1b @dcsUdkEscape |
        0x7f |
        sequenceC0 |
        xdigit @dcsUdkHex |
        ';' @dcsUdkValueSeparator |
        (0x20..0x7e - (xdigit | ';')) @dcsUdkInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsUdkInvalid
    )*;

    dcsUdkInvalid := (
        cancel |
        stringC1 |
        0x9c @dcsUdkSt |
        0x1b @dcsUdkEscape |
        0x7f |
        sequenceC0 |
        ';' @dcsUdkInvalidSeparator |
        (0x20..0x7e - ';') @dcsUdkInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsUdkInvalid
    )*;

    dcsUdkEscape := (
        cancel |
        stringC1 |
        0x9c @dcsUdkSt |
        '\\' @dcsUdkSt |
        0x1b @dcsUdkEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsUdkEscapedData
    )*;

    dcsPayload := (
        cancel |
        stringC1 |
        0x9c @dcsSt |
        0x1b @dcsPayloadEscape |
        0x7f |
        (0x00..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsPayloadData
    )*;

    dcsPayloadEscape := (
        cancel |
        stringC1 |
        0x9c @{ ragelFinishDcs(); fnext main; fbreak; } |
        '\\' @{ ragelFinishDcs(); fnext main; fbreak; } |
        0x1b @dcsEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsEscapedData
    )*;

    dcsIgnore := (
        cancel |
        stringC1 |
        0x9c @dcsIgnoreSt |
        0x1b @{ fgoto dcsIgnoreEscape; } |
        (0x00..0x17 | 0x19 | 0x1c..0x8f |
         0x91..0x95 | 0x99 | 0xa0..0xff) @dcsIgnoreData
    )*;

    dcsIgnoreEscape := (
        cancel |
        stringC1 |
        0x9c @dcsIgnoreSt |
        '\\' @dcsIgnoreSt |
        0x1b |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @{ fgoto dcsIgnore; }
    )*;

    oscCommand := (
        cancel |
        stringC1 |
        0x9c @oscCommandSt @oscDispatch @oscDone |
        0x07 @oscCommandBell @oscDispatch @oscDone |
        0x1b @{ fgoto oscCommandEscape; } |
        0x7f |
        sequenceC0 |
        digit @oscCommandDigit |
        ';' @oscCommandSeparator |
        (0x20..0x7e - (digit | ';')) @oscCommandInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscCommandInvalid
    )*;

    oscCommandEscape := (
        cancel |
        '\\' @oscCommandSt @oscDispatch @oscDone |
        0x1b @oscEscapedEscape @{ fgoto oscInvalidEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscInvalidEscapedData
    )*;

    oscCwdEntry := (
        cancel |
        stringC1 |
        0x9c @oscCwdSt @oscCwdDispatch @oscDone |
        0x07 @oscCwdBell @oscCwdDispatch @oscDone |
        0x1b @{ fgoto oscCwdInvalidEscape; } |
        0x7f |
        sequenceC0 |
        '/' @oscCwdPathStart |
        'f' @oscCwdRaw @{ fgoto oscCwdFile; } |
        (0x20..0x7e - ('/' | 'f')) @oscCwdInvalidData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff)
            @oscCwdInvalidData
    )*;

    oscCwdPrefixGap = (
        cancel |
        stringC1 |
        0x9c @oscCwdSt @oscCwdDispatch @oscDone |
        0x07 @oscCwdBell @oscCwdDispatch @oscDone |
        0x1b @{ fgoto oscCwdInvalidEscape; } |
        0x7f |
        sequenceC0
    )*;

    oscCwdFile := (
        oscCwdPrefixGap
        'i' @oscCwdRaw
        oscCwdPrefixGap
        'l' @oscCwdRaw
        oscCwdPrefixGap
        'e' @oscCwdRaw
        oscCwdPrefixGap
        ':' @oscCwdRaw
        oscCwdPrefixGap
        '/' @oscCwdRaw
        oscCwdPrefixGap
        '/' @oscCwdRaw
        @{ fgoto oscCwdAuthority; }
    ) $err(oscCwdPrefixError);

    oscCwdAuthority := (
        cancel |
        stringC1 |
        0x9c @oscCwdSt @oscCwdDispatch @oscDone |
        0x07 @oscCwdBell @oscCwdDispatch @oscDone |
        0x1b @{ fgoto oscCwdAuthorityEscape; } |
        0x7f |
        sequenceC0 |
        '/' @oscCwdPathStart |
        (0x20..0x7e - '/') @oscCwdAuthorityData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff)
            @oscCwdAuthorityData
    )*;

    oscCwdPath := (
        cancel |
        stringC1 |
        0x9c @oscCwdSt @oscCwdDispatch @oscDone |
        0x07 @oscCwdBell @oscCwdDispatch @oscDone |
        0x1b @{ fgoto oscCwdPathEscape; } |
        0x7f |
        sequenceC0 |
        '%' @oscCwdPercentStart |
        (0x20..0x7e - '%') @oscCwdPathData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff)
            @oscCwdPathData
    )*;

    oscCwdPercentHigh := (
        cancel |
        stringC1 |
        0x9c @oscCwdSt @oscCwdDispatch @oscDone |
        0x07 @oscCwdBell @oscCwdDispatch @oscDone |
        0x1b @{ fgoto oscCwdInvalidEscape; } |
        0x7f |
        sequenceC0 |
        xdigit @oscCwdPercentHigh |
        (0x20..0x7e - xdigit) @oscCwdInvalidData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff)
            @oscCwdInvalidData
    )*;

    oscCwdPercentLow := (
        cancel |
        stringC1 |
        0x9c @oscCwdSt @oscCwdDispatch @oscDone |
        0x07 @oscCwdBell @oscCwdDispatch @oscDone |
        0x1b @{ fgoto oscCwdInvalidEscape; } |
        0x7f |
        sequenceC0 |
        xdigit @oscCwdPercentLow |
        (0x20..0x7e - xdigit) @oscCwdInvalidData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff)
            @oscCwdInvalidData
    )*;

    oscCwdInvalid := (
        cancel |
        stringC1 |
        0x9c @oscCwdSt @oscCwdDispatch @oscDone |
        0x07 @oscCwdBell @oscCwdDispatch @oscDone |
        0x1b @{ fgoto oscCwdInvalidEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscCwdInvalidEscape := (
        cancel |
        '\\' @oscCwdSt @oscCwdDispatch @oscDone |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscCwdInvalidEscaped
    )*;

    oscCwdAuthorityEscape := (
        cancel |
        '\\' @oscCwdSt @oscCwdDispatch @oscDone |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\'))
            @oscCwdAuthorityEscaped
    )*;

    oscCwdPathEscape := (
        cancel |
        '\\' @oscCwdSt @oscCwdDispatch @oscDone |
        0x1b @oscEscapedEscape @oscCwdPathEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscCwdPathEscaped
    )*;

    oscTitle0 := (
        cancel |
        stringC1 |
        0x9c @oscTitle0St |
        0x07 @oscTitle0St |
        0x1b @{ fgoto oscTitle0Escape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscTitleData
    )*;

    oscTitle1 := (
        cancel |
        stringC1 |
        0x9c @oscTitle1St |
        0x07 @oscTitle1St |
        0x1b @{ fgoto oscTitle1Escape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscTitleData
    )*;

    oscTitle2 := (
        cancel |
        stringC1 |
        0x9c @oscTitle2St |
        0x07 @oscTitle2St |
        0x1b @{ fgoto oscTitle2Escape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscTitleData
    )*;

    oscTitle0Escape := (
        cancel |
        '\\' @oscTitle0St |
        0x1b @oscTitleEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscTitleEscaped0
    )*;

    oscTitle1Escape := (
        cancel |
        '\\' @oscTitle1St |
        0x1b @oscTitleEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscTitleEscaped1
    )*;

    oscTitle2Escape := (
        cancel |
        '\\' @oscTitle2St |
        0x1b @oscTitleEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscTitleEscaped2
    )*;

    oscHyperlinkParamStart := (
        cancel |
        stringC1 |
        0x9c @oscHyperlinkMalformedSt |
        0x07 @oscHyperlinkMalformedSt |
        0x1b @{ fgoto oscHyperlinkParamEscape; } |
        0x7f |
        sequenceC0 |
        'i' @oscHyperlinkI |
        ':' @oscHyperlinkColon |
        ';' @oscHyperlinkUri |
        (0x20..0x7e - ('i' | ':' | ';')) @oscHyperlinkParamData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscHyperlinkParamData
    )*;

    oscHyperlinkParamI := (
        cancel |
        stringC1 |
        0x9c @oscHyperlinkMalformedSt |
        0x07 @oscHyperlinkMalformedSt |
        0x1b @{ fgoto oscHyperlinkParamEscape; } |
        0x7f |
        sequenceC0 |
        'd' @oscHyperlinkD |
        ':' @oscHyperlinkColon |
        ';' @oscHyperlinkUri |
        (0x20..0x7e - ('d' | ':' | ';')) @oscHyperlinkParamData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscHyperlinkParamData
    )*;

    oscHyperlinkParamId := (
        cancel |
        stringC1 |
        0x9c @oscHyperlinkMalformedSt |
        0x07 @oscHyperlinkMalformedSt |
        0x1b @{ fgoto oscHyperlinkParamEscape; } |
        0x7f |
        sequenceC0 |
        '=' @oscHyperlinkEqual |
        ':' @oscHyperlinkColon |
        ';' @oscHyperlinkUri |
        (0x20..0x7e - ('=' | ':' | ';')) @oscHyperlinkParamData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscHyperlinkParamData
    )*;

    oscHyperlinkIdValue := (
        cancel |
        stringC1 |
        0x9c @oscHyperlinkMalformedSt |
        0x07 @oscHyperlinkMalformedSt |
        0x1b @{ fgoto oscHyperlinkIdEscape; } |
        0x7f |
        ':' @oscHyperlinkIdColon |
        ';' @oscHyperlinkIdUri |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscHyperlinkParamSkip := (
        cancel |
        stringC1 |
        0x9c @oscHyperlinkMalformedSt |
        0x07 @oscHyperlinkMalformedSt |
        0x1b @{ fgoto oscHyperlinkParamEscape; } |
        0x7f |
        ':' @oscHyperlinkColon |
        ';' @oscHyperlinkUri |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscHyperlinkUri := (
        cancel |
        stringC1 |
        0x9c @oscHyperlinkSt |
        0x07 @oscHyperlinkSt |
        0x1b @{ fgoto oscHyperlinkUriEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscHyperlinkParamEscape := (
        cancel |
        '\\' @oscHyperlinkMalformedSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscHyperlinkParamEscaped
    )*;

    oscHyperlinkIdEscape := (
        cancel |
        '\\' @oscHyperlinkMalformedSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscHyperlinkIdEscaped
    )*;

    oscHyperlinkUriEscape := (
        cancel |
        '\\' @oscHyperlinkSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscHyperlinkUriEscaped
    )*;

    oscProgressEntry := (
        cancel |
        stringC1 |
        0x9c @oscProgressNotifySt |
        0x07 @oscProgressNotifySt |
        0x1b @{ fgoto oscProgressEntryEscape; } |
        0x7f |
        sequenceC0 |
        '4' @oscProgressFour |
        (0x20..0x7e - '4') @oscProgressNotifyData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscProgressNotifyData
    )*;

    oscProgressFour := (
        cancel |
        stringC1 |
        0x9c @oscProgressNotifySt |
        0x07 @oscProgressNotifySt |
        0x1b @{ fgoto oscProgressFourEscape; } |
        0x7f |
        sequenceC0 |
        ';' @oscProgressBeginState |
        (0x20..0x7e - ';') @oscProgressNotifyData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscProgressNotifyData
    )*;

    oscProgressState := (
        cancel |
        stringC1 |
        0x9c @oscProgressDiscardSt |
        0x07 @oscProgressDiscardSt |
        0x1b @{ fgoto oscProgressDiscardEscape; } |
        0x7f |
        sequenceC0 |
        digit @oscProgressStateDigit |
        ';' @oscProgressBeginPercent |
        (0x20..0x7e - (digit | ';')) @oscProgressDiscardData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscProgressDiscardData
    )*;

    oscProgressPercent := (
        cancel |
        stringC1 |
        0x9c @oscProgressSt |
        0x07 @oscProgressSt |
        0x1b @{ fgoto oscProgressPercentEscape; } |
        0x7f |
        sequenceC0 |
        digit @oscProgressPercentDigit |
        (0x20..0x7e - digit) @oscProgressDiscardData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscProgressDiscardData
    )*;

    oscProgressNotify := (
        cancel |
        stringC1 |
        0x9c @oscProgressNotifySt |
        0x07 @oscProgressNotifySt |
        0x1b @{ fgoto oscProgressNotifyEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscProgressDiscard := (
        cancel |
        stringC1 |
        0x9c @oscProgressDiscardSt |
        0x07 @oscProgressDiscardSt |
        0x1b @{ fgoto oscProgressDiscardEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscProgressEntryEscape := (
        cancel |
        '\\' @oscProgressNotifySt |
        0x1b @oscEscapedEscape @{ fgoto oscProgressNotifyEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscProgressNotifyEscaped
    )*;

    oscProgressFourEscape := (
        cancel |
        '\\' @oscProgressNotifySt |
        0x1b @oscEscapedEscape @{ fgoto oscProgressNotifyEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscProgressNotifyEscaped
    )*;

    oscProgressNotifyEscape := (
        cancel |
        '\\' @oscProgressNotifySt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscProgressNotifyEscaped
    )*;

    oscProgressDiscardEscape := (
        cancel |
        '\\' @oscProgressDiscardSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscProgressDiscardEscaped
    )*;

    oscProgressPercentEscape := (
        cancel |
        '\\' @oscProgressSt |
        0x1b @oscEscapedEscape @{ fgoto oscProgressDiscardEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscProgressDiscardEscaped
    )*;

    osc52Selectors := (
        cancel |
        stringC1 |
        0x9c @osc52MalformedSt |
        0x07 @osc52MalformedBell |
        0x1b @{ fgoto osc52SelectorsEscape; } |
        0x7f |
        sequenceC0 |
        ';' @osc52BeginPayload |
        (0x20..0x7e - ';') @osc52Selector |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @osc52Selector
    )*;

    osc52Payload := (
        cancel |
        stringC1 |
        0x9c @osc52St @osc52Dispatch @oscDone |
        0x07 @osc52Bell @osc52Dispatch @oscDone |
        0x1b @{ fgoto osc52PayloadEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @osc52Data
    )*;

    osc52SelectorsEscape := (
        cancel |
        '\\' @osc52MalformedSt |
        0x1b @oscEscapedEscape @{ fgoto oscInvalidEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @osc52SelectorEscaped
    )*;

    osc52PayloadEscape := (
        cancel |
        '\\' @osc52St @osc52Dispatch @oscDone |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @osc52PayloadEscaped
    )*;

    oscNotificationField := (
        cancel |
        stringC1 |
        0x9c @oscNotificationInvalidSt |
        0x07 @oscNotificationInvalidBell |
        0x1b @{ fgoto oscNotificationInvalidEscape; } |
        0x7f |
        sequenceC0 |
        [A-Za-z] @oscNotificationKey |
        ';' @oscNotificationBeginPayload |
        (0x20..0x7e - ([A-Za-z] | ';')) @oscNotificationInvalidData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscNotificationInvalidData
    )*;

    oscNotificationEqual := (
        cancel |
        stringC1 |
        0x9c @oscNotificationInvalidSt |
        0x07 @oscNotificationInvalidBell |
        0x1b @{ fgoto oscNotificationInvalidEscape; } |
        0x7f |
        sequenceC0 |
        '=' @oscNotificationBeginValue |
        (0x20..0x7e - '=') @oscNotificationInvalidData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscNotificationInvalidData
    )*;

    oscNotificationValue := (
        cancel |
        stringC1 |
        0x9c @oscNotificationInvalidSt |
        0x07 @oscNotificationInvalidBell |
        0x1b @{ fgoto oscNotificationInvalidEscape; } |
        0x7f |
        ':' @oscNotificationFinishField @oscNotificationNextField |
        ';' @oscNotificationFinishField @oscNotificationBeginPayload |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e -
         (':' | ';')) @oscNotificationValueData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscNotificationValueData
    )*;

    oscNotificationPayload := (
        cancel |
        stringC1 |
        0x9c @oscNotificationSt @oscNotificationDispatch @oscDone |
        0x07 @oscNotificationBell @oscNotificationDispatch @oscDone |
        0x1b @{ fgoto oscNotificationPayloadEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff)
            @oscNotificationPayloadData
    )*;

    oscNotificationInvalid := (
        cancel |
        stringC1 |
        0x9c @oscNotificationInvalidSt |
        0x07 @oscNotificationInvalidBell |
        0x1b @{ fgoto oscNotificationInvalidEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscNotificationPayloadEscape := (
        cancel |
        '\\' @oscNotificationSt @oscNotificationDispatch @oscDone |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscNotificationPayloadEscaped
    )*;

    oscNotificationInvalidEscape := (
        cancel |
        '\\' @oscNotificationInvalidSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscNotificationInvalidEscaped
    )*;

    oscIndexedColorIndex := (
        cancel |
        stringC1 |
        0x9c @oscInvalidSt |
        0x07 @oscInvalidBell |
        0x1b @{ fgoto oscIndexedColorIndexEscape; } |
        0x7f |
        sequenceC0 |
        digit @oscIndexedColorIndexDigit |
        ';' @oscIndexedColorBegin |
        (0x20..0x7e - (digit | ';')) @oscIndexedColorIndexInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff)
            @oscIndexedColorIndexInvalid
    )*;

    oscIndexedColorIndexEscape := (
        cancel |
        '\\' @oscInvalidSt |
        0x1b @oscEscapedEscape
            @{ parser.oscFieldNumeric = false; fgoto oscIndexedColorIndexEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\'))
            @oscIndexedColorIndexEscapedInvalid
    )*;

    oscIndexedColor := (
        oscColorValue (
            ';' @oscIndexedColorCommit @oscIndexedColorNext |
            0x9c @oscIndexedColorSt @oscIndexedColorCommit @oscDone |
            0x07 @oscIndexedColorBell @oscIndexedColorCommit @oscDone |
            0x1b @{ fgoto oscIndexedColorEscape; }
        )
    ) $err(oscIndexedColorInvalid);

    oscIndexedColorEscape := (
        cancel |
        '\\' @oscIndexedColorSt @oscIndexedColorCommit @oscDone |
        0x1b @oscEscapedEscape
            @{ fgoto oscIndexedColorDiscardEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\'))
            @oscIndexedColorEscapedInvalid
    )*;

    oscIndexedColorDiscard := (
        cancel |
        stringC1 |
        0x9c @oscInvalidSt |
        0x07 @oscInvalidBell |
        0x1b @{ fgoto oscIndexedColorDiscardEscape; } |
        0x7f |
        sequenceC0 |
        ';' @oscIndexedColorDiscardNext |
        (0x20..0x7e - ';') @oscData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscIndexedColorDiscardEscape := (
        cancel |
        '\\' @oscInvalidSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\'))
            @oscIndexedColorEscapedInvalid
    )*;

    oscDynamicColor := (
        oscColorValue (
            ';' @oscDynamicColorCommit @oscDynamicColorNext |
            0x9c @oscDynamicColorSt @oscDynamicColorCommit @oscDone |
            0x07 @oscDynamicColorBell @oscDynamicColorCommit @oscDone |
            0x1b @{ fgoto oscDynamicColorEscape; }
        )
    ) $err(oscDynamicColorInvalid);

    oscDynamicColorEscape := (
        cancel |
        '\\' @oscDynamicColorSt @oscDynamicColorCommit @oscDone |
        0x1b @oscEscapedEscape
            @{ fgoto oscDynamicColorDiscardEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\'))
            @oscDynamicColorEscapedInvalid
    )*;

    oscDynamicColorDiscard := (
        cancel |
        stringC1 |
        0x9c @oscInvalidSt |
        0x07 @oscInvalidBell |
        0x1b @{ fgoto oscDynamicColorDiscardEscape; } |
        0x7f |
        sequenceC0 |
        ';' @oscDynamicColorDiscardNext |
        (0x20..0x7e - ';') @oscData |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscDynamicColorDiscardEscape := (
        cancel |
        '\\' @oscInvalidSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\'))
            @oscDynamicColorEscapedInvalid
    )*;

    oscNumericFields := (
        cancel |
        stringC1 |
        0x9c @oscNumericSt @oscNumericFinalField @oscDone |
        0x07 @oscNumericFinalField @oscNumericBell @oscDone |
        0x1b @{ fgoto oscNumericEscape; } |
        0x7f |
        sequenceC0 |
        digit @oscNumericDigit |
        ';' @oscNumericField @oscNumericSeparator |
        (0x20..0x7e - (digit | ';')) @oscNumericInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff)
            @oscNumericInvalid
    )*;

    oscNumericEscape := (
        cancel |
        '\\' @oscNumericFinalField @oscNumericSt @oscDone |
        0x1b @oscEscapedEscape
            @{ parser.oscFieldPresent = true; parser.oscFieldNumeric = false; } |
        (any - (0x18 | 0x1a | 0x1b | '\\'))
            @oscNumericEscapedInvalid
    )*;

    oscShellEntry := (
        cancel |
        stringC1 |
        0x9c @oscShellUnknownSt |
        0x07 @oscShellUnknownSt |
        0x1b @{ fgoto oscShellUnknownEscape; } |
        0x7f |
        sequenceC0 |
        'A' @oscShellA |
        'B' @oscShellB |
        'C' @oscShellC |
        'D' @oscShellD |
        (0x20..0x7e - ('A' | 'B' | 'C' | 'D')) @oscShellInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscShellInvalid
    )*;

    oscShellAComplete := (
        cancel |
        stringC1 |
        0x9c @oscShellASt |
        0x07 @oscShellASt |
        0x1b @{ fgoto oscShellACompleteEscape; } |
        0x7f |
        sequenceC0 |
        ';' @{ ragelAppendString(fc, parser.maxOscBytes); fgoto oscShellATail; } |
        (0x20..0x7e - ';') @oscShellInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscShellInvalid
    )*;

    oscShellBComplete := (
        cancel |
        stringC1 |
        0x9c @oscShellBSt |
        0x07 @oscShellBSt |
        0x1b @{ fgoto oscShellBCompleteEscape; } |
        0x7f |
        sequenceC0 |
        ';' @{ ragelAppendString(fc, parser.maxOscBytes); fgoto oscShellBTail; } |
        (0x20..0x7e - ';') @oscShellInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscShellInvalid
    )*;

    oscShellCComplete := (
        cancel |
        stringC1 |
        0x9c @oscShellCSt |
        0x07 @oscShellCSt |
        0x1b @{ fgoto oscShellCCompleteEscape; } |
        0x7f |
        sequenceC0 |
        ';' @{ ragelAppendString(fc, parser.maxOscBytes); fgoto oscShellCTail; } |
        (0x20..0x7e - ';') @oscShellInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscShellInvalid
    )*;

    oscShellDComplete := (
        cancel |
        stringC1 |
        0x9c @oscShellDSt |
        0x07 @oscShellDSt |
        0x1b @{ fgoto oscShellDCompleteEscape; } |
        0x7f |
        sequenceC0 |
        ';' @{ ragelAppendString(fc, parser.maxOscBytes); fgoto oscShellDTail; } |
        (0x20..0x7e - ';') @oscShellInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscShellInvalid
    )*;

    oscShellATail := (
        cancel |
        stringC1 |
        0x9c @oscShellASt |
        0x07 @oscShellASt |
        0x1b @{ fgoto oscShellATailEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscShellBTail := (
        cancel |
        stringC1 |
        0x9c @oscShellBSt |
        0x07 @oscShellBSt |
        0x1b @{ fgoto oscShellBTailEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscShellCTail := (
        cancel |
        stringC1 |
        0x9c @oscShellCSt |
        0x07 @oscShellCSt |
        0x1b @{ fgoto oscShellCTailEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscShellDTail := (
        cancel |
        stringC1 |
        0x9c @oscShellDSt |
        0x07 @oscShellDSt |
        0x1b @{ fgoto oscShellDTailEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscShellUnknown := (
        cancel |
        stringC1 |
        0x9c @oscShellUnknownSt |
        0x07 @oscShellUnknownSt |
        0x1b @{ fgoto oscShellUnknownEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscShellACompleteEscape := (
        cancel |
        '\\' @oscShellASt |
        0x1b @oscEscapedEscape @{ fgoto oscShellUnknownEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedUnknown
    )*;

    oscShellBCompleteEscape := (
        cancel |
        '\\' @oscShellBSt |
        0x1b @oscEscapedEscape @{ fgoto oscShellUnknownEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedUnknown
    )*;

    oscShellCCompleteEscape := (
        cancel |
        '\\' @oscShellCSt |
        0x1b @oscEscapedEscape @{ fgoto oscShellUnknownEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedUnknown
    )*;

    oscShellDCompleteEscape := (
        cancel |
        '\\' @oscShellDSt |
        0x1b @oscEscapedEscape @{ fgoto oscShellUnknownEscape; } |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedUnknown
    )*;

    oscShellATailEscape := (
        cancel |
        '\\' @oscShellASt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedA
    )*;

    oscShellBTailEscape := (
        cancel |
        '\\' @oscShellBSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedB
    )*;

    oscShellCTailEscape := (
        cancel |
        '\\' @oscShellCSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedC
    )*;

    oscShellDTailEscape := (
        cancel |
        '\\' @oscShellDSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedD
    )*;

    oscShellUnknownEscape := (
        cancel |
        '\\' @oscShellUnknownSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscShellEscapedUnknown
    )*;

    oscPayload := (
        cancel |
        stringC1 |
        0x9c @oscSt @oscDispatch @oscDone |
        0x07 @oscBell @oscDispatch @oscDone |
        0x1b @oscEscape |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscRawData
    )*;

    oscEscape := (
        cancel |
        '\\' @oscSt @oscDispatch @oscDone |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscEscapedData
    )*;

    oscInvalid := (
        cancel |
        stringC1 |
        0x9c @oscInvalidSt |
        0x07 @oscInvalidBell |
        0x1b @{ fgoto oscInvalidEscape; } |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscRawData
    )*;

    oscInvalidEscape := (
        cancel |
        '\\' @oscInvalidSt |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscInvalidEscapedData
    )*;

    string := (
        cancel |
        stringC1 |
        0x9c @ignoredSt |
        0x1b @ignoredEscape |
        0x7f |
        (0x00..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @ignoredData
    )*;

    stringEscape := (
        cancel |
        '\\' @{ parser.stringUtf8Remaining = 0; parser.stringLimit = 0; if constexpr (traced) { parserTrace->stringEnd(); } fnext main; fbreak; } |
        0x1b |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @ignoredEscapedData
    )*;

}%%

#if defined(SHITTY_PARSER_DATA)
%% write data;
#elif defined(SHITTY_PARSER_INIT)
%% write init;
#elif defined(SHITTY_PARSER_EXEC)
%% write exec;
#endif
