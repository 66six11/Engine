#include "asharia/shader_authoring/shader_parser.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace asharia::shader_authoring {

    namespace {

        enum class TokenKind : std::uint8_t {
            End,
            Identifier,
            Number,
            String,
            LBrace,
            RBrace,
            LBracket,
            RBracket,
            Equal,
            Comma,
            Unknown,
        };

        struct Token {
            TokenKind kind{TokenKind::End};
            std::string_view text;
            SourceSpan span{};
        };

        bool isIdentifierStart(char value) {
            const auto character = static_cast<unsigned char>(value);
            return std::isalpha(character) != 0 || value == '_';
        }

        bool isIdentifierBody(char value) {
            const auto character = static_cast<unsigned char>(value);
            return std::isalnum(character) != 0 || value == '_' || value == '.' || value == '-';
        }

        bool isNumberStart(std::string_view source, std::size_t offset) {
            if (offset >= source.size()) {
                return false;
            }
            const char value = source[offset];
            if (std::isdigit(static_cast<unsigned char>(value)) != 0 || value == '.') {
                return true;
            }
            if ((value == '-' || value == '+') && offset + 1 < source.size()) {
                const char next = source[offset + 1];
                return std::isdigit(static_cast<unsigned char>(next)) != 0 || next == '.';
            }
            return false;
        }

        bool isIntegerText(std::string_view text, bool allowNegative) {
            if (text.empty()) {
                return false;
            }
            std::size_t index = 0;
            if (text.front() == '-') {
                if (!allowNegative) {
                    return false;
                }
                index = 1;
            } else if (text.front() == '+') {
                index = 1;
            }
            if (index == text.size()) {
                return false;
            }
            return std::all_of(
                text.begin() + static_cast<std::ptrdiff_t>(index), text.end(),
                [](char value) { return std::isdigit(static_cast<unsigned char>(value)) != 0; });
        }

        bool isNumberText(std::string_view text) {
            if (text.empty()) {
                return false;
            }
            bool sawDigit = false;
            bool sawDot = false;
            std::size_t index = 0;
            if (text.front() == '-' || text.front() == '+') {
                index = 1;
            }
            for (; index < text.size(); ++index) {
                const char value = text[index];
                if (std::isdigit(static_cast<unsigned char>(value)) != 0) {
                    sawDigit = true;
                    continue;
                }
                if (value == '.' && !sawDot) {
                    sawDot = true;
                    continue;
                }
                return false;
            }
            return sawDigit;
        }

        std::optional<std::uint32_t> parseUnsigned(std::string_view text) {
            std::uint32_t value = 0;
            const auto* begin = text.data();
            const auto* end = text.data() + text.size();
            const auto result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end) {
                return std::nullopt;
            }
            return value;
        }

        SourceSpan joinSpan(SourceSpan begin, SourceSpan end) {
            return SourceSpan{.begin = begin.begin, .end = end.end};
        }

        class Lexer {
        public:
            explicit Lexer(std::string_view source) : source_(source) {}

            [[nodiscard]] std::string_view source() const {
                return source_;
            }

            [[nodiscard]] SourcePosition positionAt(std::size_t targetOffset) const {
                SourcePosition position{};
                position.offset = 0;
                position.line = 1;
                position.column = 1;
                const std::size_t clampedOffset = std::min(targetOffset, source_.size());
                for (std::size_t index = 0; index < clampedOffset; ++index) {
                    advancePosition(position, source_[index]);
                }
                position.offset = static_cast<std::uint32_t>(clampedOffset);
                return position;
            }

            void setOffset(std::size_t offset) {
                offset_ = std::min(offset, source_.size());
                position_ = positionAt(offset_);
            }

            Token next() {
                skipWhitespaceAndComments();
                const SourcePosition begin = position_;
                if (offset_ >= source_.size()) {
                    return Token{
                        .kind = TokenKind::End,
                        .text = {},
                        .span = SourceSpan{.begin = begin, .end = begin},
                    };
                }

                const char value = source_[offset_];
                switch (value) {
                case '{':
                    return single(TokenKind::LBrace);
                case '}':
                    return single(TokenKind::RBrace);
                case '[':
                    return single(TokenKind::LBracket);
                case ']':
                    return single(TokenKind::RBracket);
                case '=':
                    return single(TokenKind::Equal);
                case ',':
                    return single(TokenKind::Comma);
                case '"':
                    return stringToken();
                default:
                    break;
                }

                if (isIdentifierStart(value)) {
                    return identifier();
                }
                if (isNumberStart(source_, offset_)) {
                    return number();
                }

                advance();
                return Token{
                    .kind = TokenKind::Unknown,
                    .text = source_.substr(begin.offset, 1),
                    .span = SourceSpan{.begin = begin, .end = position_},
                };
            }

        private:
            static void advancePosition(SourcePosition& position, char value) {
                ++position.offset;
                if (value == '\n') {
                    ++position.line;
                    position.column = 1;
                } else {
                    ++position.column;
                }
            }

            void advance() {
                if (offset_ >= source_.size()) {
                    return;
                }
                advancePosition(position_, source_[offset_]);
                ++offset_;
            }

            Token single(TokenKind kind) {
                const SourcePosition begin = position_;
                const std::size_t tokenOffset = offset_;
                advance();
                return Token{
                    .kind = kind,
                    .text = source_.substr(tokenOffset, 1),
                    .span = SourceSpan{.begin = begin, .end = position_},
                };
            }

            void skipWhitespaceAndComments() {
                bool consumed = true;
                while (consumed) {
                    consumed = false;
                    while (offset_ < source_.size() &&
                           std::isspace(static_cast<unsigned char>(source_[offset_])) != 0) {
                        advance();
                        consumed = true;
                    }
                    if (offset_ + 1 >= source_.size() || source_[offset_] != '/') {
                        continue;
                    }
                    if (source_[offset_ + 1] == '/') {
                        while (offset_ < source_.size() && source_[offset_] != '\n') {
                            advance();
                        }
                        consumed = true;
                    } else if (source_[offset_ + 1] == '*') {
                        advance();
                        advance();
                        while (offset_ + 1 < source_.size() &&
                               (source_[offset_] != '*' || source_[offset_ + 1] != '/')) {
                            advance();
                        }
                        if (offset_ + 1 < source_.size()) {
                            advance();
                            advance();
                        }
                        consumed = true;
                    }
                }
            }

            Token identifier() {
                const SourcePosition begin = position_;
                const std::size_t tokenOffset = offset_;
                while (offset_ < source_.size() && isIdentifierBody(source_[offset_])) {
                    advance();
                }
                return Token{
                    .kind = TokenKind::Identifier,
                    .text = source_.substr(tokenOffset, offset_ - tokenOffset),
                    .span = SourceSpan{.begin = begin, .end = position_},
                };
            }

            Token number() {
                const SourcePosition begin = position_;
                const std::size_t tokenOffset = offset_;
                if (offset_ < source_.size() &&
                    (source_[offset_] == '-' || source_[offset_] == '+')) {
                    advance();
                }
                bool sawDot = false;
                while (offset_ < source_.size()) {
                    const char value = source_[offset_];
                    if (std::isdigit(static_cast<unsigned char>(value)) != 0) {
                        advance();
                        continue;
                    }
                    if (value == '.' && !sawDot) {
                        sawDot = true;
                        advance();
                        continue;
                    }
                    break;
                }
                return Token{
                    .kind = TokenKind::Number,
                    .text = source_.substr(tokenOffset, offset_ - tokenOffset),
                    .span = SourceSpan{.begin = begin, .end = position_},
                };
            }

            Token stringToken() {
                const SourcePosition begin = position_;
                const std::size_t quoteOffset = offset_;
                advance();
                const std::size_t textOffset = offset_;
                bool escaped = false;
                while (offset_ < source_.size()) {
                    const char value = source_[offset_];
                    if (!escaped && value == '"') {
                        const std::size_t textSize = offset_ - textOffset;
                        advance();
                        return Token{
                            .kind = TokenKind::String,
                            .text = source_.substr(textOffset, textSize),
                            .span = SourceSpan{.begin = begin, .end = position_},
                        };
                    }
                    escaped = !escaped && value == '\\';
                    if (value != '\\') {
                        escaped = false;
                    }
                    advance();
                }
                return Token{
                    .kind = TokenKind::String,
                    .text = source_.substr(textOffset, source_.size() - quoteOffset),
                    .span = SourceSpan{.begin = begin, .end = position_},
                };
            }

            std::string_view source_;
            std::size_t offset_{0};
            SourcePosition position_{};
        };

        std::optional<ShaderPropertyType> propertyTypeFromName(std::string_view name) {
            if (name == "float") {
                return ShaderPropertyType::Float;
            }
            if (name == "float2") {
                return ShaderPropertyType::Float2;
            }
            if (name == "float3") {
                return ShaderPropertyType::Float3;
            }
            if (name == "float4") {
                return ShaderPropertyType::Float4;
            }
            if (name == "color") {
                return ShaderPropertyType::Color;
            }
            if (name == "int") {
                return ShaderPropertyType::Int;
            }
            if (name == "uint") {
                return ShaderPropertyType::UInt;
            }
            if (name == "bool") {
                return ShaderPropertyType::Bool;
            }
            if (name == "texture2D") {
                return ShaderPropertyType::Texture2D;
            }
            if (name == "sampler") {
                return ShaderPropertyType::Sampler;
            }
            return std::nullopt;
        }

        bool isVectorType(ShaderPropertyType type) {
            return type == ShaderPropertyType::Float2 || type == ShaderPropertyType::Float3 ||
                   type == ShaderPropertyType::Float4 || type == ShaderPropertyType::Color;
        }

        std::size_t expectedVectorSize(ShaderPropertyType type) {
            switch (type) {
            case ShaderPropertyType::Float2:
                return 2;
            case ShaderPropertyType::Float3:
                return 3;
            case ShaderPropertyType::Float4:
            case ShaderPropertyType::Color:
                return 4;
            default:
                return 0;
            }
        }

        struct RawBlockMatch {
            std::size_t closeOffset{0};
        };

        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        std::optional<RawBlockMatch> findRawBlockClose(std::string_view source,
                                                       std::size_t openOffset) {
            std::uint32_t depth = 0;
            bool inString = false;
            bool inLineComment = false;
            bool inBlockComment = false;
            bool escaped = false;

            for (std::size_t index = openOffset; index < source.size(); ++index) {
                const char value = source[index];
                const char next = index + 1 < source.size() ? source[index + 1] : '\0';

                if (inLineComment) {
                    if (value == '\n') {
                        inLineComment = false;
                    }
                    continue;
                }
                if (inBlockComment) {
                    if (value == '*' && next == '/') {
                        inBlockComment = false;
                        ++index;
                    }
                    continue;
                }
                if (inString) {
                    if (!escaped && value == '"') {
                        inString = false;
                    }
                    escaped = !escaped && value == '\\';
                    if (value != '\\') {
                        escaped = false;
                    }
                    continue;
                }

                if (value == '/' && next == '/') {
                    inLineComment = true;
                    ++index;
                    continue;
                }
                if (value == '/' && next == '*') {
                    inBlockComment = true;
                    ++index;
                    continue;
                }
                if (value == '"') {
                    inString = true;
                    escaped = false;
                    continue;
                }
                if (value == '{') {
                    ++depth;
                    continue;
                }
                if (value == '}') {
                    if (depth == 0) {
                        return std::nullopt;
                    }
                    --depth;
                    if (depth == 0) {
                        return RawBlockMatch{.closeOffset = index};
                    }
                }
            }
            return std::nullopt;
        }

        class Parser {
        public:
            Parser(std::string_view source, const ShaderParseOptions& /*options*/)
                : lexer_(source) {
                advance();
            }

            ShaderParseResult parse() {
                ShaderDocument document{};
                document.fullSpan.begin = current_.span.begin;

                parseSchema(document);
                parseShader(document);

                if (!document.rawSlang && document.slangFiles.empty()) {
                    addDiagnostic(
                        ShaderDiagnosticCode::MissingSlangReference,
                        ShaderDiagnosticTarget::SlangReference, document.fullSpan,
                        "shader document requires a slang file reference or raw slang block");
                }

                document.fullSpan.end = current_.span.end;
                return ShaderParseResult{.document = std::move(document),
                                          .diagnostics = std::move(diagnostics_)};
            }

        private:
            void advance() {
                current_ = lexer_.next();
            }

            [[nodiscard]] bool isIdentifier(std::string_view text) const {
                return current_.kind == TokenKind::Identifier && current_.text == text;
            }

            bool consumeIdentifier(std::string_view text) {
                if (!isIdentifier(text)) {
                    return false;
                }
                advance();
                return true;
            }

            bool expectIdentifier(std::string_view text, ShaderDiagnosticTarget target) {
                if (consumeIdentifier(text)) {
                    return true;
                }
                addDiagnostic(ShaderDiagnosticCode::ExpectedToken, target, current_.span,
                              std::string{"expected '"} + std::string{text} + "'");
                return false;
            }

            bool expect(TokenKind kind, std::string_view text, ShaderDiagnosticTarget target) {
                if (current_.kind == kind) {
                    advance();
                    return true;
                }
                addDiagnostic(ShaderDiagnosticCode::ExpectedToken, target, current_.span,
                              std::string{"expected "} + std::string{text});
                return false;
            }

            std::optional<Token> consume(TokenKind kind) {
                if (current_.kind != kind) {
                    return std::nullopt;
                }
                Token token = current_;
                advance();
                return token;
            }

            void addDiagnostic(ShaderDiagnosticCode code, ShaderDiagnosticTarget target,
                               SourceSpan span, std::string message) {
                diagnostics_.push_back(ShaderDiagnostic{
                    .severity = ShaderDiagnosticSeverity::Error,
                    .code = code,
                    .target = target,
                    .span = span,
                    .message = std::move(message),
                });
            }

            void parseSchema(ShaderDocument& document) {
                if (!expectIdentifier("schema", ShaderDiagnosticTarget::File)) {
                    return;
                }
                const auto versionToken = consume(TokenKind::Number);
                if (!versionToken) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken,
                                  ShaderDiagnosticTarget::File, current_.span,
                                  "expected schema version number");
                    return;
                }
                const auto version = parseUnsigned(versionToken->text);
                if (!version) {
                    addDiagnostic(ShaderDiagnosticCode::UnsupportedSchema,
                                  ShaderDiagnosticTarget::File, versionToken->span,
                                  "schema version must be an unsigned integer");
                    return;
                }
                document.schemaVersion = *version;
                if (*version != 2U) {
                    addDiagnostic(ShaderDiagnosticCode::UnsupportedSchema,
                                  ShaderDiagnosticTarget::File, versionToken->span,
                                  "only .shader schema 2 is supported");
                }
            }

            void parseShader(ShaderDocument& document) {
                if (!expectIdentifier("shader", ShaderDiagnosticTarget::Shader)) {
                    synchronizeToBlock();
                    return;
                }
                const auto shaderName = consume(TokenKind::String);
                if (!shaderName) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken,
                                  ShaderDiagnosticTarget::Shader, current_.span,
                                  "expected shader stable type id string");
                } else {
                    document.shaderTypeId = std::string{shaderName->text};
                }

                if (!expect(TokenKind::LBrace, "'{'", ShaderDiagnosticTarget::Shader)) {
                    return;
                }

                while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
                    if (consumeIdentifier("properties")) {
                        parseProperties(document);
                    } else if (consumeIdentifier("pass")) {
                        parsePass(document);
                    } else if (isIdentifier("slang")) {
                        parseSlangReference(document, nullptr);
                    } else if (isIdentifier("graph")) {
                        parseGraphReference(document, nullptr);
                    } else {
                        addDiagnostic(ShaderDiagnosticCode::UnexpectedToken,
                                      ShaderDiagnosticTarget::Shader, current_.span,
                                      "unexpected token in shader block");
                        advance();
                    }
                }

                expect(TokenKind::RBrace, "'}'", ShaderDiagnosticTarget::Shader);
            }

            void parseProperties(ShaderDocument& document) {
                if (!expect(TokenKind::LBrace, "'{'", ShaderDiagnosticTarget::Property)) {
                    return;
                }

                while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
                    parseProperty(document);
                }

                expect(TokenKind::RBrace, "'}'", ShaderDiagnosticTarget::Property);
            }

            void parseProperty(ShaderDocument& document) {
                const auto typeToken = consume(TokenKind::Identifier);
                if (!typeToken) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken,
                                  ShaderDiagnosticTarget::Property, current_.span,
                                  "expected property type");
                    advance();
                    return;
                }

                const auto propertyType = propertyTypeFromName(typeToken->text);
                if (!propertyType) {
                    addDiagnostic(ShaderDiagnosticCode::UnknownPropertyType,
                                  ShaderDiagnosticTarget::Property, typeToken->span,
                                  std::string{"unknown property type '"} +
                                      std::string{typeToken->text} + "'");
                }

                const auto nameToken = consume(TokenKind::Identifier);
                if (!nameToken) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken,
                                  ShaderDiagnosticTarget::Property, current_.span,
                                  "expected property name");
                    return;
                }

                ShaderPropertyDefault defaultValue{};
                if (current_.kind == TokenKind::Equal) {
                    advance();
                    defaultValue = parsePropertyDefault();
                }

                if (!propertyType) {
                    return;
                }

                if (!propertyNames_.insert(std::string{nameToken->text}).second) {
                    addDiagnostic(ShaderDiagnosticCode::DuplicateProperty,
                                  ShaderDiagnosticTarget::Property, nameToken->span,
                                  std::string{"duplicate property '"} +
                                      std::string{nameToken->text} + "'");
                }

                if (!validateDefault(*propertyType, defaultValue)) {
                    addDiagnostic(ShaderDiagnosticCode::InvalidDefaultValue,
                                  ShaderDiagnosticTarget::Property, defaultValue.span,
                                  std::string{"invalid default value for property '"} +
                                      std::string{nameToken->text} + "'");
                }

                document.properties.push_back(ShaderPropertyDecl{
                    .type = *propertyType,
                    .typeName = std::string{typeToken->text},
                    .name = std::string{nameToken->text},
                    .defaultValue = std::move(defaultValue),
                    .span = joinSpan(typeToken->span, nameToken->span),
                    .typeSpan = typeToken->span,
                    .nameSpan = nameToken->span,
                });
            }

            ShaderPropertyDefault parsePropertyDefault() {
                if (const auto vectorDefault = parseVectorDefault()) {
                    return *vectorDefault;
                }

                if (const auto number = consume(TokenKind::Number)) {
                    const ShaderPropertyDefaultKind kind =
                        isIntegerText(number->text, true) ? ShaderPropertyDefaultKind::Integer
                                                          : ShaderPropertyDefaultKind::Number;
                    return ShaderPropertyDefault{
                        .kind = kind,
                        .text = std::string{number->text},
                        .elements = {std::string{number->text}},
                        .span = number->span,
                    };
                }

                if (const auto identifier = consume(TokenKind::Identifier)) {
                    if (identifier->text == "true" || identifier->text == "false") {
                        return ShaderPropertyDefault{
                            .kind = ShaderPropertyDefaultKind::Boolean,
                            .text = std::string{identifier->text},
                            .elements = {std::string{identifier->text}},
                            .span = identifier->span,
                        };
                    }
                    return ShaderPropertyDefault{
                        .kind = ShaderPropertyDefaultKind::None,
                        .text = std::string{identifier->text},
                        .elements = {},
                        .span = identifier->span,
                    };
                }

                SourceSpan span = current_.span;
                if (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
                    advance();
                }
                return ShaderPropertyDefault{.text = {}, .elements = {}, .span = span};
            }

            std::optional<ShaderPropertyDefault> parseVectorDefault() {
                const auto open = consume(TokenKind::LBracket);
                if (!open) {
                    return std::nullopt;
                }

                std::vector<std::string> elements;
                SourceSpan span = open->span;
                while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBracket) {
                    const auto value = consume(TokenKind::Number);
                    if (!value) {
                        span.end = current_.span.end;
                        addDiagnostic(ShaderDiagnosticCode::InvalidDefaultValue,
                                      ShaderDiagnosticTarget::Property, current_.span,
                                      "vector default requires numeric elements");
                        advance();
                        continue;
                    }
                    span.end = value->span.end;
                    elements.emplace_back(value->text);
                    if (current_.kind == TokenKind::Comma) {
                        advance();
                    }
                }

                if (const auto close = consume(TokenKind::RBracket)) {
                    span.end = close->span.end;
                } else {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken,
                                  ShaderDiagnosticTarget::Property, current_.span,
                                  "expected ']' after vector default");
                }

                std::string text = "[";
                for (std::size_t index = 0; index < elements.size(); ++index) {
                    if (index > 0) {
                        text += ", ";
                    }
                    text += elements[index];
                }
                text += "]";
                return ShaderPropertyDefault{
                    .kind = ShaderPropertyDefaultKind::Vector,
                    .text = std::move(text),
                    .elements = std::move(elements),
                    .span = span,
                };
            }

            [[nodiscard]] static bool validateDefault(ShaderPropertyType type,
                                                      const ShaderPropertyDefault& defaultValue) {
                if (defaultValue.kind == ShaderPropertyDefaultKind::None &&
                    defaultValue.text.empty()) {
                    return true;
                }

                if (isVectorType(type)) {
                    return defaultValue.kind == ShaderPropertyDefaultKind::Vector &&
                           defaultValue.elements.size() == expectedVectorSize(type) &&
                           std::ranges::all_of(defaultValue.elements, [](const std::string& text) {
                               return isNumberText(text);
                           });
                }

                switch (type) {
                case ShaderPropertyType::Float:
                    return (defaultValue.kind == ShaderPropertyDefaultKind::Number ||
                            defaultValue.kind == ShaderPropertyDefaultKind::Integer) &&
                           isNumberText(defaultValue.text);
                case ShaderPropertyType::Int:
                    return defaultValue.kind == ShaderPropertyDefaultKind::Integer &&
                           isIntegerText(defaultValue.text, true);
                case ShaderPropertyType::UInt:
                    return defaultValue.kind == ShaderPropertyDefaultKind::Integer &&
                           isIntegerText(defaultValue.text, false);
                case ShaderPropertyType::Bool:
                    return defaultValue.kind == ShaderPropertyDefaultKind::Boolean;
                case ShaderPropertyType::Texture2D:
                case ShaderPropertyType::Sampler:
                    return false;
                case ShaderPropertyType::Float2:
                case ShaderPropertyType::Float3:
                case ShaderPropertyType::Float4:
                case ShaderPropertyType::Color:
                    break;
                }
                return false;
            }

            void parsePass(ShaderDocument& document) {
                ShaderPassDecl pass{};
                const SourceSpan passBegin = current_.span;
                const auto name = consume(TokenKind::String);
                if (!name) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken,
                                  ShaderDiagnosticTarget::Pass, current_.span,
                                  "expected pass name string");
                } else {
                    pass.name = std::string{name->text};
                    pass.nameSpan = name->span;
                }

                if (!expect(TokenKind::LBrace, "'{'", ShaderDiagnosticTarget::Pass)) {
                    return;
                }

                while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
                    if (consumeIdentifier("tag")) {
                        pass.tag = parseStringValue(ShaderDiagnosticTarget::Pass, "pass tag");
                    } else if (consumeIdentifier("vertex")) {
                        pass.vertexEntry =
                            parseIdentifierValue(ShaderDiagnosticTarget::Pass, "vertex entry");
                    } else if (consumeIdentifier("fragment")) {
                        pass.fragmentEntry =
                            parseIdentifierValue(ShaderDiagnosticTarget::Pass, "fragment entry");
                    } else if (consumeIdentifier("compute")) {
                        pass.computeEntry =
                            parseIdentifierValue(ShaderDiagnosticTarget::Pass, "compute entry");
                    } else if (consumeIdentifier("cull")) {
                        pass.cullMode =
                            parseIdentifierValue(ShaderDiagnosticTarget::Pass, "cull mode");
                    } else if (consumeIdentifier("depthTest")) {
                        pass.depthTest =
                            parseIdentifierValue(ShaderDiagnosticTarget::Pass, "depth test");
                    } else if (consumeIdentifier("depthWrite")) {
                        pass.depthWrite =
                            parseBoolValue(ShaderDiagnosticTarget::Pass, "depth write flag");
                    } else if (consumeIdentifier("blend")) {
                        pass.blendMode =
                            parseIdentifierValue(ShaderDiagnosticTarget::Pass, "blend mode");
                    } else if (isIdentifier("slang")) {
                        parseSlangReference(document, &pass);
                    } else if (isIdentifier("graph")) {
                        parseGraphReference(document, &pass);
                    } else {
                        addDiagnostic(ShaderDiagnosticCode::UnexpectedToken,
                                      ShaderDiagnosticTarget::Pass, current_.span,
                                      "unexpected token in pass block");
                        advance();
                    }
                }

                const SourceSpan closeSpan = current_.span;
                expect(TokenKind::RBrace, "'}'", ShaderDiagnosticTarget::Pass);
                pass.span = SourceSpan{.begin = passBegin.begin, .end = closeSpan.end};

                if (!pass.vertexEntry && !pass.fragmentEntry && !pass.computeEntry) {
                    addDiagnostic(ShaderDiagnosticCode::MissingPassEntry,
                                  ShaderDiagnosticTarget::Pass, pass.span,
                                  std::string{"pass '"} + pass.name +
                                      "' requires vertex, fragment, or compute entry");
                }

                document.passes.push_back(std::move(pass));
            }

            std::optional<std::string> parseStringValue(ShaderDiagnosticTarget target,
                                                        std::string_view label) {
                const auto token = consume(TokenKind::String);
                if (!token) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken, target, current_.span,
                                  std::string{"expected "} + std::string{label} + " string");
                    return std::nullopt;
                }
                return std::string{token->text};
            }

            std::optional<std::string> parseIdentifierValue(ShaderDiagnosticTarget target,
                                                            std::string_view label) {
                const auto token = consume(TokenKind::Identifier);
                if (!token) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken, target, current_.span,
                                  std::string{"expected "} + std::string{label} + " identifier");
                    return std::nullopt;
                }
                return std::string{token->text};
            }

            std::optional<bool> parseBoolValue(ShaderDiagnosticTarget target,
                                               std::string_view label) {
                const auto token = consume(TokenKind::Identifier);
                if (!token || (token->text != "true" && token->text != "false")) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken, target,
                                  token ? token->span : current_.span,
                                  std::string{"expected "} + std::string{label} + " boolean");
                    return std::nullopt;
                }
                return token->text == "true";
            }

            void parseSlangReference(ShaderDocument& document, ShaderPassDecl* pass) {
                const SourceSpan keywordSpan = current_.span;
                advance();
                if (const auto path = consume(TokenKind::String)) {
                    ShaderSourceReference reference{.path = std::string{path->text},
                                                     .span = path->span};
                    document.slangFiles.push_back(reference);
                    if (pass != nullptr) {
                        pass->slangFiles.push_back(std::move(reference));
                    }
                    return;
                }
                if (current_.kind == TokenKind::LBrace) {
                    parseRawSlangBlock(document);
                    return;
                }
                addDiagnostic(ShaderDiagnosticCode::ExpectedToken,
                              ShaderDiagnosticTarget::SlangReference, keywordSpan,
                              "expected slang file string or raw slang block");
            }

            void parseGraphReference(ShaderDocument& document, ShaderPassDecl* pass) {
                advance();
                const auto path = consume(TokenKind::String);
                if (!path) {
                    addDiagnostic(ShaderDiagnosticCode::ExpectedToken,
                                  ShaderDiagnosticTarget::GraphReference, current_.span,
                                  "expected graph file string");
                    return;
                }
                ShaderSourceReference reference{.path = std::string{path->text},
                                                 .span = path->span};
                document.graphFiles.push_back(reference);
                if (pass != nullptr) {
                    pass->graphFiles.push_back(std::move(reference));
                }
            }

            void parseRawSlangBlock(ShaderDocument& document) {
                const std::size_t openOffset = current_.span.begin.offset;
                const auto match = findRawBlockClose(lexer_.source(), openOffset);
                if (!match) {
                    addDiagnostic(ShaderDiagnosticCode::UnbalancedRawSlangBlock,
                                  ShaderDiagnosticTarget::RawSlangBlock, current_.span,
                                  "raw slang block has unbalanced braces");
                    lexer_.setOffset(lexer_.source().size());
                    advance();
                    return;
                }

                const std::size_t bodyOffset = openOffset + 1;
                const SourceSpan rawSpan{
                    .begin = lexer_.positionAt(openOffset),
                    .end = lexer_.positionAt(match->closeOffset + 1),
                };
                const SourceSpan bodySpan{
                    .begin = lexer_.positionAt(bodyOffset),
                    .end = lexer_.positionAt(match->closeOffset),
                };
                if (document.rawSlang) {
                    addDiagnostic(ShaderDiagnosticCode::UnexpectedToken,
                                  ShaderDiagnosticTarget::RawSlangBlock, rawSpan,
                                  "only one raw slang block is supported in this slice");
                } else {
                    document.rawSlang = ShaderRawSlangBlock{
                        .text = std::string{lexer_.source().substr(bodyOffset, match->closeOffset -
                                                                                   bodyOffset)},
                        .span = rawSpan,
                        .bodySpan = bodySpan,
                    };
                }

                lexer_.setOffset(match->closeOffset + 1);
                advance();
            }

            void synchronizeToBlock() {
                while (current_.kind != TokenKind::End && current_.kind != TokenKind::LBrace) {
                    advance();
                }
            }

            Lexer lexer_;
            Token current_{};
            std::vector<ShaderDiagnostic> diagnostics_;
            std::unordered_set<std::string> propertyNames_;
        };

    } // namespace

    std::string_view toString(ShaderDiagnosticSeverity severity) {
        switch (severity) {
        case ShaderDiagnosticSeverity::Warning:
            return "warning";
        case ShaderDiagnosticSeverity::Error:
            return "error";
        }
        return "unknown";
    }

    std::string_view toString(ShaderDiagnosticCode code) {
        switch (code) {
        case ShaderDiagnosticCode::ExpectedToken:
            return "expected-token";
        case ShaderDiagnosticCode::UnexpectedToken:
            return "unexpected-token";
        case ShaderDiagnosticCode::UnsupportedSchema:
            return "unsupported-schema";
        case ShaderDiagnosticCode::DuplicateProperty:
            return "duplicate-property";
        case ShaderDiagnosticCode::UnknownPropertyType:
            return "unknown-property-type";
        case ShaderDiagnosticCode::InvalidDefaultValue:
            return "invalid-default-value";
        case ShaderDiagnosticCode::MissingPassEntry:
            return "missing-pass-entry";
        case ShaderDiagnosticCode::MissingSlangReference:
            return "missing-slang-reference";
        case ShaderDiagnosticCode::UnbalancedRawSlangBlock:
            return "unbalanced-raw-slang-block";
        case ShaderDiagnosticCode::GeneratedSlangUnsupportedInput:
            return "generated-slang-unsupported-input";
        }
        return "unknown";
    }

    std::string_view toString(ShaderPropertyType type) {
        switch (type) {
        case ShaderPropertyType::Float:
            return "float";
        case ShaderPropertyType::Float2:
            return "float2";
        case ShaderPropertyType::Float3:
            return "float3";
        case ShaderPropertyType::Float4:
            return "float4";
        case ShaderPropertyType::Color:
            return "color";
        case ShaderPropertyType::Int:
            return "int";
        case ShaderPropertyType::UInt:
            return "uint";
        case ShaderPropertyType::Bool:
            return "bool";
        case ShaderPropertyType::Texture2D:
            return "texture2D";
        case ShaderPropertyType::Sampler:
            return "sampler";
        }
        return "unknown";
    }

    bool hasErrors(const std::vector<ShaderDiagnostic>& diagnostics) {
        return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == ShaderDiagnosticSeverity::Error;
        });
    }

    ShaderParseResult parseShaderDocument(std::string_view source,
                                            const ShaderParseOptions& options) {
        Parser parser{source, options};
        return parser.parse();
    }

} // namespace asharia::shader_authoring
