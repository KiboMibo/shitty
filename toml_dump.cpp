/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 *
 * Test tool: parses a TOML document from stdin and prints the toml-test
 * JSON encoding of it, or exits non-zero on any parse or semantic error.
 * The parser proper is a syntax-level SAX machine; the table/array
 * redefinition rules of TOML live in the consumer, and this tool carries
 * the full set so the toml-test corpus exercises both layers.
 */

#include "toml.h"

#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace stl;

namespace {
    struct Node;

    using NodeRef = std::unique_ptr<Node>;

    struct Node {
        enum class Kind {
            Table,
            Array,
            Scalar
        };

        Kind kind;
        TomlType type;
        std::string text;
        std::vector<std::pair<std::string, NodeRef>> entries;
        std::vector<NodeRef> items;
        bool inlineClosed;
        bool explicitHeader;
        bool dotted;
        bool tableArray;

        Node(Kind kind);

        Node* find(const std::string& name);
        Node* addTable(const std::string& name);
    };

    struct Frame {
        Node* container;
        std::vector<std::string> pendingPath;
    };

    struct DomSink: public TomlSink {
        Node root;
        Node* current;
        std::vector<std::string> pendingPath;
        std::vector<Frame> stack;
        bool broken;

        DomSink();

        bool tomlTable(const StringView* path, size_t count, bool array) override;
        bool tomlKey(const StringView* path, size_t count) override;
        bool tomlScalar(TomlType type, StringView text) override;
        bool tomlArrayBegin() override;
        bool tomlArrayEnd() override;
        bool tomlInlineTableBegin() override;
        bool tomlInlineTableEnd() override;
        void tomlError(size_t line, StringView message) override;

        bool place(NodeRef node);
        bool attach(Node* table, const std::vector<std::string>& path, NodeRef node);
    };
}

Node::Node(Kind kind)
    : kind(kind)
    , type(TomlType::String)
    , inlineClosed(false)
    , explicitHeader(false)
    , dotted(false)
    , tableArray(false)
{
}

Node* Node::find(const std::string& name) {
    for (auto& entry : entries) {
        if (entry.first == name) {
            return entry.second.get();
        }
    }
    return nullptr;
}

Node* Node::addTable(const std::string& name) {
    entries.emplace_back(name, std::make_unique<Node>(Kind::Table));
    return entries.back().second.get();
}

DomSink::DomSink()
    : root(Node::Kind::Table)
    , current(&root)
    , broken(false)
{
}

bool DomSink::tomlTable(const StringView* path, size_t count, bool array) {
    Node* at = &root;
    for (size_t i = 0; i + 1 < count; ++i) {
        const std::string name((const char*)path[i].data(), path[i].length());
        Node* next = at->find(name);
        if (next == nullptr) {
            next = at->addTable(name);
        }
        // Traversing THROUGH a dotted-defined table on the way to a deeper
        // header is legal; only naming one as the header target is not.
        if (next->kind == Node::Kind::Array && next->tableArray) {
            next = next->items.back().get();
        } else if (next->kind != Node::Kind::Table || next->inlineClosed) {
            return false;
        }
        at = next;
    }
    const std::string name((const char*)path[count - 1].data(), path[count - 1].length());
    Node* target = at->find(name);
    if (array) {
        if (target == nullptr) {
            target = at->entries.emplace_back(name, std::make_unique<Node>(Node::Kind::Array)).second.get();
            target->tableArray = true;
        }
        if (target->kind != Node::Kind::Array || !target->tableArray) {
            return false;
        }
        target->items.emplace_back(std::make_unique<Node>(Node::Kind::Table));
        current = target->items.back().get();
        current->explicitHeader = true;
        return true;
    }
    if (target == nullptr) {
        target = at->addTable(name);
        target->explicitHeader = true;
        current = target;
        return true;
    }
    if (target->kind != Node::Kind::Table || target->inlineClosed || target->dotted || target->explicitHeader) {
        return false;
    }
    target->explicitHeader = true;
    current = target;
    return true;
}

bool DomSink::tomlKey(const StringView* path, size_t count) {
    std::vector<std::string>* into = &pendingPath;
    if (!stack.empty()) {
        into = &stack.back().pendingPath;
    }
    into->clear();
    for (size_t i = 0; i < count; ++i) {
        into->emplace_back((const char*)path[i].data(), path[i].length());
    }
    return true;
}

bool DomSink::tomlScalar(TomlType type, StringView text) {
    auto node = std::make_unique<Node>(Node::Kind::Scalar);
    node->type = type;
    node->text.assign((const char*)text.data(), text.length());
    return place(std::move(node));
}

bool DomSink::tomlArrayBegin() {
    stack.push_back(Frame{new Node(Node::Kind::Array), {}});
    return true;
}

bool DomSink::tomlArrayEnd() {
    NodeRef node(stack.back().container);
    stack.pop_back();
    return place(std::move(node));
}

bool DomSink::tomlInlineTableBegin() {
    stack.push_back(Frame{new Node(Node::Kind::Table), {}});
    return true;
}

bool DomSink::tomlInlineTableEnd() {
    NodeRef node(stack.back().container);
    stack.pop_back();
    node->inlineClosed = true;
    return place(std::move(node));
}

bool DomSink::place(NodeRef node) {
    if (stack.empty()) {
        return attach(current, pendingPath, std::move(node));
    }
    Frame& frame = stack.back();
    if (frame.container->kind == Node::Kind::Array) {
        frame.container->items.push_back(std::move(node));
        return true;
    }
    return attach(frame.container, frame.pendingPath, std::move(node));
}

bool DomSink::attach(Node* table, const std::vector<std::string>& path, NodeRef node) {
    Node* at = table;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        Node* next = at->find(path[i]);
        if (next == nullptr) {
            next = at->addTable(path[i]);
            next->dotted = true;
        } else if (next->kind != Node::Kind::Table || next->inlineClosed || next->explicitHeader || !next->dotted) {
            return false;
        }
        at = next;
    }
    if (at->find(path.back()) != nullptr) {
        return false;
    }
    at->entries.emplace_back(path.back(), std::move(node));
    return true;
}

void DomSink::tomlError(size_t line, StringView message) {
    fprintf(stderr, "toml: line %zu: %.*s\n", line, (int)message.length(), (const char*)message.data());
    broken = true;
}

namespace {
    void printJsonString(const std::string& text) {
        putchar('"');
        for (unsigned char byte : text) {
            if (byte == '"' || byte == '\\') {
                putchar('\\');
                putchar(byte);
            } else if (byte == '\n') {
                fputs("\\n", stdout);
            } else if (byte == '\r') {
                fputs("\\r", stdout);
            } else if (byte == '\t') {
                fputs("\\t", stdout);
            } else if (byte < 0x20 || byte == 0x7f) {
                printf("\\u%04x", byte);
            } else {
                putchar(byte);
            }
        }
        putchar('"');
    }

    bool renderInteger(const std::string& text, std::string& value) {
        errno = 0;
        long long parsed = 0;
        if (text.size() > 2 && (text[1] == 'x' || text[1] == 'o' || text[1] == 'b')) {
            const int base = text[1] == 'x' ? 16 : text[1] == 'o' ? 8 : 2;
            const unsigned long long wide = strtoull(text.c_str() + 2, nullptr, base);
            if (errno == ERANGE || wide > (unsigned long long)INT64_MAX) {
                return false;
            }
            parsed = (long long)wide;
        } else {
            parsed = strtoll(text.c_str(), nullptr, 10);
            if (errno == ERANGE) {
                return false;
            }
        }
        char formatted[32];
        snprintf(formatted, sizeof(formatted), "%lld", parsed);
        value = formatted;
        return true;
    }

    void renderFloat(const std::string& text, std::string& value) {
        const double parsed = strtod(text.c_str(), nullptr);
        if (parsed != parsed) {
            value = "nan";
            return;
        }
        if (parsed > 1.7976931348623157e308) {
            value = "inf";
            return;
        }
        if (parsed < -1.7976931348623157e308) {
            value = "-inf";
            return;
        }
        char formatted[64];
        snprintf(formatted, sizeof(formatted), "%.17g", parsed);
        value = formatted;
    }

    const char* typeName(TomlType type) {
        switch (type) {
            case TomlType::String:
                return "string";
            case TomlType::Integer:
                return "integer";
            case TomlType::Float:
                return "float";
            case TomlType::Boolean:
                return "bool";
            case TomlType::OffsetDatetime:
                return "datetime";
            case TomlType::LocalDatetime:
                return "datetime-local";
            case TomlType::LocalDate:
                return "date-local";
            case TomlType::LocalTime:
                return "time-local";
        }
        return "string";
    }

    bool printNode(const Node& node) {
        if (node.kind == Node::Kind::Scalar) {
            std::string value = node.text;
            if (node.type == TomlType::Integer && !renderInteger(node.text, value)) {
                return false;
            }
            if (node.type == TomlType::Float) {
                renderFloat(node.text, value);
            }
            fputs("{\"type\":", stdout);
            printJsonString(typeName(node.type));
            fputs(",\"value\":", stdout);
            printJsonString(value);
            putchar('}');
            return true;
        }
        if (node.kind == Node::Kind::Array) {
            putchar('[');
            bool first = true;
            for (const auto& item : node.items) {
                if (!first) {
                    putchar(',');
                }
                first = false;
                if (!printNode(*item)) {
                    return false;
                }
            }
            putchar(']');
            return true;
        }
        putchar('{');
        bool first = true;
        for (const auto& entry : node.entries) {
            if (!first) {
                putchar(',');
            }
            first = false;
            printJsonString(entry.first);
            putchar(':');
            if (!printNode(*entry.second)) {
                return false;
            }
        }
        putchar('}');
        return true;
    }
}

int main() {
    std::string input;
    char chunk[4096];
    size_t got = 0;
    while ((got = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        input.append(chunk, got);
    }
    DomSink sink;
    if (!parseToml(StringView((const u8*)input.data(), input.size()), sink)) {
        if (!sink.broken) {
            fprintf(stderr, "toml: document breaks table or key rules\n");
        }
        return 1;
    }
    if (!printNode(sink.root)) {
        fprintf(stderr, "toml: integer out of range\n");
        return 1;
    }
    putchar('\n');
    return 0;
}
