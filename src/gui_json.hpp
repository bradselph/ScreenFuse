s#pragma once
#include "common.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace screenfuse { namespace gui {

inline void AppendJsonEscaped(std::string& out, const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
}

inline std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    AppendJsonEscaped(out, text);
    return out;
}

class JsonBuilder {
public:
    JsonBuilder& BeginObject() {
        ValuePrefix();
        buf_ += '{';
        stack_.push_back({ true, true });
        return *this;
    }

    JsonBuilder& EndObject() {
        buf_ += '}';
        if (!stack_.empty()) stack_.pop_back();
        return *this;
    }

    JsonBuilder& BeginArray() {
        ValuePrefix();
        buf_ += '[';
        stack_.push_back({ false, true });
        return *this;
    }

    JsonBuilder& EndArray() {
        buf_ += ']';
        if (!stack_.empty()) stack_.pop_back();
        return *this;
    }

    JsonBuilder& Key(const char* name) {
        if (!stack_.empty() && stack_.back().isObject) {
            if (!stack_.back().first) buf_ += ',';
            stack_.back().first = false;
        }
        buf_ += '"';
        AppendJsonEscaped(buf_, name);
        buf_ += "\":";
        return *this;
    }

    JsonBuilder& Str(const std::string& value) {
        ValuePrefix();
        buf_ += '"';
        AppendJsonEscaped(buf_, value);
        buf_ += '"';
        return *this;
    }

    JsonBuilder& Wide(const std::wstring& value) { return Str(ToUtf8(value)); }

    JsonBuilder& Int(long long value) {
        ValuePrefix();
        char digits[32];
        snprintf(digits, sizeof(digits), "%lld", value);
        buf_ += digits;
        return *this;
    }

    JsonBuilder& Num(double value) {
        ValuePrefix();
        if (!std::isfinite(value)) {
            buf_ += "null";
            return *this;
        }
        char digits[64];
        snprintf(digits, sizeof(digits), "%.10g", value);
        buf_ += digits;
        return *this;
    }

    JsonBuilder& Bool(bool value) {
        ValuePrefix();
        buf_ += value ? "true" : "false";
        return *this;
    }

    JsonBuilder& Null() {
        ValuePrefix();
        buf_ += "null";
        return *this;
    }

    JsonBuilder& Raw(const std::string& alreadyJson) {
        ValuePrefix();
        buf_ += alreadyJson;
        return *this;
    }

    JsonBuilder& Field(const char* name, const std::string& value) { return Key(name).Str(value); }
    JsonBuilder& Field(const char* name, const char* value) { return Key(name).Str(std::string(value)); }
    JsonBuilder& Field(const char* name, const std::wstring& value) { return Key(name).Wide(value); }
    JsonBuilder& Field(const char* name, long long value) { return Key(name).Int(value); }
    JsonBuilder& Field(const char* name, int value) { return Key(name).Int(static_cast<long long>(value)); }
    JsonBuilder& Field(const char* name, double value) { return Key(name).Num(value); }
    JsonBuilder& Field(const char* name, bool value) { return Key(name).Bool(value); }

    const std::string& str() const { return buf_; }
    std::string take() { return std::move(buf_); }

private:
    struct Frame {
        bool isObject;
        bool first;
    };

    void ValuePrefix() {
        if (stack_.empty()) return;
        Frame& top = stack_.back();
        if (!top.isObject) {
            if (!top.first) buf_ += ',';
            top.first = false;
        }
    }

    std::string buf_;
    std::vector<Frame> stack_;
};

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> fields;

    bool IsNull() const { return type == Type::Null; }

    const JsonValue* Find(const char* key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& field : fields) {
            if (field.first == key) return &field.second;
        }
        return nullptr;
    }

    bool Has(const char* key) const { return Find(key) != nullptr; }

    bool GetBool(const char* key, bool fallback) const {
        const JsonValue* value = Find(key);
        return (value && value->type == Type::Bool) ? value->boolean : fallback;
    }

    int GetInt(const char* key, int fallback) const {
        const JsonValue* value = Find(key);
        if (!value || value->type != Type::Number) return fallback;
        return ClampToInt32(value->number);
    }

    long long GetInt64(const char* key, long long fallback) const {
        const JsonValue* value = Find(key);
        if (!value || value->type != Type::Number) return fallback;
        return ClampToInt64(value->number);
    }

    double GetNumber(const char* key, double fallback) const {
        const JsonValue* value = Find(key);
        return (value && value->type == Type::Number) ? value->number : fallback;
    }

    std::string GetString(const char* key, const std::string& fallback) const {
        const JsonValue* value = Find(key);
        return (value && value->type == Type::String) ? value->text : fallback;
    }

private:
    static int ClampToInt32(double value) {
        if (value != value) return 0;
        if (value <= static_cast<double>(INT32_MIN)) return INT32_MIN;
        if (value >= static_cast<double>(INT32_MAX)) return INT32_MAX;
        return static_cast<int>(value);
    }

    static long long ClampToInt64(double value) {
        if (value != value) return 0;
        const double lowerBound = -9223372036854775808.0;
        const double upperBoundExclusive = 9223372036854775808.0;
        if (value <= lowerBound) return INT64_MIN;
        if (value >= upperBoundExclusive) return INT64_MAX;
        return static_cast<long long>(value);
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text), pos_(0), depth_(0) {}

    bool Parse(JsonValue& out, std::string& error) {
        if (text_.size() > kMaxJsonBytes) {
            error = "input exceeds the 4 MB limit";
            return false;
        }

        SkipWhitespace();
        if (!ParseValue(out, error)) return false;
        SkipWhitespace();

        if (pos_ != text_.size()) {
            return Fail(error, "trailing content after the top-level value");
        }

        return true;
    }

private:
    static constexpr size_t kMaxJsonBytes = 4u * 1024u * 1024u;
    static constexpr int kMaxDepth = 64;

    const std::string& text_;
    size_t pos_;
    int depth_;

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return text_[pos_]; }
    static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

    bool Fail(std::string& error, const std::string& message) {
        error = message + " at byte " + std::to_string(pos_);
        return false;
    }

    void SkipWhitespace() {
        while (!AtEnd()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    bool ParseValue(JsonValue& out, std::string& error) {
        if (AtEnd()) return Fail(error, "unexpected end of input");

        const char c = Peek();
        if (c == '{') return ParseObject(out, error);
        if (c == '[') return ParseArray(out, error);
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return ParseString(out.text, error);
        }
        if (c == 't' || c == 'f') return ParseBool(out, error);
        if (c == 'n') return ParseNull(out, error);
        if (c == '-' || IsDigit(c)) return ParseNumber(out, error);

        return Fail(error, "unexpected character");
    }

    bool ParseObject(JsonValue& out, std::string& error) {
        if (++depth_ > kMaxDepth) {
            --depth_;
            return Fail(error, "nesting depth exceeds the limit of 64");
        }

        out.type = JsonValue::Type::Object;
        ++pos_;
        SkipWhitespace();

        if (!AtEnd() && Peek() == '}') {
            ++pos_;
            --depth_;
            return true;
        }

        for (;;) {
            SkipWhitespace();
            if (AtEnd() || Peek() != '"') {
                --depth_;
                return Fail(error, "expected a string key");
            }

            std::string key;
            if (!ParseString(key, error)) {
                --depth_;
                return false;
            }

            SkipWhitespace();
            if (AtEnd() || Peek() != ':') {
                --depth_;
                return Fail(error, "expected ':' after an object key");
            }
            ++pos_;
            SkipWhitespace();

            JsonValue value;
            if (!ParseValue(value, error)) {
                --depth_;
                return false;
            }
            out.fields.emplace_back(std::move(key), std::move(value));

            SkipWhitespace();
            if (AtEnd()) {
                --depth_;
                return Fail(error, "unterminated object");
            }

            if (Peek() == ',') { ++pos_; continue; }
            if (Peek() == '}') { ++pos_; break; }

            --depth_;
            return Fail(error, "expected ',' or '}' in object");
        }

        --depth_;
        return true;
    }

    bool ParseArray(JsonValue& out, std::string& error) {
        if (++depth_ > kMaxDepth) {
            --depth_;
            return Fail(error, "nesting depth exceeds the limit of 64");
        }

        out.type = JsonValue::Type::Array;
        ++pos_;
        SkipWhitespace();

        if (!AtEnd() && Peek() == ']') {
            ++pos_;
            --depth_;
            return true;
        }

        for (;;) {
            SkipWhitespace();

            JsonValue value;
            if (!ParseValue(value, error)) {
                --depth_;
                return false;
            }
            out.items.push_back(std::move(value));

            SkipWhitespace();
            if (AtEnd()) {
                --depth_;
                return Fail(error, "unterminated array");
            }

            if (Peek() == ',') { ++pos_; continue; }
            if (Peek() == ']') { ++pos_; break; }

            --depth_;
            return Fail(error, "expected ',' or ']' in array");
        }

        --depth_;
        return true;
    }

    bool ParseBool(JsonValue& out, std::string& error) {
        if (text_.compare(pos_, 4, "true") == 0) {
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            pos_ += 4;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            out.type = JsonValue::Type::Bool;
            out.boolean = false;
            pos_ += 5;
            return true;
        }
        return Fail(error, "invalid literal");
    }

    bool ParseNull(JsonValue& out, std::string& error) {
        if (text_.compare(pos_, 4, "null") == 0) {
            out.type = JsonValue::Type::Null;
            pos_ += 4;
            return true;
        }
        return Fail(error, "invalid literal");
    }

    bool ParseNumber(JsonValue& out, std::string& error) {
        const size_t start = pos_;

        if (!AtEnd() && Peek() == '-') ++pos_;

        if (AtEnd() || !IsDigit(Peek())) return Fail(error, "malformed number");

        if (Peek() == '0') {
            ++pos_;
        } else {
            while (!AtEnd() && IsDigit(Peek())) ++pos_;
        }

        if (!AtEnd() && Peek() == '.') {
            ++pos_;
            if (AtEnd() || !IsDigit(Peek())) return Fail(error, "malformed number");
            while (!AtEnd() && IsDigit(Peek())) ++pos_;
        }

        if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
            ++pos_;
            if (!AtEnd() && (Peek() == '+' || Peek() == '-')) ++pos_;
            if (AtEnd() || !IsDigit(Peek())) return Fail(error, "malformed number");
            while (!AtEnd() && IsDigit(Peek())) ++pos_;
        }

        const std::string token = text_.substr(start, pos_ - start);

        errno = 0;
        char* endPtr = nullptr;
        const double value = strtod(token.c_str(), &endPtr);

        if (endPtr != token.c_str() + token.size()) return Fail(error, "malformed number");
        if (errno == ERANGE) return Fail(error, "number out of range");

        out.type = JsonValue::Type::Number;
        out.number = value;
        return true;
    }

    bool ParseString(std::string& out, std::string& error) {
        ++pos_;
        out.clear();

        for (;;) {
            if (AtEnd()) return Fail(error, "unterminated string");

            const unsigned char c = static_cast<unsigned char>(text_[pos_]);

            if (c == '"') { ++pos_; return true; }

            if (c == '\\') {
                ++pos_;
                if (AtEnd()) return Fail(error, "unterminated string");

                const char esc = text_[pos_];
                switch (esc) {
                    case '"': out += '"'; ++pos_; break;
                    case '\\': out += '\\'; ++pos_; break;
                    case '/': out += '/'; ++pos_; break;
                    case 'b': out += '\b'; ++pos_; break;
                    case 'f': out += '\f'; ++pos_; break;
                    case 'n': out += '\n'; ++pos_; break;
                    case 'r': out += '\r'; ++pos_; break;
                    case 't': out += '\t'; ++pos_; break;
                    case 'u': {
                        ++pos_;
                        unsigned int code = 0;
                        if (!ParseHex4(code, error)) return false;

                        if (code >= 0xD800 && code <= 0xDBFF) {
                            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                                const size_t beforeLow = pos_;
                                pos_ += 2;
                                unsigned int low = 0;
                                if (!ParseHex4(low, error)) return false;

                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    const unsigned int combined = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
                                    AppendUtf8(out, combined);
                                } else {
                                    pos_ = beforeLow;
                                    AppendUtf8(out, 0xFFFD);
                                }
                            } else {
                                AppendUtf8(out, 0xFFFD);
                            }
                        } else if (code >= 0xDC00 && code <= 0xDFFF) {
                            AppendUtf8(out, 0xFFFD);
                        } else {
                            AppendUtf8(out, code);
                        }
                        break;
                    }
                    default:
                        return Fail(error, "invalid escape sequence");
                }
                continue;
            }

            if (c < 0x20) return Fail(error, "control character in string");

            out += static_cast<char>(c);
            ++pos_;
        }
    }

    bool ParseHex4(unsigned int& out, std::string& error) {
        if (pos_ + 4 > text_.size()) return Fail(error, "truncated \\u escape");

        unsigned int value = 0;
        for (size_t i = 0; i < 4; ++i) {
            const char c = text_[pos_ + i];
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return Fail(error, "invalid \\u escape");
            value = (value << 4) | static_cast<unsigned int>(digit);
        }

        pos_ += 4;
        out = value;
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned int codepoint) {
        if (codepoint <= 0x7F) {
            out += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7FF) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }
};

inline bool ParseJson(const std::string& text, JsonValue& out, std::string& error) {
    out = JsonValue{};
    JsonParser parser(text);
    return parser.Parse(out, error);
}

} } // namespace screenfuse::gui
