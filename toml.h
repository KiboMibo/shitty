/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

#include <stddef.h>

enum class TomlType : u8 {
    String,
    Integer,
    Float,
    Boolean,
    OffsetDatetime,
    LocalDatetime,
    LocalDate,
    LocalTime,
};

// SAX sink for parseToml. Events arrive in document order; every callback
// returning false aborts the parse. StringView arguments point into scratch
// storage owned by the parser and are valid only during the callback.
//
// Scalar text is normalized: strings are fully unescaped UTF-8, numbers have
// their underscores stripped, booleans are "true"/"false", datetimes keep
// their source spelling.
struct TomlSink {
    // [path] or [[path]] header; array distinguishes the [[...]] form.
    virtual bool tomlTable(const stl::StringView* path, size_t count, bool array) = 0;
    // Dotted key of the value that follows, relative to the innermost open
    // table or inline table.
    virtual bool tomlKey(const stl::StringView* path, size_t count) = 0;
    virtual bool tomlScalar(TomlType type, stl::StringView text) = 0;
    virtual bool tomlArrayBegin() = 0;
    virtual bool tomlArrayEnd() = 0;
    virtual bool tomlInlineTableBegin() = 0;
    virtual bool tomlInlineTableEnd() = 0;
    virtual void tomlError(size_t line, stl::StringView message) = 0;
};

// Parses one whole TOML 1.0 document. Returns false after reporting
// tomlError, or when a callback aborted the parse.
bool parseToml(stl::StringView text, TomlSink& sink);
