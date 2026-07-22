#include "Subprocess.h"

#include <utility> // std::exchange

// ===========================================================================
// Windows: CreateProcess + anonymous pipe, keep the process HANDLE to kill.
// ===========================================================================
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace editor {

Subprocess Subprocess::Spawn(const std::vector<std::string>& argv) {
    Subprocess s;
    if (argv.empty()) { s.error_ = "empty argv"; return s; }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        s.error_ = "CreatePipe failed";
        return s;
    }
    // Our read end must NOT be inherited by the child (only its write end is).
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    // ".\argv0 arg1 arg2 ...": the leading .\ resolves against the CWD (the exe
    // dir), immune to PATH-search rules like NoDefaultCurrentDirectoryInExePath.
    std::string cmd = ".\\" + argv[0] + ".exe";
    for (size_t i = 1; i < argv.size(); ++i) cmd += " " + argv[i];
    std::vector<char> mut(cmd.begin(), cmd.end());
    mut.push_back('\0');

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessA(nullptr, mut.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writeEnd); // ours closes now, so EOF arrives when the child exits
    if (!ok) {
        CloseHandle(readEnd);
        s.error_ = "CreateProcess failed (is " + argv[0] + ".exe built?)";
        return s;
    }
    CloseHandle(pi.hThread);

    s.proc_ = pi.hProcess;
    s.readPipe_ = readEnd;
    s.running_ = true;
    return s;
}

bool Subprocess::readChunk(std::string& out) {
    if (!readPipe_) return false;
    char buf[512];
    DWORD got = 0;
    if (ReadFile(readPipe_, buf, sizeof(buf), &got, nullptr) && got > 0) {
        out.append(buf, got);
        return true;
    }
    return false;
}

int Subprocess::wait() {
    if (reaped_) return exitCode_;
    if (proc_) {
        WaitForSingleObject(proc_, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(proc_, &code);
        exitCode_ = static_cast<int>(code);
    }
    reaped_ = true;
    return exitCode_;
}

void Subprocess::kill() {
    if (proc_ && !reaped_) TerminateProcess(proc_, 1);
}

void Subprocess::closeAll_() {
    if (readPipe_) { CloseHandle(readPipe_); readPipe_ = nullptr; }
    if (proc_)     { CloseHandle(proc_);     proc_ = nullptr; }
    running_ = false;
}

// ===========================================================================
// POSIX: posix_spawn + pipe(), keep the pid to kill(SIGKILL).
// ===========================================================================
#else

#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <cerrno>
#include <cstring>

extern char** environ;

namespace editor {

Subprocess Subprocess::Spawn(const std::vector<std::string>& argv) {
    Subprocess s;
    if (argv.empty()) { s.error_ = "empty argv"; return s; }

    int fds[2];
    if (pipe(fds) != 0) {
        s.error_ = std::string("pipe failed: ") + std::strerror(errno);
        return s;
    }
    const int readFd = fds[0], writeFd = fds[1];

    // Child stdout -> pipe write end; then close both inherited pipe fds in the
    // child (the dup2 target survives). stderr/stdin are left inherited.
    // dup the write end onto the child's stdout, then close the inherited pipe
    // fds -- but NEVER close STDOUT_FILENO itself: if the editor was launched
    // with fd 1 closed, pipe() hands back fd 1 as one of the ends, and an
    // unguarded addclose(fd1) would shut the child's stdout (the parent then
    // reads instant EOF and the report comes back silently empty).
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, writeFd, STDOUT_FILENO);
    if (readFd  != STDOUT_FILENO) posix_spawn_file_actions_addclose(&fa, readFd);
    if (writeFd != STDOUT_FILENO) posix_spawn_file_actions_addclose(&fa, writeFd);

    // "./argv0 arg1 ...": explicit relative path (has a slash) so posix_spawn
    // execs it directly from the CWD without a PATH search.
    const std::string path = "./" + argv[0];
    std::vector<std::string> store = argv;
    store[0] = path;
    std::vector<char*> cargv;
    for (auto& a : store) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, path.c_str(), &fa, nullptr,
                               cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(writeFd); // ours closes now, so EOF arrives when the child exits
    if (rc != 0) {
        close(readFd);
        s.error_ = std::string("posix_spawn failed (is ") + argv[0] +
                   " built?): " + std::strerror(rc);
        return s;
    }

    s.pid_ = pid;
    s.readFd_ = readFd;
    s.running_ = true;
    return s;
}

bool Subprocess::readChunk(std::string& out) {
    if (readFd_ < 0) return false;
    char buf[512];
    ssize_t got;
    // Retry on EINTR; return the first real chunk, or false at EOF/error.
    do { got = ::read(readFd_, buf, sizeof(buf)); } while (got < 0 && errno == EINTR);
    if (got > 0) {
        out.append(buf, static_cast<size_t>(got));
        return true;
    }
    return false;
}

int Subprocess::wait() {
    if (reaped_) return exitCode_;
    if (pid_ > 0) {
        int status = 0;
        pid_t r;
        do { r = ::waitpid(pid_, &status, 0); } while (r < 0 && errno == EINTR);
        if (r == pid_) {
            if (WIFEXITED(status))        exitCode_ = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) exitCode_ = 128 + WTERMSIG(status);
        }
    }
    // Reaped: the kernel may now recycle this pid, so never signal it again.
    pid_ = -1;
    reaped_ = true;
    return exitCode_;
}

void Subprocess::kill() {
    // Guard on !reaped_ so a kill can never target a pid waitpid() already
    // reaped (which the kernel could have reassigned to an unrelated process).
    if (pid_ > 0 && !reaped_) ::kill(pid_, SIGKILL);
}

void Subprocess::closeAll_() {
    if (readFd_ >= 0) { ::close(readFd_); readFd_ = -1; }
    // pid_ is reaped by wait(); nothing to close here.
    running_ = false;
}

#endif // platform

// ===========================================================================
// Shared: move semantics + destructor (member set differs per platform, so the
// moves are written per-platform-member but the logic is identical).
// ===========================================================================
Subprocess::~Subprocess() { closeAll_(); }

Subprocess::Subprocess(Subprocess&& o) noexcept {
    *this = std::move(o);
}

Subprocess& Subprocess::operator=(Subprocess&& o) noexcept {
    if (this == &o) return *this;
    closeAll_();
    running_  = std::exchange(o.running_, false);
    reaped_   = std::exchange(o.reaped_, false);
    exitCode_ = std::exchange(o.exitCode_, 0);
    error_    = std::move(o.error_);
#if defined(_WIN32)
    proc_     = std::exchange(o.proc_, nullptr);
    readPipe_ = std::exchange(o.readPipe_, nullptr);
#else
    pid_      = std::exchange(o.pid_, -1);
    readFd_   = std::exchange(o.readFd_, -1);
#endif
    return *this;
}

} // namespace editor
