#include "ap/json.hpp"
namespace ap {
const Json& Json::get(const Str& key) const {
    if (kind == Object)
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return values[i];
    static Json* empty = nullptr;
    if (!empty) {
        empty = static_cast<Json*>(ap_allocate(sizeof(Json)));
        if (!empty)
            ap_panic("Out of memory");
        new (ApPlacement{}, empty) Json();
    }
    return *empty;
}
Json* Json::find(const Str& key) {
    if (kind == Object)
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return &values[i];
    return nullptr;
}
void Json::set(const Str& key, Json value) {
    if (kind == Null)
        kind = Object;
    if (Json* v = find(key)) {
        *v = move(value);
        return;
    }
    keys.add(key);
    values.add(move(value));
}
void Json::erase(const Str& key) {
    if (kind != Object)
        return;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key) {
            keys.erase(i);
            values.erase(i);
            return;
        }
}
Str jsonQuote(const Str& input) {
    const char hex[] = "0123456789abcdef";
    Str out = "\"";
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = input[i];
        if (c == '"' || c == '\\') {
            out.push('\\');
            out.push(char(c));
        } else if (c < 32) {
            out.append("\\u00");
            out.push(hex[c >> 4]);
            out.push(hex[c & 15]);
        } else
            out.push(char(c));
    }
    out.push('"');
    return out;
}
bool Json::same(const Json& other) const {
    if (kind != other.kind)
        return false;
    if (kind == Null)
        return true;
    if (kind == String || kind == Number)
        return text == other.text;
    if (kind == Bool)
        return flag == other.flag;
    if (values.size() != other.values.size())
        return false;
    if (kind == Object) {
        for (size_t i = 0; i < keys.size(); ++i) {
            bool found = false;
            for (const auto& key : other.keys)
                if (key == keys[i])
                    found = true;
            if (!found || !values[i].same(other.get(keys[i])))
                return false;
        }
        return true;
    }
    for (size_t i = 0; i < values.size(); ++i)
        if (!values[i].same(other.values[i]))
            return false;
    return true;
}
Str Json::dump() const {
    if (kind == Null)
        return "null";
    if (kind == Bool)
        return flag ? "true" : "false";
    if (kind == Number)
        return text;
    if (kind == String)
        return jsonQuote(text);
    Str out = kind == Object ? "{" : "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out.push(',');
        if (kind == Object) {
            out.append(jsonQuote(keys[i]));
            out.push(':');
        }
        out.append(values[i].dump());
    }
    out.push(kind == Object ? '}' : ']');
    return out;
}
namespace {
class Parser {
    const Str& in;
    size_t at = 0, nodes = 0;
    Str error;
    void ws() {
        while (at < in.size() && (in[at] == ' ' || in[at] == '\t' || in[at] == '\r' || in[at] == '\n'))
            ++at;
    }
    bool fail(const char* why) {
        if (error.empty())
            error = Str(why) + " at byte " + number(at);
        return false;
    }
    bool consume(char ch) {
        ws();
        if (at >= in.size() || in[at] != ch)
            return false;
        ++at;
        return true;
    }
    bool hex4(uint32_t& value) {
        value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (at >= in.size())
                return fail("Incomplete Unicode escape");
            char c = in[at++];
            unsigned d = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : 16;
            if (d > 15)
                return fail("Invalid Unicode escape");
            value = value * 16 + d;
        }
        return true;
    }
    void codepoint(Str& out, uint32_t c) {
        if (c < 128)
            out.push(char(c));
        else if (c < 2048) {
            out.push(char(0xc0 | (c >> 6)));
            out.push(char(0x80 | (c & 63)));
        } else if (c < 65536) {
            out.push(char(0xe0 | (c >> 12)));
            out.push(char(0x80 | ((c >> 6) & 63)));
            out.push(char(0x80 | (c & 63)));
        } else {
            out.push(char(0xf0 | (c >> 18)));
            out.push(char(0x80 | ((c >> 12) & 63)));
            out.push(char(0x80 | ((c >> 6) & 63)));
            out.push(char(0x80 | (c & 63)));
        }
    }
    bool string(Str& out) {
        if (!consume('"'))
            return fail("Expected string");
        while (at < in.size()) {
            unsigned char c = in[at++];
            if (c == '"')
                return utf8(out) || fail("Invalid UTF-8");
            if (c < 32)
                return fail("Control character in string");
            if (c != '\\') {
                out.push(char(c));
                continue;
            }
            if (at >= in.size())
                return fail("Incomplete escape");
            char e = in[at++];
            if (e == '"' || e == '\\' || e == '/')
                out.push(e);
            else if (e == 'b')
                out.push('\b');
            else if (e == 'f')
                out.push('\f');
            else if (e == 'n')
                out.push('\n');
            else if (e == 'r')
                out.push('\r');
            else if (e == 't')
                out.push('\t');
            else if (e == 'u') {
                uint32_t cp;
                if (!hex4(cp))
                    return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (at + 2 > in.size() || in[at] != '\\' || in[at + 1] != 'u')
                        return fail("Missing low surrogate");
                    at += 2;
                    uint32_t low;
                    if (!hex4(low) || low < 0xdc00 || low > 0xdfff)
                        return fail("Invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    return fail("Unpaired surrogate");
                codepoint(out, cp);
            } else
                return fail("Unknown escape");
        }
        return fail("Unterminated string");
    }
    bool value(Json& out, unsigned depth) {
        ws();
        if (depth > 64 || ++nodes > 1000000)
            return fail("JSON complexity limit exceeded");
        if (at >= in.size())
            return fail("Expected value");
        char c = in[at];
        if (c == '"') {
            out.kind = Json::String;
            return string(out.text);
        }
        if (c == '{' || c == '[') {
            ++at;
            out.kind = c == '{' ? Json::Object : Json::Array;
            char close = c == '{' ? '}' : ']';
            ws();
            if (consume(close))
                return true;
            for (;;) {
                Str key;
                if (c == '{') {
                    if (!string(key) || !consume(':'))
                        return fail("Expected object key and colon");
                    if (out.find(key))
                        return fail("Duplicate object key");
                }
                Json child;
                if (!value(child, depth + 1))
                    return false;
                if (c == '{')
                    out.set(key, move(child));
                else
                    out.add(move(child));
                if (consume(close))
                    return true;
                if (!consume(','))
                    return fail("Expected comma");
            }
        }
        const char* literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : nullptr;
        if (literal) {
            size_t n = length(literal);
            if (in.substr(at, n) != literal)
                return fail("Invalid literal");
            at += n;
            if (c != 'n') {
                out.kind = Json::Bool;
                out.flag = c == 't';
            }
            return true;
        }
        size_t begin = at;
        if (c == '-')
            ++at;
        if (at >= in.size())
            return fail("Invalid number");
        if (in[at] == '0')
            ++at;
        else if (in[at] >= '1' && in[at] <= '9')
            while (at < in.size() && in[at] >= '0' && in[at] <= '9')
                ++at;
        else
            return fail("Invalid number");
        if (at < in.size() && in[at] == '.') {
            ++at;
            size_t start = at;
            while (at < in.size() && in[at] >= '0' && in[at] <= '9')
                ++at;
            if (start == at)
                return fail("Invalid fraction");
        }
        if (at < in.size() && (in[at] == 'e' || in[at] == 'E')) {
            ++at;
            if (at < in.size() && (in[at] == '+' || in[at] == '-'))
                ++at;
            size_t start = at;
            while (at < in.size() && in[at] >= '0' && in[at] <= '9')
                ++at;
            if (start == at)
                return fail("Invalid exponent");
        }
        out.kind = Json::Number;
        out.text = in.substr(begin, at - begin);
        return true;
    }

  public:
    explicit Parser(const Str& input) : in(input) {}
    Status parse(Json& out) {
        if (in.size() > 16 * 1024 * 1024)
            return Status::fail("JSON exceeds 16 MiB");
        if (!value(out, 0))
            return Status::fail(error);
        ws();
        if (at != in.size())
            return Status::fail("Trailing JSON data at byte " + number(at));
        return {};
    }
};
} // namespace
Status parseJson(const Str& input, Json& output) {
    Json value;
    Status result = Parser(input).parse(value);
    if (result)
        output = move(value);
    return result;
}
} // namespace ap
