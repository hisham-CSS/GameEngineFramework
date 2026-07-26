#include "UITextField.h"

#include "../render2d/Font.h"

#include <algorithm>

namespace MyCoreEngine::ui {

namespace {
    // A UTF-8 continuation byte is 10xxxxxx; every other byte starts a
    // codepoint. That single rule is all the navigation below needs.
    bool isContinuation(unsigned char c) { return (c & 0xC0u) == 0x80u; }
} // namespace

std::size_t UITextEdit::PrevBoundary(const std::string& s, std::size_t at) {
    if (at == 0) return 0;
    if (at > s.size()) at = s.size();
    std::size_t i = at - 1;
    // Bounded by construction: `i` strictly decreases and stops at 0, so
    // malformed input (a run of continuation bytes with no lead) steps back to
    // the start rather than looping.
    while (i > 0 && isContinuation((unsigned char)s[i])) --i;
    return i;
}

std::size_t UITextEdit::NextBoundary(const std::string& s, std::size_t at) {
    if (at >= s.size()) return s.size();
    std::size_t i = at + 1;
    while (i < s.size() && isContinuation((unsigned char)s[i])) ++i;
    return i;
}

std::size_t UITextEdit::ClampToBoundary(const std::string& s, std::size_t at) {
    if (at >= s.size()) return s.size();
    while (at > 0 && isContinuation((unsigned char)s[at])) --at;
    return at;
}

void UITextEdit::clampCaret_() {
    caret_ = ClampToBoundary(value_, std::min(caret_, value_.size()));
    anchor_ = ClampToBoundary(value_, std::min(anchor_, value_.size()));
}

void UITextEdit::setValue(std::string v) {
    // An external write (a binding, gameplay) is not an edit the user made, so
    // it clears the history rather than becoming an undo step they never did.
    undo_.clear();
    redo_.clear();
    lastWasTyping_ = false;
    value_ = std::move(v);
    if (maxBytes_ && value_.size() > maxBytes_) {
        // Truncate on a BOUNDARY, or the tail becomes a broken character.
        value_.resize(ClampToBoundary(value_, maxBytes_));
    }
    clampCaret_();
}

std::string UITextEdit::selectedText() const {
    if (!hasSelection()) return {};
    return value_.substr(selectionBegin(), selectionEnd() - selectionBegin());
}

std::string UITextEdit::displayText() const {
    if (mask_.empty()) return value_;
    // One mask glyph per CODEPOINT, not per byte: a masked field must not leak
    // the length of the underlying bytes for non-ASCII input.
    std::string out;
    for (std::size_t i = 0; i < value_.size(); i = NextBoundary(value_, i)) out += mask_;
    return out;
}

std::size_t UITextEdit::LineStart(const std::string& s, std::size_t at) {
    if (at > s.size()) at = s.size();
    const std::size_t nl = s.rfind('\n', at ? at - 1 : 0);
    return (nl == std::string::npos || at == 0) ? 0 : nl + 1;
}

std::size_t UITextEdit::LineEnd(const std::string& s, std::size_t at) {
    const std::size_t nl = s.find('\n', std::min(at, s.size()));
    return nl == std::string::npos ? s.size() : nl;
}

std::size_t UITextEdit::LineIndexOf(const std::string& s, std::size_t at) {
    if (at > s.size()) at = s.size();
    return std::size_t(std::count(s.begin(), s.begin() + at, '\n'));
}

void UITextEdit::pushUndo_(bool coalesce) {
    // A run of typing is ONE undo step: undoing letter by letter is nobody's
    // idea of undo. Anything else (a delete, a paste, a caret jump) breaks the
    // run so it lands as its own entry.
    if (coalesce && lastWasTyping_ && !undo_.empty()) {
        lastWasTyping_ = true;
        redo_.clear();
        return;
    }
    undo_.push_back(Snapshot{ value_, caret_, anchor_ });
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
    lastWasTyping_ = coalesce;
}

bool UITextEdit::Undo() {
    if (undo_.empty()) return false;
    redo_.push_back(Snapshot{ value_, caret_, anchor_ });
    const Snapshot s = undo_.back();
    undo_.pop_back();
    value_ = s.value;
    caret_ = s.caret;
    anchor_ = s.anchor;
    clampCaret_();
    lastWasTyping_ = false;   // typing after an undo starts a new run
    return true;
}

bool UITextEdit::Redo() {
    if (redo_.empty()) return false;
    undo_.push_back(Snapshot{ value_, caret_, anchor_ });
    const Snapshot s = redo_.back();
    redo_.pop_back();
    value_ = s.value;
    caret_ = s.caret;
    anchor_ = s.anchor;
    clampCaret_();
    lastWasTyping_ = false;
    return true;
}

bool UITextEdit::DeleteSelection() {
    if (!hasSelection()) return false;
    const std::size_t b = selectionBegin(), e = selectionEnd();
    value_.erase(b, e - b);
    caret_ = anchor_ = b;
    return true;
}

bool UITextEdit::InsertText(const std::string& utf8) {
    if (utf8.empty()) return false;
    // A selection being replaced is a distinct action, so it breaks the run.
    pushUndo_(/*coalesce=*/!hasSelection());
    bool changed = DeleteSelection();
    std::string add = utf8;
    if (maxBytes_) {
        if (value_.size() >= maxBytes_) return changed;
        const std::size_t room = maxBytes_ - value_.size();
        if (add.size() > room) add.resize(ClampToBoundary(add, room));
        if (add.empty()) return changed;
    }
    value_.insert(caret_, add);
    caret_ += add.size();
    anchor_ = caret_;
    return true;
}

bool UITextEdit::Backspace() {
    if (caret_ == 0 && !hasSelection()) return false;
    pushUndo_(/*coalesce=*/false);
    if (DeleteSelection()) return true;
    if (caret_ == 0) return false;
    const std::size_t prev = PrevBoundary(value_, caret_);
    value_.erase(prev, caret_ - prev);
    caret_ = anchor_ = prev;
    return true;
}

bool UITextEdit::DeleteForward() {
    if (caret_ >= value_.size() && !hasSelection()) return false;
    pushUndo_(/*coalesce=*/false);
    if (DeleteSelection()) return true;
    if (caret_ >= value_.size()) return false;
    const std::size_t next = NextBoundary(value_, caret_);
    value_.erase(caret_, next - caret_);
    anchor_ = caret_;
    return true;
}

void UITextEdit::SetCaret(std::size_t byteOffset, bool select) {
    caret_ = ClampToBoundary(value_, std::min(byteOffset, value_.size()));
    if (!select) anchor_ = caret_;
}

void UITextEdit::MoveLeft(bool select) {
    // Without Shift, an existing selection COLLAPSES to its near edge rather
    // than moving from the caret — that is what every editor does, and it is
    // why this is not simply "move the caret one step".
    if (!select && hasSelection()) { caret_ = anchor_ = selectionBegin(); return; }
    caret_ = PrevBoundary(value_, caret_);
    if (!select) anchor_ = caret_;
}

void UITextEdit::MoveRight(bool select) {
    if (!select && hasSelection()) { caret_ = anchor_ = selectionEnd(); return; }
    caret_ = NextBoundary(value_, caret_);
    if (!select) anchor_ = caret_;
}

void UITextEdit::MoveToStart(bool select) {
    // The start of the LINE when there are lines, which is what Home means to
    // anyone who has used a text editor.
    caret_ = multiline_ ? LineStart(value_, caret_) : 0;
    if (!select) anchor_ = caret_;
    lastWasTyping_ = false;
}

void UITextEdit::MoveToEnd(bool select) {
    caret_ = multiline_ ? LineEnd(value_, caret_) : value_.size();
    if (!select) anchor_ = caret_;
    lastWasTyping_ = false;
}

void UITextEdit::MoveUp(bool select) {
    if (!multiline_) return;
    const std::size_t start = LineStart(value_, caret_);
    if (start == 0) { caret_ = 0; if (!select) anchor_ = caret_; return; }
    const std::size_t column = caret_ - start;
    const std::size_t prevStart = LineStart(value_, start - 1);
    const std::size_t prevEnd = start - 1;      // the newline that ended it
    caret_ = ClampToBoundary(value_, std::min(prevStart + column, prevEnd));
    if (!select) anchor_ = caret_;
    lastWasTyping_ = false;
}

void UITextEdit::MoveDown(bool select) {
    if (!multiline_) return;
    const std::size_t start = LineStart(value_, caret_);
    const std::size_t end = LineEnd(value_, caret_);
    if (end >= value_.size()) { caret_ = value_.size(); if (!select) anchor_ = caret_; return; }
    const std::size_t column = caret_ - start;
    const std::size_t nextStart = end + 1;
    const std::size_t nextEnd = LineEnd(value_, nextStart);
    caret_ = ClampToBoundary(value_, std::min(nextStart + column, nextEnd));
    if (!select) anchor_ = caret_;
    lastWasTyping_ = false;
}

void UITextEdit::SelectAll() {
    anchor_ = 0;
    caret_ = value_.size();
}

bool UITextEdit::HandleKey(const UIKeyEvent& k, bool& valueChanged) {
    valueChanged = false;
    switch (k.key) {
    case UIKey::Backspace: valueChanged = Backspace();      return true;
    case UIKey::Delete:    valueChanged = DeleteForward();  return true;
    case UIKey::Left:      MoveLeft(k.shift);               return true;
    case UIKey::Right:     MoveRight(k.shift);              return true;
    case UIKey::Home:      MoveToStart(k.shift);            return true;
    case UIKey::End:       MoveToEnd(k.shift);              return true;
    case UIKey::Up:        if (!multiline_) return false; MoveUp(k.shift);   return true;
    case UIKey::Down:      if (!multiline_) return false; MoveDown(k.shift); return true;
    case UIKey::Enter:
        // Consumed ONLY by a multi-line field. In a single-line one Enter is
        // "submit", which belongs to whatever contains the field.
        if (!multiline_ || k.ctrl) return false;
        valueChanged = InsertText("\n");
        return true;
    case UIKey::Z:
        if (!k.ctrl) return false;
        // Ctrl+Shift+Z is redo on every platform that also has Ctrl+Y.
        valueChanged = k.shift ? Redo() : Undo();
        return true;
    case UIKey::Y:
        if (!k.ctrl) return false;
        valueChanged = Redo();
        return true;
    case UIKey::A:
        // Ctrl+A only. A bare 'a' is text, and it arrives as TextInput —
        // consuming it here would make the letter untypeable.
        if (!k.ctrl) return false;
        SelectAll();
        return true;
    // Deliberately NOT consumed, so they keep bubbling: Tab stays navigation,
    // Enter and Escape are for whatever contains the field to decide, and
    // Up/Down mean nothing in a single-line control.
    // Deliberately NOT consumed. Tab stays navigation even here — a field that
    // trapped it would strand a keyboard user, and every web textarea agrees.
    // Clipboard keys are handled by UIDocument, which is where the host's
    // clipboard hooks live.
    case UIKey::Tab:
    case UIKey::Escape:
    case UIKey::PageUp:
    case UIKey::PageDown:
    case UIKey::C:
    case UIKey::V:
    case UIKey::X:
    case UIKey::None:
    default:
        return false;
    }
}

} // namespace MyCoreEngine::ui
