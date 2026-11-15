#include "ap/platform.hpp"
namespace ap::os {
Status readText(const Str& path, Str& value, size_t limit) {
    Info stat = info(path);
    if (!stat.exists || stat.directory || stat.link)
        return Status::fail("Cannot read regular file: " + path);
    if (stat.size > limit)
        return Status::fail("File exceeds size limit: " + path);
    Str out;
    out.resize(size_t(stat.size));
    size_t done = 0;
    while (done < out.size()) {
        size_t actual = 0;
        AP_TRY(read(path, done, out.data() + done, out.size() - done, actual));
        if (!actual)
            return Status::fail("Unexpected end of file: " + path);
        done += actual;
    }
    value = move(out);
    return {};
}
Status writeText(const Str& path, const Str& value) {
    return write(path, 0, value.c_str(), value.size(), true);
}
Status directories(const Str& path) {
    if (path.empty())
        return {};
    Info stat = info(path);
    if (stat.exists)
        return stat.directory && !stat.link ? Status{} : Status::fail("Not a safe directory: " + path);
    Str up = parent(path);
    if (up == path)
        return Status::fail("Cannot create root directory: " + path);
    if (!up.empty())
        AP_TRY(directories(up));
    return mkdir(path);
}
Status safeAncestors(const Str& path) {
    Str p = slash(path);
    if (!absolutePath(p))
        return Status::fail("Installation directory must be absolute");
    if (equal(name(), "windows")) {
        bool drive = p.size() > 2 && p[1] == ':' && p[2] == '/' &&
                     ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z'));
        if (!drive && !p.starts("//"))
            return Status::fail("Windows paths need an absolute drive or UNC location");
    } else if (!p.starts("/"))
        return Status::fail("Native installation paths must start with /");
    for (size_t i = 0; i < p.size(); ++i)
        if (uint8_t(p[i]) < 32)
            return Status::fail("Control character in path");
    for (const Str& part : split(p, '/'))
        if (part == "." || part == "..")
            return Status::fail("Dot path components are not allowed");
    Str cursor = p;
    while (!cursor.empty()) {
        Info stat = info(cursor);
        if (stat.link)
            return Status::fail("Symbolic links/reparse points are not allowed: " + cursor);
        if (stat.exists && !stat.directory && cursor != p)
            return Status::fail("Parent is not a directory: " + cursor);
        Str next = parent(cursor);
        if (next == cursor)
            break;
        cursor = next;
    }
    return {};
}
Status copy(const Str& from, const Str& to, uint64_t limit) {
    Info stat = info(from);
    if (!stat.exists || stat.directory || stat.link)
        return Status::fail("Cannot copy file: " + from);
    uint64_t size = stat.size;
    if (limit < size)
        size = limit;
    AP_TRY(directories(parent(to)));
    AP_TRY(write(to, 0, "", 0, true));
    const size_t capacity = 262144;
    void* buffer = ap_allocate(capacity);
    if (!buffer)
        return Status::fail("Out of memory");
    Status result;
    for (uint64_t offset = 0; offset < size;) {
        size_t take = size_t(size - offset);
        if (take > capacity)
            take = capacity;
        size_t actual = 0;
        result = read(from, offset, buffer, take, actual);
        if (!result || actual == 0) {
            if (result)
                result = Status::fail("Unexpected end while copying " + from);
            break;
        }
        result = write(to, offset, buffer, actual);
        if (!result)
            break;
        offset += actual;
    }
    ap_deallocate(buffer);
    return result;
}
Status removeTree(const Str& path) {
    Info stat = info(path);
    if (!stat.exists)
        return {};
    if (stat.link || !stat.directory)
        return removeFile(path);
    List<Entry> entries;
    AP_TRY(list(path, entries));
    for (const auto& entry : entries) {
        Str error;
        if (!relativePath(entry.name, error))
            return Status::fail(error);
        AP_TRY(removeTree(join(path, entry.name)));
    }
    return removeDirectory(path);
}
Str shellQuote(const Str& value) {
    return Str("'") + value.replace("'", "'\\''") + "'";
}
Str commandQuote(const Str& value) {
    Str out = "\"";
    size_t slashes = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '\\') {
            ++slashes;
            continue;
        }
        if (c == '"') {
            for (size_t j = 0; j < slashes * 2 + 1; ++j)
                out.push('\\');
            out.push('"');
        } else {
            for (size_t j = 0; j < slashes; ++j)
                out.push('\\');
            out.push(c);
        }
        slashes = 0;
    }
    for (size_t j = 0; j < slashes * 2; ++j)
        out.push('\\');
    out.push('"');
    return out;
}
} // namespace ap::os
