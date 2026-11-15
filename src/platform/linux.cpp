#include "ap/platform.hpp"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
namespace ap::os {
namespace {
Status failure(const Str& operation, const Str& path) {
    return Status::fail(operation + ": " + path + " (" + strerror(errno) + ")");
}
} // namespace
const char* name() {
    return "linux";
}
void initialize(int, char**, char**) {}
Str environment(const Str& key) {
    const char* value = getenv(key.c_str());
    return value ? value : "";
}
bool hasEnvironment(const Str& key) {
    return getenv(key.c_str()) != nullptr;
}
Str home() {
    Str result = environment("HOME");
    if (absolutePath(result))
        return result;
    passwd* user = getpwuid(getuid());
    return user ? Str(user->pw_dir) : Str();
}
Str programs(bool machine) {
    return machine ? Str("/opt") : join(home(), ".local/opt");
}
Str configHome() {
    Str value = environment("XDG_CONFIG_HOME");
    return absolutePath(value) ? value : join(home(), ".config");
}
Str stateHome() {
    Str value = environment("XDG_STATE_HOME");
    return absolutePath(value) ? value : join(home(), ".local/state");
}
Str desktop(bool machine) {
    if (machine)
        return "/usr/local/share/applications";
    Str text;
    if (readText(join(configHome(), "user-dirs.dirs"), text, 65536)) {
        for (const Str& line : split(text, '\n')) {
            Str trimmed = line.trim();
            if (trimmed.starts("XDG_DESKTOP_DIR=\"")) {
                Str value = trimmed.substr(17);
                size_t end = value.find('"');
                if (end != size_t(-1)) {
                    value = value.substr(0, end).replace("$HOME", home());
                    if (absolutePath(value) && value.find('$') == size_t(-1) &&
                        value.find('`') == size_t(-1) && value.find('\\') == size_t(-1))
                        return value;
                }
            }
        }
    }
    return join(home(), "Desktop");
}
Str menu(bool machine) {
    Str data = environment("XDG_DATA_HOME");
    return machine ? Str("/usr/local/share/applications")
                   : join(absolutePath(data) ? data : join(home(), ".local/share"), "applications");
}
Str executable(const char* argv0) {
    char buffer[4096];
    ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (n > 0 && size_t(n) < sizeof(buffer))
        return Str(buffer, size_t(n));
    if (argv0 && absolutePath(argv0))
        return argv0;
    char cwd[4096];
    return getcwd(cwd, sizeof(cwd)) ? join(cwd, argv0 ? argv0 : "") : Str();
}
uint64_t nonce() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t(t.tv_sec) * 1000000000ULL + uint64_t(t.tv_nsec)) ^ uint64_t(getpid());
}
bool administrator() {
    return geteuid() == 0;
}
Info info(const Str& path) {
    struct stat s {};
    if (lstat(path.c_str(), &s) != 0)
        return {};
    Info out;
    out.exists = true;
    out.directory = S_ISDIR(s.st_mode);
    out.link = S_ISLNK(s.st_mode);
    out.size = uint64_t(s.st_size);
    out.mode = uint32_t(s.st_mode & 0777);
    return out;
}
Status read(const Str& path, uint64_t offset, void* buffer, size_t size, size_t& actual) {
    actual = 0;
    if (offset > INT64_MAX)
        return Status::fail("File offset exceeds platform limit");
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return failure("open", path);
    ssize_t n;
    do {
        n = pread(fd, buffer, size, off_t(offset));
    } while (n < 0 && errno == EINTR);
    int error = errno;
    close(fd);
    errno = error;
    if (n < 0)
        return failure("read", path);
    actual = size_t(n);
    return {};
}
Status write(const Str& path, uint64_t offset, const void* buffer, size_t size, bool truncate) {
    AP_TRY(safeAncestors(parent(path)));
    int fd =
        open(path.c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC | (truncate ? (O_CREAT | O_TRUNC) : 0), 0600);
    if (fd < 0)
        return failure("open for writing", path);
    size_t done = 0;
    while (done < size) {
        ssize_t n = pwrite(fd, static_cast<const char*>(buffer) + done, size - done, off_t(offset + done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            Status result = failure("write", path);
            close(fd);
            return result;
        }
        done += size_t(n);
    }
    if (fsync(fd) != 0) {
        Status result = failure("fsync", path);
        close(fd);
        return result;
    }
    if (close(fd) != 0)
        return failure("close", path);
    return {};
}
Status mkdir(const Str& path, bool exclusive) {
    if (::mkdir(path.c_str(), 0755) == 0)
        return {};
    if (!exclusive && errno == EEXIST) {
        Info s = info(path);
        if (s.directory && !s.link)
            return {};
    }
    return failure("mkdir", path);
}
Status rename(const Str& from, const Str& to) {
    AP_TRY(safeAncestors(parent(from)));
    AP_TRY(safeAncestors(parent(to)));
    if (::rename(from.c_str(), to.c_str()) != 0)
        return failure("rename", from);
    int fd = open(parent(to).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        int result = fsync(fd);
        close(fd);
        if (result != 0)
            return failure("fsync directory", parent(to));
    }
    return {};
}
Status removeFile(const Str& path) {
    if (unlink(path.c_str()) == 0 || errno == ENOENT)
        return {};
    return failure("unlink", path);
}
Status removeDirectory(const Str& path) {
    if (rmdir(path.c_str()) == 0 || errno == ENOENT)
        return {};
    return failure("rmdir", path);
}
Status chmod(const Str& path, uint32_t mode) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return failure("chmod open", path);
    int rc = fchmod(fd, mode & 0777);
    int err = errno;
    close(fd);
    errno = err;
    return rc == 0 ? Status{} : failure("chmod", path);
}
Status list(const Str& path, List<Entry>& result) {
    if (info(path).link)
        return Status::fail("Refusing linked directory");
    DIR* dir = opendir(path.c_str());
    if (!dir)
        return failure("opendir", path);
    errno = 0;
    while (dirent* entry = readdir(dir)) {
        Str n = entry->d_name;
        if (n == "." || n == "..")
            continue;
        result.add({n, info(join(path, n))});
    }
    int error = errno;
    closedir(dir);
    if (error) {
        errno = error;
        return failure("readdir", path);
    }
    return {};
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
    return ".desktop";
}
Status registryRead(const Json&, Json&) {
    return Status::fail("Registry is only available on Windows");
}
Status registryWrite(const Json&, const Json&) {
    return Status::fail("Registry is only available on Windows");
}
Status shortcut(const Str& output, const Str& executable, const List<Str>& arguments, const Str& working,
                const Str& icon) {
    auto quote = [](const Str& argument) {
        Str escaped = "\"" +
                      argument.replace("\\", "\\\\")
                          .replace("\"", "\\\"")
                          .replace("`", "\\`")
                          .replace("$", "\\$")
                          .replace("%", "%%") +
                      "\"";
        // Desktop-entry string escaping is applied before Exec argument parsing.
        return escaped.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t");
    };
    Str exec = quote(executable);
    for (const auto& arg : arguments)
        exec.append(" " + quote(arg));
    Str text =
        "[Desktop Entry]\nType=Application\nName=" + leaf(output).replace(".desktop", "").replace("\n", " ") +
        "\nExec=" + exec + "\nTerminal=false\n";
    if (!working.empty())
        text.append("Path=" + working.replace("\\", "\\\\").replace("\n", " ") + "\n");
    if (!icon.empty())
        text.append("Icon=" + icon.replace("\\", "\\\\").replace("\n", " ") + "\n");
    AP_TRY(writeText(output, text));
    return chmod(output, 0755);
}
Status launch(const Str& path, const List<Str>& args, const Json& env, const Str& working, bool machine) {
    if (machine && administrator() && environment("SUDO_UID").empty())
        return Status::fail("Launch the application from your normal user session");
    pid_t pid = fork();
    if (pid < 0)
        return failure("fork", path);
    if (pid == 0) {
        if (machine && geteuid() == 0) {
            uint64_t uid = 0, gid = 0;
            if (!unsignedNumber(environment("SUDO_UID"), uid) ||
                !unsignedNumber(environment("SUDO_GID"), gid) || uid == 0 || setgroups(0, nullptr) != 0 ||
                setgid(gid_t(gid)) != 0 || setuid(uid_t(uid)) != 0)
                _exit(126);
        }
        for (size_t i = 0; i < env.keys.size(); ++i)
            setenv(env.keys[i].c_str(), env.values[i].text.c_str(), 1);
        if (!working.empty() && chdir(working.c_str()) != 0)
            _exit(126);
        List<char*> argv;
        argv.add(const_cast<char*>(path.c_str()));
        for (const auto& arg : args)
            argv.add(const_cast<char*>(arg.c_str()));
        argv.add(nullptr);
        execv(path.c_str(), argv.begin());
        _exit(127);
    }
    return {};
}
void changedEnvironment() {}
Status refreshAssociations(bool machine) {
    Str mime = join(parent(menu(machine)), "mime");
    const char* commands[] = {"/usr/bin/update-mime-database", "/usr/bin/update-desktop-database"};
    for (unsigned i = 0; i < 2; ++i) {
        if (!info(commands[i]).exists) {
            if (i == 0)
                return Status::fail("MIME registration needs /usr/bin/update-mime-database");
            continue;
        }
        Str directory = i == 0 ? mime : menu(machine);
        pid_t pid = fork();
        if (pid < 0)
            return failure("fork", commands[i]);
        if (pid == 0) {
            execl(commands[i], commands[i], directory.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR)
                return failure("waitpid", commands[i]);
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return Status::fail("Could not refresh application associations: " + Str(commands[i]));
    }
    return {};
}
void output(const Str& text) {
    size_t at = 0;
    while (at < text.size()) {
        ssize_t n = ::write(STDOUT_FILENO, text.c_str() + at, text.size() - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        at += size_t(n);
    }
}
bool input(Str& line) {
    line.clear();
    char c;
    while (line.size() < 4096) {
        ssize_t n = ::read(STDIN_FILENO, &c, 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return !line.empty();
        if (c == '\n')
            return true;
        if (c != '\r')
            line.push(c);
    }
    return false;
}
bool interactive() {
    return isatty(STDIN_FILENO);
}
void sleep(unsigned milliseconds) {
    timespec t{milliseconds / 1000, long(milliseconds % 1000) * 1000000};
    while (nanosleep(&t, &t) < 0 && errno == EINTR) {
    }
}
[[noreturn]] void exit(int code) {
    ::exit(code);
}
} // namespace ap::os
