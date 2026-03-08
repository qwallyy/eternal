#include "eternal/config/KDLParser.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

namespace eternal {

// ===========================================================================
// KDLDocument
// ===========================================================================

const KDLNode* KDLDocument::getNode(std::string_view name) const {
    for (const auto& node : nodes) {
        if (node.name == name)
            return &node;
    }
    return nullptr;
}

std::vector<const KDLNode*> KDLDocument::getNodes(std::string_view name) const {
    std::vector<const KDLNode*> result;
    for (const auto& node : nodes) {
        if (node.name == name)
            result.push_back(&node);
    }
    return result;
}

// ===========================================================================
// KDLParseError
// ===========================================================================

KDLParseError::KDLParseError(const std::string& message,
                             std::size_t line,
                             std::size_t column)
    : std::runtime_error(
          "KDL parse error at " + std::to_string(line) + ":" +
          std::to_string(column) + ": " + message),
      line_(line),
      column_(column) {}

// ===========================================================================
// KDLTokenizer
// ===========================================================================

KDLTokenizer::KDLTokenizer(std::string_view source)
    : source_(source) {}

bool KDLTokenizer::atEnd() const noexcept {
    return pos_ >= source_.size();
}

char KDLTokenizer::current() const {
    if (pos_ >= source_.size())
        return '\0';
    return source_[pos_];
}

char KDLTokenizer::advance() {
    char c = current();
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    ++pos_;
    return c;
}

bool KDLTokenizer::match(char c) {
    if (current() == c) {
        advance();
        return true;
    }
    return false;
}

[[noreturn]] void KDLTokenizer::error(const std::string& msg) const {
    throw KDLParseError(msg, line_, column_);
}

void KDLTokenizer::skipLineComment() {
    // Consume until newline (but don't consume the newline itself).
    while (!atEnd() && current() != '\n')
        advance();
}

void KDLTokenizer::skipBlockComment() {
    // We've already consumed "/*" -- support nested block comments (KDL v2)
    int depth = 1;
    while (!atEnd() && depth > 0) {
        if (current() == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '*') {
            advance();
            advance();
            ++depth;
        } else if (current() == '*' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
            advance();
            advance();
            --depth;
        } else {
            advance();
        }
    }
    if (depth > 0)
        error("unterminated block comment");
}

void KDLTokenizer::skipWhitespaceAndComments() {
    while (!atEnd()) {
        char c = current();
        // Skip whitespace (but NOT newlines -- those are significant tokens).
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
            continue;
        }
        // UTF-8 non-breaking space (U+00A0 = 0xC2 0xA0)
        if (c == '\xC2' && pos_ + 1 < source_.size() &&
            static_cast<unsigned char>(source_[pos_ + 1]) == 0xA0) {
            advance();
            advance();
            continue;
        }
        // UTF-8 BOM (U+FEFF = 0xEF 0xBB 0xBF)
        if (c == '\xEF' && pos_ + 2 < source_.size() &&
            static_cast<unsigned char>(source_[pos_ + 1]) == 0xBB &&
            static_cast<unsigned char>(source_[pos_ + 2]) == 0xBF) {
            advance();
            advance();
            advance();
            continue;
        }
        // Unicode whitespace: U+2000..U+200A, U+202F, U+205F, U+3000, U+FEFF
        // Encoded as 3-byte UTF-8 sequences starting with 0xE2
        if (static_cast<unsigned char>(c) == 0xE2 && pos_ + 2 < source_.size()) {
            unsigned char b1 = static_cast<unsigned char>(source_[pos_ + 1]);
            unsigned char b2 = static_cast<unsigned char>(source_[pos_ + 2]);
            // U+2000..U+200A: 0xE2 0x80 0x80..0x8A
            if (b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) {
                advance(); advance(); advance(); continue;
            }
            // U+202F: 0xE2 0x80 0xAF
            if (b1 == 0x80 && b2 == 0xAF) {
                advance(); advance(); advance(); continue;
            }
            // U+205F: 0xE2 0x81 0x9F
            if (b1 == 0x81 && b2 == 0x9F) {
                advance(); advance(); advance(); continue;
            }
        }
        // U+3000 (ideographic space): 0xE3 0x80 0x80
        if (static_cast<unsigned char>(c) == 0xE3 && pos_ + 2 < source_.size() &&
            static_cast<unsigned char>(source_[pos_ + 1]) == 0x80 &&
            static_cast<unsigned char>(source_[pos_ + 2]) == 0x80) {
            advance(); advance(); advance(); continue;
        }
        // Line continuation: backslash followed by optional whitespace then newline
        if (c == '\\') {
            std::size_t saved = pos_;
            std::size_t savedLine = line_;
            std::size_t savedCol = column_;
            advance(); // consume backslash
            // skip whitespace between backslash and newline
            while (!atEnd() && (current() == ' ' || current() == '\t' || current() == '\r'))
                advance();
            if (!atEnd() && current() == '\n') {
                advance(); // consume the newline -- line continuation
                continue;
            }
            // Not a line continuation, put back
            pos_ = saved;
            line_ = savedLine;
            column_ = savedCol;
            break;
        }
        // Line comment //
        if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
            // But not slashdash "/-"
            if (source_[pos_ + 1] == '/') {
                advance(); // first /
                advance(); // second /
                skipLineComment();
                continue;
            }
        }
        // Block comment /*
        if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '*') {
            advance();
            advance();
            skipBlockComment();
            continue;
        }
        break;
    }
}

KDLToken KDLTokenizer::readString() {
    std::size_t startLine = line_;
    std::size_t startCol = column_;
    advance(); // consume opening "

    // Check for multi-line string (triple quote)
    if (current() == '"' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '"') {
        advance(); // second "
        advance(); // third "
        // Multi-line string: read until closing """
        std::string value;
        while (!atEnd()) {
            if (current() == '"' && pos_ + 2 < source_.size() &&
                source_[pos_ + 1] == '"' && source_[pos_ + 2] == '"') {
                advance(); advance(); advance();
                return {KDLTokenType::String, std::move(value), startLine, startCol};
            }
            value += current();
            advance();
        }
        error("unterminated multi-line string");
    }

    std::string value;
    while (!atEnd()) {
        char c = current();
        if (c == '"') {
            advance();
            return {KDLTokenType::String, std::move(value), startLine, startCol};
        }
        if (c == '\\') {
            advance(); // consume backslash
            if (atEnd()) error("unexpected end of string after backslash");
            char esc = current();
            advance();
            switch (esc) {
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '/':  value += '/';  break;
                case 'b':  value += '\b'; break;
                case 'f':  value += '\f'; break;
                case 's':  value += ' ';  break; // KDL v2 whitespace escape
                case '\n': {
                    // Escaped newline: line continuation in string
                    // Skip leading whitespace on next line
                    while (!atEnd() && (current() == ' ' || current() == '\t' || current() == '\r'))
                        advance();
                    break;
                }
                case 'u': {
                    // Unicode escape: \u{XXXX}
                    if (current() != '{')
                        error("expected '{' after \\u");
                    advance();
                    std::string hex;
                    while (!atEnd() && current() != '}') {
                        hex += current();
                        advance();
                    }
                    if (atEnd() || current() != '}')
                        error("expected '}' after unicode escape");
                    advance();
                    if (hex.empty() || hex.size() > 6)
                        error("invalid unicode escape length");
                    unsigned long cp = 0;
                    try {
                        cp = std::stoul(hex, nullptr, 16);
                    } catch (...) {
                        error("invalid unicode codepoint: " + hex);
                    }
                    if (cp > 0x10FFFF)
                        error("unicode codepoint out of range: " + hex);
                    // Surrogate range check
                    if (cp >= 0xD800 && cp <= 0xDFFF)
                        error("surrogate codepoints are not allowed: " + hex);
                    // Encode as UTF-8
                    if (cp < 0x80) {
                        value += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        value += static_cast<char>(0xC0 | (cp >> 6));
                        value += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        value += static_cast<char>(0xE0 | (cp >> 12));
                        value += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        value += static_cast<char>(0xF0 | (cp >> 18));
                        value += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        value += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    error(std::string("unknown escape sequence: \\") + esc);
            }
        } else if (c == '\n') {
            // Bare newline in non-multiline string is an error in KDL v2
            error("unescaped newline in string literal");
        } else {
            value += c;
            advance();
        }
    }
    error("unterminated string");
}

KDLToken KDLTokenizer::readRawString() {
    std::size_t startLine = line_;
    std::size_t startCol = column_;
    advance(); // consume 'r'

    // Count leading '#' characters
    int hashCount = 0;
    while (!atEnd() && current() == '#') {
        ++hashCount;
        advance();
    }
    if (current() != '"')
        error("expected '\"' after 'r' and '#' characters in raw string");
    advance(); // consume opening "

    // Check for multi-line raw string r#"""..."""#
    bool multiline = false;
    if (current() == '"' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '"') {
        advance(); // second "
        advance(); // third "
        multiline = true;
    }

    std::string value;
    if (multiline) {
        // Build closing delimiter: """ followed by hashCount '#'
        while (!atEnd()) {
            if (current() == '"' && pos_ + 2 < source_.size() &&
                source_[pos_ + 1] == '"' && source_[pos_ + 2] == '"') {
                std::size_t savedPos = pos_;
                (void)0;
                advance(); advance(); advance(); // consume """
                bool matched = true;
                for (int i = 0; i < hashCount; ++i) {
                    if (atEnd() || current() != '#') {
                        matched = false;
                        break;
                    }
                    advance();
                }
                if (matched) {
                    return {KDLTokenType::RawString, std::move(value), startLine, startCol};
                }
                // Not the closing delimiter, rewind partially
                value += "\"\"\"";
                std::size_t consumed = pos_ - savedPos - 3;
                for (std::size_t i = 0; i < consumed; ++i)
                    value += '#';
                continue;
            }
            value += current();
            advance();
        }
        error("unterminated multi-line raw string");
    }

    // Single-line raw string: read until closing " followed by hashCount '#'
    while (!atEnd()) {
        if (current() == '"') {
            std::size_t savedPos = pos_;
            (void)0;
            advance(); // consume "
            bool matched = true;
            for (int i = 0; i < hashCount; ++i) {
                if (atEnd() || current() != '#') {
                    matched = false;
                    break;
                }
                advance();
            }
            if (matched) {
                return {KDLTokenType::RawString, std::move(value), startLine, startCol};
            }
            // Not the closer
            value += '"';
            std::size_t consumed = pos_ - savedPos - 1;
            for (std::size_t i = 0; i < consumed; ++i)
                value += '#';
            continue;
        }
        value += current();
        advance();
    }
    error("unterminated raw string");
}

KDLToken KDLTokenizer::readNumber() {
    std::size_t startLine = line_;
    std::size_t startCol = column_;

    std::string num;
    bool isFloat = false;
    bool hasPrefix = false;

    // Optional sign
    if (current() == '+' || current() == '-') {
        num += current();
        advance();
    }

    // Check for 0x, 0o, 0b prefixes
    if (current() == '0' && pos_ + 1 < source_.size()) {
        char next = source_[pos_ + 1];
        if (next == 'x' || next == 'X' || next == 'o' || next == 'O' ||
            next == 'b' || next == 'B') {
            num += current(); advance(); // '0'
            num += current(); advance(); // prefix letter
            hasPrefix = true;
        }
    }

    // Read digits (and underscores, which are separators)
    while (!atEnd()) {
        char c = current();
        if (c == '_') {
            advance();
            continue; // skip underscores
        }
        if (hasPrefix) {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F')) {
                num += c;
                advance();
            } else {
                break;
            }
        } else {
            if (c >= '0' && c <= '9') {
                num += c;
                advance();
            } else if (c == '.') {
                // Check that the next char is a digit (not ".name" property access)
                if (pos_ + 1 < source_.size() && source_[pos_ + 1] >= '0' &&
                    source_[pos_ + 1] <= '9') {
                    isFloat = true;
                    num += c;
                    advance();
                } else {
                    break;
                }
            } else if (c == 'e' || c == 'E') {
                isFloat = true;
                num += c;
                advance();
                if (!atEnd() && (current() == '+' || current() == '-')) {
                    num += current();
                    advance();
                }
            } else {
                break;
            }
        }
    }

    if (isFloat) {
        return {KDLTokenType::Float, std::move(num), startLine, startCol};
    }
    return {KDLTokenType::Integer, std::move(num), startLine, startCol};
}

KDLToken KDLTokenizer::readIdentifierOrKeyword() {
    std::size_t startLine = line_;
    std::size_t startCol = column_;

    std::string ident;
    while (!atEnd()) {
        char c = current();
        // Identifiers: letters, digits, '-', '_', '.', and non-ASCII
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            static_cast<unsigned char>(c) > 127) {
            ident += c;
            advance();
        } else {
            break;
        }
    }

    if (ident.empty())
        error(std::string("unexpected character: '") + current() + "'");

    // Check for keywords
    if (ident == "true")
        return {KDLTokenType::BoolTrue, ident, startLine, startCol};
    if (ident == "false")
        return {KDLTokenType::BoolFalse, ident, startLine, startCol};
    if (ident == "null")
        return {KDLTokenType::Null, ident, startLine, startCol};
    // KDL v2 inf and nan
    if (ident == "inf" || ident == "+inf")
        return {KDLTokenType::Float, "inf", startLine, startCol};
    if (ident == "-inf")
        return {KDLTokenType::Float, "-inf", startLine, startCol};
    if (ident == "nan")
        return {KDLTokenType::Float, "nan", startLine, startCol};

    return {KDLTokenType::Identifier, std::move(ident), startLine, startCol};
}

KDLToken KDLTokenizer::peek() {
    if (peeked_)
        return *peeked_;
    peeked_ = next();
    return *peeked_;
}

KDLToken KDLTokenizer::next() {
    if (peeked_) {
        auto tok = std::move(*peeked_);
        peeked_.reset();
        return tok;
    }

    skipWhitespaceAndComments();

    if (atEnd())
        return {KDLTokenType::Eof, "", line_, column_};

    char c = current();

    // Newline
    if (c == '\n') {
        std::size_t l = line_, col = column_;
        advance();
        return {KDLTokenType::Newline, "\n", l, col};
    }

    // Single-character tokens
    if (c == '{') { std::size_t l = line_, col = column_; advance(); return {KDLTokenType::OpenBrace, "{", l, col}; }
    if (c == '}') { std::size_t l = line_, col = column_; advance(); return {KDLTokenType::CloseBrace, "}", l, col}; }
    if (c == ';') { std::size_t l = line_, col = column_; advance(); return {KDLTokenType::Semicolon, ";", l, col}; }
    if (c == '=') { std::size_t l = line_, col = column_; advance(); return {KDLTokenType::Equals, "=", l, col}; }
    if (c == '(') { std::size_t l = line_, col = column_; advance(); return {KDLTokenType::OpenParen, "(", l, col}; }
    if (c == ')') { std::size_t l = line_, col = column_; advance(); return {KDLTokenType::CloseParen, ")", l, col}; }

    // Slashdash /-
    if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '-') {
        std::size_t l = line_, col = column_;
        advance();
        advance();
        return {KDLTokenType::SlashDash, "/-", l, col};
    }

    // String
    if (c == '"')
        return readString();

    // Raw string
    if (c == 'r' && pos_ + 1 < source_.size() &&
        (source_[pos_ + 1] == '"' || source_[pos_ + 1] == '#'))
        return readRawString();

    // Number (starts with digit, or +/- followed by digit)
    if ((c >= '0' && c <= '9') ||
        ((c == '+' || c == '-') && pos_ + 1 < source_.size() &&
         source_[pos_ + 1] >= '0' && source_[pos_ + 1] <= '9')) {
        return readNumber();
    }

    // Identifier or keyword
    return readIdentifierOrKeyword();
}

// ===========================================================================
// KDLParser
// ===========================================================================

KDLParser::KDLParser(std::string_view source)
    : source_(source) {}

KDLDocument KDLParser::parse(std::string_view source) {
    KDLParser parser(source);
    return parser.parseDocument();
}

KDLDocument KDLParser::parseFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        throw KDLParseError("failed to open file: " + path.string(), 0, 0);

    std::ostringstream ss;
    ss << file.rdbuf();
    return parse(ss.str());
}

std::optional<std::string> KDLParser::tryParseTypeAnnotation(KDLTokenizer& tokenizer) {
    if (tokenizer.peek().type == KDLTokenType::OpenParen) {
        (void)tokenizer.next(); // consume (
        auto tok = tokenizer.next();
        if (tok.type != KDLTokenType::Identifier &&
            tok.type != KDLTokenType::String &&
            tok.type != KDLTokenType::RawString)
            throw KDLParseError("expected type name in annotation", tok.line, tok.column);
        std::string typeName = tok.value;
        auto close = tokenizer.next();
        if (close.type != KDLTokenType::CloseParen)
            throw KDLParseError("expected ')' after type annotation", close.line, close.column);
        return typeName;
    }
    return std::nullopt;
}

KDLValue KDLParser::tokenToValue(const KDLToken& token) {
    switch (token.type) {
        case KDLTokenType::String:
        case KDLTokenType::RawString:
            return token.value;

        case KDLTokenType::Integer: {
            const std::string& s = token.value;
            int64_t val = 0;
            try {
                if (s.size() > 2 && s[0] != '-' && s[0] != '+') {
                    if (s[1] == 'x' || s[1] == 'X')
                        val = static_cast<int64_t>(std::stoll(s, nullptr, 16));
                    else if (s[1] == 'o' || s[1] == 'O')
                        val = static_cast<int64_t>(std::stoll(s, nullptr, 8));
                    else if (s[1] == 'b' || s[1] == 'B')
                        val = static_cast<int64_t>(std::stoll(s, nullptr, 2));
                    else
                        val = static_cast<int64_t>(std::stoll(s));
                } else if (s.size() > 3 && (s[0] == '-' || s[0] == '+')) {
                    if (s[2] == 'x' || s[2] == 'X')
                        val = static_cast<int64_t>(std::stoll(s, nullptr, 16));
                    else if (s[2] == 'o' || s[2] == 'O')
                        val = static_cast<int64_t>(std::stoll(s, nullptr, 8));
                    else if (s[2] == 'b' || s[2] == 'B')
                        val = static_cast<int64_t>(std::stoll(s, nullptr, 2));
                    else
                        val = static_cast<int64_t>(std::stoll(s));
                } else {
                    val = static_cast<int64_t>(std::stoll(s));
                }
            } catch (const std::exception& e) {
                throw KDLParseError(
                    "invalid integer literal: " + s + " (" + e.what() + ")",
                    token.line, token.column);
            }
            return val;
        }

        case KDLTokenType::Float: {
            try {
                if (token.value == "inf" || token.value == "+inf")
                    return std::numeric_limits<double>::infinity();
                if (token.value == "-inf")
                    return -std::numeric_limits<double>::infinity();
                if (token.value == "nan")
                    return std::numeric_limits<double>::quiet_NaN();
                return std::stod(token.value);
            } catch (const std::exception& e) {
                throw KDLParseError(
                    "invalid float literal: " + token.value + " (" + e.what() + ")",
                    token.line, token.column);
            }
        }

        case KDLTokenType::BoolTrue:
            return true;

        case KDLTokenType::BoolFalse:
            return false;

        case KDLTokenType::Null:
            return std::monostate{};

        default:
            throw KDLParseError(
                "unexpected token for value: " + token.value,
                token.line, token.column);
    }
}

std::optional<KDLNode> KDLParser::parseNode(KDLTokenizer& tokenizer) {
    // Skip newlines and semicolons between nodes.
    while (tokenizer.peek().type == KDLTokenType::Newline ||
           tokenizer.peek().type == KDLTokenType::Semicolon) {
        (void)tokenizer.next();
    }

    if (tokenizer.peek().type == KDLTokenType::Eof ||
        tokenizer.peek().type == KDLTokenType::CloseBrace) {
        return std::nullopt;
    }

    // Check for slashdash (comment-out the next node).
    bool slashdashed = false;
    if (tokenizer.peek().type == KDLTokenType::SlashDash) {
        (void)tokenizer.next();
        slashdashed = true;
    }

    // Parse optional type annotation on node name.
    auto typeAnnotation = tryParseTypeAnnotation(tokenizer);

    // Node name: identifier or string.
    auto nameTok = tokenizer.next();
    if (nameTok.type != KDLTokenType::Identifier &&
        nameTok.type != KDLTokenType::String &&
        nameTok.type != KDLTokenType::RawString) {
        throw KDLParseError(
            "expected node name, got: " + nameTok.value,
            nameTok.line, nameTok.column);
    }

    KDLNode node;
    node.name = nameTok.value;
    node.type_annotation = typeAnnotation;

    // Parse arguments, properties, and children until end-of-node.
    while (true) {
        const auto& peek = tokenizer.peek();

        // End of node: newline, semicolon, eof, or close-brace
        if (peek.type == KDLTokenType::Newline ||
            peek.type == KDLTokenType::Semicolon ||
            peek.type == KDLTokenType::Eof ||
            peek.type == KDLTokenType::CloseBrace) {
            break;
        }

        // Children block
        if (peek.type == KDLTokenType::OpenBrace) {
            (void)tokenizer.next(); // consume {
            while (true) {
                if (tokenizer.peek().type == KDLTokenType::CloseBrace) {
                    (void)tokenizer.next(); // consume }
                    break;
                }
                if (tokenizer.peek().type == KDLTokenType::Eof) {
                    throw KDLParseError("unterminated children block",
                                        peek.line, peek.column);
                }
                auto child = parseNode(tokenizer);
                if (child)
                    node.children.push_back(std::move(*child));
            }
            break; // children block ends the node
        }

        // Slashdash on argument/property/children
        bool argSlashdashed = false;
        if (peek.type == KDLTokenType::SlashDash) {
            (void)tokenizer.next();
            argSlashdashed = true;

            // If the next token is '{', the slashdash applies to the children block
            if (tokenizer.peek().type == KDLTokenType::OpenBrace) {
                (void)tokenizer.next(); // consume {
                int depth = 1;
                while (depth > 0 && tokenizer.peek().type != KDLTokenType::Eof) {
                    if (tokenizer.peek().type == KDLTokenType::OpenBrace) depth++;
                    else if (tokenizer.peek().type == KDLTokenType::CloseBrace) depth--;
                    if (depth > 0) (void)tokenizer.next();
                }
                if (tokenizer.peek().type == KDLTokenType::CloseBrace)
                    (void)tokenizer.next(); // consume final }
                break;
            }
        }

        // Type annotation on value
        auto valType = tryParseTypeAnnotation(tokenizer);
        (void)valType; // We store type annotations on node level only for now

        // Read next token -- could be value or property key
        auto tok = tokenizer.next();

        // Check if this is a property (identifier/string followed by =)
        if ((tok.type == KDLTokenType::Identifier ||
             tok.type == KDLTokenType::String ||
             tok.type == KDLTokenType::RawString) &&
            tokenizer.peek().type == KDLTokenType::Equals) {
            (void)tokenizer.next(); // consume =
            auto propValType = tryParseTypeAnnotation(tokenizer);
            (void)propValType;
            auto valTok = tokenizer.next();
            if (!argSlashdashed) {
                node.properties[tok.value] = tokenToValue(valTok);
            }
        } else {
            // It's an argument
            if (!argSlashdashed) {
                node.arguments.push_back(tokenToValue(tok));
            }
        }
    }

    if (slashdashed)
        return std::nullopt; // The entire node is commented out

    return node;
}

KDLDocument KDLParser::parseDocument() {
    KDLTokenizer tokenizer(source_);
    KDLDocument doc;

    while (tokenizer.peek().type != KDLTokenType::Eof) {
        auto node = parseNode(tokenizer);
        if (node)
            doc.nodes.push_back(std::move(*node));
    }

    return doc;
}

} // namespace eternal
