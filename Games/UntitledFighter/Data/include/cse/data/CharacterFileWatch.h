// Notices when a character file on disk has changed, so a host can land the
// edit in a running match (ROADMAP M1.5). WHAT landing means is ADR-016's
// decision and is not decided here: this class only answers "did the file
// change since I last said so", and the caller restarts the match through its
// one existing start path. It must never be used to justify swapping a
// MatchData under a live session -- FightSession.h's Restore contract and the
// replay header's single matchDataHash both forbid that, and the ADR says why.
//
// The stamp is (mtime, size), BOTH halves, copied from the UI toolkit's
// UIAssetDocument -- the in-repo precedent this deliberately mirrors rather
// than improves on. mtime alone misses a save that lands on the same
// filesystem tick as the last one, which is exactly what saving twice while
// iterating produces; size alone misses the common edit that changes a number
// in place. tests/test_ui_hotreload.cpp pins the same pair one subsystem over.
//
// A report REFRESHES the stamps even though this class cannot know whether the
// caller's reload will succeed. That is the keep-last-good half of the
// authoring loop: a half-typed file is the NORMAL state while editing, and
// stamps that only refreshed on a successful reload would re-report the same
// broken save every poll -- while stamps that latched off on failure would
// make one typo kill hot reload for the rest of the session. Report once,
// stay quiet, and the save that fixes the file reports again.
#pragma once

#include <string>

namespace cse::data {

class CharacterFileWatch {
public:
    // Resolves baseDir/relPath through the same containment gate as every
    // other authored read (PathIsContained: absolute paths, drive/UNC roots
    // and `..` are refused before the filesystem is touched) and takes the
    // starting stamp. A MISSING file binds successfully on purpose: the mode
    // binds after a load that may have failed, and the file APPEARING -- the
    // content getting staged, say -- is a change the caller wants to hear
    // about, because it is the one that revives a dead match screen.
    bool Bind(const std::string& baseDir, const std::string& relPath,
              std::string& error);

    // Accumulates dt and stats the file at most once per poll interval.
    // Returns true when the file changed since the last report (or since
    // Bind), refreshing the stamps as it says so -- see the header note for
    // why refresh-on-report rather than refresh-on-successful-reload.
    bool Update(float dt);

    // 0 polls on every call; tests use that so "time passed" is not simulated
    // with real sleeps. The default matches the UI toolkit's 0.25 s.
    void SetPollInterval(float seconds) { pollInterval_ = seconds; }

    bool Bound() const { return bound_; }

private:
    struct Stamp {
        long long          writeTime = 0;
        unsigned long long size      = 0;
        bool operator==(const Stamp& o) const {
            return writeTime == o.writeTime && size == o.size;
        }
    };
    Stamp stampNow_() const;

    std::string full_;              // resolved path; empty until Bind succeeds
    bool        bound_        = false;
    float       pollInterval_ = 0.25f;
    float       accum_        = 0.0f;
    Stamp       stamp_{};
};

} // namespace cse::data
