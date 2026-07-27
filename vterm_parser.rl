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
        ragelBeginString(VtermTraceString::Osc, true);
        fgoto osc;
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
        csi_CUB();
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
        ragelBeginString(VtermTraceString::Osc, true);
        fgoto osc;
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
            dcsCommand = DcsCommand::Decrqss;
            decrqssQuery = DecrqssQuery::Unknown;
            fgoto dcsDecrqssEntry;
        } else if (dcsIntermediateCount == 1 && dcsIntermediates[0] == '+' && fc == 'q') {
            dcsCommand = DcsCommand::Xtgettcap;
            dcsCapabilityOffset = argBuf.used();
            dcsCapabilityDecodedLength = 0;
            dcsCapabilityCandidates = 0x0f;
            dcsCapabilityHasHighNibble = false;
            dcsCapabilityValid = true;
            fgoto dcsXtgettcap;
        } else if (dcsIntermediateCount == 0 && fc == '|') {
            dcsCommand = DcsCommand::Decudk;
            dcsUdkValueOffset = dcsDecoded.used();
            dcsUdkCode = 0;
            dcsUdkHasCode = false;
            dcsUdkHasHighNibble = false;
            dcsUdkValid = true;
            dcsUdkInValue = false;
            fgoto dcsUdkCode;
        } else {
            dcsCommand = DcsCommand::Ignore;
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
        decrqssQuery = DecrqssQuery::Decscl;
        fgoto dcsDecrqssComplete;
    }

    action dcsDecrqssSgr {
        ragelAppendString(fc, maxDcsBytes);
        decrqssQuery = DecrqssQuery::Sgr;
        fgoto dcsDecrqssComplete;
    }

    action dcsDecrqssDecstbm {
        ragelAppendString(fc, maxDcsBytes);
        decrqssQuery = DecrqssQuery::Decstbm;
        fgoto dcsDecrqssComplete;
    }

    action dcsDecrqssDecslrm {
        ragelAppendString(fc, maxDcsBytes);
        decrqssQuery = DecrqssQuery::Decslrm;
        fgoto dcsDecrqssComplete;
    }

    action dcsDecrqssDecslpp {
        ragelAppendString(fc, maxDcsBytes);
        decrqssQuery = DecrqssQuery::Decslpp;
        fgoto dcsDecrqssComplete;
    }

    action dcsDecrqssDecscusr {
        ragelAppendString(fc, maxDcsBytes);
        decrqssQuery = DecrqssQuery::Decscusr;
        fgoto dcsDecrqssComplete;
    }

    action dcsDecrqssDecsca {
        ragelAppendString(fc, maxDcsBytes);
        decrqssQuery = DecrqssQuery::Decsca;
        fgoto dcsDecrqssComplete;
    }

    action dcsDecrqssInvalid {
        stringUtf8Continuation(fc);
        ragelAppendString(fc, maxDcsBytes);
        decrqssQuery = DecrqssQuery::Unknown;
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
        decrqssQuery = DecrqssQuery::Unknown;
    }

    action dcsDecrqssEscapedData {
        ragelAppendEscapedString(fc, maxDcsBytes);
        decrqssQuery = DecrqssQuery::Unknown;
        fgoto dcsDecrqssInvalid;
    }

    action dcsDecrqssSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxDcsBytes);
            decrqssQuery = DecrqssQuery::Unknown;
            fgoto dcsDecrqssInvalid;
        } else {
            ragelFinishDcs();
            fnext main;
            fbreak;
        }
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
        dcsCapabilityRequests.pushBack({
            dcsCapabilityOffset,
            argBuf.used() - dcsCapabilityOffset,
            capability,
        });
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
            dcsCapabilityRequests.pushBack({
                dcsCapabilityOffset,
                argBuf.used() - dcsCapabilityOffset,
                capability,
            });
            ragelFinishDcs();
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
            if (dcsDecoded.used() - dcsUdkValueOffset < 255) {
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
        if (dcsUdkValid && !dcsUdkHasHighNibble) {
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
            if (dcsUdkInValue && dcsUdkValid && !dcsUdkHasHighNibble) {
                dcsUdkDefinitions.pushBack({
                    dcsUdkValueOffset,
                    dcsDecoded.used() - dcsUdkValueOffset,
                    dcsUdkCode,
                });
            }
            ragelFinishDcs();
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

    action oscData {
        stringUtf8Continuation(fc);
        if (!executeC0InSequence(fc, true)) {
            ragelAppendString(fc, maxOscBytes);
        }
    }

    action oscSt {
        if (stringUtf8Continuation(fc)) {
            ragelAppendString(fc, maxOscBytes);
        } else {
            ragelFinishOsc();
            fnext main;
            fbreak;
        }
    }

    action oscBell {
        ragelFinishOsc();
        fnext main;
        fbreak;
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
        fgoto osc;
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
            ragelBeginString(VtermTraceString::Osc, true);
            fgoto osc;
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
        0x9c @dcsDecrqssSt |
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
        0x9c @dcsDecrqssSt |
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
        0x9c @dcsDecrqssSt |
        0x1b @dcsDecrqssEscape |
        0x7f |
        sequenceC0 |
        'q' @dcsDecrqssDecscusr |
        (0x20..0x7e - 'q') @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssComplete := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssSt |
        0x1b @dcsDecrqssEscape |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssInvalid := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssSt |
        0x1b @dcsDecrqssEscape |
        0x7f |
        sequenceC0 |
        0x20..0x7e @dcsDecrqssInvalid |
        (0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @dcsDecrqssInvalid
    )*;

    dcsDecrqssEscape := (
        cancel |
        stringC1 |
        0x9c @dcsDecrqssSt |
        '\\' @dcsDecrqssSt |
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

    osc := (
        cancel |
        stringC1 |
        0x9c @oscSt |
        0x07 @oscBell |
        0x1b @oscEscape |
        0x7f |
        (0x00..0x06 | 0x08..0x17 | 0x19 | 0x1c..0x7e |
         0x80..0x8f | 0x91..0x95 | 0x99 | 0xa0..0xff) @oscData
    )*;

    oscEscape := (
        cancel |
        '\\' @{ ragelFinishOsc(); fnext main; fbreak; } |
        0x1b @oscEscapedEscape |
        (any - (0x18 | 0x1a | 0x1b | '\\')) @oscEscapedData
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
    if (printerControllerMode) {
        p += consumePrinterController(p, pe - p);
        continue;
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
