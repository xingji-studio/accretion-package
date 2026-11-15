#include "ap/package.hpp"
#include "epf.hpp"
#include "lz4.h"
#include "pe.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif
namespace fs = std::filesystem;
using namespace ap;
namespace {
struct Arguments {
    Str command;
    std::map<std::string, std::string> flags;
    bool has(const char* key) const {
        return flags.count(key) != 0;
    }
    Str get(const char* key, const Str& fallback = {}) const {
        auto at = flags.find(key);
        return at == flags.end() ? fallback : Str(at->second.c_str());
    }
};
Arguments arguments(int argc, char** argv) {
    Arguments out;
    if (argc > 1)
        out.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0)
            throw std::runtime_error("Unexpected argument: " + arg);
        size_t equals = arg.find('=');
        if (equals != std::string::npos)
            out.flags[arg.substr(2, equals - 2)] = arg.substr(equals + 1);
        else if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
            out.flags[arg.substr(2)] = argv[++i];
        else
            out.flags[arg.substr(2)] = "true";
    }
    for (const auto& flag : out.flags) {
        bool known = false;
        const char* allowed[] = {"project", "target",      "ui",        "format", "output",
                                 "stub",    "compression", "input",     "main",   "name",
                                 "id",      "version",     "publisher", "force",  "package"};
        for (const char* name : allowed)
            if (flag.first == name)
                known = true;
        if (!known)
            throw std::runtime_error("Unknown option: --" + flag.first);
    }
    return out;
}
std::string bytes(const Str& value) {
    return {value.c_str(), value.size()};
}
Str string(const fs::path& value) {
    auto text = value.u8string();
    return Str(text.data(), text.size());
}
fs::path path(const Str& value) {
    return fs::u8path(bytes(value));
}
void require(Status status) {
    if (!status)
        throw std::runtime_error(bytes(status.message));
}
Str read(const fs::path& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open " + filename.u8string());
    std::string value((std::istreambuf_iterator<char>(file)), {});
    return Str(value.data(), value.size());
}
void write(const fs::path& filename, const Str& text) {
    if (!filename.parent_path().empty())
        fs::create_directories(filename.parent_path());
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    file.write(text.c_str(), std::streamsize(text.size()));
    if (!file)
        throw std::runtime_error("Cannot write " + filename.u8string());
}
Json load(const fs::path& filename) {
    Json result;
    require(parseJson(read(filename), result));
    require(validateManifest(result));
    return result;
}
bool glob(const Str& pattern, const Str& name, size_t p = 0, size_t n = 0) {
    while (p < pattern.size()) {
        char c = pattern[p++];
        if (c == '*') {
            while (p < pattern.size() && pattern[p] == '*')
                ++p;
            if (p == pattern.size())
                return true;
            for (size_t i = n; i <= name.size(); ++i)
                if (glob(pattern, name, p, i))
                    return true;
            return false;
        }
        if (n == name.size() || (c != '?' && c != name[n]))
            return false;
        ++n;
    }
    return n == name.size();
}
bool matches(const Json& patterns, const Str& filename) {
    for (const auto& pattern : patterns.values)
        if (glob(pattern.text, filename))
            return true;
    return false;
}
bool windowsPath(const Str& filename) {
    const char* invalid = "<>:\"\\|?*";
    for (const Str& part : split(filename, '/')) {
        if (part.ends(".") || part.ends(" ") || part.size() > 240)
            return false;
        for (size_t i = 0; i < part.size(); ++i)
            for (size_t j = 0; invalid[j]; ++j)
                if (part[i] == invalid[j])
                    return false;
        Str stem = part.substr(0, part.find('.')).lower();
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "clock$")
            return false;
        for (unsigned i = 1; i <= 9; ++i)
            if (stem == "com" + number(i) || stem == "lpt" + number(i))
                return false;
    }
    return true;
}
struct Input {
    fs::path source;
    Str destination, component, policy;
    uint32_t mode;
};
void collect(std::vector<Input>& out, const fs::path& folder, const Str& destination, const Str& component,
             const Json& config, const Str& target) {
    if (!fs::is_directory(folder))
        throw std::runtime_error("Input folder does not exist: " + folder.u8string());
    fs::path root = fs::canonical(folder);
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        auto status = entry.symlink_status();
        if (fs::is_directory(status))
            continue;
        if (fs::is_symlink(status)) {
            auto canonical = fs::canonical(entry.path());
            auto rel = canonical.lexically_relative(root);
            if (rel.empty() || *rel.begin() == ".." || !fs::is_regular_file(canonical))
                throw std::runtime_error("Link escapes input or points to a directory: " +
                                         entry.path().u8string());
            if (!config.boolean("dereferenceInternalLinks", true))
                throw std::runtime_error("Symbolic links are disabled: " + entry.path().u8string());
        } else if (!fs::is_regular_file(status))
            throw std::runtime_error("Only regular files are supported: " + entry.path().u8string());
        Str relative = slash(string(entry.path().lexically_relative(root)));
        if (matches(config.get("exclude"), relative))
            continue;
        if (config.get("include").kind == Json::Array && !matches(config.get("include"), relative))
            continue;
        Str dest = join(destination, relative), error;
        if (!relativePath(dest, error))
            throw std::runtime_error(bytes(error));
        if (target == "windows-x64" && !windowsPath(dest))
            throw std::runtime_error("Unsupported Windows filename: " + bytes(dest));
        Str policy = matches(config.get("preserve"), dest) ? "preserve" : "replace";
        auto perms = fs::status(entry.path()).permissions();
        out.push_back({entry.path(), dest, component, policy, uint32_t(perms) & 0777});
    }
}
void appendFile(std::ostream& output, const fs::path& filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + filename.u8string());
    char buffer[65536];
    while (input) {
        input.read(buffer, sizeof(buffer));
        output.write(buffer, input.gcount());
    }
    if (!input.eof() || !output)
        throw std::runtime_error("I/O error copying " + filename.u8string());
}
void digestBytes(const Str& hex, uint8_t* output) {
    for (unsigned i = 0; i < 32; ++i) {
        auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        output[i] = uint8_t(digit(hex[i * 2]) * 16 + digit(hex[i * 2 + 1]));
    }
}
Str platformFor(const Str& target) {
    if (target == "windows-x64")
        return "windows";
    if (target == "linux-x64")
        return "linux";
    if (target == "xj380-x64")
        return "xj380";
    throw std::runtime_error("Target must be windows-x64, linux-x64 or xj380-x64");
}
void resource(Json& manifest, const fs::path& base, const char* key, std::vector<Input>& inputs) {
    Json* branding = manifest.find("branding");
    if (!branding)
        return;
    Str original = branding->str(key);
    if (original.empty())
        return;
    fs::path file = base / path(original);
    if (!fs::is_regular_file(file))
        throw std::runtime_error("Missing branding resource: " + file.u8string());
    Str destination = "_ap_assets/" + Str(key) + string(file.extension());
    inputs.push_back({file, destination, "", "replace", 0644});
    branding->set(key, Json(destination));
}
void licenses(Json& manifest, const fs::path& base) {
    Json* branding = manifest.find("branding");
    if (!branding)
        return;
    Json texts = Json::object();
    const auto& paths = branding->get("licenses");
    for (size_t i = 0; i < paths.keys.size(); ++i) {
        Str text = read(base / path(paths.values[i].text));
        if (!utf8(text) || text.size() > 1024 * 1024 || text.find('\0') != size_t(-1))
            throw std::runtime_error("License must be UTF-8 and at most 1 MiB");
        texts.set(paths.keys[i], Json(text));
    }
    branding->set("licenseTexts", move(texts));
    branding->erase("licenses");
}
void build(const Arguments& args, const fs::path& tool) {
    fs::path project = fs::absolute(path(args.get("project", "project.json")));
    Json manifest = load(project);
    Str target = args.get("target", Str(os::name()) + "-x64"), platform = platformFor(target),
        ui = args.get("ui", manifest.get("ui").str("default", "gui"));
    if (ui != "gui" && ui != "tui")
        throw std::runtime_error("--ui must be gui or tui");
    if (!manifest.get("ui").get("modes").values.empty()) {
        bool enabled = false;
        for (const auto& mode : manifest.get("ui").get("modes").values)
            if (mode.text == ui)
                enabled = true;
        if (!enabled)
            throw std::runtime_error("Requested UI is disabled by ui.modes");
    }
    Str format = args.get("format", "installer");
    if (format != "installer" && format != "archive" && format != "bundle" && format != "epf")
        throw std::runtime_error("Supported formats: installer, archive, bundle, epf");
    if (format == "epf" && target != "xj380-x64")
        throw std::runtime_error("EPF output requires xj380-x64");
    Str suffix = format == "epf"           ? Str(".epf")
                 : target == "windows-x64" ? ".exe"
                 : target == "xj380-x64"   ? ".elf"
                                           : ".run";
    Str name =
        manifest.get("app").str("id") + "-" + manifest.get("app").str("version") + "-" + target + "-" + ui;
    fs::path output = path(args.get("output", join("out", name + (format == "archive"  ? ".acpkg"
                                                                  : format == "bundle" ? ""
                                                                                       : suffix))));
    if (!output.parent_path().empty())
        fs::create_directories(output.parent_path());
    fs::path temp = output;
    temp += ".payload.tmp";
    fs::path archive = output;
    archive += ".archive.tmp";
    struct Cleanup {
        fs::path a, b;
        ~Cleanup() {
            std::error_code error;
            fs::remove(a, error);
            fs::remove(b, error);
        }
    } cleanup{temp, archive};
    fs::path stub = path(args.get("stub", string(tool.parent_path() / "ap-stubs" /
                                                 path("ap-setup-" + target + "-" + ui +
                                                      (target == "windows-x64" ? ".exe"
                                                       : target == "xj380-x64" ? ".elf"
                                                                               : "")))));
    Str input = manifest.get("inputs").str(target);
    if (input.empty())
        throw std::runtime_error("No input folder configured for " + bytes(target));
    Str installerIconOriginal = manifest.get("branding").str("installerIcon");
    std::vector<Input> files;
    collect(files, project.parent_path() / path(input), "", "", manifest.get("files"), target);
    for (const auto& component : manifest.get("components").values) {
        Str source = component.get("inputs").str(target);
        if (!source.empty())
            collect(files, project.parent_path() / path(source), component.str("destination"),
                    component.str("id"), manifest.get("files"), target);
    }
    licenses(manifest, project.parent_path());
    resource(manifest, project.parent_path(), "applicationIcon", files);
    resource(manifest, project.parent_path(), "installerIcon", files);
    for (const auto& shortcut : manifest.get("shortcuts").values) {
        Str icon = shortcut.str("icon");
        if (!icon.empty()) {
            Str error;
            if (!relativePath(icon, error))
                throw std::runtime_error("Shortcut icon must be a relative payload path");
        }
    }
    if (format == "epf") {
        if (!fs::is_regular_file(stub))
            throw std::runtime_error("Missing XJ380 ELF stub");
        files.push_back({stub, "_ap_assets/runtime.elf", "", "replace", 0644});
        Json runtime = Json::object();
        runtime.set("stub", Json("_ap_assets/runtime.elf"));
        manifest.set("runtime", move(runtime));
    }
    std::sort(files.begin(), files.end(),
              [](const Input& a, const Input& b) { return bytes(a.destination) < bytes(b.destination); });
    std::map<std::string, bool> seen;
    for (const auto& file : files) {
        std::string key = bytes(target == "windows-x64" ? file.destination.lower() : file.destination);
        if (seen[key])
            throw std::runtime_error("Duplicate destination: " + key);
        seen[key] = true;
    }
    Json index = Json::array();
    std::ofstream payload(temp, std::ios::binary | std::ios::trunc);
    if (!payload)
        throw std::runtime_error("Cannot create payload staging file");
    uint64_t payloadBytes = 0;
    std::vector<char> raw(ChunkSize), compressed(size_t(LZ4_compressBound(ChunkSize)));
    Str compression = args.get("compression", manifest.str("compression", "lz4"));
    if (compression != "lz4" && compression != "store")
        throw std::runtime_error("--compression must be lz4 or store");
    bool compress = compression == "lz4";
    for (const auto& inputFile : files) {
        std::ifstream file(inputFile.source, std::ios::binary);
        if (!file)
            throw std::runtime_error("Cannot read " + inputFile.source.u8string());
        uint64_t expected = fs::file_size(inputFile.source), size = 0;
        Sha256 hash;
        Json chunks = Json::array();
        while (file) {
            file.read(raw.data(), raw.size());
            size_t n = size_t(file.gcount());
            if (!n)
                break;
            hash.update(raw.data(), n);
            int compressedSize =
                compress ? LZ4_compress_default(raw.data(), compressed.data(), int(n), int(compressed.size()))
                         : 0;
            bool useCompressed = compressedSize > 0 && size_t(compressedSize) < n;
            size_t stored = useCompressed ? size_t(compressedSize) : n;
            Json chunk = Json::object();
            chunk.set("offset", Json::integer(payloadBytes));
            chunk.set("stored", Json::integer(stored));
            chunk.set("size", Json::integer(n));
            chunk.set("codec", Json(useCompressed ? "lz4" : "store"));
            chunks.add(move(chunk));
            payload.write(useCompressed ? compressed.data() : raw.data(), std::streamsize(stored));
            size += n;
            payloadBytes += stored;
        }
        if (!file.eof() || size != expected)
            throw std::runtime_error("Source changed while packaging: " + inputFile.source.u8string());
        Json item = Json::object();
        item.set("path", Json(inputFile.destination));
        item.set("size", Json::integer(size));
        item.set("sha256", Json(hash.hex()));
        uint32_t mode = inputFile.mode;
        for (const auto& app : manifest.get("applications").values)
            if (app.str(platform) == inputFile.destination)
                mode |= 0111;
        item.set("mode", Json::integer(mode));
        item.set("chunks", move(chunks));
        item.set("policy", Json(inputFile.policy));
        if (!inputFile.component.empty())
            item.set("component", Json(inputFile.component));
        index.add(move(item));
    }
    payload.close();
    if (!payload)
        throw std::runtime_error("Cannot finish payload staging file");
    manifest.erase("inputs");
    if (Json* components = manifest.find("components"))
        for (auto& component : components->values)
            component.erase("inputs");
    Json* uiConfig = manifest.find("ui");
    if (!uiConfig) {
        manifest.set("ui", Json::object());
        uiConfig = manifest.find("ui");
    }
    uiConfig->set("default", Json(ui));
    manifest.set("target", Json(target));
    manifest.set("files", move(index));
    require(validateManifest(manifest, true));
    Str text = manifest.dump();
    if (text.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Manifest exceeds 16 MiB");
    uint8_t header[64]{};
    ap_memcpy(header, "ACPKG001", 8);
    write32(header + 8, 1);
    write32(header + 12, 64);
    write64(header + 16, text.size());
    write64(header + 24, payloadBytes);
    digestBytes(sha256(text.c_str(), text.size()), header + 32);
    {
        std::ofstream out(archive, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<char*>(header), 64);
        out.write(text.c_str(), std::streamsize(text.size()));
        appendFile(out, temp);
        if (!out)
            throw std::runtime_error("Cannot finish package");
    }

    if (format == "epf") {
        Str result = xj380Epf(read(stub), read(archive));
        write(output, result);
    } else if (format == "archive") {
        fs::copy_file(archive, output, fs::copy_options::overwrite_existing);
    } else if (format == "bundle") {
        if (!fs::is_regular_file(stub))
            throw std::runtime_error("Missing runtime stub: " + stub.u8string());
        fs::create_directories(output);
        if (target == "windows-x64") {
            Str icon;
            if (!installerIconOriginal.empty())
                icon = read(project.parent_path() / path(installerIconOriginal));
            write(output / path("setup" + suffix), windowsResources(read(stub), icon));
        } else {
            fs::copy_file(stub, output / path("setup" + suffix), fs::copy_options::overwrite_existing);
            fs::permissions(output / path("setup" + suffix),
                            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add);
        }
        fs::copy_file(archive, output / "application.acpkg", fs::copy_options::overwrite_existing);
        write(output / "RUN.txt", "Run setup" + suffix + " --package application.acpkg\n");
    } else {
        if (!fs::is_regular_file(stub))
            throw std::runtime_error("Missing runtime stub: " + stub.u8string() +
                                     "; build it first or pass --stub");
        fs::path partial = output;
        partial += ".tmp";
        {
            std::ofstream out(partial, std::ios::binary | std::ios::trunc);
            if (target == "windows-x64") {
                Str icon;
                if (!installerIconOriginal.empty())
                    icon = read(project.parent_path() / path(installerIconOriginal));
                Str binary = windowsResources(read(stub), icon);
                out.write(binary.c_str(), std::streamsize(binary.size()));
            } else
                appendFile(out, stub);
            uint64_t stubSize = fs::file_size(stub);
            appendFile(out, archive);
            uint8_t footer[64]{};
            ap_memcpy(footer, "ACFTR001", 8);
            write64(footer + 8, stubSize);
            write64(footer + 16, fs::file_size(archive));
            Str digest;
            require(fileHash(string(archive), digest));
            digestBytes(digest, footer + 24);
            write64(footer + 56, stubSize);
            out.write(reinterpret_cast<char*>(footer), 64);
            if (!out)
                throw std::runtime_error("Cannot write installer");
        }
        std::error_code error;
        fs::rename(partial, output, error);
        if (error) {
            fs::remove(output, error);
            fs::rename(partial, output);
        }
        fs::permissions(output, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add);
    }
    std::cout << "Built " << output.u8string() << " (" << files.size() << " files, " << payloadBytes
              << " payload bytes, " << target.c_str() << ", " << ui.c_str() << ")\n";
}
void init(const Arguments& args) {
    Str target = args.get("target", Str(os::name()) + "-x64"), platform = platformFor(target),
        id = args.get("id", "com.example.application"), name = args.get("name", "My Application");
    Json m = Json::object();
    m.set("schemaVersion", Json::integer(1));
    Json app = Json::object();
    app.set("id", Json(id));
    app.set("name", Json(name));
    app.set("version", Json(args.get("version", "1.0.0")));
    m.set("app", move(app));
    Json inputs = Json::object();
    inputs.set(target, Json(args.get("input", "payload")));
    m.set("inputs", move(inputs));
    Json applications = Json::object(), main = Json::object();
    main.set(platform, Json(args.get("main", target == "windows-x64" ? "app.exe"
                                             : target == "xj380-x64" ? "app.elf"
                                                                     : "app")));
    applications.set("main", move(main));
    m.set("applications", move(applications));
    Json ui = Json::object(), modes = Json::array();
    modes.add(Json("gui"));
    modes.add(Json("tui"));
    ui.set("modes", move(modes));
    ui.set("default", Json("gui"));
    m.set("ui", move(ui));
    Json install = Json::object();
    install.set("scope", Json("user"));
    install.set("allowScopeChange", Json::boolean(true));
    install.set("allowPathChange", Json::boolean(true));
    m.set("install", move(install));
    Json shortcuts = Json::array(), shortcut = Json::object();
    shortcut.set("id", Json("desktop"));
    shortcut.set("location", Json("desktop"));
    shortcut.set("application", Json("main"));
    shortcut.set("defaultChecked", Json::boolean(true));
    shortcuts.add(move(shortcut));
    m.set("shortcuts", move(shortcuts));
    Json env = Json::array(), entry = Json::object();
    entry.set("id", Json("path"));
    entry.set("name", Json("PATH"));
    entry.set("value", Json("${install.dir}"));
    entry.set("operation", Json("appendUnique"));
    entry.set("defaultChecked", Json::boolean(true));
    env.add(move(entry));
    m.set("environment", move(env));
    require(validateManifest(m));
    fs::path output = path(args.get("output", "project.json"));
    if (fs::exists(output) && !args.has("force"))
        throw std::runtime_error("Project already exists; choose a new --output or use --force");
    write(output, m.dump() + "\n");
    std::cout << "Created " << output.u8string() << "\n";
}
} // namespace
int builderMain(int argc, char** argv) {
    os::initialize(argc, argv, nullptr);
    try {
        auto args = arguments(argc, argv);
        if (args.command == "init")
            init(args);
        else if (args.command == "build")
            build(args, path(os::executable(argv[0])));
        else if (args.command == "validate") {
            load(path(args.get("project", "project.json")));
            std::cout << "Project is valid\n";
        } else if (args.command == "inspect") {
            Str binary = read(path(args.get("package")));
            if (binary.starts("$EPF")) {
                std::cout << inspectEpfPackage(binary).dump().c_str() << "\n";
            } else {
                Package package;
                require(package.open(args.get("package")));
                std::cout << package.manifest.dump().c_str() << "\n";
            }
        } else if (args.command == "wizard") {
            Arguments create;
            create.command = "init";
            std::string value;
            std::cout << "Application name: ";
            std::getline(std::cin, value);
            create.flags["name"] = value;
            std::cout << "Product id (com.example.app): ";
            std::getline(std::cin, value);
            if (!value.empty())
                create.flags["id"] = value;
            std::cout << "Input folder: ";
            std::getline(std::cin, value);
            create.flags["input"] = value;
            std::cout << "Main executable relative path: ";
            std::getline(std::cin, value);
            create.flags["main"] = value;
            std::cout << "Target (windows-x64/linux-x64/xj380-x64): ";
            std::getline(std::cin, value);
            if (!value.empty())
                create.flags["target"] = value;
            create.flags["output"] = bytes(args.get("output", "project.json"));
            init(create);
        } else {
            std::cout
                << "Accretion Package 0.1.0\n\napack init --input DIR --main RELATIVE_PATH [--target TARGET] "
                   "[--output project.json]\napack validate --project project.json\napack build --project "
                   "project.json --target TARGET --ui gui|tui [--stub PATH] [--output FILE]\n            "
                   "[--format installer|archive|bundle|epf] [--compression lz4|store]\napack inspect "
                   "--package INSTALLER_OR_ARCHIVE\napack wizard [--output project.json]\n";
            return args.command.empty() || args.command == "--help" || args.command == "help" ? 0 : 2;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "apack: " << e.what() << "\n";
        return 1;
    }
}

int main(int argc, char** argv) {
#ifdef _WIN32
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        return 1;
    std::vector<std::string> text;
    std::vector<char*> pointers;
    text.reserve(count);
    for (int i = 0; i < count; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) {
            LocalFree(wide);
            return 1;
        }
        text.emplace_back(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.back().data(), size, nullptr, nullptr);
    }
    LocalFree(wide);
    for (auto& value : text)
        pointers.push_back(value.data());
    return builderMain(count, pointers.data());
#else
    return builderMain(argc, argv);
#endif
}
