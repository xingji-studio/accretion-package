#include "ap/engine.hpp"
namespace ap {
namespace {
Str hex(const Str& bytes) {
    const char digits[] = "0123456789abcdef";
    Str out;
    out.reserve(bytes.size() * 2 + 1);
    for (size_t i = 0; i < bytes.size(); ++i) {
        uint8_t c = bytes[i];
        out.push(digits[c >> 4]);
        out.push(digits[c & 15]);
    }
    return out;
}
Status unhex(const Str& text, Str& out) {
    if (text.size() % 2)
        return Status::fail("Invalid receipt byte encoding");
    for (size_t i = 0; i < text.size(); i += 2) {
        unsigned value = 0;
        for (unsigned j = 0; j < 2; ++j) {
            char c = text[i + j];
            unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
            if (digit > 15)
                return Status::fail("Invalid receipt byte encoding");
            value = value * 16 + digit;
        }
        out.push(char(value));
    }
    return {};
}
Str extension() {
    return equal(os::name(), "windows") ? ".exe" : equal(os::name(), "xj380") ? ".elf" : "";
}
Json descriptor(bool machine, const Str& key, const Str& name) {
    Json d = Json::object();
    d.set("hive", Json(machine ? "HKLM" : "HKCU"));
    d.set("key", Json(key));
    d.set("name", Json(name));
    d.set("view", Json::integer(64));
    return d;
}
Json registryValue(const Str& value, const Str& type = "string") {
    Json result = Json::object();
    result.set("exists", Json::boolean(true));
    result.set("type", Json(type));
    result.set("value", Json(value));
    return result;
}
Str desktopName(const Str& value) {
    return value.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t");
}
Str xml(const Str& value) {
    return value.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace("\"", "&quot;")
        .replace("'", "&apos;");
}
bool pathEqual(const Str& a, const Str& b) {
    return equal(os::name(), "windows") ? slash(a).lower() == slash(b).lower() : a == b;
}
} // namespace
Status Installer::integrate(Transaction& transaction, Json& records, const Json& oldReceipt) {
    const auto& previous = oldReceipt.get("integrations").values;
    for (size_t i = previous.size(); i > 0; --i)
        AP_TRY(undoIntegration(transaction, previous[i - 1]));
    auto mapped = [&](const Str& real, const Str& relative) -> Str {
        return options.stateRoot.empty() ? real : join(options.stateRoot, relative);
    };
    auto fileRecord = [&](const Str& destination, const Str& staged, const Str& block) -> Status {
        Json record = Json::object();
        record.set("kind", Json(block.empty() ? "file" : "block"));
        record.set("path", Json(destination));
        auto before = os::info(destination);
        record.set("existed", Json::boolean(before.exists));
        record.set("mode", Json::integer(before.mode));
        if (before.exists) {
            Str data;
            AP_TRY(os::readText(destination, data, 4 * 1024 * 1024));
            record.set("beforeHex", Json(hex(data)));
        }
        Str digest;
        AP_TRY(fileHash(staged, digest));
        record.set("afterHash", Json(digest));
        if (!block.empty())
            record.set("block", Json(block));
        AP_TRY(transaction.file(staged, destination));
        records.add(move(record));
        return {};
    };
    auto textRecord = [&](const Str& destination, const Str& value, uint32_t mode,
                          const Str& block = Str()) -> Status {
        Str staged = join(transaction.work(), "stage/integration-" + number(records.values.size()));
        AP_TRY(os::writeText(staged, value));
        AP_TRY(os::chmod(staged, mode));
        return fileRecord(destination, staged, block);
    };
    auto regRecord = [&](const Json& d, const Json& after, const Json& ownership = Json()) -> Status {
        if (!options.stateRoot.empty()) {
            Str key = sha256(d.dump().c_str(), d.dump().size());
            return textRecord(join(options.stateRoot, "registry/" + key + ".json"), after.dump(), 0644);
        }
        Json before;
        AP_TRY(os::registryRead(d, before));
        Json record = Json::object();
        record.set("kind", Json("registry"));
        record.set("descriptor", d);
        record.set("before", before);
        record.set("after", after);
        if (ownership.kind != Json::Null)
            record.set("ownership", ownership);
        AP_TRY(transaction.registry(d, after));
        records.add(move(record));
        return {};
    };
    auto nativeRecord = [&](const Json& descriptor, const Json& after,
                            const Json& ownership = Json()) -> Status {
        if (!options.stateRoot.empty()) {
            Str key = descriptor.dump();
            return textRecord(join(options.stateRoot, "native/" + sha256(key.c_str(), key.size()) + ".json"),
                              after.dump(), 0644);
        }
        Json before;
        AP_TRY(os::nativeRead(descriptor, before));
        Json record = Json::object();
        record.set("kind", Json("native"));
        record.set("descriptor", descriptor);
        record.set("before", before);
        record.set("after", after);
        if (ownership.kind != Json::Null)
            record.set("ownership", ownership);
        AP_TRY(transaction.native(descriptor, after));
        records.add(move(record));
        return {};
    };
    Str internal = join(options.directory, ".accretion"), helper = join(internal, "launch" + extension());
    Json context = Json::object();
    context.set("mode", Json("launch"));
    context.set("root", Json(options.directory));
    if (!options.stateRoot.empty())
        context.set("stateRoot", Json(options.stateRoot));
    Str launchStage = join(transaction.work(), "stage/launch-helper");
    AP_TRY(makeHelper(helperSource(), helperSize(), launchStage, context));
    AP_TRY(transaction.file(launchStage, helper));
    for (const auto& shortcut : manifest.get("shortcuts").values) {
        if (!options.chosen("shortcut", shortcut) || !component(shortcut.str("component")))
            continue;
        Str location = shortcut.str("location", "desktop"), application = shortcut.str("application");
        Str name = shortcut.str("name", manifest.get("app").str("name"));
        Str filename = name;
        Str error;
        if (!relativePath(filename, error) || filename.find('/') != size_t(-1) ||
            filename.find('\\') != size_t(-1) || filename.find(':') != size_t(-1))
            filename = manifest.get("app").str("id") + "-" + shortcut.str("id");
        Str folder = mapped(location == "desktop" ? os::desktop(options.machine) : os::menu(options.machine),
                            location);
        Str suffix = equal(os::name(), "windows") ? ".lnk"
                     : equal(os::name(), "xj380") ? os::shortcutExtension()
                                                  : Str(".desktop");
        Str destination = join(folder, filename + suffix),
            staged = join(transaction.work(), "stage/shortcut-" + number(records.values.size()));
        if (equal(os::name(), "xj380") && !(os::nativeCapabilities() & 2)) {
            Json ctx = context;
            ctx.set("application", Json(application));
            ctx.set("arguments", shortcut.get("arguments"));
            AP_TRY(makeHelper(helperSource(), helperSize(), staged, ctx));
        } else {
            List<Str> arguments;
            arguments.add("--application");
            arguments.add(application);
            const auto& extras = shortcut.get("arguments").values;
            if (!extras.empty()) {
                arguments.add("--");
                for (const auto& arg : extras)
                    arguments.add(expand(arg.text, manifest, options.directory, options.machine));
            }
            Str icon = shortcut.str("icon", manifest.get("branding").str("applicationIcon"));
            if (!icon.empty())
                icon = join(options.directory, icon);
            AP_TRY(os::shortcut(staged, helper, arguments, options.directory, icon));
            if (equal(os::name(), "linux")) {
                Str text;
                AP_TRY(os::readText(staged, text));
                size_t begin = text.find("Name="), end = text.find('\n', begin);
                if (begin != size_t(-1) && end != size_t(-1))
                    text = text.substr(0, begin) + "Name=" + desktopName(name) + text.substr(end);
                if (shortcut.boolean(
                        "terminal", manifest.get("applications").get(application).boolean("terminal", false)))
                    text = text.replace("Terminal=false", "Terminal=true");
                AP_TRY(os::writeText(staged, text));
                AP_TRY(os::chmod(staged, 0755));
            }
        }
        AP_TRY(fileRecord(destination, staged, {}));
    }
    Str userShell, machineShell, userService, machineService;
    for (const auto& e : manifest.get("environment").values) {
        if (!options.chosen("environment", e) || !component(e.str("component")))
            continue;
        Str scope = e.str("scope", "inherit");
        if (scope == "application" || (equal(os::name(), "xj380") && scope == "inherit"))
            continue;
        bool machine = scope == "machine" || (scope == "inherit" && options.machine);
        Str key = e.str("name"), value = expand(e.str("value"), manifest, options.directory, options.machine),
            operation = e.str("operation", "setIfMissing");
        if (equal(os::name(), "xj380")) {
            Json descriptor = Json::object();
            descriptor.set("kind", Json("environment"));
            descriptor.set("scope", Json(machine ? "machine" : "user"));
            descriptor.set("name", Json(key));
            Json before;
            if (options.stateRoot.empty())
                AP_TRY(os::nativeRead(descriptor, before));
            else {
                Str encoded = descriptor.dump(),
                    file = join(options.stateRoot,
                                "native/" + sha256(encoded.c_str(), encoded.size()) + ".json"),
                    text;
                if (os::info(file).exists) {
                    AP_TRY(os::readText(file, text));
                    AP_TRY(parseJson(text, before));
                }
            }
            Str original = before.str("value"), after = value;
            bool contributed = true;
            if (operation == "setIfMissing" && before.boolean("exists", false)) {
                after = original;
                contributed = false;
            } else if (operation == "appendUnique" || operation == "prependUnique") {
                bool found = false;
                for (const auto& part : split(original, ':'))
                    if (part == value)
                        found = true;
                if (found) {
                    after = original;
                    contributed = false;
                } else if (!original.empty())
                    after = operation == "appendUnique" ? original + ":" + value : value + ":" + original;
            }
            Json snapshot = Json::object();
            snapshot.set("exists", Json::boolean(true));
            snapshot.set("value", Json(after));
            Json ownership = Json::object();
            ownership.set("operation", Json(operation));
            ownership.set("token", Json(value));
            ownership.set("contributed", Json::boolean(contributed));
            AP_TRY(nativeRecord(descriptor, snapshot, ownership));
        } else if (equal(os::name(), "windows")) {
            Json d = descriptor(machine,
                                machine ? "SYSTEM/CurrentControlSet/Control/Session Manager/Environment"
                                        : "Environment",
                                key),
                 before;
            if (options.stateRoot.empty())
                AP_TRY(os::registryRead(d, before));
            else {
                Str storage = join(options.stateRoot,
                                   "registry/" + sha256(d.dump().c_str(), d.dump().size()) + ".json"),
                    text;
                if (os::info(storage).exists) {
                    AP_TRY(os::readText(storage, text));
                    AP_TRY(parseJson(text, before));
                }
            }
            Str original = before.str("value"), result = value;
            bool contributed = true;
            if (operation == "setIfMissing" && before.boolean("exists", false)) {
                result = original;
                contributed = false;
            } else if (operation == "appendUnique" || operation == "prependUnique") {
                bool found = false;
                for (const auto& item : split(original, ';'))
                    if (pathEqual(item, value))
                        found = true;
                if (found) {
                    result = original;
                    contributed = false;
                } else if (!original.empty())
                    result = operation == "appendUnique" ? original + ";" + value : value + ";" + original;
            }
            Json ownership = Json::object();
            ownership.set("operation", Json(operation));
            ownership.set("token", Json(value));
            ownership.set("contributed", Json::boolean(contributed));
            AP_TRY(regRecord(d, registryValue(result, before.str("type", "expandString")), ownership));
        } else if (equal(os::name(), "linux")) {
            Str command;
            if (operation == "setIfMissing")
                command = "if [ -z \"${" + key + "+x}\" ]; then export " + key + "=" + os::shellQuote(value) +
                          "; fi\n";
            else if (operation == "set")
                command = "export " + key + "=" + os::shellQuote(value) + "\n";
            else {
                Str quote = os::shellQuote(value);
                command = "case \":${" + key + "-}:\" in *:" + quote + ":*) ;; *) export " + key + "=" +
                          (operation == "appendUnique" ? Str("\"${") + key + ":+${" + key + "}:}\"" + quote
                                                       : quote + "\"${" + key + ":+:${" + key + "}}\"") +
                          " ;; esac\n";
            }
            Str serviceValue = value.replace("\\", "\\\\").replace("\"", "\\\"").replace("$", "$$");
            if (operation == "appendUnique")
                serviceValue = "${" + key + ":+${" + key + "}:}" + serviceValue;
            else if (operation == "prependUnique")
                serviceValue = serviceValue + "${" + key + ":+:${" + key + "}}";
            else if (operation == "setIfMissing")
                serviceValue = "${" + key + ":-" + serviceValue + "}";
            Str service = key + "=\"" + serviceValue + "\"\n";
            if (machine) {
                machineShell.append(command);
                machineService.append(service);
            } else {
                userShell.append(command);
                userService.append(service);
            }
        }
    }
    if (!userShell.empty()) {
        Str envFile = mapped(join(os::configHome(), "accretion/" + manifest.get("app").str("id") + ".sh"),
                             "environment/user.sh");
        AP_TRY(textRecord(envFile, userShell, 0644));
        Str profile = mapped(join(os::home(), ".profile"), "profiles/.profile");
        Str before;
        if (os::info(profile).exists)
            AP_TRY(os::readText(profile, before, 4 * 1024 * 1024));
        Str marker = manifest.get("app").str("id"),
            block = "\n# Accretion begin " + marker + "\n[ ! -r " + os::shellQuote(envFile) + " ] || . " +
                    os::shellQuote(envFile) + "\n# Accretion end " + marker + "\n";
        AP_TRY(textRecord(profile, before + block, 0644, block));
        if (os::environment("SHELL").ends("zsh")) {
            Str zprofile = mapped(join(os::home(), ".zprofile"), "profiles/.zprofile"), existing;
            if (os::info(zprofile).exists)
                AP_TRY(os::readText(zprofile, existing));
            AP_TRY(textRecord(zprofile, existing + block, 0644, block));
        }
        AP_TRY(textRecord(mapped(join(os::configHome(), "environment.d/60-accretion-" + marker + ".conf"),
                                 "environment/user.conf"),
                          userService, 0644));
    }
    if (!machineShell.empty()) {
        Str id = manifest.get("app").str("id");
        AP_TRY(textRecord(mapped("/etc/profile.d/accretion-" + id + ".sh", "environment/machine.sh"),
                          machineShell, 0644));
        AP_TRY(
            textRecord(mapped("/etc/environment.d/60-accretion-" + id + ".conf", "environment/machine.conf"),
                       machineService, 0644));
    }
    if (equal(os::name(), "windows")) {
        const auto& app = manifest.get("app");
        Str key = "Software/Microsoft/Windows/CurrentVersion/Uninstall/" + app.str("id"),
            uninstaller = join(internal, "uninstall.exe");
        AP_TRY(regRecord(descriptor(options.machine, key, "DisplayName"), registryValue(app.str("name"))));
        AP_TRY(
            regRecord(descriptor(options.machine, key, "DisplayVersion"), registryValue(app.str("version"))));
        AP_TRY(regRecord(descriptor(options.machine, key, "Publisher"), registryValue(app.str("publisher"))));
        AP_TRY(
            regRecord(descriptor(options.machine, key, "InstallLocation"), registryValue(options.directory)));
        AP_TRY(regRecord(descriptor(options.machine, key, "UninstallString"),
                         registryValue(os::commandQuote(uninstaller))));
        AP_TRY(regRecord(descriptor(options.machine, key, "QuietUninstallString"),
                         registryValue(os::commandQuote(uninstaller) + " --silent")));
        Str icon = manifest.get("branding").str("applicationIcon");
        if (!icon.empty())
            AP_TRY(regRecord(descriptor(options.machine, key, "DisplayIcon"),
                             registryValue(join(options.directory, icon))));
        for (const auto& entry : manifest.get("registry").values) {
            Json d = descriptor(entry.str("scope", "inherit") == "machine" ||
                                    (entry.str("scope", "inherit") == "inherit" && options.machine),
                                expand(entry.str("key"), manifest, options.directory, options.machine),
                                entry.str("name"));
            Json value =
                registryValue(expand(entry.str("value"), manifest, options.directory, options.machine),
                              entry.str("type", "string"));
            if (entry.str("type") == "dword")
                value.set("value", entry.get("value"));
            AP_TRY(regRecord(d, value));
        }
        Str capabilities = "Software/Accretion/Applications/" + app.str("id") + "/Capabilities";
        if (!manifest.get("associations").values.empty()) {
            AP_TRY(regRecord(descriptor(options.machine, capabilities, "ApplicationName"),
                             registryValue(app.str("name"))));
            AP_TRY(regRecord(descriptor(options.machine, capabilities, "ApplicationDescription"),
                             registryValue(app.str("description", app.str("name")))));
            AP_TRY(regRecord(descriptor(options.machine, "Software/RegisteredApplications", app.str("id")),
                             registryValue(capabilities.replace("/", "\\"))));
        }
        for (const auto& association : manifest.get("associations").values) {
            Str id = app.str("id") + "." + association.str("id"), base = "Software/Classes/" + id;
            AP_TRY(regRecord(descriptor(options.machine, base, ""),
                             registryValue(association.str("description", app.str("name")))));
            Str command = os::commandQuote(helper) + " --application " +
                          os::commandQuote(association.str("application")) + " -- \"%1\"";
            AP_TRY(regRecord(descriptor(options.machine, base + "/shell/open/command", ""),
                             registryValue(command)));
            for (const auto& extension : association.get("extensions").values) {
                AP_TRY(regRecord(descriptor(options.machine,
                                            "Software/Classes/" + extension.text + "/OpenWithProgids", id),
                                 registryValue("")));
                AP_TRY(
                    regRecord(descriptor(options.machine, capabilities + "/FileAssociations", extension.text),
                              registryValue(id)));
            }
        }
    } else if (!manifest.get("registry").values.empty())
        warnings.add("Windows registry entries do not apply to this platform");
    if (equal(os::name(), "linux")) {
        Str data = parent(os::menu(options.machine));
        for (const auto& association : manifest.get("associations").values) {
            Str id = manifest.get("app").str("id") + "." + association.str("id"),
                mime = "application/x-" + id.lower(),
                description = association.str("description", manifest.get("app").str("name"));
            Str document = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                           "  <mime-type type=\"" +
                           xml(mime) + "\">\n    <comment>" + xml(description) + "</comment>\n";
            for (const auto& ext : association.get("extensions").values)
                document.append("    <glob pattern=\"*" + xml(ext.text) + "\"/>\n");
            document.append("  </mime-type>\n</mime-info>\n");
            AP_TRY(
                textRecord(mapped(join(data, "mime/packages/" + id + ".xml"), "mime/packages/" + id + ".xml"),
                           document, 0644));
            Str staged = join(transaction.work(), "stage/mime-desktop-" + number(records.values.size()));
            List<Str> arguments;
            arguments.add("--application");
            arguments.add(association.str("application"));
            arguments.add("--");
            AP_TRY(os::shortcut(staged, helper, arguments, options.directory, ""));
            Str text;
            AP_TRY(os::readText(staged, text));
            size_t exec = text.find("Exec="), end = text.find('\n', exec);
            if (exec == size_t(-1) || end == size_t(-1))
                return Status::fail("Invalid native desktop entry");
            text = text.substr(0, end) + " %f" + text.substr(end);
            size_t name = text.find("Name="), nameEnd = text.find('\n', name);
            text = text.substr(0, name) + "Name=" + desktopName(description) + text.substr(nameEnd);
            text.append("MimeType=" + mime + ";\nNoDisplay=true\n");
            if (manifest.get("applications").get(association.str("application")).boolean("terminal", false))
                text = text.replace("Terminal=false", "Terminal=true");
            AP_TRY(os::writeText(staged, text));
            AP_TRY(fileRecord(
                mapped(join(os::menu(options.machine), id + ".desktop"), "menu/" + id + ".desktop"), staged,
                {}));
        }
    }
    if (equal(os::name(), "xj380")) {
        for (const auto& association : manifest.get("associations").values) {
            Json descriptor = Json::object();
            descriptor.set("kind", Json("association"));
            descriptor.set("scope", Json(options.machine ? "machine" : "user"));
            descriptor.set("id", Json(manifest.get("app").str("id") + "." + association.str("id")));
            Json value = association;
            value.set("executable", Json(helper));
            Json arguments = Json::array();
            arguments.add(Json("--application"));
            arguments.add(Json(association.str("application")));
            arguments.add(Json("--"));
            arguments.add(Json("%1"));
            value.set("arguments", move(arguments));
            Json snapshot = Json::object();
            snapshot.set("exists", Json::boolean(true));
            snapshot.set("value", move(value));
            AP_TRY(nativeRecord(descriptor, snapshot));
        }
    }
    return {};
}
Status Installer::undoIntegration(Transaction& transaction, const Json& record) {
    Str kind = record.str("kind");
    if (kind == "registry" || kind == "native") {
        Json current;
        AP_TRY(kind == "registry" ? os::registryRead(record.get("descriptor"), current)
                                  : os::nativeRead(record.get("descriptor"), current));
        const auto& own = record.get("ownership");
        Str operation = own.str("operation");
        if ((operation == "appendUnique" || operation == "prependUnique") &&
            own.boolean("contributed", false) && current.boolean("exists", false)) {
            Str value;
            bool removed = false;
            for (const auto& token : split(current.str("value"), kind == "native" ? ':' : ';')) {
                if (pathEqual(token, own.str("token"))) {
                    removed = true;
                    continue;
                }
                if (!value.empty())
                    value.push(kind == "native" ? ':' : ';');
                value.append(token);
            }
            if (removed) {
                Json after = current;
                if (value.empty() && !record.get("before").boolean("exists", false))
                    after = record.get("before");
                else
                    after.set("value", Json(value));
                AP_TRY(kind == "registry" ? transaction.registry(record.get("descriptor"), after)
                                          : transaction.native(record.get("descriptor"), after));
            }
            return {};
        }
        if (current.same(record.get("after")))
            return kind == "registry" ? transaction.registry(record.get("descriptor"), record.get("before"))
                                      : transaction.native(record.get("descriptor"), record.get("before"));
        warnings.add("Preserved changed registry value: " + record.get("descriptor").str("name"));
        return {};
    }
    if (kind != "file" && kind != "block")
        return Status::fail("Unknown integration receipt record");
    Str path = record.str("path");
    auto stat = os::info(path);
    if (!stat.exists)
        return {};
    if (stat.directory || stat.link)
        return Status::fail("Refusing linked integration file: " + path);
    if (kind == "block") {
        Str current;
        AP_TRY(os::readText(path, current, 4 * 1024 * 1024));
        Str block = record.str("block");
        if (block.empty() || current.find(block) == size_t(-1)) {
            warnings.add("Preserved edited shell profile: " + path);
            return {};
        }
        Str result = current.replace(block, "");
        if (result.empty() && !record.boolean("existed", false))
            return transaction.remove(path);
        return transaction.text(path, result, uint32_t(record.integer("mode", 0644)));
    }
    Str currentHash;
    AP_TRY(fileHash(path, currentHash));
    if (currentHash != record.str("afterHash")) {
        warnings.add("Preserved modified integration file: " + path);
        return {};
    }
    if (!record.boolean("existed", false))
        return transaction.remove(path);
    Str before;
    AP_TRY(unhex(record.str("beforeHex"), before));
    return transaction.text(path, before, uint32_t(record.integer("mode", 0644)));
}
} // namespace ap
