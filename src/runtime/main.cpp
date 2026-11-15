#include "ap/ui.hpp"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
extern "C" volatile uint8_t ap_icon_slot[262144];
namespace {
ap::List<wchar_t> commandWide(const ap::Str& text) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), int(text.size()), nullptr, 0);
    auto* temporary = static_cast<wchar_t*>(ap_allocate((size_t(n) + 1) * sizeof(wchar_t)));
    if (n)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), int(text.size()), temporary, n);
    ap::List<wchar_t> result;
    for (int i = 0; i < n; ++i)
        result.add(temporary[i]);
    result.add(0);
    ap_deallocate(temporary);
    return result;
}
ap::Status relocateUninstaller(const ap::Str& self, const ap::Str& root, const ap::Str& stateRoot, int argc,
                               char** argv, bool& started) {
    started = false;
    ap::Str actual = ap::slash(self).lower(), directory = ap::slash(root).lower();
    if (!actual.starts(directory + "/"))
        return {};
    ap::Str cache = ap::join(stateRoot.empty() ? ap::join(ap::os::stateHome(), "accretion/cache") : stateRoot,
                             "uninstall-" + ap::number(ap::os::nonce()));
    AP_TRY(ap::os::directories(cache));
    ap::Str executable = ap::join(cache, "uninstall.exe");
    AP_TRY(ap::os::copy(self, executable));
    ap::Str command = ap::os::commandQuote(executable);
    for (int i = 1; i < argc; ++i)
        command.append(" " + ap::os::commandQuote(argv[i]));
    command.append(" --relocated --wait-pid " + ap::number(GetCurrentProcessId()));
    auto path = commandWide(executable), line = commandWide(command);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    HANDLE standard[3] = {GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE),
                          GetStdHandle(STD_ERROR_HANDLE)},
           duplicates[3]{};
    for (unsigned i = 0; i < 3; ++i)
        if (standard[i] && standard[i] != INVALID_HANDLE_VALUE)
            DuplicateHandle(GetCurrentProcess(), standard[i], GetCurrentProcess(), &duplicates[i], 0, TRUE,
                            DUPLICATE_SAME_ACCESS);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = duplicates[0];
    startup.hStdOutput = duplicates[1];
    startup.hStdError = duplicates[2];
    PROCESS_INFORMATION process{};
    BOOL ok = CreateProcessW(path.begin(), line.begin(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                             &startup, &process);
    for (HANDLE h : duplicates)
        if (h)
            CloseHandle(h);
    if (!ok)
        return ap::Status::fail("Cannot start temporary uninstaller (Windows " + ap::number(GetLastError()) +
                                ")");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    started = true;
    return {};
}
} // namespace
#endif
namespace ap {
namespace {
struct Arguments {
    Json values = Json::object();
    List<Str> select, deselect, extra;
    bool flag(const Str& name) const {
        return values.boolean(name, false) || values.str(name) == "true";
    }
    Str get(const Str& name, const Str& fallback = {}) const {
        return values.str(name, fallback);
    }
};
Status parseArguments(int argc, char** argv, Arguments& out) {
    const char* booleans[] = {"help",           "silent",  "accept-license", "uninstall", "dry-run",
                              "gui-smoke-test", "recover", "launch",         "no-launch", "relocated"};
    const char* parameters[] = {"package", "dir",      "scope",       "ui",   "state-root",
                                "select",  "deselect", "application", "self", "wait-pid"};
    for (int i = 1; i < argc; ++i) {
        Str arg = argv[i];
        if (arg == "--") {
            for (++i; i < argc; ++i)
                out.extra.add(Str(argv[i]));
            break;
        }
        if (!arg.starts("--"))
            return Status::fail("Unexpected argument: " + arg);
        arg = arg.substr(2);
        size_t equals = arg.find('=');
        Str key = arg.substr(0, equals), value = equals == size_t(-1) ? Str() : arg.substr(equals + 1);
        bool known = false;
        for (const char* name : booleans)
            if (key == name) {
                if (equals != size_t(-1))
                    return Status::fail("Boolean option takes no value: --" + key);
                out.values.set(key, Json::boolean(true));
                known = true;
                break;
            }
        if (known)
            continue;
        for (const char* name : parameters)
            if (key == name) {
                known = true;
                break;
            }
        if (!known)
            return Status::fail("Unknown option: --" + key);
        if (equals == size_t(-1)) {
            if (i + 1 >= argc)
                return Status::fail("Missing value: --" + key);
            value = argv[++i];
        }
        if (key == "select")
            out.select.add(value);
        else if (key == "deselect")
            out.deselect.add(value);
        else
            out.values.set(key, Json(value));
    }
    return {};
}
int finish(const Status& result, const Installer* installer = nullptr) {
    if (!result) {
        os::output("Accretion: " + result.message + "\n");
        return 1;
    }
    if (installer)
        for (const auto& warning : installer->warnings)
            os::output("Notice: " + warning + "\n");
    return 0;
}
void selection(Installer& installer, const List<Str>& ids, bool value) {
    for (const auto& input : ids) {
        bool found = false;
        for (const auto& item : choices(installer)) {
            Str qualified = item.category + ":" + item.id;
            if (input == qualified || input == item.id) {
                choose(installer, item, value);
                found = true;
            }
        }
        if (!found)
            ap_panic(("Unknown option id: " + input).c_str());
    }
}
} // namespace
int run(int argc, char** argv, char** envp) {
    os::initialize(argc, argv, envp);
    Arguments args;
    Status parsed = parseArguments(argc, argv, args);
    if (!parsed)
        return finish(parsed);
    if (args.flag("help")) {
        os::output("Accretion Installer 0.1.0\n\n--ui gui|tui|auto  --dir PATH  --scope "
                   "user|machine\n--select [category:]id  --deselect [category:]id\n--accept-license  "
                   "--silent  --dry-run  --launch  --no-launch\n--uninstall --dir PATH  --recover --dir "
                   "PATH\n--package FILE  --state-root DIRECTORY  --gui-smoke-test\n");
        return 0;
    }
#if defined(_WIN32)
    if (args.values.has("wait-pid")) {
        uint64_t pid = 0;
        if (!unsignedNumber(args.get("wait-pid"), pid) || pid > UINT32_MAX)
            return finish(Status::fail("Invalid parent PID"));
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
        if (process) {
            WaitForSingleObject(process, INFINITE);
            CloseHandle(process);
        }
    }
#endif
    Str self = args.get("self", os::executable(argc ? argv[0] : nullptr));
    Json context = readContext();
    Str root = args.get("dir", context.str("root"));
    if (args.flag("recover")) {
        if (!absolutePath(root))
            return finish(Status::fail("Recovery needs --dir with an absolute path"));
        return finish(Transaction::recover(slash(root)));
    }
    Installer installer;
    bool launcher = context.str("mode") == "launch" && !args.flag("uninstall");
    bool uninstall = args.flag("uninstall") || context.str("mode") == "uninstall";
    Status opened;
    if (launcher || uninstall) {
        if (!absolutePath(root))
            return finish(Status::fail("Installed launcher needs an absolute installation root"));
        opened = installer.installed(root, self);
        installer.options.uninstall = uninstall;
    } else {
        Str packagePath = args.get("package", self);
#if defined(AP_XJ380)
        if (!args.values.has("package") && embeddedPayload.size) {
            uint64_t address = embeddedPayload.address, size = embeddedPayload.size;
            if (size > 256 * 1024 * 1024 || address < 4096 || address > UINT64_MAX - size)
                return finish(Status::fail("Invalid loaded payload descriptor"));
            const auto* memory = reinterpret_cast<const uint8_t*>(address);
            Sha256 hash;
            hash.update(memory, size_t(size));
            uint8_t digest[32];
            hash.finish(digest);
            for (unsigned i = 0; i < 32; ++i)
                if (digest[i] != embeddedPayload.digest[i])
                    return finish(Status::fail("Loaded payload integrity check failed"));
            opened = installer.openMemory(memory, size_t(size), self, embeddedPayload.stubSize);
        } else
#endif
            opened = installer.open(packagePath, self);
        if (!opened && !args.values.has("package")) {
            Str sidecar = join(parent(self), "application.acpkg");
            if (os::info(sidecar).exists)
                opened = installer.open(sidecar, self);
        }
    }
    if (!opened)
        return finish(opened);
    installer.options.stateRoot = args.get("state-root", context.str("stateRoot"));
    if (!installer.options.stateRoot.empty() && !absolutePath(installer.options.stateRoot))
        return finish(Status::fail("--state-root must be absolute"));
    if (!root.empty())
        installer.options.directory = slash(root);
#if defined(_WIN32)
    if (uninstall && !args.flag("relocated") && !args.flag("dry-run") && !args.flag("gui-smoke-test")) {
        bool started = false;
        Status status = relocateUninstaller(self, installer.options.directory, installer.options.stateRoot,
                                            argc, argv, started);
        if (!status)
            return finish(status);
        if (started)
            return 0;
    }
#endif
    Str scope = args.get("scope");
    if (!scope.empty()) {
        if (scope != "user" && scope != "machine")
            return finish(Status::fail("--scope must be user or machine"));
        installer.options.machine = scope == "machine";
        if (root.empty())
            installer.options.directory = installer.defaultDirectory(installer.options.machine);
    }
    installer.options.accepted = args.flag("accept-license");
    installer.options.dryRun = args.flag("dry-run");
    if (args.flag("launch"))
        installer.options.launch = true;
    if (args.flag("no-launch"))
        installer.options.launch = false;
    if (!launcher && !uninstall) {
        Status adopted = installer.adoptExisting();
        if (!adopted)
            return finish(adopted);
    }
    installer.options.extraArguments = args.extra;
    for (const auto& argument : context.get("arguments").values)
        installer.options.extraArguments.add(argument.text);
    selection(installer, args.select, true);
    selection(installer, args.deselect, false);
    if (launcher) {
        Str app =
            args.get("application",
                     context.str("application",
                                 installer.manifest.get("completion")
                                     .str("application", installer.manifest.get("applications").keys[0])));
        return finish(installer.launch(app), &installer);
    }
    if (installer.options.dryRun) {
        if (installer.options.uninstall)
            return finish(installer.uninstall(), &installer);
        installer.options.accepted = true;
        return finish(installer.execute(), &installer);
    }
    if (args.flag("silent")) {
        Status result = uninstall ? installer.uninstall() : installer.execute();
        if (result && !uninstall && installer.options.launch)
            result =
                installer.launch(installer.manifest.get("completion")
                                     .str("application", installer.manifest.get("applications").keys[0]));
        return finish(result, &installer);
    }
    Str mode = args.get("ui", installer.manifest.get("ui").str("default",
#if defined(AP_DEFAULT_TUI)
                                                               "tui"
#else
                                                               "gui"
#endif
                                                               ));
    if (args.flag("gui-smoke-test"))
        mode = "gui";
    if (mode != "gui" && mode != "tui" && mode != "auto")
        return finish(Status::fail("--ui must be gui, tui or auto"));
    const auto& modes = installer.manifest.get("ui").get("modes").values;
    if (mode != "auto" && !modes.empty()) {
        bool enabled = false;
        for (const auto& value : modes)
            if (value.text == mode)
                enabled = true;
        if (!enabled)
            return finish(Status::fail("Requested UI is disabled by this package"));
    }
    if (mode == "gui" || mode == "auto") {
        Status result = gui(installer, args.flag("gui-smoke-test"));
        if (mode != "auto" || result ||
            result.message.find("unavailable") == size_t(-1) && !result.message.starts("No graphical"))
            return finish(result, &installer);
    }
    Status result = tui(installer);
    if (result && !uninstall && installer.options.launch)
        result = installer.launch(installer.manifest.get("completion")
                                      .str("application", installer.manifest.get("applications").keys[0]));
    return finish(result, &installer);
}
} // namespace ap

#if defined(_WIN32)
namespace {
int windowsMain() {
    uint8_t resourceAnchor = ap_icon_slot[0];
    (void)resourceAnchor;
    int argc = 0;
    LPWSTR* wideArgs = CommandLineToArgvW(GetCommandLineW(), &argc);
    ap::List<ap::Str> values;
    for (int i = 0; i < argc; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, nullptr, 0, nullptr, nullptr);
        ap::Str value;
        value.resize(size_t(size));
        WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, value.data(), size, nullptr, nullptr);
        value.resize(value.size() - 1);
        values.add(ap::move(value));
    }
    LocalFree(wideArgs);
    bool console = false;
    for (const auto& value : values)
        if (value == "tui" || value == "--ui=tui")
            console = true;
#if defined(AP_DEFAULT_TUI)
    console = true;
#endif
    if (console) {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!handle || handle == INVALID_HANDLE_VALUE) {
            if (!AttachConsole(ATTACH_PARENT_PROCESS))
                AllocConsole();
        }
    }
    ap::List<char*> args;
    for (auto& value : values)
        args.add(value.data());
    args.add(nullptr);
    return ap::run(argc, args.begin(), nullptr);
}
} // namespace
#if defined(AP_DEFAULT_TUI)
int main() {
    return windowsMain();
}
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return windowsMain();
}
#endif
#elif defined(AP_XJ380)
extern "C" int ap_xj380_main(int argc, char** argv, char** envp) {
    return ap::run(argc, argv, envp);
}
extern "C" void* memcpy(void* to, const void* from, size_t bytes) {
    return ap_memcpy(to, from, bytes);
}
extern "C" void* memset(void* to, int value, size_t bytes) {
    return ap_memset(to, value, bytes);
}
extern "C" void* memmove(void* to, const void* from, size_t bytes) {
    return ap_memmove(to, from, bytes);
}
#else
extern char** environ;
int main(int argc, char** argv) {
    return ap::run(argc, argv, environ);
}
#endif
