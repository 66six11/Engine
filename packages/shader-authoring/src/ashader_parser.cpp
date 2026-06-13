#include "asharia/shader_authoring/ashader_parser.hpp"

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

        std::optional<AshaderPropertyType> propertyTypeFromName(std::string_view name) {
            if (name == "float") {
                return AshaderPropertyType::Float;
            }
            if (name == "float2") {
                return AshaderPropertyType::Float2;
            }
            if (name == "float3") {
                return AshaderPropertyType::Float3;
            }
            if (name == "float4") {
                return AshaderPropertyType::Float4;
            }
            if (name == "color") {
                return AshaderPropertyType::Color;
            }
            if (name == "int") {
                return AshaderPropertyType::Int;
            }
            if (name == "uint") {
                return AshaderPropertyType::UInt;
            }
            if (name == "bool") {
                return AshaderPropertyType::Bool;
            }
            if (name == "texture2D") {
                return AshaderPropertyType::Texture2D;
            }
            if (name == "sampler") {
                return AshaderPropertyType::Sampler;
            }
            return std::nullopt;
        }

        bool isVectorType(AshaderPropertyType type) {
            return type == AshaderPropertyType::Float2 || type == AshaderPropertyType::Float3 ||
                   type == AshaderPropertyType::Float4 || type == AshaderPropertyType::Color;
        }

        std::size_t expectedVectorSize(AshaderPropertyType type) {
            switch (type) {
            case AshaderPropertyType::Float2:
                return 2;
            case AshaderPropertyType::Float3:
                return 3;
            case AshaderPropertyType::Float4:
            case AshaderPropertyType::Color:
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
            Parser(std::string_view source, const AshaderParseOptions& /*options*/)
                : lexer_(source) {
                advance();
            }

            AshaderParseResult parse() {
                AshaderDocument document{};
                document.fullSpan.begin = current_.span.begin;

                parseSchema(document);
                parseShader(document);

                if (!document.rawSlang && document.slangFiles.empty()) {
                    addDiagnostic(
                        AshaderDiagnosticCode::MissingSlangReference,
                        AshaderDiagnosticTarget::SlangReference, document.fullSpan,
                        "shader document requires a slang file reference or raw slang block");
                }

                document.fullSpan.end = current_.span.end;
                return AshaderParseResult{.document = std::move(document),
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

            bool expectIdentifier(std::string_view text, AshaderDiagnosticTarget target) {
                if (consumeIdentifier(text)) {
                    return true;
                }
                addDiagnostic(AshaderDiagnosticCode::ExpectedToken, target, current_.span,
                              std::string{"expected '"} + std::string{text} + "'");
                return false;
            }

            bool expect(TokenKind kind, std::string_view text, AshaderDiagnosticTarget target) {
                if (current_.kind == kind) {
                    advance();
                    return true;
                }
                addDiagnostic(AshaderDiagnosticCode::ExpectedToken, target, current_.span,
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

            void addDiagnostic(AshaderDiagnosticCode code, AshaderDiagnosticTarget target,
                               SourceSpan span, std::string message) {
                diagnostics_.push_back(AshaderDiagnostic{
                    .severity = AshaderDiagnosticSeverity::Error,
                    .code = code,
                    .target = target,
                    .span = span,
                    .message = std::move(message),
                });
            }

            void parseSchema(AshaderDocument& document) {
                if (!expectIdentifier("schema", AshaderDiagnosticTarget::File)) {
                    return;
                }
                const auto versionToken = consume(TokenKind::Number);
                if (!versionToken) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken,
                                  AshaderDiagnosticTarget::File, current_.span,
                                  "expected schema version number");
                    return;
                }
                const auto version = parseUnsigned(versionToken->text);
                if (!version) {
                    addDiagnostic(AshaderDiagnosticCode::UnsupportedSchema,
                                  AshaderDiagnosticTarget::File, versionToken->span,
                                  "schema version must be an unsigned integer");
                    return;
                }
                document.schemaVersion = *version;
                if (*version != 2U) {
                    addDiagnostic(AshaderDiagnosticCode::UnsupportedSchema,
                                  AshaderDiagnosticTarget::File, versionToken->span,
                                  "only .ashader schema 2 is supported");
                }
            }

            void parseShader(AshaderDocument& document) {
                if (!expectIdentifier("shader", AshaderDiagnosticTarget::Shader)) {
                    synchronizeToBlock();
                    return;
                }
                const auto shaderName = consume(TokenKind::String);
                if (!shaderName) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken,
                                  AshaderDiagnosticTarget::Shader, current_.span,
                                  "expected shader stable type id string");
                } else {
                    document.shaderTypeId = std::string{shaderName->text};
                }

                if (!expect(TokenKind::LBrace, "'{'", AshaderDiagnosticTarget::Shader)) {
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
                        addDiagnostic(AshaderDiagnosticCode::UnexpectedToken,
                                      AshaderDiagnosticTarget::Shader, current_.span,
                                      "unexpected token in shader block");
                        advance();
                    }
                }

                expect(TokenKind::RBrace, "'}'", AshaderDiagnosticTarget::Shader);
            }

            void parseProperties(AshaderDocument& document) {
                if (!expect(TokenKind::LBrace, "'{'", AshaderDiagnosticTarget::Property)) {
                    return;
                }

                while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
                    parseProperty(document);
                }

                expect(TokenKind::RBrace, "'}'", AshaderDiagnosticTarget::Property);
            }

            void parseProperty(AshaderDocument& document) {
                const auto typeToken = consume(TokenKind::Identifier);
                if (!typeToken) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken,
                                  AshaderDiagnosticTarget::Property, current_.span,
                                  "expected property type");
                    advance();
                    return;
                }

                const auto propertyType = propertyTypeFromName(typeToken->text);
                if (!propertyType) {
                    addDiagnostic(AshaderDiagnosticCode::UnknownPropertyType,
                                  AshaderDiagnosticTarget::Property, typeToken->span,
                                  std::string{"unknown property type '"} +
                                      std::string{typeToken->text} + "'");
                }

                const auto nameToken = consume(TokenKind::Identifier);
                if (!nameToken) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken,
                                  AshaderDiagnosticTarget::Property, current_.span,
                                  "expected property name");
                    return;
                }

                AshaderPropertyDefault defaultValue{};
                if (current_.kind == TokenKind::Equal) {
                    advance();
                    defaultValue = parsePropertyDefault();
                }

                if (!propertyType) {
                    return;
                }

                if (!propertyNames_.insert(std::string{nameToken->text}).second) {
                    addDiagnostic(AshaderDiagnosticCode::DuplicateProperty,
                                  AshaderDiagnosticTarget::Property, nameToken->span,
                                  std::string{"duplicate property '"} +
                                      std::string{nameToken->text} + "'");
                }

                if (!validateDefault(*propertyType, defaultValue)) {
                    addDiagnostic(AshaderDiagnosticCode::InvalidDefaultValue,
                                  AshaderDiagnosticTarget::Property, defaultValue.span,
                                  std::string{"invalid default value for property '"} +
                                      std::string{nameToken->text} + "'");
                }

                document.properties.push_back(AshaderPropertyDecl{
                    .type = *propertyType,
                    .typeName = std::string{typeToken->text},
                    .name = std::string{nameToken->text},
                    .defaultValue = std::move(defaultValue),
                    .span = joinSpan(typeToken->span, nameToken->span),
                    .typeSpan = typeToken->span,
                    .nameSpan = nameToken->span,
                });
            }

            AshaderPropertyDefault parsePropertyDefault() {
                if (const auto vectorDefault = parseVectorDefault()) {
                    return *vectorDefault;
                }

                if (const auto number = consume(TokenKind::Number)) {
                    const AshaderPropertyDefaultKind kind =
                        isIntegerText(number->text, true) ? AshaderPropertyDefaultKind::Integer
                                                          : AshaderPropertyDefaultKind::Number;
                    return AshaderPropertyDefault{
                        .kind = kind,
                        .text = std::string{number->text},
                        .elements = {std::string{number->text}},
                        .span = number->span,
                    };
                }

                if (const auto identifier = consume(TokenKind::Identifier)) {
                    if (identifier->text == "true" || identifier->text == "false") {
                        return AshaderPropertyDefault{
                            .kind = AshaderPropertyDefaultKind::Boolean,
                            .text = std::string{identifier->text},
                            .elements = {std::string{identifier->text}},
                            .span = identifier->span,
                        };
                    }
                    return AshaderPropertyDefault{
                        .kind = AshaderPropertyDefaultKind::None,
                        .text = std::string{identifier->text},
                        .elements = {},
                        .span = identifier->span,
                    };
                }

                SourceSpan span = current_.span;
                if (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
                    advance();
                }
                return AshaderPropertyDefault{.text = {}, .elements = {}, .span = span};
            }

            std::optional<AshaderPropertyDefault> parseVectorDefault() {
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
                        addDiagnostic(AshaderDiagnosticCode::InvalidDefaultValue,
                                      AshaderDiagnosticTarget::Property, current_.span,
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
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken,
                                  AshaderDiagnosticTarget::Property, current_.span,
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
                return AshaderPropertyDefault{
                    .kind = AshaderPropertyDefaultKind::Vector,
                    .text = std::move(text),
                    .elements = std::move(elements),
                    .span = span,
                };
            }

            [[nodiscard]] static bool validateDefault(AshaderPropertyType type,
                                                      const AshaderPropertyDefault& defaultValue) {
                if (defaultValue.kind == AshaderPropertyDefaultKind::None &&
                    defaultValue.text.empty()) {
                    return true;
                }

                if (isVectorType(type)) {
                    return defaultValue.kind == AshaderPropertyDefaultKind::Vector &&
                           defaultValue.elements.size() == expectedVectorSize(type) &&
                           std::ranges::all_of(defaultValue.elements, [](const std::string& text) {
                               return isNumberText(text);
                           });
                }

                switch (type) {
                case AshaderPropertyType::Float:
                    return (defaultValue.kind == AshaderPropertyDefaultKind::Number ||
                            defaultValue.kind == AshaderPropertyDefaultKind::Integer) &&
                           isNumberText(defaultValue.text);
                case AshaderPropertyType::Int:
                    return defaultValue.kind == AshaderPropertyDefaultKind::Integer &&
                           isIntegerText(defaultValue.text, true);
                case AshaderPropertyType::UInt:
                    return defaultValue.kind == AshaderPropertyDefaultKind::Integer &&
                           isIntegerText(defaultValue.text, false);
                case AshaderPropertyType::Bool:
                    return defaultValue.kind == AshaderPropertyDefaultKind::Boolean;
                case AshaderPropertyType::Texture2D:
                case AshaderPropertyType::Sampler:
                    return false;
                case AshaderPropertyType::Float2:
                case AshaderPropertyType::Float3:
                case AshaderPropertyType::Float4:
                case AshaderPropertyType::Color:
                    break;
                }
                return false;
            }

            void parsePass(AshaderDocument& document) {
                AshaderPassDecl pass{};
                const SourceSpan passBegin = current_.span;
                const auto name = consume(TokenKind::String);
                if (!name) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken,
                                  AshaderDiagnosticTarget::Pass, current_.span,
                                  "expected pass name string");
                } else {
                    pass.name = std::string{name->text};
                    pass.nameSpan = name->span;
                }

                if (!expect(TokenKind::LBrace, "'{'", AshaderDiagnosticTarget::Pass)) {
                    return;
                }

                while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
                    if (consumeIdentifier("tag")) {
                        pass.tag = parseStringValue(AshaderDiagnosticTarget::Pass, "pass tag");
                    } else if (consumeIdentifier("vertex")) {
                        pass.vertexEntry =
                            parseIdentifierValue(AshaderDiagnosticTarget::Pass, "vertex entry");
                    } else if (consumeIdentifier("fragment")) {
                        pass.fragmentEntry =
                            parseIdentifierValue(AshaderDiagnosticTarget::Pass, "fragment entry");
                    } else if (consumeIdentifier("compute")) {
                        pass.computeEntry =
                            parseIdentifierValue(AshaderDiagnosticTarget::Pass, "compute entry");
                    } else if (consumeIdentifier("cull")) {
                        pass.cullMode =
                            parseIdentifierValue(AshaderDiagnosticTarget::Pass, "cull mode");
                    } else if (consumeIdentifier("depthTest")) {
                        pass.depthTest =
                            parseIdentifierValue(AshaderDiagnosticTarget::Pass, "depth test");
                    } else if (consumeIdentifier("depthWrite")) {
                        pass.depthWrite =
                            parseBoolValue(AshaderDiagnosticTarget::Pass, "depth write flag");
                    } else if (consumeIdentifier("blend")) {
                        pass.blendMode =
                            parseIdentifierValue(AshaderDiagnosticTarget::Pass, "blend mode");
                    } else if (isIdentifier("slang")) {
                        parseSlangReference(document, &pass);
                    } else if (isIdentifier("graph")) {
                        parseGraphReference(document, &pass);
                    } else {
                        addDiagnostic(AshaderDiagnosticCode::UnexpectedToken,
                                      AshaderDiagnosticTarget::Pass, current_.span,
                                      "unexpected token in pass block");
                        advance();
                    }
                }

                const SourceSpan closeSpan = current_.span;
                expect(TokenKind::RBrace, "'}'", AshaderDiagnosticTarget::Pass);
                pass.span = SourceSpan{.begin = passBegin.begin, .end = closeSpan.end};

                if (!pass.vertexEntry && !pass.fragmentEntry && !pass.computeEntry) {
                    addDiagnostic(AshaderDiagnosticCode::MissingPassEntry,
                                  AshaderDiagnosticTarget::Pass, pass.span,
                                  std::string{"pass '"} + pass.name +
                                      "' requires vertex, fragment, or compute entry");
                }

                document.passes.push_back(std::move(pass));
            }

            std::optional<std::string> parseStringValue(AshaderDiagnosticTarget target,
                                                        std::string_view label) {
                const auto token = consume(TokenKind::String);
                if (!token) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken, target, current_.span,
                                  std::string{"expected "} + std::string{label} + " string");
                    return std::nullopt;
                }
                return std::string{token->text};
            }

            std::optional<std::string> parseIdentifierValue(AshaderDiagnosticTarget target,
                                                            std::string_view label) {
                const auto token = consume(TokenKind::Identifier);
                if (!token) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken, target, current_.span,
                                  std::string{"expected "} + std::string{label} + " identifier");
                    return std::nullopt;
                }
                return std::string{token->text};
            }

            std::optional<bool> parseBoolValue(AshaderDiagnosticTarget target,
                                               std::string_view label) {
                const auto token = consume(TokenKind::Identifier);
                if (!token || (token->text != "true" && token->text != "false")) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken, target,
                                  token ? token->span : current_.span,
                                  std::string{"expected "} + std::string{label} + " boolean");
                    return std::nullopt;
                }
                return token->text == "true";
            }

            void parseSlangReference(AshaderDocument& document, AshaderPassDecl* pass) {
                const SourceSpan keywordSpan = current_.span;
                advance();
                if (const auto path = consume(TokenKind::String)) {
                    AshaderSourceReference reference{.path = std::string{path->text},
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
                addDiagnostic(AshaderDiagnosticCode::ExpectedToken,
                              AshaderDiagnosticTarget::SlangReference, keywordSpan,
                              "expected slang file string or raw slang block");
            }

            void parseGraphReference(AshaderDocument& document, AshaderPassDecl* pass) {
                advance();
                const auto path = consume(TokenKind::String);
                if (!path) {
                    addDiagnostic(AshaderDiagnosticCode::ExpectedToken,
                                  AshaderDiagnosticTarget::GraphReference, current_.span,
                                  "expected graph file string");
                    return;
                }
                AshaderSourceReference reference{.path = std::string{path->text},
                                                 .span = path->span};
                document.graphFiles.push_back(reference);
                if (pass != nullptr) {
                    pass->graphFiles.push_back(std::move(reference));
                }
            }

            void parseRawSlangBlock(AshaderDocument& document) {
                const std::size_t openOffset = current_.span.begin.offset;
                const auto match = findRawBlockClose(lexer_.source(), openOffset);
                if (!match) {
                    addDiagnostic(AshaderDiagnosticCode::UnbalancedRawSlangBlock,
                                  AshaderDiagnosticTarget::RawSlangBlock, current_.span,
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
                    addDiagnostic(AshaderDiagnosticCode::UnexpectedToken,
                                  AshaderDiagnosticTarget::RawSlangBlock, rawSpan,
                                  "only one raw slang block is supported in this slice");
                } else {
                    document.rawSlang = AshaderRawSlangBlock{
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
            std::vector<AshaderDiagnostic> diagnostics_;
            std::unordered_set<std::string> propertyNames_;
        };

    } // namespace

    std::string_view toString(AshaderDiagnosticSeverity severity) {
        switch (severity) {
        case AshaderDiagnosticSeverity::Warning:
            return "warning";
        case AshaderDiagnosticSeverity::Error:
            return "error";
        }
        return "unknown";
    }

    std::string_view toString(AshaderDiagnosticCode code) {
        switch (code) {
        case AshaderDiagnosticCode::ExpectedToken:
            return "expected-token";
        case AshaderDiagnosticCode::UnexpectedToken:
            return "unexpected-token";
        case AshaderDiagnosticCode::UnsupportedSchema:
            return "unsupported-schema";
        case AshaderDiagnosticCode::DuplicateProperty:
            return "duplicate-property";
        case AshaderDiagnosticCode::UnknownPropertyType:
            return "unknown-property-type";
        case AshaderDiagnosticCode::InvalidDefaultValue:
            return "invalid-default-value";
        case AshaderDiagnosticCode::MissingPassEntry:
            return "missing-pass-entry";
        case AshaderDiagnosticCode::MissingSlangReference:
            return "missing-slang-reference";
        case AshaderDiagnosticCode::UnbalancedRawSlangBlock:
            return "unbalanced-raw-slang-block";
        }
        return "unknown";
    }

    std::string_view toString(AshaderPropertyType type) {
        switch (type) {
        case AshaderPropertyType::Float:
            return "float";
        case AshaderPropertyType::Float2:
            return "float2";
        case AshaderPropertyType::Float3:
            return "float3";
        case AshaderPropertyType::Float4:
            return "float4";
        case AshaderPropertyType::Color:
            return "color";
        case AshaderPropertyType::Int:
            return "int";
        case AshaderPropertyType::UInt:
            return "uint";
        case AshaderPropertyType::Bool:
            return "bool";
        case AshaderPropertyType::Texture2D:
            return "texture2D";
        case AshaderPropertyType::Sampler:
            return "sampler";
        }
        return "unknown";
    }

    bool hasErrors(const std::vector<AshaderDiagnostic>& diagnostics) {
        return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == AshaderDiagnosticSeverity::Error;
        });
    }

    AshaderParseResult parseAshaderDocument(std::string_view source,
                                            const AshaderParseOptions& options) {
        Parser parser{source, options};
        return parser.parse();
    }

} // namespace asharia::shader_authoring
