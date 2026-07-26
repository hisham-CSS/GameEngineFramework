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

bool UITextEdit::DeleteSelection() {
    if (!hasSelection()) return false;
    const std::size_t b = selectionBegin(), e = selectionEnd();
    value_.erase(b, e - b);
    caret_ = anchor_ = b;
    return true;
}

bool UITextEdit::InsertText(const std::string& utf8) {
    if (utf8.empty()) return false;
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
    if (DeleteSelection()) return true;
    if (caret_ == 0) return false;
    const std::size_t prev = PrevBoundary(value_, caret_);
    value_.erase(prev, caret_ - prev);
    caret_ = anchor_ = prev;
    return true;
}

bool UITextEdit::DeleteForward() {
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
    caret_ = 0;
    if (!select) anchor_ = 0;
}

void UITextEdit::MoveToEnd(bool select) {
    caret_ = value_.size();
    if (!select) anchor_ = caret_;
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
    case UIKey::A:
        // Ctrl+A only. A bare 'a' is text, and it arrives as TextInput —
        // consuming it here would make the letter untypeable.
        if (!k.ctrl) return false;
        SelectAll();
        return true;
    // Deliberately NOT consumed, so they keep bubbling: Tab stays navigation,
    // Enter and Escape are for whatever contains the field to decide, and
    // Up/Down mean nothing in a single-line control.
    case UIKey::Tab:
    case UIKey::Enter:
    case UIKey::Escape:
    case UIKey::Up:
    case UIKey::Down:
    case UIKey::PageUp:
    case UIKey::PageDown:
    case UIKey::C:
    case UIKey::V:
    case UIKey::X:
    case UIKey::Z:
    case UIKey::Y:
    case UIKey::None:
    default:
        return false;
    }
}

} // namespace MyCoreEngine::ui
