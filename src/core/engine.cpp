#include "ap/engine.hpp"
namespace ap {
namespace {
bool samePath(const Str& a, const Str& b) {
    return equal(os::name(), "windows") ? slash(a).lower() == slash(b).lower() : a == b;
}
const Json* ownedFile(const Json& receipt, const Str& path) {
    for (const auto& file : receipt.get("installedFiles").values)
        if (file.str("path") == path)
            return &file;
    return nullptr;
}
Str helperExtension() {
    return equal(os::name(), "windows") ? ".exe" : equal(os::name(), "xj380") ? ".elf" : "";
}
Status loadReceipt(const Str& root, Json& receipt) {
    Str text;
    AP_TRY(os::readText(join(root, ".accretion/receipt.json"), text));
    AP_TRY(parseJson(text, receipt));
    if (receipt.integer("schemaVersion", 0) != 1 || !samePath(receipt.str("root"), root) ||
        receipt.str("platform") != os::name())
        return Status::fail("Invalid installation receipt");
    AP_TRY(validateManifest(receipt.get("manifest"), true));
    for (const auto& file : receipt.get("installedFiles").values) {
        Str error;
        if (!relativePath(file.str("path"), error))
            return Status::fail("Invalid receipt file path");
    }
    return {};
}
} // namespace
Installer::~Installer() {
    for (const auto& path : temporaryDirectories)
        os::removeTree(path);
}
Str Installer::defaultDirectory(bool machine) const {
    Str scope = machine ? "machine" : "user";
    Str configured = manifest.get("install").get("paths").get(os::name()).str(scope);
    if (configured.empty())
        configured = Str(machine ? "${paths.machinePrograms}/" : "${paths.userPrograms}/") +
                     (equal(os::name(), "windows") ? "${app.name}" : "${app.id}");
    return slash(expand(configured, manifest, "", machine));
}
void Installer::defaults() {
    options.machine = manifest.get("install").str("scope", "user") == "machine";
    options.directory = defaultDirectory(options.machine);
    options.launch = manifest.get("completion").boolean("launchDefault", false);
    options.selected = Json::object();
}
Status Installer::open(const Str& packagePath, const Str& executable) {
    self = executable;
    AP_TRY(package.open(packagePath));
    manifest = package.manifest;
    Str target = Str(os::name()) + "-x64";
    if (manifest.str("target") != target)
        return Status::fail("Package target is " + manifest.str("target") + ", runtime is " + target);
    defaults();
    return {};
}
Status Installer::openMemory(const uint8_t* data, size_t size, const Str& executable, uint64_t stubSize) {
    self = executable;
    AP_TRY(package.fromMemory(data, size));
    package.stubSize = stubSize;
    manifest = package.manifest;
    if (manifest.str("target") != Str(os::name()) + "-x64")
        return Status::fail("Embedded payload target mismatch");
    defaults();
    return {};
}
Str Installer::helperSource() const {
    Str runtime = manifest.get("runtime").str("stub");
    return runtime.empty() ? self : join(options.directory, runtime);
}
uint64_t Installer::helperSize() const {
    return manifest.get("runtime").str("stub").empty() ? package.stubSize : 0;
}
Status Installer::installed(const Str& root, const Str& executable) {
    self = executable;
    AP_TRY(loadReceipt(slash(root), receipt));
    manifest = receipt.get("manifest");
    options.directory = receipt.str("root");
    options.machine = receipt.boolean("machine", false);
    options.selected = receipt.get("selected");
    options.uninstall = true;
    return {};
}
Status Installer::adoptExisting() {
    if (options.uninstall || !os::info(join(options.directory, ".accretion/receipt.json")).exists)
        return {};
    Json previous;
    AP_TRY(loadReceipt(options.directory, previous));
    if (previous.get("manifest").get("app").str("id") != manifest.get("app").str("id"))
        return Status::fail("Destination belongs to a different product");
    Json selected = previous.get("selected");
    for (size_t i = 0; i < options.selected.keys.size(); ++i)
        selected.set(options.selected.keys[i], options.selected.values[i]);
    options.selected = move(selected);
    return {};
}
Str Installer::license() const {
    const auto& branding = manifest.get("branding");
    const auto& texts = branding.get("licenseTexts");
    Str language = manifest.get("ui").str("language", "zh-CN");
    if (texts.get(language).kind == Json::String)
        return texts.str(language);
    if (!texts.values.empty())
        return texts.values[0].text;
    return branding.str("licenseText");
}
bool Installer::component(const Str& id) const {
    if (id.empty())
        return true;
    for (const auto& item : manifest.get("components").values)
        if (item.str("id") == id)
            return item.boolean("required", false) || options.chosen("component", item);
    return false;
}
Status Installer::check() const {
    if (!absolutePath(options.directory) || options.directory.find('\0') != size_t(-1) ||
        options.directory.size() > 2000)
        return Status::fail("Choose an absolute installation directory");
    Str root = slash(options.directory);
    if (root == "/" || (root.size() <= 3 && root.size() > 1 && root[1] == ':') || root.starts("//"))
        return Status::fail("Choose an application directory, not a filesystem root");
    AP_TRY(os::safeAncestors(root));
    if (!utf8(root) || root.find("${") != size_t(-1))
        return Status::fail("Invalid/unresolved installation directory");
    if (options.machine && !os::administrator())
        return Status::fail("System installation needs administrator privileges; restart the installer with "
                            "elevated permissions");
    if (!options.uninstall) {
        if (equal(os::name(), "xj380")) {
            if (!manifest.get("associations").values.empty() && !(os::nativeCapabilities() & 4))
                return Status::fail("XJ380 native file-association adapter is not available yet");
            for (const auto& application : manifest.get("applications").values)
                if (application.has("workingDirectory") && !(os::nativeCapabilities() & 8))
                    return Status::fail("Custom XJ380 working directories require the native launch adapter");
        }
        if (!manifest.get("install").boolean("allowScopeChange", true) &&
            options.machine != (manifest.get("install").str("scope", "user") == "machine"))
            return Status::fail("Installation scope is fixed by this package");
        if (!manifest.get("install").boolean("allowPathChange", true) &&
            !samePath(root, defaultDirectory(options.machine)))
            return Status::fail("Installation path is fixed by this package");
        if (!license().empty() && !options.accepted)
            return Status::fail("Accept the license agreement before installing");
        for (const auto& item : manifest.get("components").values)
            if (component(item.str("id")))
                for (const auto& dep : item.get("requires").values)
                    if (!component(dep.text))
                        return Status::fail("Component " + item.str("id") + " requires " + dep.text);
        for (const auto& entry : manifest.get("environment").values)
            if (options.chosen("environment", entry)) {
                Str value = expand(entry.str("value"), manifest, root, options.machine);
                if (value.find("${") != size_t(-1) || value.find('\0') != size_t(-1) ||
                    value.find('\n') != size_t(-1) || value.find('\r') != size_t(-1))
                    return Status::fail("Invalid/unresolved environment variable: " + entry.str("name"));
                Str scope = entry.str("scope", "inherit");
                if (scope == "machine" && !os::administrator())
                    return Status::fail("Environment variable requires administrator privileges: " +
                                        entry.str("name"));
                if (equal(os::name(), "xj380") && (scope == "machine" || scope == "user") &&
                    !(os::nativeCapabilities() & 1))
                    return Status::fail("This XAPI profile supports application environment variables; "
                                        "global environment persistence needs a system-specific adapter");
            }
    }
    return {};
}
Str Installer::integrationRoot() const {
    if (!options.stateRoot.empty())
        return join(options.stateRoot, "integrations");
    if (options.machine) {
        if (equal(os::name(), "windows"))
            return join(os::environment("ProgramData"), "Accretion/installed");
        if (equal(os::name(), "xj380"))
            return "/config/accretion/installed";
        return "/var/lib/accretion/installed";
    }
    return join(os::stateHome(), "accretion/installed");
}
Json Installer::applicationEnvironment() const {
    Json env = Json::object();
    char separator = equal(os::name(), "windows") ? ';' : ':';
    for (const auto& e : manifest.get("environment").values) {
        if (!options.chosen("environment", e) || !component(e.str("component")))
            continue;
        Str name = e.str("name"),
            value = expand(e.str("value"), manifest, options.directory, options.machine),
            before = env.str(name, os::environment(name)), operation = e.str("operation", "setIfMissing");
        if (operation == "setIfMissing") {
            if (env.has(name) || os::hasEnvironment(name))
                value = before;
        } else if (operation == "appendUnique" || operation == "prependUnique") {
            bool found = false;
            for (const Str& item : split(before, separator))
                if (samePath(item, value))
                    found = true;
            if (found)
                value = before;
            else if (!before.empty())
                value = operation == "appendUnique" ? before + Str(&separator, 1) + value
                                                    : value + Str(&separator, 1) + before;
        }
        env.set(name, Json(value));
    }
    return env;
}
Status Installer::launch(const Str& application) {
    Str relative = targetExecutable(manifest, application);
    if (relative.empty())
        return Status::fail("Unknown application: " + application);
    List<Str> args;
    for (const auto& arg : manifest.get("applications").get(application).get("arguments").values)
        args.add(expand(arg.text, manifest, options.directory, options.machine));
    for (const auto& arg : options.extraArguments)
        args.add(arg);
    Str working = manifest.get("applications").get(application).str("workingDirectory", "${install.dir}");
    return os::launch(join(options.directory, relative), args, applicationEnvironment(),
                      expand(working, manifest, options.directory, options.machine), options.machine);
}
Json Installer::plan() const {
    Json plan = Json::object();
    plan.set("app", manifest.get("app"));
    plan.set("platform", Json(os::name()));
    plan.set("directory", Json(options.directory));
    plan.set("scope", Json(options.machine ? "machine" : "user"));
    Json files = Json::array();
    for (const auto& file : manifest.get("files").values)
        if (component(file.str("component")))
            files.add(Json(file.str("path")));
    plan.set("files", move(files));
    plan.set("environment", applicationEnvironment());
    plan.set("selected", options.selected);
    plan.set("uninstall", Json::boolean(options.uninstall));
    return plan;
}
Status Installer::execute() {
    options.directory = slash(options.directory);
    while (options.directory.ends("/") && options.directory.size() > 3)
        options.directory = options.directory.substr(0, options.directory.size() - 1);
    AP_TRY(check());
    if (options.dryRun) {
        os::output(plan().dump() + "\n");
        return {};
    }
    AP_TRY(os::requestAccess(parent(options.directory), true));
    Json previous;
    if (os::info(join(options.directory, ".accretion/receipt.json")).exists) {
        AP_TRY(loadReceipt(options.directory, previous));
        if (previous.get("manifest").get("app").str("id") != manifest.get("app").str("id") ||
            previous.boolean("machine", false) != options.machine)
            return Status::fail("Destination belongs to a different product or installation scope");
    }
    Transaction transaction(options.directory);
    AP_TRY(transaction.begin(manifest.get("app").str("id")));
    auto perform = [&]() -> Status {
        Json installedFiles = Json::array();
        uint64_t total = 0, current = 0;
        for (const auto& file : manifest.get("files").values)
            if (component(file.str("component")))
                total += file.integer("size", 0);
        size_t index = 0;
        for (const auto& file : manifest.get("files").values) {
            if (!component(file.str("component")))
                continue;
            Str relative = file.str("path"), destination = join(options.directory, relative);
            auto existing = os::info(destination);
            if (file.str("policy") == "preserve" && existing.exists) {
                if (const Json* owned = ownedFile(previous, relative))
                    installedFiles.add(*owned);
                continue;
            }
            if (existing.exists && !ownedFile(previous, relative))
                return Status::fail("Destination contains an unrelated file: " + destination);
            if (!progress.report("Extracting " + relative, current, total))
                return Status::fail("Installation cancelled");
            Str staged = join(transaction.work(), "stage/payload-" + number(index++));
            AP_TRY(package.extract(file, staged));
            AP_TRY(transaction.file(staged, destination));
            Json record = Json::object();
            record.set("path", Json(relative));
            record.set("sha256", Json(file.str("sha256")));
            record.set("mode", Json::integer(file.integer("mode", 0644)));
            installedFiles.add(move(record));
            current += file.integer("size", 0);
        }
        for (const auto& old : previous.get("installedFiles").values) {
            Str path = old.str("path");
            bool retained = false;
            for (const auto& item : installedFiles.values)
                if (item.str("path") == path)
                    retained = true;
            if (!retained && os::info(join(options.directory, path)).exists) {
                Str digest;
                AP_TRY(fileHash(join(options.directory, path), digest));
                if (digest == old.str("sha256"))
                    AP_TRY(transaction.remove(join(options.directory, path)));
                else
                    warnings.add("Preserved modified obsolete file: " + path);
            }
        }
        if (!progress.report("Registering application", current, total))
            return Status::fail("Installation cancelled");
        Json integrations = Json::array();
        AP_TRY(integrate(transaction, integrations, previous));
        Json context = Json::object();
        context.set("mode", Json("uninstall"));
        context.set("root", Json(options.directory));
        if (!options.stateRoot.empty())
            context.set("stateRoot", Json(options.stateRoot));
        Str uninstallStage = join(transaction.work(), "stage/uninstaller");
        AP_TRY(makeHelper(helperSource(), helperSize(), uninstallStage, context));
        Str uninstaller = join(options.directory, ".accretion/uninstall" + helperExtension());
        AP_TRY(transaction.file(uninstallStage, uninstaller));
        receipt = Json::object();
        receipt.set("schemaVersion", Json::integer(1));
        receipt.set("root", Json(options.directory));
        receipt.set("platform", Json(os::name()));
        receipt.set("machine", Json::boolean(options.machine));
        receipt.set("manifest", manifest);
        receipt.set("selected", options.selected);
        receipt.set("installedFiles", move(installedFiles));
        receipt.set("integrations", move(integrations));
        AP_TRY(transaction.text(join(options.directory, ".accretion/receipt.json"), receipt.dump(), 0600));
        Json inventory = Json::object();
        inventory.set("root", Json(options.directory));
        inventory.set("app", manifest.get("app"));
        inventory.set("platform", Json(os::name()));
        AP_TRY(transaction.text(join(integrationRoot(), manifest.get("app").str("id") + ".json"),
                                inventory.dump(), 0644));
        return {};
    };
    Status result = perform();
    if (!result) {
        Status restored = transaction.rollback();
        if (!restored)
            return Status::fail(result.message + "\n" + restored.message);
        return result;
    }
    AP_TRY(transaction.commit());
    os::changedEnvironment();
    if (options.stateRoot.empty() && (!manifest.get("associations").values.empty() ||
                                      !previous.get("manifest").get("associations").values.empty())) {
        Status refreshed = os::refreshAssociations(options.machine);
        if (!refreshed)
            warnings.add(refreshed.message);
    }
    progress.report("Installation complete", 1, 1);
    return {};
}
Status Installer::uninstall() {
    AP_TRY(check());
    if (options.dryRun) {
        os::output(plan().dump() + "\n");
        return {};
    }
    Transaction transaction(options.directory);
    AP_TRY(transaction.begin(manifest.get("app").str("id")));
    auto perform = [&]() -> Status {
        const auto& records = receipt.get("integrations").values;
        for (size_t i = records.size(); i > 0; --i)
            AP_TRY(undoIntegration(transaction, records[i - 1]));
        for (const auto& file : receipt.get("installedFiles").values) {
            Str destination = join(options.directory, file.str("path"));
            if (!os::info(destination).exists)
                continue;
            Str digest;
            AP_TRY(fileHash(destination, digest));
            if (digest != file.str("sha256")) {
                warnings.add("Preserved modified file: " + destination);
                continue;
            }
            if (!progress.report("Removing " + file.str("path"), 0, 1))
                return Status::fail("Uninstall cancelled");
            AP_TRY(transaction.remove(destination));
        }
        List<os::Entry> helpers;
        Str internal = join(options.directory, ".accretion");
        if (os::info(internal).directory) {
            AP_TRY(os::list(internal, helpers));
            for (const auto& entry : helpers)
                if (!entry.info.directory && !entry.info.link)
                    AP_TRY(transaction.remove(join(internal, entry.name)));
        }
        AP_TRY(transaction.remove(join(integrationRoot(), manifest.get("app").str("id") + ".json")));
        return {};
    };
    Status result = perform();
    if (!result) {
        Status restored = transaction.rollback();
        return restored ? result : Status::fail(result.message + "\n" + restored.message);
    }
    AP_TRY(transaction.commit());
    for (const auto& file : receipt.get("installedFiles").values) {
        Str directory = parent(join(options.directory, file.str("path")));
        while (directory.starts(options.directory + "/")) {
            os::removeDirectory(directory);
            directory = parent(directory);
        }
    }
    os::removeDirectory(join(options.directory, ".accretion"));
    os::removeDirectory(options.directory);
    os::changedEnvironment();
    if (options.stateRoot.empty() && !manifest.get("associations").values.empty()) {
        Status refreshed = os::refreshAssociations(options.machine);
        if (!refreshed)
            warnings.add(refreshed.message);
    }
    progress.report("Uninstall complete", 1, 1);
    return {};
}
} // namespace ap
