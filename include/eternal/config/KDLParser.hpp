#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// KDL value types
// ---------------------------------------------------------------------------

using KDLValue = std::variant<std::monostate, std::string, int64_t, double, bool>;

// ---------------------------------------------------------------------------
// KDL AST nodes
// ---------------------------------------------------------------------------

struct KDLNode {
    std::string name;
    std::optional<std::string> type_annotation;
    std::vector<KDLValue> arguments;
    std::unordered_map<std::string, KDLValue> properties;
    std::vector<KDLNode> children;
};

// ---------------------------------------------------------------------------
// KDL document
// ---------------------------------------------------------------------------

struct KDLDocument {
    std::vector<KDLNode> nodes;

    /// Return the first node whose name matches, or nullptr.
    [[nodiscard]] const KDLNode* getNode(std::string_view name) const;

    /// Return every node whose name matches.
    [[nodiscard]] std::vector<const KDLNode*> getNodes(std::string_view name) const;
};

// ---------------------------------------------------------------------------
// Parse errors
// ---------------------------------------------------------------------------

class KDLParseError : public std::runtime_error {
public:
    KDLParseError(const std::string& message, std::size_t line, std::size_t column);

    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    [[nodiscard]] std::size_t column() const noexcept { return column_; }

private:
    std::size_t line_;
    std::size_t column_;
};

// ---------------------------------------------------------------------------
// Token types (internal, but exposed for testability)
// ---------------------------------------------------------------------------

enum class KDLTokenType {
    Identifier,
    String,
    RawString,
    Integer,
    Float,
    BoolTrue,
    BoolFalse,
    Null,
    Equals,
    OpenBrace,
    CloseBrace,
    Semicolon,
    Newline,
    SlashDash,
    OpenParen,
    CloseParen,
    Eof,
};

struct KDLToken {
    KDLTokenType type;
    std::string value;
    std::size_t line;
    std::size_t column;
};

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

class KDLTokenizer {
public:
    explicit KDLTokenizer(std::string_view source);

    [[nodiscard]] KDLToken next();
    [[nodiscard]] KDLToken peek();
    [[nodiscard]] bool atEnd() const noexcept;

private:
    void skipWhitespaceAndComments();
    void skipLineComment();
    void skipBlockComment();
    KDLToken readString();
    KDLToken readRawString();
    KDLToken readNumber();
    KDLToken readIdentifierOrKeyword();

    [[noreturn]] void error(const std::string& msg) const;

    char current() const;
    char advance();
    bool match(char c);

    std::string_view source_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
    std::optional<KDLToken> peeked_;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class KDLParser {
public:
    /// Parse a KDL document from a string.
    [[nodiscard]] static KDLDocument parse(std::string_view source);

    /// Parse a KDL document from a file on disk.
    [[nodiscard]] static KDLDocument parseFile(const std::filesystem::path& path);

private:
    explicit KDLParser(std::string_view source);

    KDLDocument parseDocument();
    std::optional<KDLNode> parseNode(KDLTokenizer& tokenizer);
    KDLValue tokenToValue(const KDLToken& token);
    std::optional<std::string> tryParseTypeAnnotation(KDLTokenizer& tokenizer);

    std::string_view source_;
};

} // namespace eternal
