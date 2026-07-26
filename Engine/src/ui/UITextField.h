#pragma once
// Text entry: the editing model behind a `<TextField>` element.
//
// Deliberately a SEPARATE object rather than more fields on UIElement. A text
// field is the only element that carries mutable state of its own (a caret, a
// selection, a scroll offset), and folding that into every element would put
// four fields on every label in the tree to serve one control.
//
// EVERY offset here is a BYTE offset into UTF-8, never a character index. That
// is the only representation that can be handed to the renderer and the font
// without converting, and the editing operations all move by whole codepoints,
// so a caret can never land inside a multi-byte character. Getting this wrong
// is how text fields corrupt non-ASCII input.
#include "../core/Core.h"
#include "UIEvent.h"

#include <cstddef>
#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    class UIElement;

    class ENGINE_API UITextEdit {
    public:
        // The edited string. Setting it clamps the caret and selection into
        // range, so an external write (a binding, gameplay) can never leave the
        // caret pointing into the middle of a character or past the end.
        const std::string& value() const { return value_; }
        void setValue(std::string v);

        // Byte offsets. `caret` is where typing inserts; `anchor` is the other
        // end of the selection, equal to caret when nothing is selected.
        std::size_t caret() const { return caret_; }
        std::size_t anchor() const { return anchor_; }
        bool hasSelection() const { return caret_ != anchor_; }
        std::size_t selectionBegin() const { return caret_ < anchor_ ? caret_ : anchor_; }
        std::size_t selectionEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }
        std::string selectedText() const;

        // Longest accepted value in BYTES, 0 = unlimited. Bytes rather than
        // characters because the limit exists to bound memory and layout, and
        // because a byte limit is the one a caller can reason about without
        // knowing the encoding of what the user will type.
        void setMaxLength(std::size_t bytes) { maxBytes_ = bytes; }
        std::size_t maxLength() const { return maxBytes_; }

        // Multi-line: Enter inserts a newline and is CONSUMED, and Up/Down move
        // between lines. Tab still navigates — a field that trapped Tab would
        // strand a keyboard user, and every web textarea agrees.
        void setMultiline(bool m) { multiline_ = m; }
        bool multiline() const { return multiline_; }

        // Renders as this character repeated, for passwords. Empty = off.
        // Stored as UTF-8 so it can be any glyph the font has.
        void setMaskCharacter(std::string utf8) { mask_ = std::move(utf8); }
        const std::string& maskCharacter() const { return mask_; }
        // What should actually be DRAWN — the value, or the mask repeated once
        // per codepoint. Never log or bind this expecting the real text.
        std::string displayText() const;

        // ---- editing. Each returns true if the VALUE changed, which is what
        // decides whether a ValueChanged event fires. ----
        bool InsertText(const std::string& utf8);   // replaces any selection
        bool Backspace();                           // deletes selection, else the codepoint before
        bool DeleteForward();                       // deletes selection, else the codepoint after
        bool DeleteSelection();

        // Caret movement. `select` extends the selection instead of collapsing
        // it, which is what Shift+arrow means everywhere.
        void MoveLeft(bool select);
        void MoveRight(bool select);
        // Home/End go to the start/end of the LINE in a multi-line field and of
        // the whole value in a single-line one, which is what both look like to
        // the user in each case.
        void MoveToStart(bool select);
        void MoveToEnd(bool select);
        // Multi-line only; a no-op otherwise. Keeps the caret near the same
        // horizontal position in bytes, which is the honest approximation
        // without measuring text here.
        void MoveUp(bool select);
        void MoveDown(bool select);
        void SelectAll();
        void SetCaret(std::size_t byteOffset, bool select = false);

        // ---- undo ----
        // A bounded snapshot stack. Consecutive typing COALESCES into one entry
        // (undoing letter by letter is nobody's idea of undo) while a deletion
        // or a caret jump always starts a new one.
        bool Undo();
        bool Redo();
        bool canUndo() const { return !undo_.empty(); }
        bool canRedo() const { return !redo_.empty(); }

        // ---- line helpers, shared with the renderer ----
        // Byte offset of the start of the line containing `at`, and of its end
        // (the '\n', or the end of the value).
        static std::size_t LineStart(const std::string& s, std::size_t at);
        static std::size_t LineEnd(const std::string& s, std::size_t at);
        // 0-based line index of `at`.
        static std::size_t LineIndexOf(const std::string& s, std::size_t at);

        // Handles one key against this field. Returns true if the key was
        // CONSUMED, so the caller can stop it bubbling — which is how a field
        // keeps its own Left/Right without the document treating them as
        // anything else.
        bool HandleKey(const UIKeyEvent& k, bool& valueChanged);

        // ---- UTF-8 navigation, exposed because the renderer needs them too ----
        // Byte offset of the codepoint boundary before/after `at`. Clamped, and
        // safe on any input including malformed UTF-8 (it steps one byte rather
        // than looping forever).
        static std::size_t PrevBoundary(const std::string& s, std::size_t at);
        static std::size_t NextBoundary(const std::string& s, std::size_t at);
        // Rounds an arbitrary offset DOWN onto a boundary.
        static std::size_t ClampToBoundary(const std::string& s, std::size_t at);

    private:
        struct Snapshot {
            std::string value;
            std::size_t caret = 0, anchor = 0;
        };
        void clampCaret_();
        // Records the CURRENT state before a change. `coalesce` merges into the
        // previous entry when the last change was also a coalescing one, which
        // is what turns a burst of typing into a single undo step.
        void pushUndo_(bool coalesce);

        std::string value_;
        std::string mask_;
        std::size_t caret_ = 0;
        std::size_t anchor_ = 0;
        std::size_t maxBytes_ = 0;
        bool        multiline_ = false;

        std::vector<Snapshot> undo_, redo_;
        bool lastWasTyping_ = false;
        // Bounded: a text field is not a document editor, and an unbounded
        // history on a long-lived HUD is a slow leak.
        static constexpr std::size_t kMaxUndo = 64;
    };

} // namespace MyCoreEngine::ui
