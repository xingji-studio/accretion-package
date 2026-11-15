#include "ap/base.hpp"
namespace ap {
Str number(uint64_t value) {
    char text[32];
    size_t n = 0;
    do {
        text[n++] = char('0' + value % 10);
        value /= 10;
    } while (value);
    Str out;
    while (n)
        out.push(text[--n]);
    return out;
}
bool unsignedNumber(const Str& text, uint64_t& value) {
    if (text.empty())
        return false;
    value = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned d = unsigned(text[i] - '0');
        if (d > 9 || value > (UINT64_MAX - d) / 10)
            return false;
        value = value * 10 + d;
    }
    return true;
}
bool utf8(const Str& text) {
    for (size_t i = 0; i < text.size();) {
        uint8_t c = uint8_t(text[i++]);
        if (c < 0x80)
            continue;
        uint32_t code;
        unsigned count;
        if (c >= 0xc2 && c <= 0xdf) {
            code = c & 31;
            count = 1;
        } else if (c >= 0xe0 && c <= 0xef) {
            code = c & 15;
            count = 2;
        } else if (c >= 0xf0 && c <= 0xf4) {
            code = c & 7;
            count = 3;
        } else
            return false;
        if (i + count > text.size())
            return false;
        unsigned n = count;
        while (n--) {
            uint8_t d = uint8_t(text[i++]);
            if ((d & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (d & 63);
        }
        if ((count == 2 && code < 0x800) || (count == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}
Str slash(const Str& path) {
    return path.replace("\\", "/");
}
List<Str> split(const Str& value, char separator) {
    List<Str> out;
    size_t begin = 0;
    for (size_t i = 0; i <= value.size(); ++i)
        if (i == value.size() || value[i] == separator) {
            out.add(value.substr(begin, i - begin));
            begin = i + 1;
        }
    return out;
}
bool absolutePath(const Str& path) {
    Str p = slash(path);
    return p.starts("/") || (p.size() > 2 && p[1] == ':' && p[2] == '/');
}
bool relativePath(const Str& path, Str& error) {
    if (path.empty() || absolutePath(path) || path.find('\\') != size_t(-1) || path.size() > 1024 ||
        !utf8(path)) {
        error = "Invalid relative path: " + path;
        return false;
    }
    for (size_t i = 0; i < path.size(); ++i)
        if (uint8_t(path[i]) < 32 || path[i] == ':' || path[i] == 127) {
            error = "Unsafe character in path: " + path;
            return false;
        }
    for (const auto& part : split(path, '/')) {
        if (part.empty() || part == "." || part == ".." || part.lower() == ".accretion") {
            error = "Unsafe path component: " + path;
            return false;
        }
    }
    return true;
}
Str parent(const Str& path) {
    Str p = slash(path);
    while (p.size() > 1 && p.ends("/"))
        p = p.substr(0, p.size() - 1);
    size_t last = size_t(-1);
    for (size_t i = 0; i < p.size(); ++i)
        if (p[i] == '/')
            last = i;
    if (last == size_t(-1))
        return {};
    if (last == 0)
        return "/";
    if (last == 2 && p[1] == ':')
        return p.substr(0, 3);
    return p.substr(0, last);
}
Str leaf(const Str& path) {
    Str p = slash(path);
    size_t last = 0;
    for (size_t i = 0; i < p.size(); ++i)
        if (p[i] == '/')
            last = i + 1;
    return p.substr(last);
}
Str join(const Str& a, const Str& b) {
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    return a + (a.ends("/") ? "" : "/") + b;
}
} // namespace ap
