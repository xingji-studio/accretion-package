#include "ap/platform.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
namespace ap::os {
namespace {
List<wchar_t> wide(const Str& text) {
    int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), int(text.size()), nullptr, 0);
    List<wchar_t> out;
    out.reserve(size_t(count) + 1);
    if (count) {
        wchar_t* buffer = static_cast<wchar_t*>(ap_allocate((size_t(count) + 1) * sizeof(wchar_t)));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), int(text.size()), buffer, count);
        for (int i = 0; i < count; ++i)
            out.add(buffer[i]);
        ap_deallocate(buffer);
    }
    out.add(0);
    return out;
}
Str narrow(const wchar_t* text, int count = -1) {
    if (!text)
        return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, count, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};
    Str out;
    out.resize(size_t(bytes));
    WideCharToMultiByte(CP_UTF8, 0, text, count, out.data(), bytes, nullptr, nullptr);
    if (count < 0)
        out.resize(out.size() - 1);
    return out;
}
Status failure(const Str& operation, const Str& path, DWORD code = GetLastError()) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    Str detail = message ? narrow(message) : number(code);
    if (message)
        LocalFree(message);
    return Status::fail(operation + ": " + path + " (" + detail.trim() + ")");
}
Str folder(int id) {
    wchar_t text[MAX_PATH];
    return SUCCEEDED(SHGetFolderPathW(nullptr, id, nullptr, SHGFP_TYPE_CURRENT, text)) ? narrow(text) : Str();
}
List<wchar_t> pathWide(const Str& text) {
    Str path = text.replace("/", "\\");
    if (path.size() > 240 && path.size() > 2 && path[1] == ':' && !path.starts("\\\\?\\"))
        path = "\\\\?\\" + path;
    return wide(path);
}
HKEY hive(const Json& descriptor) {
    return descriptor.str("hive") == "HKLM" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}
REGSAM view(const Json& descriptor) {
    return descriptor.integer("view", 64) == 32 ? KEY_WOW64_32KEY : KEY_WOW64_64KEY;
}
} // namespace
const char* name() {
    return "windows";
}
void initialize(int, char**, char**) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    if (!GetStdHandle(STD_OUTPUT_HANDLE) || GetStdHandle(STD_OUTPUT_HANDLE) == INVALID_HANDLE_VALUE)
        AttachConsole(ATTACH_PARENT_PROCESS);
}
Str environment(const Str& key) {
    auto name = wide(key);
    DWORD count = GetEnvironmentVariableW(name.begin(), nullptr, 0);
    if (!count)
        return {};
    auto* buffer = static_cast<wchar_t*>(ap_allocate(size_t(count) * sizeof(wchar_t)));
    DWORD actual = GetEnvironmentVariableW(name.begin(), buffer, count);
    Str out = actual < count ? narrow(buffer, int(actual)) : Str();
    ap_deallocate(buffer);
    return out;
}
bool hasEnvironment(const Str& key) {
    auto name = wide(key);
    SetLastError(ERROR_SUCCESS);
    DWORD count = GetEnvironmentVariableW(name.begin(), nullptr, 0);
    return count != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
}
Str home() {
    return folder(CSIDL_PROFILE);
}
Str programs(bool machine) {
    return machine ? environment("ProgramFiles") : join(folder(CSIDL_LOCAL_APPDATA), "Programs");
}
Str desktop(bool machine) {
    return folder(machine ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY);
}
Str menu(bool machine) {
    return folder(machine ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS);
}
Str configHome() {
    return folder(CSIDL_APPDATA);
}
Str stateHome() {
    return folder(CSIDL_LOCAL_APPDATA);
}
Str executable(const char*) {
    wchar_t buffer[32768];
    DWORD n = GetModuleFileNameW(nullptr, buffer, 32768);
    return n && n < 32768 ? narrow(buffer, int(n)) : Str();
}
uint64_t nonce() {
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    return uint64_t(time.QuadPart) ^ GetCurrentProcessId();
}
bool administrator() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID sid = nullptr;
    BOOL result = FALSE;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0,
                                 0, 0, 0, &sid)) {
        CheckTokenMembership(nullptr, sid, &result);
        FreeSid(sid);
    }
    return result != FALSE;
}
Info info(const Str& path) {
    if (path.empty())
        return {};
    auto w = pathWide(path);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(w.begin(), GetFileExInfoStandard, &data))
        return {};
    Info out;
    out.exists = true;
    out.directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out.link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    out.size = (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    out.mode = out.directory ? 0755 : 0644;
    return out;
}
Status read(const Str& path, uint64_t offset, void* buffer, size_t size, size_t& actual) {
    actual = 0;
    auto w = pathWide(path);
    HANDLE file = CreateFileW(w.begin(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return failure("open", path);
    BY_HANDLE_FILE_INFORMATION stat{};
    if (!GetFileInformationByHandle(file, &stat) ||
        (stat.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(file);
        return Status::fail("Not a regular file: " + path);
    }
    LARGE_INTEGER pos;
    pos.QuadPart = LONGLONG(offset);
    DWORD n = 0;
    BOOL ok =
        SetFilePointerEx(file, pos, nullptr, FILE_BEGIN) && ReadFile(file, buffer, DWORD(size), &n, nullptr);
    DWORD error = GetLastError();
    CloseHandle(file);
    if (!ok)
        return failure("read", path, error);
    actual = n;
    return {};
}
Status write(const Str& path, uint64_t offset, const void* buffer, size_t size, bool truncate) {
    AP_TRY(safeAncestors(parent(path)));
    auto w = pathWide(path);
    HANDLE file = CreateFileW(w.begin(), GENERIC_WRITE, 0, nullptr, truncate ? CREATE_ALWAYS : OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return failure("open for writing", path);
    BY_HANDLE_FILE_INFORMATION stat{};
    if (!GetFileInformationByHandle(file, &stat) ||
        (stat.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(file);
        return Status::fail("Refusing reparse point: " + path);
    }
    LARGE_INTEGER pos;
    pos.QuadPart = LONGLONG(offset);
    BOOL ok = SetFilePointerEx(file, pos, nullptr, FILE_BEGIN);
    size_t done = 0;
    while (ok && done < size) {
        DWORD n = 0;
        ok = WriteFile(file, static_cast<const char*>(buffer) + done, DWORD(size - done), &n, nullptr);
        if (!n) {
            ok = FALSE;
            break;
        }
        done += n;
    }
    if (ok)
        ok = FlushFileBuffers(file);
    DWORD error = GetLastError();
    CloseHandle(file);
    return ok ? Status{} : failure("write", path, error);
}
Status mkdir(const Str& path, bool exclusive) {
    auto w = pathWide(path);
    if (CreateDirectoryW(w.begin(), nullptr))
        return {};
    if (!exclusive && GetLastError() == ERROR_ALREADY_EXISTS) {
        Info s = info(path);
        if (s.directory && !s.link)
            return {};
    }
    return failure("mkdir", path);
}
Status rename(const Str& from, const Str& to) {
    AP_TRY(safeAncestors(parent(from)));
    AP_TRY(safeAncestors(parent(to)));
    auto a = pathWide(from), b = pathWide(to);
    return MoveFileExW(a.begin(), b.begin(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
               ? Status{}
               : failure("rename", from);
}
Status removeFile(const Str& path) {
    auto w = pathWide(path);
    if (DeleteFileW(w.begin()))
        return {};
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return {};
    return failure("delete", path, error);
}
Status removeDirectory(const Str& path) {
    auto w = pathWide(path);
    if (RemoveDirectoryW(w.begin()))
        return {};
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return {};
    return failure("rmdir", path, error);
}
Status chmod(const Str&, uint32_t) {
    return {};
}
Status list(const Str& path, List<Entry>& result) {
    if (info(path).link)
        return Status::fail("Refusing reparse point directory");
    auto pattern = pathWide(join(path, "*"));
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern.begin(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? Status{} : failure("list", path);
    do {
        Str n = narrow(data.cFileName);
        if (n == "." || n == "..")
            continue;
        result.add({n, info(join(path, n))});
    } while (FindNextFileW(find, &data));
    DWORD error = GetLastError();
    FindClose(find);
    return error == ERROR_NO_MORE_FILES ? Status{} : failure("list", path, error);
}
Status requestAccess(const Str& path, bool) {
    return safeAncestors(path);
}
uint64_t nativeCapabilities() {
    return 0;
}
Status nativeRead(const Json&, Json&) {
    return Status::fail("Native XJ380 adapter unavailable on this platform");
}
Status nativeWrite(const Json&, const Json&) {
    return Status::fail("Native XJ380 adapter unavailable on this platform");
}
Str shortcutExtension() {
    return ".lnk";
}
Status registryRead(const Json& d, Json& value) {
    auto key = wide(d.str("key").replace("/", "\\")), name = wide(d.str("name"));
    HKEY handle;
    LONG status = RegOpenKeyExW(hive(d), key.begin(), 0, KEY_QUERY_VALUE | view(d), &handle);
    value = Json::object();
    value.set("exists", Json::boolean(false));
    if (status == ERROR_FILE_NOT_FOUND)
        return {};
    if (status != ERROR_SUCCESS)
        return failure("open registry", d.str("key"), status);
    DWORD type = 0, size = 0;
    status = RegQueryValueExW(handle, name.begin(), nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(handle);
        return {};
    }
    if (status != ERROR_SUCCESS || size > 1024 * 1024 ||
        (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_DWORD)) {
        RegCloseKey(handle);
        return Status::fail("Unsupported existing registry value: " + d.str("key"));
    }
    auto* data = static_cast<uint8_t*>(ap_allocate(size + 4));
    ap_memset(data, 0, size + 4);
    status = RegQueryValueExW(handle, name.begin(), nullptr, &type, data, &size);
    RegCloseKey(handle);
    if (status == ERROR_SUCCESS) {
        value.set("exists", Json::boolean(true));
        value.set("type", Json(type == REG_DWORD       ? "dword"
                               : type == REG_EXPAND_SZ ? "expandString"
                                                       : "string"));
        if (type == REG_DWORD && size == 4)
            value.set("value", Json::integer(*reinterpret_cast<DWORD*>(data)));
        else if (type != REG_DWORD)
            value.set("value", Json(narrow(reinterpret_cast<wchar_t*>(data))));
        else
            status = ERROR_INVALID_DATA;
    }
    ap_deallocate(data);
    return status == ERROR_SUCCESS ? Status{} : failure("read registry", d.str("key"), status);
}
Status registryWrite(const Json& d, const Json& value) {
    auto key = wide(d.str("key").replace("/", "\\")), name = wide(d.str("name"));
    HKEY handle;
    LONG status;
    if (!value.boolean("exists", false)) {
        status = RegOpenKeyExW(hive(d), key.begin(), 0, KEY_SET_VALUE | view(d), &handle);
        if (status == ERROR_FILE_NOT_FOUND)
            return {};
        if (status != ERROR_SUCCESS)
            return failure("open registry", d.str("key"), status);
        status = RegDeleteValueW(handle, name.begin());
        RegCloseKey(handle);
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
        return status == ERROR_SUCCESS ? Status{} : failure("delete registry value", d.str("key"), status);
    }
    status = RegCreateKeyExW(hive(d), key.begin(), 0, nullptr, 0, KEY_SET_VALUE | view(d), nullptr, &handle,
                             nullptr);
    if (status != ERROR_SUCCESS)
        return failure("create registry key", d.str("key"), status);
    Str type = value.str("type", "string");
    if (type == "dword") {
        DWORD data = DWORD(value.integer("value", 0));
        status = RegSetValueExW(handle, name.begin(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), 4);
    } else {
        auto data = wide(value.str("value"));
        status =
            RegSetValueExW(handle, name.begin(), 0, type == "expandString" ? REG_EXPAND_SZ : REG_SZ,
                           reinterpret_cast<const BYTE*>(data.begin()), DWORD(data.size() * sizeof(wchar_t)));
    }
    RegCloseKey(handle);
    return status == ERROR_SUCCESS ? Status{} : failure("write registry value", d.str("key"), status);
}
Status shortcut(const Str& output, const Str& executable, const List<Str>& arguments, const Str& working,
                const Str& icon) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                      reinterpret_cast<void**>(&link));
    if (SUCCEEDED(result)) {
        auto target = pathWide(executable), dir = pathWide(working), image = pathWide(icon);
        Str args;
        for (const auto& a : arguments) {
            if (!args.empty())
                args.push(' ');
            args.append(commandQuote(a));
        }
        auto params = wide(args);
        result = link->SetPath(target.begin());
        if (SUCCEEDED(result))
            result = link->SetArguments(params.begin());
        if (SUCCEEDED(result) && !working.empty())
            result = link->SetWorkingDirectory(dir.begin());
        if (SUCCEEDED(result) && !icon.empty())
            result = link->SetIconLocation(image.begin(), 0);
        IPersistFile* file = nullptr;
        if (SUCCEEDED(result))
            result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
        if (SUCCEEDED(result)) {
            auto path = pathWide(output);
            result = file->Save(path.begin(), TRUE);
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return SUCCEEDED(result) ? Status{}
                             : Status::fail("Cannot create shortcut: " + output + " (HRESULT " +
                                            number(uint32_t(result)) + ")");
}
Status launch(const Str& path, const List<Str>& arguments, const Json& env, const Str& working,
              bool machine) {
    if (machine && administrator())
        return Status::fail("Launch the application from your normal user session");
    Str command = commandQuote(path);
    for (const auto& arg : arguments)
        command.append(" " + commandQuote(arg));
    auto line = wide(command), exe = pathWide(path), dir = pathWide(working);
    List<Str> entries;
    LPWCH raw = GetEnvironmentStringsW();
    if (raw) {
        for (const wchar_t* p = raw; *p;) {
            Str value = narrow(p);
            entries.add(value);
            while (*p)
                ++p;
            ++p;
        }
        FreeEnvironmentStringsW(raw);
    }
    for (size_t i = 0; i < env.keys.size(); ++i) {
        bool replaced = false;
        for (auto& entry : entries) {
            size_t at = entry.find('=');
            if (at != size_t(-1) && entry.substr(0, at).lower() == env.keys[i].lower()) {
                entry = env.keys[i] + "=" + env.values[i].text;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            entries.add(env.keys[i] + "=" + env.values[i].text);
    }
    List<wchar_t> block;
    for (const auto& entry : entries) {
        auto value = wide(entry);
        for (wchar_t c : value)
            block.add(c);
    }
    block.add(0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    BOOL ok = CreateProcessW(exe.begin(), line.begin(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
                             block.begin(), working.empty() ? nullptr : dir.begin(), &startup, &process);
    if (!ok)
        return failure("launch", path);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {};
}
Status refreshAssociations(bool) {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return {};
}
void changedEnvironment() {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 2000, &result);
}
void output(const Str& text) {
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (handle && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        auto value = wide(text);
        DWORD written = 0;
        WriteConsoleW(handle, value.begin(), DWORD(value.size() - 1), &written, nullptr);
    } else if (handle && handle != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(handle, text.c_str(), DWORD(text.size()), &written, nullptr);
    } else {
        auto value = wide(text);
        OutputDebugStringW(value.begin());
    }
}
bool input(Str& line) {
    line.clear();
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(handle, &mode)) {
        wchar_t value[4096];
        DWORD read = 0;
        if (!ReadConsoleW(handle, value, 4095, &read, nullptr) || !read)
            return false;
        line = narrow(value, int(read)).trim();
        return true;
    }
    char c;
    DWORD read;
    while (line.size() < 4096) {
        if (!ReadFile(handle, &c, 1, &read, nullptr) || !read)
            return !line.empty();
        if (c == '\n')
            return true;
        if (c != '\r')
            line.push(c);
    }
    return false;
}
bool interactive() {
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}
void sleep(unsigned milliseconds) {
    Sleep(milliseconds);
}
[[noreturn]] void exit(int code) {
    ExitProcess(UINT(code));
}
} // namespace ap::os
