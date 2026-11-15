#pragma once
#include "ap/json.hpp"
namespace ap::os {
struct Info {
    bool exists = false, directory = false, link = false;
    uint64_t size = 0;
    uint32_t mode = 0644;
};
struct Entry {
    Str name;
    Info info;
};
const char* name();
Str environment(const Str& name);
bool hasEnvironment(const Str& name);
Str home();
Str programs(bool machine);
Str desktop(bool machine);
Str menu(bool machine);
Str configHome();
Str stateHome();
Str executable(const char* argv0);
uint64_t nonce();
bool administrator();
Info info(const Str& path);
Status read(const Str& path, uint64_t offset, void* buffer, size_t size, size_t& actual);
Status write(const Str& path, uint64_t offset, const void* buffer, size_t size, bool truncate = false);
Status mkdir(const Str& path, bool exclusive = false);
Status rename(const Str& from, const Str& to);
Status removeFile(const Str& path);
Status removeDirectory(const Str& path);
Status chmod(const Str& path, uint32_t mode);
Status list(const Str& path, List<Entry>& result);
Status requestAccess(const Str& path, bool write);
uint64_t nativeCapabilities();
Status nativeRead(const Json& descriptor, Json& snapshot);
Status nativeWrite(const Json& descriptor, const Json& snapshot);
Str shortcutExtension();
Status registryRead(const Json& descriptor, Json& value);
Status registryWrite(const Json& descriptor, const Json& value);
Status shortcut(const Str& output, const Str& executable, const List<Str>& arguments, const Str& working,
                const Str& icon);
Status launch(const Str& executable, const List<Str>& arguments, const Json& environment, const Str& working,
              bool machine);
void changedEnvironment();
Status refreshAssociations(bool machine);
void output(const Str& text);
bool input(Str& line);
bool interactive();
void sleep(unsigned milliseconds);
[[noreturn]] void exit(int code);
void initialize(int argc, char** argv, char** envp);

Status readText(const Str& path, Str& value, size_t limit = 16 * 1024 * 1024);
Status writeText(const Str& path, const Str& value);
Status directories(const Str& path);
Status safeAncestors(const Str& path);
Status copy(const Str& from, const Str& to, uint64_t limit = UINT64_MAX);
Status removeTree(const Str& path);
Str shellQuote(const Str& value);
Str commandQuote(const Str& value);
} // namespace ap::os
