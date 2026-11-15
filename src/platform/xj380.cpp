#include "ap/xj380.hpp"
#include "ap/platform.hpp"
#include "ap/xj380_adapter.h"
namespace ap::os {
const ap_xj380_adapter_v1* xjAdapter();
namespace {
char** inheritedEnvironment = nullptr;
char* mutableText(const Str& value) {
    return const_cast<char*>(value.c_str());
}
Status result(uint64_t code, const Str& operation, const Str& path) {
    return code == 0 ? Status{}
                     : Status::fail(operation + ": " + path + " (XAPI " +
                                    number(int64_t(code) < 0 ? uint64_t(-int64_t(code)) : code) + ")");
}
} // namespace
const char* name() {
    return "xj380";
}
void initialize(int, char**, char** envp) {
    inheritedEnvironment = envp;
}
Str environment(const Str& key) {
    if (inheritedEnvironment)
        for (size_t i = 0; inheritedEnvironment[i]; ++i) {
            Str entry = inheritedEnvironment[i];
            size_t equals = entry.find('=');
            if (equals != size_t(-1) && entry.substr(0, equals) == key)
                return entry.substr(equals + 1);
        }
    return {};
}
bool hasEnvironment(const Str& key) {
    if (inheritedEnvironment)
        for (size_t i = 0; inheritedEnvironment[i]; ++i) {
            Str entry = inheritedEnvironment[i];
            size_t pos = entry.find('=');
            if (pos != size_t(-1) && entry.substr(0, pos) == key)
                return true;
        }
    return false;
}
Str home() {
    Str value = environment("HOME");
    if (absolutePath(value))
        return value;
    XUser user{};
    xapi_GetCurrentUser(&user);
    user.name[63] = 0;
    return join("/users", user.name);
}
Str programs(bool machine) {
    return machine ? Str("/apps") : join(home(), "Applications");
}
Str configHome() {
    return join(home(), ".config");
}
Str stateHome() {
    return join(home(), ".local/state");
}
Str desktop(bool machine) {
    const auto* adapter = xjAdapter();
    if (adapter && adapter->desktop_directory) {
        const char* path = adapter->desktop_directory(machine ? 1 : 0);
        if (path && absolutePath(path))
            return path;
    }
    return join(home(), "desktop");
}
Str menu(bool machine) {
    return machine ? Str("/apps") : join(home(), "Applications");
}
Str executable(const char* argv0) {
    Str override = environment("ACCRETION_SELF");
    if (absolutePath(override))
        return override;
    return argv0 ? slash(argv0) : Str();
}
uint64_t nonce() {
    return xapi_GetTimeNano();
}
bool administrator() {
    XUser user{};
    xapi_GetCurrentUser(&user);
    return user.type <= 2;
}
Status requestAccess(const Str& path, bool write) {
    uint64_t access = 8 | 4 | (write ? (2 | 16 | 32) : 1) | 64;
    int64_t rc = xapi_QueryFileAccess(path.c_str(), access);
    if (rc > 0)
        rc = xapi_RequestFileAccess(path.c_str(), access);
    return rc == 0 ? Status{}
                   : Status::fail("XAPI permission request failed: " + path + " (" +
                                  number(rc < 0 ? uint64_t(-rc) : uint64_t(rc)) + ")");
}
Info info(const Str& filename) {
    Str path = slash(filename);
    if (path == "/") {
        Info out;
        out.exists = true;
        out.directory = true;
        out.mode = 0755;
        return out;
    }
    Str folder = parent(path), name = leaf(path);
    if (folder.empty())
        folder = ".";
    int32_t count = 0;
    XDirectoryEntry* entries = nullptr;
    xapi_SearchFile(mutableText(folder), &count, &entries);
    Info out;
    if (count >= 0 && entries) {
        for (int32_t i = 0; i < count; ++i) {
            entries[i].filename[255] = 0;
            if (name == entries[i].filename) {
                out.exists = true;
                out.directory = entries[i].filetype == 1;
                out.link = entries[i].filetype > 1;
                out.size = entries[i].length;
                out.mode = out.directory ? 0755 : 0644;
                break;
            }
        }
        xapi_SearchFile_freem(entries, count);
    }
    return out;
}
Status read(const Str& path, uint64_t offset, void* buffer, size_t size, size_t& actual) {
    actual = 0;
    int64_t rc = int64_t(xapi_ReadFile(mutableText(path), static_cast<char*>(buffer), size, offset));
    if (rc < 0)
        return Status::fail("XAPI read failed: " + path + " (" + number(uint64_t(-rc)) + ")");
    if (uint64_t(rc) > size)
        return Status::fail("XAPI returned an invalid read length");
    actual = size_t(rc);
    return {};
}
Status write(const Str& path, uint64_t offset, const void* buffer, size_t size, bool truncate) {
    AP_TRY(requestAccess(path, true));
    if (truncate) {
        auto stat = info(path);
        if (stat.directory || stat.link)
            return Status::fail("Refusing non-regular XAPI file");
        if (stat.exists)
            AP_TRY(result(xapi_DeleteFile(mutableText(path)), "delete before truncate", path));
        AP_TRY(result(xapi_CreateFile(mutableText(path)), "create", path));
    }
    size_t done = 0;
    while (done < size) {
        int64_t rc = int64_t(xapi_WriteFile(mutableText(path),
                                            const_cast<char*>(static_cast<const char*>(buffer)) + done,
                                            size - done, offset + done));
        if (rc <= 0 || uint64_t(rc) > size - done)
            return Status::fail("XAPI write failed: " + path);
        done += size_t(rc);
    }
    return {};
}
Status mkdir(const Str& path, bool exclusive) {
    if (!exclusive) {
        auto stat = info(path);
        if (stat.exists)
            return stat.directory && !stat.link ? Status{} : Status::fail("Not a directory: " + path);
    }
    AP_TRY(requestAccess(path, true));
    return result(xapi_Mkdir(mutableText(path)), "mkdir", path);
}
Status rename(const Str& from, const Str& to) {
    AP_TRY(requestAccess(from, true));
    AP_TRY(requestAccess(to, true));
    return result(xapi_RenameFile(mutableText(from), mutableText(to)), "rename", from);
}
Status removeFile(const Str& path) {
    if (!info(path).exists)
        return {};
    AP_TRY(requestAccess(path, true));
    return result(xapi_DeleteFile(mutableText(path)), "delete", path);
}
Status removeDirectory(const Str& path) {
    if (!info(path).exists)
        return {};
    AP_TRY(requestAccess(path, true));
    return result(xapi_Rmdir(mutableText(path)), "rmdir", path);
}
Status chmod(const Str&, uint32_t) {
    return {};
}
Status list(const Str& path, List<Entry>& output) {
    int32_t count = 0;
    XDirectoryEntry* entries = nullptr;
    xapi_SearchFile(mutableText(path), &count, &entries);
    if (count < 0)
        return Status::fail("XAPI directory listing failed: " + path);
    for (int32_t i = 0; i < count; ++i) {
        entries[i].filename[255] = 0;
        Str name = entries[i].filename;
        if (name == "." || name == "..")
            continue;
        Info stat;
        stat.exists = true;
        stat.directory = entries[i].filetype == 1;
        stat.link = entries[i].filetype > 1;
        stat.size = entries[i].length;
        output.add({name, stat});
    }
    if (entries)
        xapi_SearchFile_freem(entries, count);
    return {};
}
Status registryRead(const Json&, Json&) {
    return Status::fail("The native XAPI profile has no Windows registry");
}
Status registryWrite(const Json&, const Json&) {
    return Status::fail("The native XAPI profile has no Windows registry");
}
Status shortcut(const Str& output, const Str& executable, const List<Str>& arguments, const Str& working,
                const Str& icon) {
    const auto* adapter = xjAdapter();
    if (!(nativeCapabilities() & AP_XJ380_CAP_SHORTCUTS))
        return Status::fail("XJ380 native shortcut adapter is unavailable");
    List<const char*> args;
    for (const auto& argument : arguments)
        args.add(argument.c_str());
    args.add(nullptr);
    return adapter->write_shortcut(output.c_str(), executable.c_str(), args.begin(), working.c_str(),
                                   icon.c_str()) == 0
               ? Status{}
               : Status::fail("XJ380 native shortcut creation failed");
}
Status launch(const Str& path, const List<Str>& arguments, const Json& env, const Str& working, bool) {
    List<Str> strings;
    if (inheritedEnvironment)
        for (size_t i = 0; inheritedEnvironment[i]; ++i) {
            Str item = inheritedEnvironment[i];
            size_t pos = item.find('=');
            if (pos == size_t(-1) || !env.has(item.substr(0, pos)))
                strings.add(item);
        }
    for (size_t i = 0; i < env.keys.size(); ++i)
        strings.add(env.keys[i] + "=" + env.values[i].text);
    List<char*> variables;
    for (auto& item : strings)
        variables.add(item.data());
    variables.add(nullptr);
    List<char*> args;
    args.add(mutableText(path));
    for (const auto& argument : arguments)
        args.add(mutableText(argument));
    args.add(nullptr);
    if (nativeCapabilities() & AP_XJ380_CAP_LAUNCH) {
        const auto* adapter = xjAdapter();
        int64_t result = adapter->launch(path.c_str(), args.begin(), variables.begin(), working.c_str(), 1);
        return result >= 0 ? Status{} : Status::fail("XJ380 native launch adapter failed");
    }
    AP_TRY(requestAccess(path, false));
    int64_t pid = int64_t(xapi_Fork());
    if (pid < 0)
        return Status::fail("XAPI fork failed");
    if (pid == 0) {
        XUser user{};
        xapi_GetCurrentUser(&user);
        if (user.type != 3 && xapi_LiftPermissions(3) != 0)
            xapi_Exit(126);
        if (xapi_QueryFileAccess(path.c_str(), 1) != 0 && xapi_RequestFileAccess(path.c_str(), 1) != 0)
            xapi_Exit(126);
        uint64_t rc = xapi_Execve(mutableText(path), args.begin(), variables.begin());
        xapi_Exit(int64_t(rc) < 0 ? 127 : rc);
    }
    return {};
}
Status refreshAssociations(bool) {
    return {};
}
void changedEnvironment() {
    const auto* adapter = xjAdapter();
    if (adapter && adapter->changed)
        adapter->changed();
}
void output(const Str& text) {
    xapi_Output(mutableText(text));
}
bool input(Str& line) {
    char buffer[1024]{};
    xapi_Getline(buffer);
    buffer[1023] = 0;
    line = Str(buffer);
    return true;
}
bool interactive() {
    return true;
}
void sleep(unsigned ms) {
    xapi_Sleep(ms);
}
[[noreturn]] void exit(int code) {
    xapi_Exit(uint64_t(code));
}
} // namespace ap::os
