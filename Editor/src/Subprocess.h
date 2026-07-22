#pragma once
// A child process with its stdout captured through a pipe, killable while it
// runs. Used for the AssetCooker validation run: we keep the process handle so
// a hung cooker can be killed (crash AND hang isolation), and drain stdout on a
// dedicated reader thread. This is the one piece of the editor that needs raw
// OS process APIs, so it lives behind this seam -- Windows uses
// CreateProcess + anonymous pipe, POSIX uses posix_spawn + pipe(). The header
// stays free of <windows.h>/<spawn.h> so the rest of the editor includes it
// cleanly on both platforms.
#include <string>
#include <vector>

namespace editor {

class Subprocess {
public:
    // Launch argv[0] resolved against the current working directory (as
    // "./argv0" -- immune to PATH search), with the given argument vector, and
    // capture its stdout. stderr is inherited (stays on the editor console).
    // On failure returns an object with ok()==false and a reason in error().
    static Subprocess Spawn(const std::vector<std::string>& argv);

    Subprocess() = default;
    ~Subprocess();
    Subprocess(Subprocess&&) noexcept;
    Subprocess& operator=(Subprocess&&) noexcept;
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;

    bool ok() const { return running_; }
    const std::string& error() const { return error_; }

    // Blocking: append the next chunk of the child's stdout to `out`. Returns
    // false at end-of-stream (child closed stdout / exited / was killed).
    bool readChunk(std::string& out);

    // Reap the child and return its exit code. Call once, after readChunk()
    // has returned false. Idempotent-safe (returns the cached code if already
    // reaped).
    int wait();

    // Force-kill the child (hung-run path). EOFs the pipe so a blocked
    // readChunk() returns; safe to call once.
    void kill();

private:
    void closeAll_();          // release OS handles/fds
    bool  running_ = false;
    bool  reaped_ = false;
    int   exitCode_ = 0;
    std::string error_;
#if defined(_WIN32)
    void* proc_ = nullptr;     // HANDLE to the child process
    void* readPipe_ = nullptr; // HANDLE, read end of the stdout pipe
#else
    int   pid_ = -1;
    int   readFd_ = -1;
#endif
};

} // namespace editor
