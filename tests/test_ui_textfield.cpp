// Text entry.
//
// Pure CPU. Nearly all of this is about UTF-8: every offset is a BYTE offset,
// and every operation must move by whole codepoints, or a caret lands inside a
// multi-byte character and the next edit produces a corrupt string. That is the
// classic way text fields break on anything but ASCII, so the é / € / emoji
// cases below are the point of the file rather than decoration.
//
// The rest is editor conventions people notice immediately when they are wrong:
// an unshifted arrow COLLAPSES a selection rather than moving from the caret,
// typing replaces a selection, and the caret is solid the instant you type.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UIMarkup.h"
#include "../Engine/src/ui/UIStyleSheet.h"
#include "../Engine/src/ui/UITextField.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

// "aé€" — 1, 2 and 3 bytes, so a byte offset and a character index disagree at
// every position after the first.
const char* kMixed = "a\xC3\xA9\xE2\x82\xAC";

struct FieldDoc {
    UIDocument doc;
    std::vector<std::string> errors;

    explicit FieldDoc(const std::string& markup =
        R"(<UI><TextField name="f" style="width: 200px; height: 30px"/></UI>)") {
        UIMarkup::LoadInto(doc, markup, errors, "t.cxml");
        UIStyleSheet sheet;
        sheet.ApplyTo(doc.root());
        doc.Layout(400.f, 400.f);
    }
    UIElement* f() { return doc.root().Find("f"); }
    UITextEdit* edit() { return f() ? f()->textEdit() : nullptr; }

    void Key(UIKey k, bool shift = false, bool ctrl = false) {
        UIKeyboardState kb;
        UIKeyEvent e;
        e.key = k; e.shift = shift; e.ctrl = ctrl;
        kb.keys.push_back(e);
        doc.UpdateKeyboard(kb);
    }
    void Type(const std::string& s) {
        UIKeyboardState kb;
        kb.text = s;
        doc.UpdateKeyboard(kb);
    }
};

} // namespace

// ------------------------------------------------------------ UTF-8 movement

TEST(UITextEdit, BoundariesStepWholeCodepoints) {
    const std::string s = kMixed;   // a=1 byte, é=2, €=3  -> size 6
    ASSERT_EQ(s.size(), 6u);

    EXPECT_EQ(UITextEdit::NextBoundary(s, 0), 1u);
    EXPECT_EQ(UITextEdit::NextBoundary(s, 1), 3u);
    EXPECT_EQ(UITextEdit::NextBoundary(s, 3), 6u);
    EXPECT_EQ(UITextEdit::NextBoundary(s, 6), 6u) << "must not run off the end";

    EXPECT_EQ(UITextEdit::PrevBoundary(s, 6), 3u);
    EXPECT_EQ(UITextEdit::PrevBoundary(s, 3), 1u);
    EXPECT_EQ(UITextEdit::PrevBoundary(s, 1), 0u);
    EXPECT_EQ(UITextEdit::PrevBoundary(s, 0), 0u);

    // An offset landing INSIDE a character rounds down onto its start.
    EXPECT_EQ(UITextEdit::ClampToBoundary(s, 2), 1u);
    EXPECT_EQ(UITextEdit::ClampToBoundary(s, 4), 3u);
    EXPECT_EQ(UITextEdit::ClampToBoundary(s, 5), 3u);
}

// Malformed input must terminate, not loop: a run of continuation bytes with no
// lead byte is exactly what a truncated network string looks like.
TEST(UITextEdit, BoundariesTerminateOnMalformedInput) {
    const std::string junk = "\x80\x80\x80";
    EXPECT_EQ(UITextEdit::PrevBoundary(junk, 3), 0u);
    EXPECT_EQ(UITextEdit::NextBoundary(junk, 0), 3u);
    EXPECT_EQ(UITextEdit::ClampToBoundary(junk, 2), 0u);
    SUCCEED();
}

TEST(UITextEdit, ArrowsMoveByCharacterNotByByte) {
    UITextEdit e;
    e.setValue(kMixed);
    e.MoveToStart(false);
    e.MoveRight(false); EXPECT_EQ(e.caret(), 1u) << "past 'a'";
    e.MoveRight(false); EXPECT_EQ(e.caret(), 3u) << "past 'e-acute'";
    e.MoveRight(false); EXPECT_EQ(e.caret(), 6u) << "past the euro sign";
    e.MoveRight(false); EXPECT_EQ(e.caret(), 6u);
    e.MoveLeft(false);  EXPECT_EQ(e.caret(), 3u);
}

TEST(UITextEdit, BackspaceAndDeleteRemoveWholeCharacters) {
    UITextEdit e;
    e.setValue(kMixed);
    e.MoveToEnd(false);
    EXPECT_TRUE(e.Backspace());
    EXPECT_EQ(e.value(), "a\xC3\xA9") << "backspace split a multi-byte character";
    EXPECT_TRUE(e.Backspace());
    EXPECT_EQ(e.value(), "a");
    EXPECT_TRUE(e.Backspace());
    EXPECT_EQ(e.value(), "");
    EXPECT_FALSE(e.Backspace()) << "backspace at the start must report no change";

    e.setValue(kMixed);
    e.MoveToStart(false);
    EXPECT_TRUE(e.DeleteForward());
    EXPECT_EQ(e.value(), "\xC3\xA9\xE2\x82\xAC");
    EXPECT_TRUE(e.DeleteForward());
    EXPECT_EQ(e.value(), "\xE2\x82\xAC");
}

TEST(UITextEdit, InsertingAtTheCaretAndAtTheEnd) {
    UITextEdit e;
    e.setValue("ac");
    e.SetCaret(1);
    EXPECT_TRUE(e.InsertText("b"));
    EXPECT_EQ(e.value(), "abc");
    EXPECT_EQ(e.caret(), 2u) << "the caret must follow what was typed";
    e.MoveToEnd(false);
    EXPECT_TRUE(e.InsertText("\xF0\x9F\x98\x80"));   // a 4-byte emoji
    EXPECT_EQ(e.value(), "abc\xF0\x9F\x98\x80");
    EXPECT_EQ(e.caret(), 7u);
    EXPECT_FALSE(e.InsertText("")) << "an empty insert is not a change";
}

// ---------------------------------------------------------------- selection

TEST(UITextEdit, ShiftArrowsExtendAndTypingReplaces) {
    UITextEdit e;
    e.setValue("hello");
    e.MoveToStart(false);
    e.MoveRight(true);
    e.MoveRight(true);
    EXPECT_TRUE(e.hasSelection());
    EXPECT_EQ(e.selectedText(), "he");

    EXPECT_TRUE(e.InsertText("HE"));
    EXPECT_EQ(e.value(), "HEllo") << "typing must replace the selection";
    EXPECT_FALSE(e.hasSelection());
    EXPECT_EQ(e.caret(), 2u);
}

// An unshifted arrow COLLAPSES a selection to its near edge rather than moving
// from the caret. Every editor does this, and getting it wrong is immediately
// noticeable.
TEST(UITextEdit, UnshiftedArrowsCollapseTheSelection) {
    UITextEdit e;
    e.setValue("hello");
    e.SelectAll();
    EXPECT_EQ(e.selectionBegin(), 0u);
    EXPECT_EQ(e.selectionEnd(), 5u);

    e.MoveLeft(false);
    EXPECT_FALSE(e.hasSelection());
    EXPECT_EQ(e.caret(), 0u) << "left must collapse to the START of the selection";

    e.SelectAll();
    e.MoveRight(false);
    EXPECT_EQ(e.caret(), 5u) << "right must collapse to the END";
}

TEST(UITextEdit, BackspaceAndDeleteRemoveTheSelection) {
    UITextEdit e;
    e.setValue("hello");
    e.SetCaret(1);
    e.SetCaret(4, /*select=*/true);
    ASSERT_EQ(e.selectedText(), "ell");

    EXPECT_TRUE(e.Backspace());
    EXPECT_EQ(e.value(), "ho");
    EXPECT_EQ(e.caret(), 1u);

    e.setValue("hello");
    e.SelectAll();
    EXPECT_TRUE(e.DeleteForward());
    EXPECT_EQ(e.value(), "");
}

// ---------------------------------------------------------- limits and mask

TEST(UITextEdit, MaxLengthTruncatesOnACharacterBoundary) {
    UITextEdit e;
    e.setMaxLength(4);
    // "aé€" is 6 bytes; 4 bytes would land inside the euro sign, so the value
    // has to stop at 3 rather than leave a broken character behind.
    e.setValue(kMixed);
    EXPECT_EQ(e.value(), "a\xC3\xA9");
    EXPECT_EQ(e.value().size(), 3u);

    e.MoveToEnd(false);
    EXPECT_TRUE(e.InsertText("b"));
    EXPECT_EQ(e.value(), "a\xC3\xA9""b");
    EXPECT_FALSE(e.InsertText("c")) << "insert past the limit must report no change";
    EXPECT_EQ(e.value().size(), 4u);

    // A multi-byte character that does not fit is refused whole.
    UITextEdit g;
    g.setMaxLength(4);
    g.setValue("abc");
    EXPECT_FALSE(g.InsertText("\xE2\x82\xAC")) << "a 3-byte glyph must not be half-inserted";
    EXPECT_EQ(g.value(), "abc");
}

TEST(UITextEdit, MaskHidesTheValueOneGlyphPerCharacter) {
    UITextEdit e;
    e.setMaskCharacter("*");
    e.setValue(kMixed);
    EXPECT_EQ(e.value(), kMixed) << "the real value must be untouched";
    // Three CHARACTERS, not six bytes: a masked field must not leak the byte
    // length of non-ASCII input.
    EXPECT_EQ(e.displayText(), "***");

    e.setMaskCharacter("");
    EXPECT_EQ(e.displayText(), kMixed);
}

TEST(UITextEdit, SettingTheValueClampsTheCaret) {
    UITextEdit e;
    e.setValue("hello world");
    e.MoveToEnd(false);
    ASSERT_EQ(e.caret(), 11u);
    e.setValue("hi");
    EXPECT_EQ(e.caret(), 2u) << "the caret was left past the end";
    EXPECT_EQ(e.anchor(), 2u);
}

// ------------------------------------------------------------ key handling

TEST(UITextEdit, HandleKeyConsumesEditingKeysAndPassesTheRest) {
    UITextEdit e;
    e.setValue("ab");
    e.MoveToEnd(false);
    bool changed = false;

    EXPECT_TRUE(e.HandleKey({ UIKey::Backspace }, changed));
    EXPECT_TRUE(changed);
    EXPECT_EQ(e.value(), "a");

    EXPECT_TRUE(e.HandleKey({ UIKey::Left }, changed));
    EXPECT_FALSE(changed) << "moving the caret is not a value change";

    // Ctrl+A selects all; a bare 'a' is TEXT and must stay typeable.
    UIKeyEvent ctrlA; ctrlA.key = UIKey::A; ctrlA.ctrl = true;
    EXPECT_TRUE(e.HandleKey(ctrlA, changed));
    EXPECT_TRUE(e.hasSelection());
    EXPECT_FALSE(e.HandleKey({ UIKey::A }, changed)) << "a bare 'a' must not be eaten";

    // Left for the caller: Tab is navigation, Enter/Escape belong to whatever
    // contains the field, Up/Down mean nothing in a single-line control.
    for (UIKey k : { UIKey::Tab, UIKey::Enter, UIKey::Escape, UIKey::Up, UIKey::Down }) {
        EXPECT_FALSE(e.HandleKey({ k }, changed)) << int(k);
    }
}

// ------------------------------------------------------- the element itself

TEST(UITextField, MarkupBuildsAFocusableFieldWithItsValue) {
    FieldDoc d(R"(<UI><TextField name="f" value="hi" maxlength="8" mask="*"
                             style="width: 200px; height: 30px"/></UI>)");
    ASSERT_TRUE(d.errors.empty()) << d.errors[0];
    ASSERT_NE(d.edit(), nullptr);
    EXPECT_EQ(d.edit()->value(), "hi");
    EXPECT_EQ(d.edit()->maxLength(), 8u);
    // A field you cannot focus is a label, so the type implies it.
    EXPECT_TRUE(d.f()->isFocusable());
    // The DISPLAY text is what layout and painting see, so a masked field never
    // measures or paints its real value.
    EXPECT_EQ(d.f()->style().text, "**");
    EXPECT_EQ(d.edit()->caret(), 2u) << "the caret should start at the end";
}

TEST(UITextField, AttributesOnlyValidOnAFieldAreRejectedElsewhere) {
    for (const char* markup : { R"(<UI><Element name="e" value="x"/></UI>)",
                                R"(<UI><Label name="e" maxlength="4"/></UI>)",
                                R"(<UI><Button name="e" mask="*"/></UI>)" }) {
        UIDocument doc;
        std::vector<std::string> errors;
        EXPECT_FALSE(UIMarkup::LoadInto(doc, markup, errors, "t.cxml")) << markup;
        ASSERT_FALSE(errors.empty()) << markup;
        EXPECT_NE(errors[0].find("only valid on a <TextField>"), std::string::npos)
            << errors[0];
    }

    UIDocument doc;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(doc, R"(<UI><TextField maxlength="lots"/></UI>)",
                                    errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("expected a byte count"), std::string::npos) << errors[0];
}

TEST(UITextField, TypingIntoTheFocusedFieldEditsItAndFiresValueChanged) {
    FieldDoc d;
    ASSERT_NE(d.f(), nullptr);
    std::vector<std::string> values;
    d.f()->OnValueChanged([&](UIEvent& e) { values.push_back(e.text); });

    d.doc.SetFocus(d.f());
    d.Type("hi");
    EXPECT_EQ(d.edit()->value(), "hi");
    EXPECT_EQ(d.f()->style().text, "hi") << "the drawn text did not follow the value";
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values[0], "hi");

    d.Key(UIKey::Backspace);
    EXPECT_EQ(d.edit()->value(), "h");
    ASSERT_EQ(values.size(), 2u);

    // Caret movement is not a value change.
    d.Key(UIKey::Left);
    EXPECT_EQ(values.size(), 2u);
}

TEST(UITextField, AnUnfocusedFieldReceivesNothing) {
    FieldDoc d;
    d.Type("hi");
    EXPECT_EQ(d.edit()->value(), "");
    d.doc.SetFocus(d.f());
    d.Type("hi");
    EXPECT_EQ(d.edit()->value(), "hi");
}

// A field consumes Left/Right, so they must not also move focus — but it does
// NOT consume Tab, so Tab still navigates out of it.
TEST(UITextField, ArrowsStayInTheFieldButTabStillNavigates) {
    FieldDoc d(R"(<UI>
        <TextField name="f" value="ab" style="width: 100px; height: 30px"/>
        <Button name="b" style="width: 100px; height: 30px"/>
      </UI>)");
    ASSERT_TRUE(d.errors.empty()) << d.errors[0];
    d.doc.SetFocus(d.f());

    d.Key(UIKey::Left);
    EXPECT_EQ(d.doc.focused(), d.f()) << "an arrow moved focus out of the field";
    EXPECT_EQ(d.edit()->caret(), 1u);

    d.Key(UIKey::Tab);
    EXPECT_EQ(d.doc.focused(), d.doc.root().Find("b")) << "Tab did not leave the field";
}

// The field's editing is a DEFAULT ACTION: it runs after handlers, and only if
// none of them claimed the key. That is the DOM's ordering, and it lets an app
// pre-empt a shortcut without the field knowing about it.
TEST(UITextField, AHandlerCanPreEmptTheFieldsOwnEditing) {
    FieldDoc d;
    d.doc.SetFocus(d.f());
    d.Type("abc");
    ASSERT_EQ(d.edit()->value(), "abc");

    d.f()->OnKeyDown([](UIEvent& e) {
        if (e.key == UIKey::Backspace) e.StopPropagation();
    });
    d.Key(UIKey::Backspace);
    EXPECT_EQ(d.edit()->value(), "abc") << "the handler did not pre-empt the delete";

    d.f()->OnTextInput([](UIEvent& e) { e.StopPropagation(); });
    d.Type("z");
    EXPECT_EQ(d.edit()->value(), "abc") << "the handler did not pre-empt the insert";
}

TEST(UITextField, ADisabledFieldTakesNoInput) {
    FieldDoc d;
    d.doc.SetFocus(d.f());
    d.f()->setEnabled(false);
    d.Type("hi");
    EXPECT_EQ(d.edit()->value(), "") << "a disabled field accepted typing";
    EXPECT_EQ(d.doc.focused(), nullptr) << "focus stayed on a disabled field";
}

// Two fields in one document must not share a caret or a value.
TEST(UITextField, FieldsAreIndependent) {
    FieldDoc d(R"(<UI>
        <TextField name="f" style="width: 100px; height: 30px"/>
        <TextField name="g" style="width: 100px; height: 30px"/>
      </UI>)");
    UIElement* g = d.doc.root().Find("g");
    ASSERT_NE(g, nullptr);
    ASSERT_NE(g->textEdit(), nullptr);
    EXPECT_NE(g->textEdit(), d.edit());

    d.doc.SetFocus(d.f());
    d.Type("one");
    d.doc.SetFocus(g);
    d.Type("two");
    EXPECT_EQ(d.edit()->value(), "one");
    EXPECT_EQ(g->textEdit()->value(), "two");
}

TEST(UITextField, OnlyAFieldHasAnEditBuffer) {
    FieldDoc d(R"(<UI><Label name="f" text="hi"/></UI>)");
    EXPECT_EQ(d.f()->textEdit(), nullptr);
    // ...and a hand-built tree can opt in.
    UIElement* made = d.doc.root().AddChild("made");
    made->MakeTextField().setValue("x");
    ASSERT_NE(made->textEdit(), nullptr);
    EXPECT_TRUE(made->isFocusable());
    EXPECT_EQ(made->textEdit()->value(), "x");
}

// ------------------------------------------------------ undo / redo

// A burst of typing is ONE undo step. Undoing letter by letter is not what
// anyone means by undo, and a deletion or a caret jump always starts a new run.
TEST(UITextEdit, TypingCoalescesIntoOneUndoStep) {
    UITextEdit e;
    EXPECT_FALSE(e.canUndo());
    e.InsertText("h");
    e.InsertText("i");
    e.InsertText("!");
    ASSERT_EQ(e.value(), "hi!");

    EXPECT_TRUE(e.Undo());
    EXPECT_EQ(e.value(), "") << "typing did not coalesce into one step";
    EXPECT_FALSE(e.canUndo());

    EXPECT_TRUE(e.Redo());
    EXPECT_EQ(e.value(), "hi!");
    EXPECT_FALSE(e.canRedo());
}

TEST(UITextEdit, DeletionsAreTheirOwnUndoSteps) {
    UITextEdit e;
    e.InsertText("abc");
    e.Backspace();
    ASSERT_EQ(e.value(), "ab");
    e.Backspace();
    ASSERT_EQ(e.value(), "a");

    EXPECT_TRUE(e.Undo()); EXPECT_EQ(e.value(), "ab");
    EXPECT_TRUE(e.Undo()); EXPECT_EQ(e.value(), "abc");
    EXPECT_TRUE(e.Undo()); EXPECT_EQ(e.value(), "");
}

// Typing after an undo starts a new run and drops the redo branch, which is
// what every editor does — the alternative is a redo that reapplies text the
// user has since replaced.
TEST(UITextEdit, TypingAfterAnUndoClearsRedo) {
    UITextEdit e;
    e.InsertText("one");
    e.Undo();
    ASSERT_TRUE(e.canRedo());
    e.InsertText("two");
    EXPECT_FALSE(e.canRedo());
    EXPECT_EQ(e.value(), "two");
}

// An external write is not an edit the user made, so it must not become a step
// they can undo to a value they never typed.
TEST(UITextEdit, SettingTheValueClearsTheHistory) {
    UITextEdit e;
    e.InsertText("typed");
    ASSERT_TRUE(e.canUndo());
    e.setValue("from a binding");
    EXPECT_FALSE(e.canUndo());
    EXPECT_FALSE(e.canRedo());
}

TEST(UITextEdit, UndoIsBounded) {
    UITextEdit e;
    // Each Backspace is its own step, so this overflows the cap.
    for (int i = 0; i < 200; ++i) { e.InsertText("x"); e.Backspace(); }
    int steps = 0;
    while (e.Undo() && steps < 1000) ++steps;
    EXPECT_LE(steps, 128) << "the undo stack is unbounded";
    SUCCEED();
}

TEST(UITextEdit, CtrlZAndCtrlYAreHandled) {
    UITextEdit e;
    e.InsertText("hello");
    bool changed = false;
    UIKeyEvent z; z.key = UIKey::Z; z.ctrl = true;
    EXPECT_TRUE(e.HandleKey(z, changed));
    EXPECT_TRUE(changed);
    EXPECT_EQ(e.value(), "");

    UIKeyEvent y; y.key = UIKey::Y; y.ctrl = true;
    EXPECT_TRUE(e.HandleKey(y, changed));
    EXPECT_EQ(e.value(), "hello");

    // Ctrl+Shift+Z is redo too, on platforms that prefer it.
    e.HandleKey(z, changed);
    UIKeyEvent sz; sz.key = UIKey::Z; sz.ctrl = true; sz.shift = true;
    EXPECT_TRUE(e.HandleKey(sz, changed));
    EXPECT_EQ(e.value(), "hello");

    // A bare 'z' is text and must stay typeable.
    EXPECT_FALSE(e.HandleKey({ UIKey::Z }, changed));
}

// ------------------------------------------------------------ multi-line

TEST(UITextEdit, EnterInsertsANewlineOnlyWhenMultiline) {
    UITextEdit single;
    bool changed = false;
    EXPECT_FALSE(single.HandleKey({ UIKey::Enter }, changed))
        << "a single-line field must leave Enter to whatever contains it";

    UITextEdit multi;
    multi.setMultiline(true);
    multi.InsertText("a");
    EXPECT_TRUE(multi.HandleKey({ UIKey::Enter }, changed));
    EXPECT_TRUE(changed);
    multi.InsertText("b");
    EXPECT_EQ(multi.value(), "a\nb");
}

// Tab is navigation even in a multi-line field. Trapping it would strand a
// keyboard user, and every web textarea agrees.
TEST(UITextEdit, MultilineStillLetsTabNavigate) {
    UITextEdit e;
    e.setMultiline(true);
    bool changed = false;
    EXPECT_FALSE(e.HandleKey({ UIKey::Tab }, changed));
}

TEST(UITextEdit, LineHelpersFindStartsEndsAndIndices) {
    const std::string s = "ab\ncde\nf";
    EXPECT_EQ(UITextEdit::LineStart(s, 0), 0u);
    EXPECT_EQ(UITextEdit::LineStart(s, 2), 0u);
    EXPECT_EQ(UITextEdit::LineStart(s, 3), 3u);
    EXPECT_EQ(UITextEdit::LineStart(s, 6), 3u);
    EXPECT_EQ(UITextEdit::LineStart(s, 7), 7u);
    EXPECT_EQ(UITextEdit::LineEnd(s, 0), 2u);
    EXPECT_EQ(UITextEdit::LineEnd(s, 4), 6u);
    EXPECT_EQ(UITextEdit::LineEnd(s, 7), 8u);
    EXPECT_EQ(UITextEdit::LineIndexOf(s, 0), 0u);
    EXPECT_EQ(UITextEdit::LineIndexOf(s, 4), 1u);
    EXPECT_EQ(UITextEdit::LineIndexOf(s, 8), 2u);
}

TEST(UITextEdit, UpAndDownMoveBetweenLines) {
    UITextEdit e;
    e.setMultiline(true);
    e.setValue("abc\ndefgh\nij");
    e.SetCaret(6);                       // line 1, column 2
    ASSERT_EQ(UITextEdit::LineIndexOf(e.value(), e.caret()), 1u);

    e.MoveUp(false);
    EXPECT_EQ(UITextEdit::LineIndexOf(e.value(), e.caret()), 0u);
    EXPECT_EQ(e.caret(), 2u) << "the column should be kept";

    e.MoveDown(false);
    EXPECT_EQ(e.caret(), 6u);
    e.MoveDown(false);
    EXPECT_EQ(UITextEdit::LineIndexOf(e.value(), e.caret()), 2u);
    // The last line is shorter, so the caret clamps to its end.
    EXPECT_EQ(e.caret(), 12u);

    // Off the top and bottom, rather than running away.
    e.MoveUp(false); e.MoveUp(false); e.MoveUp(false);
    EXPECT_EQ(UITextEdit::LineIndexOf(e.value(), e.caret()), 0u);
    e.MoveDown(false); e.MoveDown(false); e.MoveDown(false);
    EXPECT_EQ(e.caret(), e.value().size());
}

// Home/End mean the LINE in a multi-line field and the whole value in a
// single-line one — which is what each looks like to the user.
TEST(UITextEdit, HomeAndEndAreLineAwareOnlyWhenMultiline) {
    UITextEdit m;
    m.setMultiline(true);
    m.setValue("abc\ndef");
    m.SetCaret(5);
    m.MoveToStart(false);
    EXPECT_EQ(m.caret(), 4u);
    m.MoveToEnd(false);
    EXPECT_EQ(m.caret(), 7u);

    UITextEdit s;
    s.setValue("abc\ndef");   // a stray newline in a single-line value
    s.SetCaret(5);
    s.MoveToStart(false);
    EXPECT_EQ(s.caret(), 0u);
    s.MoveToEnd(false);
    EXPECT_EQ(s.caret(), 7u);
}

TEST(UITextEdit, UpAndDownDoNothingInASingleLineField) {
    UITextEdit e;
    e.setValue("hello");
    e.SetCaret(2);
    e.MoveUp(false);
    EXPECT_EQ(e.caret(), 2u);
    bool changed = false;
    EXPECT_FALSE(e.HandleKey({ UIKey::Up }, changed))
        << "Up must stay available to whatever contains a single-line field";
}

// ------------------------------------------------------------- clipboard

TEST(UITextField, ClipboardCopyCutAndPaste) {
    FieldDoc d;
    std::string board;
    d.doc.SetClipboardHandlers([&](const std::string& t) { board = t; },
                               [&] { return board; });
    d.doc.SetFocus(d.f());
    d.Type("hello");
    d.edit()->SelectAll();

    d.Key(UIKey::C, /*shift=*/false, /*ctrl=*/true);
    EXPECT_EQ(board, "hello");
    EXPECT_EQ(d.edit()->value(), "hello") << "copy must not modify the value";

    d.Key(UIKey::X, false, true);
    EXPECT_EQ(board, "hello");
    EXPECT_EQ(d.edit()->value(), "") << "cut did not remove the selection";

    d.Key(UIKey::V, false, true);
    EXPECT_EQ(d.edit()->value(), "hello");
    // ...and the paste is undoable as one step.
    EXPECT_TRUE(d.edit()->Undo());
    EXPECT_EQ(d.edit()->value(), "");
}

// Cutting a masked field must put the REAL text on the clipboard, never a row
// of asterisks.
TEST(UITextField, ClipboardUsesTheRealValueNotTheMask) {
    FieldDoc d(R"(<UI><TextField name="f" mask="*" style="width: 200px; height: 30px"/></UI>)");
    std::string board;
    d.doc.SetClipboardHandlers([&](const std::string& t) { board = t; },
                               [&] { return board; });
    d.doc.SetFocus(d.f());
    d.Type("secret");
    ASSERT_EQ(d.f()->style().text, "******");
    d.edit()->SelectAll();
    d.Key(UIKey::C, false, true);
    EXPECT_EQ(board, "secret");
}

// Without host handlers the keys do nothing rather than half-working against a
// private buffer the rest of the machine cannot see.
TEST(UITextField, ClipboardKeysAreInertWithoutAHost) {
    FieldDoc d;
    d.doc.SetFocus(d.f());
    d.Type("hi");
    d.edit()->SelectAll();
    d.Key(UIKey::X, false, true);
    EXPECT_EQ(d.edit()->value(), "hi") << "cut worked with no clipboard to cut to";
}

TEST(UITextField, MultilineIsDeclaredInMarkup) {
    FieldDoc d(R"(<UI><TextField name="f" multiline="true" value="a"
                             style="width: 200px; height: 60px"/></UI>)");
    ASSERT_TRUE(d.errors.empty()) << d.errors[0];
    ASSERT_NE(d.edit(), nullptr);
    EXPECT_TRUE(d.edit()->multiline());

    d.doc.SetFocus(d.f());
    d.Key(UIKey::Enter);
    d.Type("b");
    EXPECT_EQ(d.edit()->value(), "a\nb");

    UIDocument other;
    std::vector<std::string> errors;
    EXPECT_FALSE(UIMarkup::LoadInto(other, R"(<UI><Element multiline="true"/></UI>)",
                                    errors, "t.cxml"));
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("only valid on a <TextField>"), std::string::npos) << errors[0];
}
