/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

%%{
    machine vterm_parser;
    alphtype unsigned char;

    action groundDone {
        fbreak;
    }

    action returnGround {
        fnext main;
        fbreak;
    }

    action cancel {
        stringUtf8Remaining = 0;
        ragelStringLimit = 0;
        if constexpr (traced) {
            parserTrace->control(fc);
            parserTrace->stringCancel();
            parserTrace->escapeCancel();
        }
        fnext main;
        fbreak;
    }

    action beginEscape {
        resetGraphemeInput();
        stringUtf8Remaining = 0;
        ragelStringLimit = 0;
        if constexpr (traced) {
            parserTrace->stringCancel();
            parserTrace->escapeCancel();
            parserTrace->escapeBegin();
        }
        inputOps[0] = 0;
        inputSeparators[0] = 0;
        nInputOps = 1;
        if (compatLevel == CompatibilityLevel::VT52) {
            fgoto escapeVt52;
        }
        fgoto escape;
    }

    action repeatEscape {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->escapeBegin();
        }
        inputOps[0] = 0;
        inputSeparators[0] = 0;
        nInputOps = 1;
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
        esc_IND();
        fnext main;
        fbreak;
    }

    action c1Nel {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        esc_NEL();
        fnext main;
        fbreak;
    }

    action c1Hts {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        esc_HTS();
        fnext main;
        fbreak;
    }

    action c1Ri {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        esc_RI();
        fnext main;
        fbreak;
    }

    action c1Ss2 {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        charsetState.ss = 2;
        fnext main;
        fbreak;
    }

    action c1Ss3 {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        charsetState.ss = 3;
        fnext main;
        fbreak;
    }

    action c1Spa {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        esc_SPA();
        fnext main;
        fbreak;
    }

    action c1Epa {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        esc_EPA();
        fnext main;
        fbreak;
    }

    action c1Da {
        if constexpr (traced) {
            parserTrace->escapeCancel();
            parserTrace->control(fc);
        }
        csi_priDA();
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
        executeC0InSequence(fc);
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
        resetGraphemeInput();
        fbreak;
    }

    action groundBell {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        resetGraphemeInput();
        host.bell();
        fbreak;
    }

    action groundBackspace {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        resetGraphemeInput();
        moveCursorBackward(1);
        fbreak;
    }

    action groundTab {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        resetGraphemeInput();
        inp_HT();
        fbreak;
    }

    action groundLineFeed {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        resetGraphemeInput();
        if (autoNewlineMode) {
            inp_CR();
        }
        esc_IND();
        fbreak;
    }

    action groundCarriageReturn {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        resetGraphemeInput();
        inp_CR();
        fbreak;
    }

    action groundShiftOut {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        resetGraphemeInput();
        charsetState.gl = 1;
        fbreak;
    }

    action groundShiftIn {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        resetGraphemeInput();
        charsetState.gl = 0;
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
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        esc_IND();
        fbreak;
    }

    action groundC1Nel {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        esc_NEL();
        fbreak;
    }

    action groundC1Hts {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        esc_HTS();
        fbreak;
    }

    action groundC1Ri {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        esc_RI();
        fbreak;
    }

    action groundC1Ss2 {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        charsetState.ss = 2;
        fbreak;
    }

    action groundC1Ss3 {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        charsetState.ss = 3;
        fbreak;
    }

    action groundC1Dcs {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        ragelBeginDcs();
        fgoto dcsEntry;
    }

    action groundC1Spa {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        esc_SPA();
        fbreak;
    }

    action groundC1Epa {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        esc_EPA();
        fbreak;
    }

    action groundC1Sos {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        ragelBeginString(VtermTraceString::Sos, false);
        fgoto string;
    }

    action groundC1Da {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        csi_priDA();
        fbreak;
    }

    action groundC1Csi {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        beginCsi();
        fgoto csiEntry;
    }

    action groundC1St {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        fbreak;
    }

    action groundC1Osc {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        ragelBeginOsc();
        fgoto oscCommand;
    }

    action groundC1Pm {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
        ragelBeginString(VtermTraceString::Pm, false);
        fgoto string;
    }

    action groundC1Apc {
        if (ragelGroundContinuation(fc)) {
            fbreak;
        }
        resetGraphemeInput();
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
        unhandledInput(fc);
        fnext main;
        fbreak;
    }

    action escapeC0 {
        if constexpr (traced) {
            parserTrace->control(fc);
        }
        unhandledInput(fc);
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

    action escapeCharset {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
        }
        scsDst = fc;
        scsMod = '\0';
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
        esc_IND();
        fnext main;
        fbreak;
    }

    action escapeRi {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_RI();
        fnext main;
        fbreak;
    }

    action escapeNel {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_NEL();
        fnext main;
        fbreak;
    }

    action escapeHts {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_HTS();
        fnext main;
        fbreak;
    }

    action escapeSs2 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        charsetState.ss = 2;
        fnext main;
        fbreak;
    }

    action escapeSs3 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        charsetState.ss = 3;
        fnext main;
        fbreak;
    }

    action escapeSpa {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_SPA();
        fnext main;
        fbreak;
    }

    action escapeEpa {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_EPA();
        fnext main;
        fbreak;
    }

    action escapeDa {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        csi_priDA();
        fnext main;
        fbreak;
    }

    action escapeRis {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_RIS();
        fnext main;
        fbreak;
    }

    action escapeBi {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_BI();
        fnext main;
        fbreak;
    }

    action escapeDecsc {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_DECSC();
        fnext main;
        fbreak;
    }

    action escapeDecrc {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_DECRC();
        fnext main;
        fbreak;
    }

    action escapeFi {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_FI();
        fnext main;
        fbreak;
    }

    action escapeAppKeypad {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        keypadMode = KeypadMode::Application;
        fnext main;
        fbreak;
    }

    action escapeNormalKeypad {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        keypadMode = KeypadMode::Normal;
        fnext main;
        fbreak;
    }

    action escapeAnsi {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        compatLevel = CompatibilityLevel::VT400;
        fnext main;
        fbreak;
    }

    action escapeLs1r {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        charsetState.gr = 1;
        fnext main;
        fbreak;
    }

    action escapeLs2 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        charsetState.gl = 2;
        fnext main;
        fbreak;
    }

    action escapeLs2r {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        charsetState.gr = 2;
        fnext main;
        fbreak;
    }

    action escapeLs3 {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        charsetState.gl = 3;
        fnext main;
        fbreak;
    }

    action escapeLs3r {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        charsetState.gr = 3;
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
        scsMod = fc;
    }

    action charsetFinal {
        if constexpr (traced) {
            parserTrace->escapeByte(fc);
            parserTrace->escapeEnd();
        }
        esc_DCS(fc);
        fnext main;
        fbreak;
    }

    action csiDigit {
        csiHadParams = true;
        inputPresent[nInputOps - 1] = true;
        if (inputOps[nInputOps - 1] > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            inputOps[nInputOps - 1] = UINT32_MAX;
        } else {
            inputOps[nInputOps - 1] = inputOps[nInputOps - 1] * 10 + fc - '0';
        }
    }

    action csiSeparator {
        if (nInputOps >= maxEscOps) {
            fgoto csiIgnore;
        }
        csiHadParams = true;
        inputSeparators[nInputOps] = fc;
        inputOps[nInputOps] = 0;
        inputPresent[nInputOps] = false;
        ++nInputOps;
    }

    action csiPrefix {
        if (csiPrefix != 0) {
            fgoto csiIgnore;
        }
        csiPrefix = fc;
    }

    action csiIntermediate {
        if (csiIntermediateCount >= sizeof(csiIntermediates)) {
            fgoto csiIgnore;
        }
        csiIntermediates[csiIntermediateCount++] = fc;
    }

    action csiTrace {
        traceCsi(fc);
    }

    action csiDone {
        if (printerControllerMode) {
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
        printerControllerMode = false;
        fnext main;
        fbreak;
    }

    action csiFinalSelect {
        fhold;
        if (csiPrefix == '>') {
            fgoto csiGreaterDispatch;
        }
        if (csiPrefix == '<') {
            fgoto csiLessDispatch;
        }
        if (csiPrefix == '=') {
            fgoto csiEqualDispatch;
        }
        if (csiPrefix == '?') {
            fgoto csiQuestionDispatch;
        }
        fgoto csiPlainDispatch;
    }

    action csiIntermediateFinalSelect {
        fhold;
        if (csiIntermediateCount != 1) {
            fgoto csiUnknownDispatch;
        }
        if (csiPrefix == '?' && csiIntermediates[0] == '$') {
            fgoto csiQuestionDollarDispatch;
        }
        if (csiPrefix != 0) {
            fgoto csiUnknownDispatch;
        }
        if (csiIntermediates[0] == '!') {
            fgoto csiBangDispatch;
        }
        if (csiIntermediates[0] == '"') {
            fgoto csiQuoteDispatch;
        }
        if (csiIntermediates[0] == ' ') {
            fgoto csiSpaceDispatch;
        }
        if (csiIntermediates[0] == '\'') {
            fgoto csiApostropheDispatch;
        }
        if (csiIntermediates[0] == '$') {
            fgoto csiDollarDispatch;
        }
        if (csiIntermediates[0] == '*') {
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
        keypadMode = KeypadMode::Application;
        fnext main;
        fbreak;
    }

    action vt52NormalKeypad {
        keypadMode = KeypadMode::Normal;
        fnext main;
        fbreak;
    }

    action vt52Ansi {
        compatLevel = CompatibilityLevel::VT100;
        fnext main;
        fbreak;
    }

    action vt52Cuu {
        csi_CUU();
        fnext main;
        fbreak;
    }

    action vt52Cud {
        csi_CUD();
        fnext main;
        fbreak;
    }

    action vt52Cuf {
        csi_CUF();
        fnext main;
        fbreak;
    }

    action vt52Cub {
        csi_CUB();
        fnext main;
        fbreak;
    }

    action vt52Graphics {
        charsetState = CharsetState{};
        charsetState.g[charsetState.gl] = Charset::DecSpec;
        fnext main;
        fbreak;
    }

    action vt52Ascii {
        charsetState = CharsetState{};
        fnext main;
        fbreak;
    }

    action vt52Cup {
        csi_CUP();
        fnext main;
        fbreak;
    }

    action vt52Ri {
        esc_RI();
        fnext main;
        fbreak;
    }

    action vt52Ed {
        csi_ED();
        fnext main;
        fbreak;
    }

    action vt52El {
        csi_EL();
        fnext main;
        fbreak;
    }

    action vt52CupBegin {
        fgoto vt52CupRow;
    }

    action vt52Identify {
        writePty("\x1b/Z");
        fnext main;
        fbreak;
    }

    action vt52Ris {
        esc_RIS();
        fnext main;
        fbreak;
    }

    action vt52Unhandled {
        unhandledInput(fc);
        fnext main;
        fbreak;
    }

    action vt52Row {
        inputOps[0] = fc - 31;
        fgoto vt52CupColumn;
    }

    action vt52Column {
        inputOps[1] = fc - 31;
        nInputOps = 2;
        csi_CUP();
        fnext main;
        fbreak;
    }

    action dcsHeaderByte {
        ragelAppendString(fc, maxDcsBytes);
    }

    action dcsDigit {
        ragelAppendString(fc, maxDcsBytes);
        inputPresent[nInputOps - 1] = true;
        if (inputOps[nInputOps - 1] > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            inputOps[nInputOps - 1] = UINT32_MAX;
        } else {
            inputOps[nInputOps - 1] = inputOps[nInputOps - 1] * 10 + fc - '0';
        }
    }

    action dcsSeparator {
        ragelAppendString(fc, maxDcsBytes);
        if (nInputOps >= maxEscOps) {
            if constexpr (traced) {
                parserTrace->stringCancel();
            }
            fgoto dcsIgnore;
        }
        inputSeparators[nInputOps] = fc;
        inputOps[nInputOps] = 0;
        inputPresent[nInputOps] = false;
        ++nInputOps;
    }

    action dcsIntermediate {
        ragelAppendString(fc, maxDcsBytes);
        if (dcsIntermediateCount >= sizeof(dcsIntermediates)) {
            if constexpr (traced) {
                parserTrace->stringCancel();
            }
            fgoto dcsIgnore;
        }
        dcsIntermediates[dcsIntermediateCount++] = fc;
    }

    action dcsFinal {
        ragelAppendString(fc, maxDcsBytes);
        if (dcsIntermediateCount == 1 && dcsIntermediates[0] == '$' && fc == 'q') {
            fgoto dcsDecrqssEntry;
        } else if (dcsIntermediateCount == 1 && dcsIntermediates[0] == '+' && fc == 'q') {
            dcsCapabilityOffset = argBuf.used();
            dcsCapabilityDecodedLength = 0;
            dcsCapabilityCandidates = 0x0f;
            dcsCapabilityHasHighNibble = false;
            dcsCapabilityValid = true;
            fgoto dcsXtgettcap;
        } else if (dcsIntermediateCount == 0 && fc == '|') {
            dcsUdkValueOffset = dcsDecoded.used();
            dcsUdkCode = 0;
            dcsUdkHasCode = false;
            dcsUdkHasHighNibble = false;
            dcsUdkValid = true;
            dcsUdkInValue = false;
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
        stringUtf8Remaining = 0;
        ragelStringLimit = 0;
        if constexpr (traced) {
            parserTrace->stringCancel();
        }
        fnext main;
        fbreak;
    }

    action dcsIgnoreSt {
        stringUtf8Remaining = 0;
        ragelStringLimit = 0;
        fnext main;
        fbreak;
    }

    action dcsPayloadData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxDcsBytes);
        }
    }

    action dcsSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxDcsBytes);
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
        if (argBuf.used() < maxDcsBytes) {
            const u8 ch = '\x1b';
            argBuf.append(&ch, 1);
        } else {
            argBufOverflowed = true;
        }
    }

    action dcsEscapedData {
        ragelAppendEscapedString(fc, maxDcsBytes);
        fgoto dcsPayload;
    }

    action dcsDecrqssQuote {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssQuote;
    }

    action dcsDecrqssSpace {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssSpace;
    }

    action dcsDecrqssDecscl {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssDecsclComplete;
    }

    action dcsDecrqssSgr {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssSgrComplete;
    }

    action dcsDecrqssDecstbm {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssDecstbmComplete;
    }

    action dcsDecrqssDecslrm {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssDecslrmComplete;
    }

    action dcsDecrqssDecslpp {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssDecslppComplete;
    }

    action dcsDecrqssDecscusr {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssDecscusrComplete;
    }

    action dcsDecrqssDecsca {
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssDecscaComplete;
    }

    action dcsDecrqssInvalid {
        stringUtf8Continuation(fc);
        ragelAppendString(fc, maxDcsBytes);
        fgoto dcsDecrqssInvalid;
    }

    action dcsDecrqssEscape {
        fgoto dcsDecrqssEscape;
    }

    action dcsDecrqssEscapedEscape {
        if constexpr (traced) {
            parserTrace->stringData((const u8*)("\x1b"), 1);
        }
        if (argBuf.used() < maxDcsBytes) {
            const u8 ch = '\x1b';
            argBuf.append(&ch, 1);
        } else {
            argBufOverflowed = true;
        }
        fgoto dcsDecrqssEscape;
    }

    action dcsDecrqssEscapedData {
        ragelAppendEscapedString(fc, maxDcsBytes);
        fgoto dcsDecrqssInvalid;
    }

    action dcsDecrqssUnknownSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxDcsBytes);
            fgoto dcsDecrqssInvalid;
        } else {
            ragelFinishDcs();
            if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT400) {
                dcs_DECRQSS_UNKNOWN();
            }
            fnext main;
            fbreak;
        }
    }

    action dcsDecrqssDecsclSt {
        ragelFinishDcs();
        if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT400) {
            dcs_DECRQSS_DECSCL();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssSgrSt {
        ragelFinishDcs();
        if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT400) {
            dcs_DECRQSS_SGR();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecstbmSt {
        ragelFinishDcs();
        if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT400) {
            dcs_DECRQSS_DECSTBM();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecslrmSt {
        ragelFinishDcs();
        if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT400) {
            dcs_DECRQSS_DECSLRM();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecslppSt {
        ragelFinishDcs();
        if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT400) {
            dcs_DECRQSS_DECSLPP();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecscusrSt {
        ragelFinishDcs();
        if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT400) {
            dcs_DECRQSS_DECSCUSR();
        }
        fnext main;
        fbreak;
    }

    action dcsDecrqssDecscaSt {
        ragelFinishDcs();
        if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT400) {
            dcs_DECRQSS_DECSCA();
        }
        fnext main;
        fbreak;
    }

    action dcsXtHex {
        ragelAppendString(fc, maxDcsBytes);
        const u8 nibble = fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10;
        if (!dcsCapabilityHasHighNibble) {
            dcsCapabilityHighNibble = nibble;
            dcsCapabilityHasHighNibble = true;
        } else {
            const u8 decoded = (dcsCapabilityHighNibble << 4) | nibble;
            static constexpr u8 terminalName[] = {'T', 'N'};
            static constexpr u8 colorCount[] = {'C', 'o'};
            static constexpr u8 colors[] = {'c', 'o', 'l', 'o', 'r', 's'};
            static constexpr u8 rgb[] = {'R', 'G', 'B'};
            const size_t index = dcsCapabilityDecodedLength++;
            if (index >= sizeof(terminalName) || terminalName[index] != decoded) {
                dcsCapabilityCandidates &= ~0x01;
            }
            if (index >= sizeof(colorCount) || colorCount[index] != decoded) {
                dcsCapabilityCandidates &= ~0x02;
            }
            if (index >= sizeof(colors) || colors[index] != decoded) {
                dcsCapabilityCandidates &= ~0x04;
            }
            if (index >= sizeof(rgb) || rgb[index] != decoded) {
                dcsCapabilityCandidates &= ~0x08;
            }
            dcsCapabilityHasHighNibble = false;
        }
    }

    action dcsXtInvalid {
        stringUtf8Continuation(fc);
        ragelAppendString(fc, maxDcsBytes);
        dcsCapabilityValid = false;
    }

    action dcsXtSeparator {
        DcsCapability capability = DcsCapability::Unknown;
        if (dcsCapabilityValid && !dcsCapabilityHasHighNibble) {
            if ((dcsCapabilityCandidates & 0x01) && dcsCapabilityDecodedLength == 2) {
                capability = DcsCapability::TerminalName;
            } else if (((dcsCapabilityCandidates & 0x02) && dcsCapabilityDecodedLength == 2) ||
                       ((dcsCapabilityCandidates & 0x04) && dcsCapabilityDecodedLength == 6)) {
                capability = DcsCapability::Colors;
            } else if ((dcsCapabilityCandidates & 0x08) && dcsCapabilityDecodedLength == 3) {
                capability = DcsCapability::Rgb;
            }
        }
        if (!argBufOverflowed) {
            dcsCapabilityRequests.pushBack({
                dcsCapabilityOffset,
                argBuf.used() - dcsCapabilityOffset,
                capability,
            });
        }
        ragelAppendString(fc, maxDcsBytes);
        dcsCapabilityOffset = argBuf.used();
        dcsCapabilityDecodedLength = 0;
        dcsCapabilityCandidates = 0x0f;
        dcsCapabilityHasHighNibble = false;
        dcsCapabilityValid = true;
    }

    action dcsXtSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxDcsBytes);
            dcsCapabilityValid = false;
        } else {
            DcsCapability capability = DcsCapability::Unknown;
            if (dcsCapabilityValid && !dcsCapabilityHasHighNibble) {
                if ((dcsCapabilityCandidates & 0x01) && dcsCapabilityDecodedLength == 2) {
                    capability = DcsCapability::TerminalName;
                } else if (((dcsCapabilityCandidates & 0x02) && dcsCapabilityDecodedLength == 2) ||
                           ((dcsCapabilityCandidates & 0x04) && dcsCapabilityDecodedLength == 6)) {
                    capability = DcsCapability::Colors;
                } else if ((dcsCapabilityCandidates & 0x08) && dcsCapabilityDecodedLength == 3) {
                    capability = DcsCapability::Rgb;
                }
            }
            if (!argBufOverflowed) {
                dcsCapabilityRequests.pushBack({
                    dcsCapabilityOffset,
                    argBuf.used() - dcsCapabilityOffset,
                    capability,
                });
            }
            ragelFinishDcs();
            if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT200) {
                dcs_XTGETTCAP();
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
        if (argBuf.used() < maxDcsBytes) {
            const u8 ch = '\x1b';
            argBuf.append(&ch, 1);
        } else {
            argBufOverflowed = true;
        }
        dcsCapabilityValid = false;
    }

    action dcsXtEscapedData {
        ragelAppendEscapedString(fc, maxDcsBytes);
        dcsCapabilityValid = false;
        fgoto dcsXtgettcap;
    }

    action dcsUdkDigit {
        ragelAppendString(fc, maxDcsBytes);
        dcsUdkHasCode = true;
        if (dcsUdkCode > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            dcsUdkValid = false;
        } else {
            dcsUdkCode = dcsUdkCode * 10 + fc - '0';
        }
    }

    action dcsUdkSlash {
        ragelAppendString(fc, maxDcsBytes);
        dcsUdkInValue = true;
        dcsUdkValid = dcsUdkValid && dcsUdkHasCode;
        dcsUdkValueOffset = dcsDecoded.used();
        dcsUdkHasHighNibble = false;
        fgoto dcsUdkValue;
    }

    action dcsUdkHex {
        ragelAppendString(fc, maxDcsBytes);
        const u8 nibble = fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10;
        if (!dcsUdkHasHighNibble) {
            dcsUdkHighNibble = nibble;
            dcsUdkHasHighNibble = true;
        } else {
            if (!argBufOverflowed &&
                dcsDecoded.used() - dcsUdkValueOffset < 255) {
                const u8 decoded = (dcsUdkHighNibble << 4) | nibble;
                dcsDecoded.append(&decoded, 1);
            } else {
                dcsUdkValid = false;
            }
            dcsUdkHasHighNibble = false;
        }
    }

    action dcsUdkCodeSeparator {
        ragelAppendString(fc, maxDcsBytes);
        dcsUdkCode = 0;
        dcsUdkHasCode = false;
        dcsUdkHasHighNibble = false;
        dcsUdkValid = true;
        dcsUdkInValue = false;
    }

    action dcsUdkValueSeparator {
        if (!argBufOverflowed && dcsUdkValid && !dcsUdkHasHighNibble) {
            dcsUdkDefinitions.pushBack({
                dcsUdkValueOffset,
                dcsDecoded.used() - dcsUdkValueOffset,
                dcsUdkCode,
            });
        }
        ragelAppendString(fc, maxDcsBytes);
        dcsUdkCode = 0;
        dcsUdkHasCode = false;
        dcsUdkHasHighNibble = false;
        dcsUdkValid = true;
        dcsUdkInValue = false;
        fgoto dcsUdkCode;
    }

    action dcsUdkInvalidSeparator {
        ragelAppendString(fc, maxDcsBytes);
        dcsUdkCode = 0;
        dcsUdkHasCode = false;
        dcsUdkHasHighNibble = false;
        dcsUdkValid = true;
        dcsUdkInValue = false;
        fgoto dcsUdkCode;
    }

    action dcsUdkInvalid {
        stringUtf8Continuation(fc);
        ragelAppendString(fc, maxDcsBytes);
        dcsUdkValid = false;
        fgoto dcsUdkInvalid;
    }

    action dcsUdkSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxDcsBytes);
            dcsUdkValid = false;
            fgoto dcsUdkInvalid;
        } else {
            if (!argBufOverflowed && dcsUdkInValue && dcsUdkValid &&
                !dcsUdkHasHighNibble) {
                dcsUdkDefinitions.pushBack({
                    dcsUdkValueOffset,
                    dcsDecoded.used() - dcsUdkValueOffset,
                    dcsUdkCode,
                });
            }
            ragelFinishDcs();
            if (!argBufOverflowed && compatLevel >= CompatibilityLevel::VT200) {
                dcs_DECUDK();
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
        if (argBuf.used() < maxDcsBytes) {
            const u8 ch = '\x1b';
            argBuf.append(&ch, 1);
        } else {
            argBufOverflowed = true;
        }
        dcsUdkValid = false;
    }

    action dcsUdkEscapedData {
        ragelAppendEscapedString(fc, maxDcsBytes);
        dcsUdkValid = false;
        fgoto dcsUdkInvalid;
    }

    action oscCommandDigit {
        ragelAppendString(fc, maxOscBytes);
        oscCommandValid = true;
        if (oscCommand > (2147483647u - (u32)(fc - '0')) / 10) {
            oscCommandValid = false;
        } else {
            oscCommand = oscCommand * 10 + fc - '0';
        }
    }

    action oscCommandSeparator {
        ragelAppendString(fc, maxOscBytes);
        if (!oscCommandValid) {
            fgoto oscInvalid;
        }
        oscPayloadOffset = argBuf.used();
        if (oscCommand == 0 || oscCommand == 1 || oscCommand == 2) {
            oscTitleHex = titleModes & 1;
            oscTitleHasHighNibble = false;
            oscTitleValid = true;
            oscTitleStopped = false;
            oscDecoded.reset();
            if (oscCommand == 0) {
                fgoto oscTitle0;
            }
            if (oscCommand == 1) {
                fgoto oscTitle1;
            }
            fgoto oscTitle2;
        } else if (oscCommand == 7) {
            oscDecoded.reset();
            oscCwdPercentHigh = 0;
            oscCwdValid = false;
            oscCwdDecode = false;
            fgoto oscCwdEntry;
        } else if (oscCommand == 4 || oscCommand == 5 || oscCommand == 6 ||
                   (oscCommand >= 10 && oscCommand <= 19) ||
                   oscCommand == 104 || oscCommand == 105 ||
                   oscCommand == 106) {
            oscFields.clear();
            oscFieldOffset = argBuf.used();
            oscFieldNumber = 0;
            oscFieldNumeric = true;
            fgoto oscFieldList;
        } else if (oscCommand == 8) {
            oscHyperlinkIdOffset = 0;
            oscHyperlinkIdLength = 0;
            oscHyperlinkUriOffset = 0;
            oscHyperlinkHasId = false;
            fgoto oscHyperlinkParamStart;
        } else if (oscCommand == 9) {
            oscProgressState = 0;
            oscProgressPercent = 0;
            oscProgressStatePresent = false;
            oscProgressPercentPresent = false;
            oscProgressValid = true;
            fgoto oscProgressEntry;
        } else if (oscCommand == 52) {
            oscBase64.reset();
            osc52ReplySelector = 0;
            osc52Primary = false;
            osc52Clipboard = false;
            osc52SelectorSeen = false;
            osc52PayloadSeen = false;
            osc52Query = false;
            fgoto osc52Selectors;
        } else if (oscCommand == 99) {
            oscNotificationFieldOffset = 0;
            oscNotificationIdOffset = 0;
            oscNotificationIdLength = 0;
            oscNotificationPayloadOffset = 0;
            oscNotificationPayloadBytes = 0;
            oscNotificationKey = 0;
            oscNotificationValid = true;
            oscNotificationEncoded = false;
            oscNotificationFinal = true;
            oscNotificationQuery = false;
            oscNotificationClose = false;
            oscNotificationBody = false;
            fgoto oscNotificationField;
        } else if (oscCommand == 133) {
            fgoto oscShellEntry;
        }
        fgoto oscPayload;
    }

    action oscCommandInvalid {
        stringUtf8Continuation(fc);
        ragelAppendString(fc, maxOscBytes);
        fgoto oscInvalid;
    }

    action oscCommandSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            oscTerminated = false;
            fgoto oscInvalid;
        } else {
            oscPayloadOffset = argBuf.used();
            ragelFinishOsc();
            oscTerminated = true;
        }
    }

    action oscCommandBell {
        oscPayloadOffset = argBuf.used();
        ragelFinishOsc();
        oscTerminated = true;
    }

    action oscData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
    }

    action oscSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            oscTerminated = false;
        } else {
            ragelFinishOsc();
            oscTerminated = true;
        }
    }

    action oscBell {
        ragelFinishOsc();
        oscTerminated = true;
    }

    action oscDispatch {
        if (oscTerminated && oscCommandValid && !argBufOverflowed) {
            const StringView payload = ragelOscPayload();
            if (oscCommand == 0) {
                osc_TITLE_0(payload);
            } else if (oscCommand == 1) {
                osc_TITLE_1(payload);
            } else if (oscCommand == 2) {
                osc_TITLE_2(payload);
            } else if (oscCommand == 8) {
                (void)payload;
            } else if (oscCommand == 9) {
                osc_NOTIFY(payload);
            } else if (oscCommand == 52) {
                osc_CLIPBOARD_MALFORMED(payload);
            } else if (oscCommand == 104) {
                osc_RESET_PALETTE();
            } else if (oscCommand == 105) {
                osc_RESET_SPECIAL_COLOR();
            } else if (oscCommand == 110) {
                osc_RESET_DEFAULT_FOREGROUND();
            } else if (oscCommand == 111) {
                osc_RESET_DEFAULT_BACKGROUND();
            } else if (oscCommand == 112) {
                osc_RESET_CURSOR_COLOR();
            } else if (oscCommand == 117) {
                osc_RESET_SELECTION_BACKGROUND();
            } else if (oscCommand == 119) {
                osc_RESET_SELECTION_FOREGROUND();
            } else if (oscCommand == 133) {
                osc_SHELL_UNKNOWN(payload);
            } else {
                osc_UNKNOWN(oscCommand, payload);
            }
        }
    }

    action oscDone {
        if (oscTerminated) {
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
        if (argBuf.used() < maxOscBytes) {
            const u8 ch = '\x1b';
            argBuf.append(&ch, 1);
        } else {
            argBufOverflowed = true;
        }
    }

    action oscEscapedData {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscPayload;
    }

    action oscInvalidSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
        } else {
            stringUtf8Remaining = 0;
            ragelStringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringEnd();
            }
            fnext main;
            fbreak;
        }
    }

    action oscInvalidBell {
        stringUtf8Remaining = 0;
        ragelStringLimit = 0;
        if constexpr (traced) {
            parserTrace->stringEnd();
        }
        fnext main;
        fbreak;
    }

    action oscInvalidEscapedData {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscInvalid;
    }

    action oscCwdRaw {
        ragelAppendString(fc, maxOscBytes);
    }

    action oscCwdInvalidData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
        oscCwdValid = false;
        oscCwdDecode = false;
        fgoto oscCwdInvalid;
    }

    action oscCwdPrefixError {
        fhold;
        fgoto oscCwdInvalid;
    }

    action oscCwdPathStart {
        ragelAppendString(fc, maxOscBytes);
        if (!argBufOverflowed) {
            oscDecoded.append(&fc, 1);
        }
        oscCwdValid = true;
        oscCwdDecode = true;
        fgoto oscCwdPath;
    }

    action oscCwdAuthorityData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
    }

    action oscCwdPathData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
            if (!argBufOverflowed) {
                oscDecoded.append(&fc, 1);
            }
        }
    }

    action oscCwdPercentStart {
        ragelAppendString(fc, maxOscBytes);
        oscCwdValid = false;
        oscCwdDecode = false;
        fgoto oscCwdPercentHigh;
    }

    action oscCwdPercentHigh {
        ragelAppendString(fc, maxOscBytes);
        oscCwdPercentHigh =
            fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10;
        fgoto oscCwdPercentLow;
    }

    action oscCwdPercentLow {
        ragelAppendString(fc, maxOscBytes);
        if (!argBufOverflowed) {
            const u8 decoded =
                (oscCwdPercentHigh << 4) |
                (fc <= '9' ? fc - '0' : (fc | 0x20) - 'a' + 10);
            oscDecoded.append(&decoded, 1);
        }
        oscCwdValid = true;
        oscCwdDecode = true;
        fgoto oscCwdPath;
    }

    action oscCwdSt {
        if (ragelStringContinuation(fc)) {
            oscTerminated = false;
        } else {
            ragelFinishOsc();
            oscTerminated = true;
        }
    }

    action oscCwdBell {
        ragelFinishOsc();
        oscTerminated = true;
    }

    action oscCwdDispatch {
        if (oscTerminated && !argBufOverflowed) {
            osc_CWD(
                ragelOscPayload(), StringView(oscDecoded), oscCwdValid
            );
        }
    }

    action oscCwdInvalidEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        oscCwdValid = false;
        oscCwdDecode = false;
        fgoto oscCwdInvalid;
    }

    action oscCwdAuthorityEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscCwdAuthority;
    }

    action oscCwdPathEscapedEscape {
        if (!argBufOverflowed) {
            const u8 escape = '\x1b';
            oscDecoded.append(&escape, 1);
        }
    }

    action oscCwdPathEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        if (!argBufOverflowed) {
            const u8 bytes[] = {'\x1b', (u8)(fc)};
            oscDecoded.append(bytes, sizeof(bytes));
        }
        fgoto oscCwdPath;
    }

    action oscShellA {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscShellAComplete;
    }

    action oscShellB {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscShellBComplete;
    }

    action oscShellC {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscShellCComplete;
    }

    action oscShellD {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscShellDComplete;
    }

    action oscShellInvalid {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
        fgoto oscShellUnknown;
    }

    action oscShellASt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            fgoto oscShellUnknown;
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed) {
                osc_SHELL_A(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellBSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            fgoto oscShellUnknown;
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed) {
                osc_SHELL_B(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellCSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            fgoto oscShellUnknown;
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed) {
                osc_SHELL_C(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellDSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            fgoto oscShellUnknown;
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed) {
                osc_SHELL_D(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellUnknownSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed) {
                osc_SHELL_UNKNOWN(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscShellEscapedUnknown {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscShellUnknown;
    }

    action oscShellEscapedA {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscShellATail;
    }

    action oscShellEscapedB {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscShellBTail;
    }

    action oscShellEscapedC {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscShellCTail;
    }

    action oscShellEscapedD {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscShellDTail;
    }

    action oscTitleData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
            if (oscTitleHex) {
                u8 nibble = 0;
                if (fc >= '0' && fc <= '9') {
                    nibble = fc - '0';
                } else if ((fc | 0x20) >= 'a' && (fc | 0x20) <= 'f') {
                    nibble = (fc | 0x20) - 'a' + 10;
                } else {
                    oscTitleValid = false;
                }
                if (oscTitleValid) {
                    if (!oscTitleHasHighNibble) {
                        oscTitleHighNibble = nibble;
                        oscTitleHasHighNibble = true;
                    } else {
                        const u8 decoded = (oscTitleHighNibble << 4) | nibble;
                        if (decoded < 32) {
                            oscTitleStopped = true;
                        } else if (!oscTitleStopped) {
                        if (!argBufOverflowed) {
                            oscDecoded.append(&decoded, 1);
                        }
                        }
                        oscTitleHasHighNibble = false;
                    }
                }
            }
        }
    }

    action oscTitleEscapedEscape {
        if constexpr (traced) {
            parserTrace->stringData((const u8*)("\x1b"), 1);
        }
        if (argBuf.used() < maxOscBytes) {
            const u8 ch = '\x1b';
            argBuf.append(&ch, 1);
        } else {
            argBufOverflowed = true;
        }
        if (oscTitleHex) {
            oscTitleValid = false;
        }
    }

    action oscTitleEscaped0 {
        ragelAppendEscapedString(fc, maxOscBytes);
        if (oscTitleHex) {
            oscTitleValid = false;
        }
        fgoto oscTitle0;
    }

    action oscTitleEscaped1 {
        ragelAppendEscapedString(fc, maxOscBytes);
        if (oscTitleHex) {
            oscTitleValid = false;
        }
        fgoto oscTitle1;
    }

    action oscTitleEscaped2 {
        ragelAppendEscapedString(fc, maxOscBytes);
        if (oscTitleHex) {
            oscTitleValid = false;
        }
        fgoto oscTitle2;
    }

    action oscTitle0St {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            if (oscTitleHex) {
                oscTitleValid = false;
            }
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed && oscTitleValid && (!oscTitleHex || !oscTitleHasHighNibble)) {
                osc_TITLE_0(oscTitleHex ? StringView(oscDecoded) : ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscTitle1St {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            if (oscTitleHex) {
                oscTitleValid = false;
            }
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed && oscTitleValid && (!oscTitleHex || !oscTitleHasHighNibble)) {
                osc_TITLE_1(oscTitleHex ? StringView(oscDecoded) : ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscTitle2St {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            if (oscTitleHex) {
                oscTitleValid = false;
            }
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed && oscTitleValid && (!oscTitleHex || !oscTitleHasHighNibble)) {
                osc_TITLE_2(oscTitleHex ? StringView(oscDecoded) : ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscHyperlinkParamData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
        fgoto oscHyperlinkParamSkip;
    }

    action oscHyperlinkI {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscHyperlinkParamI;
    }

    action oscHyperlinkD {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscHyperlinkParamId;
    }

    action oscHyperlinkEqual {
        ragelAppendString(fc, maxOscBytes);
        if (!oscHyperlinkHasId) {
            oscHyperlinkIdOffset = argBuf.used();
        }
        fgoto oscHyperlinkIdValue;
    }

    action oscHyperlinkColon {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscHyperlinkParamStart;
    }

    action oscHyperlinkIdColon {
        if (!oscHyperlinkHasId) {
            oscHyperlinkIdLength = argBuf.used() - oscHyperlinkIdOffset;
            oscHyperlinkHasId = true;
        }
        ragelAppendString(fc, maxOscBytes);
        fgoto oscHyperlinkParamStart;
    }

    action oscHyperlinkUri {
        ragelAppendString(fc, maxOscBytes);
        oscHyperlinkUriOffset = argBuf.used();
        fgoto oscHyperlinkUri;
    }

    action oscHyperlinkIdUri {
        if (!oscHyperlinkHasId) {
            oscHyperlinkIdLength = argBuf.used() - oscHyperlinkIdOffset;
            oscHyperlinkHasId = true;
        }
        ragelAppendString(fc, maxOscBytes);
        oscHyperlinkUriOffset = argBuf.used();
        fgoto oscHyperlinkUri;
    }

    action oscHyperlinkMalformedSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            fgoto oscHyperlinkParamSkip;
        } else {
            ragelFinishOsc();
            fnext main;
            fbreak;
        }
    }

    action oscHyperlinkSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed) {
                const auto* data = (const u8*)(argBuf.data());
                osc_HYPERLINK(
                    StringView(data + oscHyperlinkIdOffset, oscHyperlinkHasId ? oscHyperlinkIdLength : 0),
                    oscHyperlinkHasId,
                    StringView(data + oscHyperlinkUriOffset, argBuf.used() - oscHyperlinkUriOffset)
                );
            }
            fnext main;
            fbreak;
        }
    }

    action oscHyperlinkParamEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscHyperlinkParamSkip;
    }

    action oscHyperlinkIdEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscHyperlinkIdValue;
    }

    action oscHyperlinkUriEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscHyperlinkUri;
    }

    action oscProgressFour {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscProgressFour;
    }

    action oscProgressBeginState {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscProgressState;
    }

    action oscProgressNotifyData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
        fgoto oscProgressNotify;
    }

    action oscProgressStateDigit {
        ragelAppendString(fc, maxOscBytes);
        oscProgressStatePresent = true;
        if (oscProgressState > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            oscProgressValid = false;
        } else {
            oscProgressState = oscProgressState * 10 + fc - '0';
        }
    }

    action oscProgressBeginPercent {
        ragelAppendString(fc, maxOscBytes);
        oscProgressValid = oscProgressValid && oscProgressStatePresent;
        fgoto oscProgressPercent;
    }

    action oscProgressPercentDigit {
        ragelAppendString(fc, maxOscBytes);
        oscProgressPercentPresent = true;
        if (oscProgressPercent > (UINT32_MAX - (u32)(fc - '0')) / 10) {
            oscProgressValid = false;
        } else {
            oscProgressPercent = oscProgressPercent * 10 + fc - '0';
        }
    }

    action oscProgressDiscardData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
        fgoto oscProgressDiscard;
    }

    action oscProgressNotifySt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed) {
                osc_NOTIFY(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action oscProgressSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            oscProgressValid = false;
            fgoto oscProgressDiscard;
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed && oscProgressValid && oscProgressPercentPresent &&
                oscProgressState <= 4 && oscProgressPercent <= 100) {
                osc_PROGRESS(oscProgressState, oscProgressPercent);
            }
            fnext main;
            fbreak;
        }
    }

    action oscProgressDiscardSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
        } else {
            ragelFinishOsc();
            fnext main;
            fbreak;
        }
    }

    action oscProgressNotifyEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscProgressNotify;
    }

    action oscProgressDiscardEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscProgressDiscard;
    }

    action osc52Selector {
        ragelAppendString(fc, maxOscBytes);
        osc52SelectorSeen = true;
        if (osc52ReplySelector == 0 && (fc == 's' || fc == 'p' || fc == 'c')) {
            osc52ReplySelector = fc;
        }
        if (fc == 'p' || (fc == 's' && !opts.osc52SelectClipboard)) {
            osc52Primary = true;
        }
        if (fc == 'c' || (fc == 's' && opts.osc52SelectClipboard)) {
            osc52Clipboard = true;
        }
    }

    action osc52BeginPayload {
        ragelAppendString(fc, maxOscBytes);
        if (!osc52SelectorSeen) {
            osc52Primary = true;
            osc52Clipboard = true;
        }
        oscDecoded.reset();
        oscBase64.reset();
        osc52PayloadSeen = false;
        osc52Query = false;
        fgoto osc52Payload;
    }

    action osc52Data {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
            if (!osc52PayloadSeen) {
                osc52PayloadSeen = true;
                osc52Query = fc == '?';
                if (!osc52Query && !argBufOverflowed) {
                    oscBase64.push(fc, oscDecoded);
                }
            } else if (osc52Query) {
                osc52Query = false;
                oscBase64.valid = false;
            } else if (!argBufOverflowed) {
                oscBase64.push(fc, oscDecoded);
            }
        }
    }

    action osc52MalformedSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            fgoto oscInvalid;
        } else {
            ragelFinishOsc();
            if (!argBufOverflowed) {
                osc_CLIPBOARD_MALFORMED(ragelOscPayload());
            }
            fnext main;
            fbreak;
        }
    }

    action osc52MalformedBell {
        ragelFinishOsc();
        if (!argBufOverflowed) {
            osc_CLIPBOARD_MALFORMED(ragelOscPayload());
        }
        fnext main;
        fbreak;
    }

    action osc52St {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            oscTerminated = false;
            fgoto oscInvalid;
        } else {
            ragelFinishOsc();
            oscTerminated = true;
        }
    }

    action osc52Bell {
        ragelFinishOsc();
        oscTerminated = true;
    }

    action osc52Dispatch {
        if (oscTerminated && !argBufOverflowed) {
            const StringView raw = ragelOscPayload();
            if (osc52PayloadSeen && osc52Query) {
                osc_CLIPBOARD_QUERY(
                    raw, osc52Primary, osc52Clipboard, osc52ReplySelector,
                    !osc52SelectorSeen
                );
            } else {
                const bool valid = oscBase64.finish(oscDecoded);
                osc_CLIPBOARD_WRITE(
                    raw, StringView(oscDecoded), valid, osc52Primary,
                    osc52Clipboard
                );
            }
        }
    }

    action osc52SelectorEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscInvalid;
    }

    action osc52PayloadEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        osc52PayloadSeen = true;
        osc52Query = false;
        oscBase64.valid = false;
        fgoto osc52Payload;
    }

    action oscNotificationKey {
        ragelAppendString(fc, maxOscBytes);
        oscNotificationKey = fc;
        fgoto oscNotificationEqual;
    }

    action oscNotificationBeginValue {
        ragelAppendString(fc, maxOscBytes);
        oscNotificationFieldOffset = argBuf.used();
        fgoto oscNotificationValue;
    }

    action oscNotificationValueData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
            if (oscNotificationKey == 'i' &&
                !((fc >= 'a' && fc <= 'z') || (fc >= 'A' && fc <= 'Z') ||
                  (fc >= '0' && fc <= '9') || fc == '_' || fc == '-' ||
                  fc == '+' || fc == '.')) {
                oscNotificationValid = false;
            }
        }
    }

    action oscNotificationFinishField {
        const auto* data = (const u8*)(argBuf.data());
        const StringView value(
            data + oscNotificationFieldOffset,
            argBuf.used() - oscNotificationFieldOffset
        );
        if (oscNotificationKey == 'i') {
            oscNotificationIdOffset = oscNotificationFieldOffset;
            oscNotificationIdLength = value.length();
            if (value.length() > 256) {
                oscNotificationValid = false;
            }
        } else if (oscNotificationKey == 'p') {
            oscNotificationQuery = value == StringView(u8"?");
            oscNotificationClose = value == StringView(u8"close");
            oscNotificationBody = value == StringView(u8"body");
            if (!oscNotificationQuery && !oscNotificationClose &&
                !oscNotificationBody && value != StringView(u8"title")) {
                oscNotificationValid = false;
            }
        } else if (oscNotificationKey == 'e') {
            if (value == StringView(u8"0")) {
                oscNotificationEncoded = false;
            } else if (value == StringView(u8"1")) {
                oscNotificationEncoded = true;
            } else {
                oscNotificationValid = false;
            }
        } else if (oscNotificationKey == 'd') {
            if (value == StringView(u8"0")) {
                oscNotificationFinal = false;
            } else if (value == StringView(u8"1")) {
                oscNotificationFinal = true;
            } else {
                oscNotificationValid = false;
            }
        }
    }

    action oscNotificationNextField {
        ragelAppendString(fc, maxOscBytes);
        fgoto oscNotificationField;
    }

    action oscNotificationBeginPayload {
        ragelAppendString(fc, maxOscBytes);
        oscNotificationPayloadOffset = argBuf.used();
        oscNotificationPayloadBytes = 0;
        oscDecoded.reset();
        oscBase64.reset();
        if (oscNotificationEncoded && oscNotificationValid &&
            !oscNotificationQuery && !oscNotificationClose) {
            const auto* data = (const u8*)(argBuf.data());
            const StringView id(
                data + oscNotificationIdOffset, oscNotificationIdLength
            );
            oscBase64 = notificationDecoder(id, oscNotificationBody);
        }
        fgoto oscNotificationPayload;
    }

    action oscNotificationPayloadData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
            ++oscNotificationPayloadBytes;
            const u32 limit = oscNotificationEncoded ? 4096 : 2048;
            if (oscNotificationPayloadBytes > limit) {
                oscNotificationValid = false;
            } else if (oscNotificationEncoded && !argBufOverflowed) {
                oscBase64.push(fc, oscDecoded);
            }
        }
    }

    action oscNotificationInvalidData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
        oscNotificationValid = false;
        fgoto oscNotificationInvalid;
    }

    action oscNotificationSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            oscNotificationValid = false;
            oscTerminated = false;
            fgoto oscNotificationInvalid;
        } else {
            ragelFinishOsc();
            oscTerminated = true;
        }
    }

    action oscNotificationBell {
        ragelFinishOsc();
        oscTerminated = true;
    }

    action oscNotificationDispatch {
        if (oscTerminated && !argBufOverflowed && oscNotificationValid) {
            const auto* data = (const u8*)(argBuf.data());
            const StringView id(
                data + oscNotificationIdOffset, oscNotificationIdLength
            );
            const StringView payload(
                data + oscNotificationPayloadOffset,
                argBuf.used() - oscNotificationPayloadOffset
            );
            if (oscNotificationEncoded && oscNotificationFinal) {
                oscBase64.finish(oscDecoded);
            }
            if (oscNotificationQuery) {
                osc_NOTIFICATION_CAPABILITIES(id);
            } else if (oscNotificationClose) {
                osc_NOTIFICATION_CLOSE(id);
            } else if (oscNotificationBody) {
                osc_NOTIFICATION_BODY(
                    id,
                    oscNotificationEncoded ? StringView(oscDecoded) : payload,
                    oscBase64, oscNotificationEncoded, oscNotificationFinal
                );
            } else {
                osc_NOTIFICATION_TITLE(
                    id,
                    oscNotificationEncoded ? StringView(oscDecoded) : payload,
                    oscBase64, oscNotificationEncoded, oscNotificationFinal
                );
            }
        }
    }

    action oscNotificationInvalidSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
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
        ragelAppendEscapedString(fc, maxOscBytes);
        oscNotificationPayloadBytes += 2;
        const u32 limit = oscNotificationEncoded ? 4096 : 2048;
        if (oscNotificationPayloadBytes > limit) {
            oscNotificationValid = false;
        } else if (oscNotificationEncoded && !argBufOverflowed) {
            oscBase64.push('\x1b', oscDecoded);
            oscBase64.push(fc, oscDecoded);
        }
        fgoto oscNotificationPayload;
    }

    action oscNotificationInvalidEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        fgoto oscNotificationInvalid;
    }

    action oscFieldData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
            if (fc < '0' || fc > '9' ||
                oscFieldNumber > (UINT32_MAX - (u32)(fc - '0')) / 10) {
                oscFieldNumeric = false;
            } else {
                oscFieldNumber = oscFieldNumber * 10 + fc - '0';
            }
        }
    }

    action oscFieldSeparator {
        if (!argBufOverflowed) {
            oscFields.pushBack({
                oscFieldOffset,
                argBuf.used() - oscFieldOffset,
                oscFieldNumber,
                oscFieldNumeric && argBuf.used() != oscFieldOffset,
            });
        }
        ragelAppendString(fc, maxOscBytes);
        oscFieldOffset = argBuf.used();
        oscFieldNumber = 0;
        oscFieldNumeric = true;
    }

    action oscFieldSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
            oscTerminated = false;
            fgoto oscInvalid;
        } else {
            ragelFinishOsc();
            oscTerminated = true;
        }
    }

    action oscFieldBell {
        ragelFinishOsc();
        oscTerminated = true;
    }

    action oscFieldDispatch {
        if (oscTerminated && !argBufOverflowed) {
            if (argBuf.used() != oscFieldOffset) {
                oscFields.pushBack({
                    oscFieldOffset,
                    argBuf.used() - oscFieldOffset,
                    oscFieldNumber,
                    oscFieldNumeric,
                });
            }
            const auto* data = (const u8*)(argBuf.data());
            if (oscCommand == 4 || oscCommand == 5) {
                for (size_t i = 0; i + 1 < oscFields.length(); i += 2) {
                    if (!oscFields[i].numeric) {
                        continue;
                    }
                    const OscField& spec = oscFields[i + 1];
                    const StringView value(data + spec.offset, spec.length);
                    if (oscCommand == 4) {
                        osc_PALETTE(oscFields[i].number, value);
                    } else {
                        osc_SPECIAL_COLOR(oscFields[i].number, value);
                    }
                }
            } else if (oscCommand == 6 || oscCommand == 106) {
                for (size_t i = 0; i + 1 < oscFields.length(); i += 2) {
                    if (oscFields[i].numeric && oscFields[i + 1].numeric) {
                        osc_SPECIAL_COLOR_MODE(
                            oscFields[i].number, oscFields[i + 1].number
                        );
                    }
                }
            } else if (oscCommand >= 10 && oscCommand <= 19) {
                u32 command = oscCommand;
                for (size_t i = 0; i < oscFields.length() && command <= 19;
                     ++i, ++command) {
                    const OscField& spec = oscFields[i];
                    osc_DYNAMIC_COLOR(
                        command, StringView(data + spec.offset, spec.length)
                    );
                }
            } else if (oscCommand == 104) {
                if (oscFields.empty()) {
                    osc_RESET_PALETTE();
                } else {
                    for (const OscField& field : oscFields) {
                        if (field.numeric) {
                            osc_RESET_PALETTE(field.number);
                        }
                    }
                }
            } else if (oscCommand == 105) {
                if (oscFields.empty()) {
                    osc_RESET_SPECIAL_COLOR();
                } else {
                    for (const OscField& field : oscFields) {
                        if (field.numeric) {
                            osc_RESET_SPECIAL_COLOR(field.number);
                        }
                    }
                }
            }
        }
    }

    action oscFieldEscaped {
        ragelAppendEscapedString(fc, maxOscBytes);
        oscFieldNumeric = false;
        fgoto oscFieldList;
    }

    action ignoredData {
        stringUtf8Continuation(fc);
        if (executeC0InSequence(fc, true)) {
            if constexpr (traced) {
                parserTrace->stringData(&fc, 1);
            }
        } else if constexpr (traced) {
            parserTrace->stringData(&fc, 1);
        }
    }

    action ignoredSt {
        if (stringUtf8Continuation(fc)) {
            if constexpr (traced) {
                parserTrace->stringData(&fc, 1);
            }
        } else {
            stringUtf8Remaining = 0;
            ragelStringLimit = 0;
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
            stringUtf8Remaining = 0;
            ragelStringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringCancel();
                parserTrace->control(fc);
            }
            esc_SPA();
            fnext main;
            fbreak;
        }
    }

    action stringControlEpa {
        if (!ragelStringContinuation(fc)) {
            stringUtf8Remaining = 0;
            ragelStringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringCancel();
                parserTrace->control(fc);
            }
            esc_EPA();
            fnext main;
            fbreak;
        }
    }

    action stringControlDa {
        if (!ragelStringContinuation(fc)) {
            stringUtf8Remaining = 0;
            ragelStringLimit = 0;
            if constexpr (traced) {
                parserTrace->stringCancel();
            }
            csi_priDA();
            fnext main;
            fbreak;
        }
    }

    csiPlainKnown = [@ABCDEFGHIJKLMPSTXZ`abcdefghijklmnqrstu];
    csiPlainFinal = (
        'T' @csiTrace @{ if (nInputOps == 5 && mouseTrk.mode == MouseTrackingMode::VT200_Highlight) { csi_XTHIMOUSE(); } else { csi_SD(); } } |
        'A' @csiTrace @{ csi_CUU(); } |
        'B' @csiTrace @{ csi_CUD(); } |
        'C' @csiTrace @{ csi_CUF(); } |
        'D' @csiTrace @{ csi_CUB(); } |
        'E' @csiTrace @{ csi_CNL(); } |
        'F' @csiTrace @{ csi_CPL(); } |
        'G' @csiTrace @{ csi_CHA(); } |
        ('H' | 'f') @csiTrace @{ csi_CUP(); } |
        'I' @csiTrace @{ csi_CHT(); } |
        'J' @csiTrace @{ csi_ED(); } |
        'K' @csiTrace @{ csi_EL(); } |
        'L' @csiTrace @{ csi_IL(); } |
        'M' @csiTrace @{ csi_DL(); } |
        'P' @csiTrace @{ csi_DCH(); } |
        'S' @csiTrace @{ csi_SU(); } |
        'X' @csiTrace @{ csi_ECH(); } |
        'Z' @csiTrace @{ csi_CBT(); } |
        '@' @csiTrace @{ csi_ICH(); } |
        '`' @csiTrace @{ csi_HPA(); } |
        'a' @csiTrace @{ csi_HPR(); } |
        'b' @csiTrace @{ csi_REP(); } |
        'c' @csiTrace @{ csi_priDA(); } |
        'd' @csiTrace @{ csi_VPA(); } |
        'e' @csiTrace @{ csi_VPR(); } |
        'g' @csiTrace @{ csi_TBC(); } |
        'h' @csiTrace @{ csi_SM(); } |
        'i' @csiTrace @{ csi_MC(false); } |
        'j' @csiTrace @{ csi_CUB(); } |
        'k' @csiTrace @{ csi_CUU(); } |
        'l' @csiTrace @{ csi_RM(); } |
        'm' @csiTrace @{ csi_SGR(); } |
        'n' @csiTrace @{ csi_DSR(); } |
        'q' @csiTrace @{ csi_DECLL(); } |
        'r' @csiTrace @{ csi_STBM(); } |
        's' @csiTrace @{ csi_SCOSC_SLRM(); } |
        't' @csiTrace @{ csi_XTWINOPS(); } |
        'u' @csiTrace @{ csi_SCORC(); } |
        (0x40..0x7e - csiPlainKnown) @csiTrace
    ) @csiDone;

    csiGreaterKnown = [Tcmqtu];
    csiGreaterFinal = (
        'T' @csiTrace @{ csi_XTTITLEMODE(false); } |
        'c' @csiTrace @{ csi_secDA(); } |
        'm' @csiTrace @{ csi_XTMODKEYS(); } |
        'q' @csiTrace @{ csi_XTVERSION(); } |
        't' @csiTrace @{ csi_XTTITLEMODE(true); } |
        'u' @csiTrace @{ csi_kittyKeyboardPush(); } |
        (0x40..0x7e - csiGreaterKnown) @csiTrace
    ) @csiDone;

    csiLessFinal = (
        'u' @csiTrace @{ csi_kittyKeyboardPop(); } |
        (0x40..0x7e - 'u') @csiTrace
    ) @csiDone;

    csiEqualKnown = [cu];
    csiEqualFinal = (
        'c' @csiTrace @{ csi_terDA(); } |
        'u' @csiTrace @{ csi_kittyKeyboardSet(); } |
        (0x40..0x7e - csiEqualKnown) @csiTrace
    ) @csiDone;

    csiQuestionKnown = [JKhilmnrsu];
    csiQuestionFinal = (
        'J' @csiTrace @{ csi_DECSED(); } |
        'K' @csiTrace @{ csi_DECSEL(); } |
        'h' @csiTrace @{ csi_privSM(); } |
        'i' @csiTrace @{ csi_MC(true); } |
        'l' @csiTrace @{ csi_privRM(); } |
        'm' @csiTrace @{ csi_XTQMODKEYS(); } |
        'n' @csiTrace @{ csi_DSR(true); } |
        'r' @csiTrace @{ csi_privRestore(); } |
        's' @csiTrace @{ csi_privSave(); } |
        'u' @csiTrace @{ csi_kittyKeyboardQuery(); } |
        (0x40..0x7e - csiQuestionKnown) @csiTrace
    ) @csiDone;

    csiBangFinal = (
        'p' @csiTrace @{ csi_DECSTR(); } |
        (0x40..0x7e - 'p') @csiTrace
    ) @csiDone;

    csiQuoteKnown = [pq];
    csiQuoteFinal = (
        'p' @csiTrace @{ csiq_DECSCL(); } |
        'q' @csiTrace @{ csi_DECSCA(); } |
        (0x40..0x7e - csiQuoteKnown) @csiTrace
    ) @csiDone;

    csiSpaceKnown = [@Aq];
    csiSpaceFinal = (
        '@' @csiTrace @{ csi_ecma48_SL(); } |
        'A' @csiTrace @{ csi_ecma48_SR(); } |
        'q' @csiTrace @{ csi_DECSCUSR(); } |
        (0x40..0x7e - csiSpaceKnown) @csiTrace
    ) @csiDone;

    csiApostropheKnown = [wz-~];
    csiApostropheFinal = (
        'w' @csiTrace @{ csi_DECEFR(); } |
        'z' @csiTrace @{ csi_DECELR(); } |
        '{' @csiTrace @{ csi_DECSLE(); } |
        '|' @csiTrace @{ csi_DECRQLP(); } |
        '}' @csiTrace @{ csi_DECIC(); } |
        '~' @csiTrace @{ csi_DECDC(); } |
        (0x40..0x7e - csiApostropheKnown) @csiTrace
    ) @csiDone;

    csiDollarKnown = [prtvxz{];
    csiDollarFinal = (
        'p' @csiTrace @{ csi_DECRQM(false); } |
        'r' @csiTrace @{ csi_DECCARA(false); } |
        't' @csiTrace @{ csi_DECCARA(true); } |
        'v' @csiTrace @{ csi_DECCRA(); } |
        'x' @csiTrace @{ csi_DECFRA(); } |
        'z' @csiTrace @{ csi_DECERA(); } |
        '{' @csiTrace @{ csi_DECERA(true); } |
        (0x40..0x7e - csiDollarKnown) @csiTrace
    ) @csiDone;

    csiStarFinal = (
        'y' @csiTrace @{ csi_DECRQCRA(); } |
        (0x40..0x7e - 'y') @csiTrace
    ) @csiDone;

    csiQuestionDollarFinal = (
        'p' @csiTrace @{ csi_DECRQM(true); } |
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
        ('(' | ')' | '*' | '+' | '-' | '.' | '/' | ',' | '$') @escapeCharset |
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
        'F' @specialFinal @{ if (compatLevel >= CompatibilityLevel::VT200) { send8BitControls = false; } fnext main; fbreak; } |
        'G' @specialFinal @{ if (compatLevel >= CompatibilityLevel::VT200) { send8BitControls = true; } fnext main; fbreak; } |
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
        '3' @specialFinal @{ setLineAttribute(1); fnext main; fbreak; } |
        '4' @specialFinal @{ setLineAttribute(2); fnext main; fbreak; } |
        '5' @specialFinal @{ setLineAttribute(0); fnext main; fbreak; } |
        '6' @specialFinal @{ setLineAttribute(3); fnext main; fbreak; } |
        '8' @specialFinal @{ esch_DECALN(); fnext main; fbreak; } |
        any @specialFinal @vt52Unhandled
    )*;

    escapePercent := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        0x20..0x2f @specialIntermediate |
        '@' @specialFinal @{ charsetState = CharsetState{}; charsetState.g[charsetState.gr] = Charset::IsoLatin1; charsetState.g[3] = Charset::IsoLatin1; fnext main; fbreak; } |
        'G' @specialFinal @{ charsetState = CharsetState{}; fnext main; fbreak; } |
        any @specialFinal @vt52Unhandled
    )*;

    selectCharset := (
        cancel |
        restartEscape |
        c1Dispatch |
        0x7f |
        highToGround |
        0x00..0x2f @charsetModifier |
        0x30..0x7e @charsetFinal |
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

    dcsDecrqssEntry := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssUnknownSt |
        0x1b @dcsDecrqssEscape |
        0x7f |
        sequenceC0 |
        '"' @dcsDecrqssQuote |
        ' ' @dcsDecrqssSpace |
        'm' @dcsDecrqssSgr |
        'r' @dcsDecrqssDecstbm |
        's' @dcsDecrqssDecslrm |
        't' @dcsDecrqssDecslpp |
        (0x20..0x7e - ('"' | ' ' | 'm' | 'r' | 's' | 't')) @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssQuote := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssUnknownSt |
        0x1b @dcsDecrqssEscape |
        0x7f |
        sequenceC0 |
        'p' @dcsDecrqssDecscl |
        'q' @dcsDecrqssDecsca |
        (0x20..0x7e - ('p' | 'q')) @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssSpace := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssUnknownSt |
        0x1b @dcsDecrqssEscape |
        0x7f |
        sequenceC0 |
        'q' @dcsDecrqssDecscusr |
        (0x20..0x7e - 'q') @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssDecsclComplete := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecsclSt |
        0x1b @{ fgoto dcsDecrqssDecsclEscape; } |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssSgrComplete := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssSgrSt |
        0x1b @{ fgoto dcsDecrqssSgrEscape; } |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssDecstbmComplete := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecstbmSt |
        0x1b @{ fgoto dcsDecrqssDecstbmEscape; } |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssDecslrmComplete := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecslrmSt |
        0x1b @{ fgoto dcsDecrqssDecslrmEscape; } |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssDecslppComplete := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecslppSt |
        0x1b @{ fgoto dcsDecrqssDecslppEscape; } |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssDecscusrComplete := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecscusrSt |
        0x1b @{ fgoto dcsDecrqssDecscusrEscape; } |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssDecscaComplete := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecscaSt |
        0x1b @{ fgoto dcsDecrqssDecscaEscape; } |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

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

    dcsDecrqssDecsclEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecsclSt |
        '\\' @dcsDecrqssDecsclSt |
        0x1b @dcsDecrqssEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsDecrqssEscapedData
    )*;

    dcsDecrqssSgrEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssSgrSt |
        '\\' @dcsDecrqssSgrSt |
        0x1b @dcsDecrqssEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsDecrqssEscapedData
    )*;

    dcsDecrqssDecstbmEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecstbmSt |
        '\\' @dcsDecrqssDecstbmSt |
        0x1b @dcsDecrqssEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsDecrqssEscapedData
    )*;

    dcsDecrqssDecslrmEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecslrmSt |
        '\\' @dcsDecrqssDecslrmSt |
        0x1b @dcsDecrqssEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsDecrqssEscapedData
    )*;

    dcsDecrqssDecslppEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecslppSt |
        '\\' @dcsDecrqssDecslppSt |
        0x1b @dcsDecrqssEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsDecrqssEscapedData
    )*;

    dcsDecrqssDecscusrEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecscusrSt |
        '\\' @dcsDecrqssDecscusrSt |
        0x1b @dcsDecrqssEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsDecrqssEscapedData
    )*;

    dcsDecrqssDecscaEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssDecscaSt |
        '\\' @dcsDecrqssDecscaSt |
        0x1b @dcsDecrqssEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\' | 0x90 | 0x96..0x98 |
                0x9a..0x9f)) @dcsDecrqssEscapedData
    )*;

    dcsXtgettcap := (
        cancel |
        stringC1 |
        0x9c @dcsXtSt |
        0x1b @dcsXtEscape |
        0x7f |
        sequenceC0 |
        xdigit @dcsXtHex |
        ';' @dcsXtSeparator |
        (0x20..0x7e - (xdigit | ';')) @dcsXtInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsXtInvalid
    )*;

    dcsXtEscape := (
        cancel |
        stringC1 |
        0x9c @dcsXtSt |
        '\\' @dcsXtSt |
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
         0x91..0x95 | 0x99 | 0xa0..0xff)
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

    oscFieldList := (
        cancel |
        stringC1 |
        0x9c @oscFieldSt @oscFieldDispatch @oscDone |
        0x07 @oscFieldBell @oscFieldDispatch @oscDone |
        0x1b @{ fgoto oscFieldEscape; } |
        0x7f |
        ';' @oscFieldSeparator |
        (0x00..0x06 | 0x08..0x17 | 0x19 | (0x1c..0x7e - ';') |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscFieldData
    )*;

    oscFieldEscape := (
        cancel |
        '\\' @oscFieldSt @oscFieldDispatch @oscDone |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscFieldEscaped
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
        ';' @{ ragelAppendString(fc, maxOscBytes); fgoto oscShellATail; } |
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
        ';' @{ ragelAppendString(fc, maxOscBytes); fgoto oscShellBTail; } |
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
        ';' @{ ragelAppendString(fc, maxOscBytes); fgoto oscShellCTail; } |
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
        ';' @{ ragelAppendString(fc, maxOscBytes); fgoto oscShellDTail; } |
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
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
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
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
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
        '\\' @{ stringUtf8Remaining = 0; ragelStringLimit = 0; if constexpr (traced) { parserTrace->stringEnd(); } fnext main; fbreak; } |
        0x1b |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @ignoredEscapedData
    )*;

}%%

%% write data;

if (!ragelInitialized) {
    %% write init;
    ragelInitialized = true;
}

while (p != pe) {
    if (cs == vterm_parser_en_printer) {
        const size_t remaining = pe - p;
        const u8* escape = (const u8*)memchr(p, 0x1b, remaining);
        const size_t beforeEscape = escape == nullptr ? remaining : escape - p;
        const u8* csi = (const u8*)memchr(p, 0x9b, beforeEscape);
        const u8* next = csi == nullptr ? escape : csi;
        const size_t count = next == nullptr ? remaining : next - p;
        appendPrinter(p, count);
        p += count;
        if (p == pe) {
            continue;
        }
    }
    if (cs == vterm_parser_en_main && *p >= 0x20 && *p < 0x7f && !utf8dec.expectsContinuation() && charsetState.ss == 0 && charsetState.g[charsetState.gl] == Charset::UTF8) {
        const size_t lines = placeAsciiLines(p, pe - p);
        if (lines != 0) {
            p += lines;
            continue;
        }
        const size_t count = printableAsciiPrefix(p, pe - p);
        if constexpr (traced) {
            parserTrace->text(p, count);
        }
        if (insertMode) {
            placeAsciiRun<true>(p, count);
        } else {
            placeAsciiRun<false>(p, count);
        }
        p += count;
        if (p + 1 < pe && p[0] == '\r' && p[1] == '\n') {
            if constexpr (traced) {
                parserTrace->control('\r');
                parserTrace->control('\n');
            }
            resetGraphemeInput();
            inp_CR();
            if (autoNewlineMode) {
                inp_CR();
            }
            esc_IND();
            p += 2;
        }
        continue;
    }
    if (cs == vterm_parser_en_main && *p >= 0xc2 && *p <= 0xf4 && !insertMode && !utf8dec.expectsContinuation() && charsetState.ss == 0 && charsetState.g[charsetState.gl] == Charset::UTF8 && charsetState.g[charsetState.gr] == Charset::UTF8) {
        const int consumed = placeUtf8Run(p, pe - p);
        if (consumed > 0) {
            if constexpr (traced) {
                parserTrace->text(p, consumed);
            }
            p += consumed;
            continue;
        }
    }
    %% write exec;
}
