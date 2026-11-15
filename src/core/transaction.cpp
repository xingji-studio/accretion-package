#include "ap/engine.hpp"
namespace ap {
Status Transaction::save() {
    Str temp = join(work_, "journal.tmp");
    AP_TRY(os::writeText(temp, journal_.dump()));
    return os::rename(temp, join(work_, "journal.json"));
}
Status Transaction::begin(const Str& product) {
    AP_TRY(os::safeAncestors(root_));
    AP_TRY(os::directories(parent(root_)));
    AP_TRY(os::mkdir(work_, true));
    active_ = true;
    journal_ = Json::object();
    journal_.set("schemaVersion", Json::integer(1));
    journal_.set("root", Json(root_));
    journal_.set("product", Json(product));
    journal_.set("state", Json("applying"));
    journal_.set("operations", Json::array());
    Status status = os::mkdir(join(work_, "backup"));
    if (status)
        status = os::mkdir(join(work_, "stage"));
    if (status)
        status = save();
    if (!status) {
        active_ = false;
        os::removeTree(work_);
    }
    return status;
}
Transaction::~Transaction() {
    if (active_)
        rollback();
}
Status Transaction::record(Json operation, size_t& index) {
    Json* operations = journal_.find("operations");
    index = operations->values.size();
    operations->add(move(operation));
    return save();
}
Status Transaction::parents(const Str& path) {
    if (path.empty())
        return {};
    auto stat = os::info(path);
    if (stat.exists)
        return stat.directory && !stat.link ? Status{} : Status::fail("Not a safe directory: " + path);
    AP_TRY(parents(parent(path)));
    Json op = Json::object();
    op.set("kind", Json("directory"));
    op.set("path", Json(path));
    size_t index;
    AP_TRY(record(move(op), index));
    return os::mkdir(path, true);
}
Status Transaction::file(const Str& source, const Str& destination) {
    AP_TRY(parents(parent(destination)));
    auto stat = os::info(destination);
    if (stat.directory || stat.link)
        return Status::fail("Refusing non-regular destination: " + destination);
    Json op = Json::object();
    op.set("kind", Json("file"));
    op.set("path", Json(destination));
    op.set("existed", Json::boolean(stat.exists));
    op.set("mode", Json::integer(stat.mode));
    size_t index = journal_.get("operations").values.size();
    if (stat.exists) {
        Str backup = join(work_, "backup/" + number(index));
        AP_TRY(os::copy(destination, backup));
        op.set("backup", Json(backup));
        op.set("restoreTemporary", Json(destination + ".accretion-restore-" + number(os::nonce())));
    }
    if (!source.empty()) {
        Str digest;
        AP_TRY(fileHash(source, digest));
        op.set("afterHash", Json(digest));
    }
    size_t recorded;
    AP_TRY(record(move(op), recorded));
    if (source.empty())
        return os::removeFile(destination);
    Str sibling = destination + ".accretion-new-" + number(os::nonce());
    Json* saved = journal_.find("operations");
    saved->values[recorded].set("temporary", Json(sibling));
    AP_TRY(save());
    AP_TRY(os::copy(source, sibling));
    AP_TRY(os::chmod(sibling, os::info(source).mode));
    AP_TRY(os::rename(sibling, destination));
    return {};
}
Status Transaction::text(const Str& destination, const Str& value, uint32_t mode) {
    Str staged = join(work_, "stage/text-" + number(journal_.get("operations").values.size()));
    AP_TRY(os::writeText(staged, value));
    AP_TRY(os::chmod(staged, mode));
    return file(staged, destination);
}
Status Transaction::remove(const Str& destination) {
    if (!os::info(destination).exists)
        return {};
    return file({}, destination);
}
Status Transaction::registry(const Json& descriptor, const Json& value) {
    Json previous;
    AP_TRY(os::registryRead(descriptor, previous));
    Json op = Json::object();
    op.set("kind", Json("registry"));
    op.set("descriptor", descriptor);
    op.set("before", move(previous));
    size_t index;
    AP_TRY(record(move(op), index));
    return os::registryWrite(descriptor, value);
}
Status Transaction::native(const Json& descriptor, const Json& value) {
    Json previous;
    AP_TRY(os::nativeRead(descriptor, previous));
    Json op = Json::object();
    op.set("kind", Json("native"));
    op.set("descriptor", descriptor);
    op.set("before", move(previous));
    size_t index;
    AP_TRY(record(move(op), index));
    return os::nativeWrite(descriptor, value);
}
Status Transaction::rollback() {
    if (!active_)
        return {};
    Str errors;
    const auto& operations = journal_.get("operations").values;
    for (size_t i = operations.size(); i > 0; --i) {
        const auto& op = operations[i - 1];
        Status result;
        Str kind = op.str("kind"), destination = op.str("path");
        if (kind == "file") {
            Str temporary = op.str("temporary");
            if (!temporary.empty())
                os::removeFile(temporary);
            if (op.boolean("existed", false)) {
                Str backup = op.str("backup");
                if (!os::info(backup).exists) {
                    errors.append("Missing rollback backup: " + backup + "\n");
                    continue;
                }
                // Integration files can be on a different volume from the application.
                Str temp =
                    op.str("restoreTemporary", destination + ".accretion-restore-" + number(os::nonce()));
                result = os::copy(backup, temp);
                if (result)
                    result = os::chmod(temp, uint32_t(op.integer("mode", 0644)));
                if (result)
                    result = os::rename(temp, destination);
            } else
                result = os::removeFile(destination);
        } else if (kind == "registry")
            result = os::registryWrite(op.get("descriptor"), op.get("before"));
        else if (kind == "native")
            result = os::nativeWrite(op.get("descriptor"), op.get("before"));
        else if (kind == "directory") {
            result = os::removeDirectory(destination);
            if (!result)
                continue;
        }
        if (!result)
            errors.append(result.message + "\n");
    }
    active_ = false;
    if (!errors.empty())
        return Status::fail("Rollback needs recovery at " + work_ + ":\n" + errors);
    return os::removeTree(work_);
}
Status Transaction::commit() {
    journal_.set("state", Json("committed"));
    AP_TRY(save());
    active_ = false;
    Status result = os::removeTree(work_);
    return result ? Status{}
                  : Status::fail("Installation committed; temporary cleanup failed: " + result.message);
}
Status Transaction::recover(const Str& root) {
    Transaction transaction(root);
    Str text;
    AP_TRY(os::readText(join(transaction.work_, "journal.json"), text));
    AP_TRY(parseJson(text, transaction.journal_));
    if (transaction.journal_.integer("schemaVersion", 0) != 1 || transaction.journal_.str("root") != root)
        return Status::fail("Invalid recovery journal");
    if (transaction.journal_.str("state") == "committed")
        return os::removeTree(transaction.work_);
    transaction.active_ = true;
    return transaction.rollback();
}
} // namespace ap
