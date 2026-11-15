#pragma once
#include "ap/package.hpp"
namespace ap {
struct Options {
    Str directory, stateRoot;
    bool machine = false, accepted = false, launch = false, uninstall = false, dryRun = false;
    Json selected = Json::object();
    List<Str> extraArguments;
    bool chosen(const Str& category, const Json& item) const {
        return selected.boolean(category + ":" + item.str("id"), item.boolean("defaultChecked", true));
    }
};
struct Progress {
    void* context = nullptr;
    bool (*update)(void*, const Str&, uint64_t, uint64_t) = nullptr;
    bool report(const Str& text, uint64_t current, uint64_t total) const {
        return !update || update(context, text, current, total);
    }
};
struct ContextBlock {
    uint8_t marker[16];
    uint32_t length, reserved;
    char json[16384];
};
extern volatile ContextBlock embeddedContext;
struct PayloadBlock {
    uint8_t marker[16];
    uint64_t address, size, stubSize;
    uint8_t digest[32];
};
extern volatile PayloadBlock embeddedPayload;
Json readContext();
Status makeHelper(const Str& source, uint64_t stubSize, const Str& output, const Json& context);

class Transaction {
    Str root_, work_;
    Json journal_;
    bool active_ = false;
    Status save();
    Status parents(const Str& path);
    Status record(Json operation, size_t& index);

  public:
    explicit Transaction(const Str& root) : root_(root), work_(root + ".accretion-txn") {}
    ~Transaction();
    const Str& work() const {
        return work_;
    }
    Status begin(const Str& product);
    Status file(const Str& source, const Str& destination);
    Status text(const Str& destination, const Str& value, uint32_t mode = 0644);
    Status remove(const Str& destination);
    Status registry(const Json& descriptor, const Json& value);
    Status native(const Json& descriptor, const Json& value);
    Status rollback();
    Status commit();
    static Status recover(const Str& root);
};

class Installer {
  public:
    Installer() = default;
    ~Installer();
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;
    Package package;
    Json manifest, receipt;
    Options options;
    Str self;
    Progress progress;
    List<Str> warnings, temporaryDirectories;
    Status open(const Str& packagePath, const Str& executable);
    Status openMemory(const uint8_t* data, size_t size, const Str& executable, uint64_t stubSize);
    Str helperSource() const;
    uint64_t helperSize() const;
    Status installed(const Str& root, const Str& executable);
    void defaults();
    Status adoptExisting();
    Str defaultDirectory(bool machine) const;
    Str license() const;
    bool component(const Str& id) const;
    Status check() const;
    Status execute();
    Status uninstall();
    Status launch(const Str& application);
    Json applicationEnvironment() const;
    Json plan() const;
    Str integrationRoot() const;

  private:
    Status integrate(Transaction& transaction, Json& integrationRecords, const Json& oldReceipt);
    Status undoIntegration(Transaction& transaction, const Json& record);
};
} // namespace ap
