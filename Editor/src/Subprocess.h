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
    //
    // This is the SIBLING-BINARY form and it stays that way: the AssetCooker is
    // built into the same directory as the editor, and pinning it to the CWD is
    // deliberate -- a PATH search could find some other program of that name.
    static Subprocess Spawn(const std::vector<std::string>& argv);

    // Launch a PROGRAM, resolved the way the OS resolves programs: argv[0] may
    // be an absolute path, a relative path containing a separator, or a bare
    // name to be found on PATH. Otherwise identical to Spawn.
    //
    // WHY THIS EXISTS AS A SECOND ENTRY POINT rather than a change to Spawn.
    // The build pipeline (Engine/src/core/BuildPipeline.h) launches `cmake`,
    // which lives on PATH or at an absolute path recorded at configure time, and
    // NOT beside the editor. Under Spawn's rule that becomes ".\cmake.exe",
    // which fails with "is cmake.exe built?" -- a message that sends the reader
    // hunting for a target that does not exist. But Spawn's rule is right for
    // its own caller, so the two resolutions are two functions rather than one
    // with a flag nobody would remember to pass.
    //
    // A BARE NAME MEANS DIFFERENT THINGS ON THE TWO PLATFORMS and the difference
    // is worth knowing before relying on it: on Windows, CreateProcess searches
    // the calling process's directory FIRST and then the PATH, so a bare name
    // finds a sibling binary as well; on POSIX this uses posix_spawnp, which
    // searches PATH ONLY -- the current directory is not on it. A caller that
    // wants a sibling on both platforms must pass a path, not a name.
    static Subprocess SpawnProgram(const std::vector<std::string>& argv);

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
    // Both entry points above, which differ ONLY in how argv[0] is resolved.
    // `sibling` true is Spawn's rule (".\argv0.exe" / "./argv0", pinned to the
    // CWD); false is SpawnProgram's (argv[0] untouched, resolved by the OS).
    // One function because everything else -- the pipe, the inherit flags, the
    // stdout redirection, the error handling -- is identical, and this file's
    // comments record two real bugs in that shared half that must not be
    // maintained in two copies.
    static Subprocess spawn_(const std::vector<std::string>& argv, bool sibling);

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
