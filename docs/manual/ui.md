# In-game UI and the 2D layer

The engine ships two related things:

- **`Renderer2D`** — a general-purpose batched 2D renderer. It knows nothing
  about UI, and is deliberately shaped so a **2D game** can be built directly on
  it (world-space camera, sprite atlases, sort layers).
- **The UI system** — a retained element tree with flexbox layout, CSS-like
  stylesheets, XML-like markup, pointer events, data binding, and hot reload. It
  is the first consumer of `Renderer2D`, not a privileged one.

The UI model is modelled on web front-end and Unity's UI Toolkit: markup
(`.cxml`) for structure, a stylesheet (`.cstyle`) for appearance, bindings for
values, and C++ for behaviour. If you know CSS flexbox, you already know this
system.

> This is not the *editor's* UI. The editor is ImGui (immediate mode); this is
> the UI your **game** draws, and it renders in both the editor's Game view and
> the shipped Player.

---

## Quick start

**UI is scene content.** The shipped sample is an entity named `HUD` in the
default scene, carrying a `UIDocumentComponent` that points at
`Exported/UI/hud.cxml` + `hud.cstyle`. Nothing in the editor or the player installs
it — select `HUD` in the Hierarchy and you are looking at the whole thing.

To put UI in your own scene: select an entity, **Add Component ▸ UI Document**,
and give it a markup path. That is the entire integration.

A host wires the system up once and never mentions a specific UI again:

```cpp
UIWorld uiWorld;                        // outlives the draw callback
uiWorld.SetFont(&font);
InstallDemoUIContent(uiWorld);          // the sample's values, actions and converter

renderer().SetUIDraw([&](Renderer2D& r2d, int w, int h, float dt) {
    uiWorld.SetPointer(pointerState);   // see "Input" below
    uiWorld.SetKeyboard(keyboardState);
    uiWorld.Update(scene.registry, w, h, dt);
    uiWorld.Draw(r2d);
});
```

`InstallDemoUIContent` does three things: it seeds the properties `hud.cxml`
binds to, it registers the three named actions the markup calls (`addScore` on
the score button, `invPrev` and `invNext` on the inventory's PREV/NEXT), and it
registers the `healthTint` converter. The seeding happens **before** the
document exists, so the first binding pass has real values rather than a frame
of defaults.

The only C++ a UI needs is what a file cannot carry: **named actions** and
**converters**. Everything else — structure, appearance, values, interaction
states — is content that hot-reloads.

`UIWorld::Update` runs each document through `Update(dt)` → `UpdateRepeats()` →
`UpdateTabs()` → `UpdateToTarget()` → `AdvanceTime(dt)` → `Layout` →
`UpdatePointer`. Then, **only for the document that owns the keyboard** — the
focused one, or the pointer's document when nothing has focus — it runs
`UpdateKeyboard` → `PageTabs` (when a page intent is pending) → `UpdateNav`.
Every document then finishes with `PublishToSources()` → `RestyleInteractive()`
→ `UpdateToTarget()` again → a conditional second `Layout`.

Paging comes **before** the directional move because a shoulder press changes
which panel is on screen, and a stick move in the same frame should land in the
panel it just switched to. That second binder pass is what lands a tab switch or
a class toggle in the same frame as the click that caused it.
Bindings run **before** layout so a changed label is measured at its new width
on the frame it changes; a `setText` from an input handler never was — it lands
after the solve and paints at the previous frame's size.

> **Upgrading an existing scene.** The `HUD` entity ships in the default
> `scene.json`, but a scene you saved earlier will not have it — saved scenes
> are never overwritten by a build. Add it in one step: select any entity and
> use **Add Component ▸ UI Document**, which seeds the sample's paths for you.

---

## Markup: `.cxml`

Abridged from the shipped `hud.cxml`:

```xml
<UI name="hud" data-source="scene">
  <Element name="topBar" class="row">
    <Element name="healthTrack" class="track">
      <Element name="healthFill" class="fill"
               bind="width: {health | percent};
                     background-color: {health | healthTint}"/>
    </Element>
    <Label name="scoreLabel" class="readout" text="SCORE {score}"/>
  </Element>

  <Element name="buttonRow" class="row-left">
    <Button name="scoreButton" class="btn" text="+100" on-click="addScore"/>
  </Element>

  <Element name="lowHealth" class="warning" if="lowHealth" text="LOW HEALTH"/>
</UI>
```

Parsed by `UIMarkup` (`Engine/src/ui/UIMarkup.h`, pugixml).

| Attribute | Means |
|---|---|
| *tag name* | the element **type**, matched by a bare type selector (`Button { ... }`) |
| `name` | the `#id`, and the handle C++ uses to `Find()` the element |
| `class` | space-separated class list, matched by `.class` selectors |
| `text` | a text **template** — literal text with `{holes}`; see [Data binding](#data-binding) |
| `style` | inline declarations; outrank **every** stylesheet rule, as in CSS |
| `data-source` | names the data source this element and its whole subtree bind against |
| `bind` | CSS declarations whose **values** carry `{holes}` |
| `if` | visibility from a bool; writes `display: flex\|none` |
| `on-<event>` | calls a named action the app registered |
| `focusable` | `true`/`false` — puts the element in the tab order |
| `disabled` | `true`/`false` (bare = true) — inert, skipped by Tab, matches `:disabled` |
| `value` | `<TextField>` or `<Slider>` — what the control starts with; both own a value |
| `maxlength` / `mask` / `multiline` | `<TextField>` only — see [Text entry](#text-entry) |
| `bind-value` | `<TextField>` or `<Slider>` — a **two-way** value binding |
| `min` / `max` / `step` / `key-step` / `key-step-max` / `key-ramp` / `vertical` | `<Slider>` only — see [Sliders](#sliders) |
| `focus-scope` / `on-back` | a navigation region and what backs out of it — see [Directional navigation](#directional-navigation-gamepad-and-keyboard) |
| `push-hovered` / `push-pressed` / `push-focused` | element state back to the source |
| `classes` | toggles classes from bools — `classes="low-health: {isLow}"` |
| `repeat` / `repeat-count` / `repeat-offset` | repeats one template over a list — see [Collections](#collections-repeat) |
| `label` | `<Tab>` only — the header's text |
| `selected` | `<TabView>` only — the initially open tab |
| `bind-selected` | `<TabView>` only — the **two-way** selection link |

Anything else is a **load error**. That matters more than it sounds: this loader
used to read the attributes it knew and ignore the rest, so `nmae="healthFill"`
produced an element no stylesheet rule and no `Find()` could ever locate.

The root tag maps onto the document's existing root, so `<UI name="hud">` names
and styles the root itself rather than creating an extra wrapper.

Tag names are free-form, with a few the loader knows by name.

A `<Button>` is focusable by default, because that is what the word means. A
`<TextField>` and a `<Slider>` are focusable for a stronger reason: each owns a
**value**, and a value no keyboard or pad can reach is one only a mouse can
change. Both display that value, so a `text=` on either is a load error.

`value` and `bind-value` are the pair those two share — both own a value and
both bind it two-way through the same mechanism — so each is an error only on a
tag that is neither. Everything else is exclusive: `maxlength`, `mask` and
`multiline` to a `<TextField>` (see [Text entry](#text-entry)), and `min`,
`max`, `step`, `key-step`, `key-step-max`, `key-ramp` and `vertical` to a
`<Slider>` (see [Sliders](#sliders)).

Beyond that the tag is only a selector hook: `Label` and `Button` carry no
tag-specific behaviour, and what a button *does* still comes from the handlers
you attach. What focus buys is operability without a mouse — Enter, and the
pad's A, run `ActivateFocused`, which synthesizes a Click at the focused
element's centre and bubbles it along the path a real click takes, holding the
press briefly so `:active` can be seen. It is keyed on **focus, not on the
tag**, so it lights up every authored `on-click` with no markup change.

**Gotcha:** the file path is run through the same containment check as models,
scripts, clips and HDRis (`PathIsContained`). Absolute paths and `..` are
refused before the file is opened, because markup is authored content flowing
into a parser.

---

## Stylesheets: `.cstyle`

```css
.row   { flex-direction: row; justify-content: space-between; align-items: center; }
.track { width: 220px; height: 18px; padding: 3px; background-color: rgba(0,0,0,0.45); }
.fill  { width: 100%; height: 100%; background-color: #d93a3d; }
#hud   { flex-direction: column; padding: 16px; }
```

Parsed by `UIStyleSheet` (`Engine/src/ui/UIStyleSheet.h`) — a hand-written CSS
subset, no dependency.

**Selectors:** `Type`, `.class`, `#name`, `*`, the `:hover`, `:active`,
`:focus` and `:disabled` pseudo-classes, compounds (`Button.primary#ok`,
`.btn:hover`), and the descendant, child, adjacent-sibling and general-sibling combinators:

```css
.panel .btn      { }   /* a .btn anywhere inside a .panel */
.panel > .btn    { }   /* only an immediate child */
.row + .btn      { }   /* the element immediately after a .row */
.row ~ .btn      { }   /* any later sibling of a .row */
.panel > .row .btn:hover { }   /* chains and states compose */
```

Siblings look **backward only**, as in CSS — there is no "previous element"
selector, because matching walks from the element being styled.

Comma-separated lists. Standard CSS **specificity** (`#id` > `.class` > type),
with later-in-file winning ties. A pseudo-class counts as a class, and
specificity **sums across the whole chain** — so `.panel .btn` (two classes)
beats a bare `.btn` regardless of file order, which is the entire point of
having contexts. A rule matched through several of its listed selectors weighs
as much as its strongest match, as in CSS.

Matching runs right-to-left from the element being styled, as every real CSS
engine does; left-to-right would need to backtrack over the whole subtree.

**Properties:**

| Group | Properties |
|---|---|
| Flex | `flex-direction`, `justify-content`, `align-items`, `align-self`, `flex-grow`, `flex-shrink`, `gap` |
| Size | `width`, `height`, `min-width`, `min-height`, `max-width`, `max-height` |
| Box | `margin`, `padding` (1–4 value CSS shorthand) |
| Position | `position: relative\|absolute`, `left`, `top`, `right`, `bottom` |
| Paint | `background-color`, `background-color-to`, `background-gradient: none\|vertical\|horizontal`, `background-image`, `background-size: stretch\|cover`, `border-radius`, `border-width`, `border-color`, `color`, `font-scale` |
| Behaviour | `overflow` / `overflow-x` / `overflow-y`: `visible\|hidden\|scroll`, `pointer-events: auto\|none`, `display: flex\|none` |
| Scrollbar | `scrollbar-width`, `scrollbar-min-thumb`, `scrollbar-color`, `scrollbar-thumb-color`, `scrollbar-visibility: auto\|always`, `scroll-behavior: instant\|smooth` |

Lengths — `auto`, `Npx`, `N%`, or a bare number (treated as px) — are the
**Size** group: `width`, `height`, `min-*`, `max-*`. `scrollbar-width`,
`scrollbar-min-thumb`, `border-radius` and `border-width` are lengths too, but
must be a **non-negative pixel** count — `auto`, a percentage or a negative is
reported, since none of the four is a percentage of anything meaningful. `margin` and `padding`
take 1–4 **pixel** values (`8px` or a bare `8`; `%` and `auto` are reported).
Everything else numeric — `left`, `top`, `right`, `bottom`, `gap`, `flex-grow`,
`flex-shrink`, `font-scale` — is a plain **number**, pixels where that is
meaningful, and the number parser rejects trailing text: `left: 0` is right,
`left: 0px` and `left: 50%` are errors. Colours are `#rgb`, `#rrggbb`,
`#rrggbbaa`, `rgb(r,g,b)`, `rgba(r,g,b,a)` (channels 0–255, alpha 0–1), or a
handful of names.

**Not supported, and reported as errors rather than silently ignored:** any
other pseudo-class, and variables. Both fail the parse, and a failed parse
rejects the whole sheet rather than leaving a `.btn:focus` rule that quietly
applies all the time.

**At-rules are not supported and are not detected**, which is worse. The parser
has no concept of `@`: it reads from the start of a rule to the next `{` as a
selector list, so `@media screen` becomes a compound whose type name is
`@media` and matches nothing. A statement at-rule is worse still — `@import
"theme.cstyle";` has no brace of its own, so the scan runs past it into the
following rule and absorbs it, and `@import "theme.cstyle"; .btn` parses as a
perfectly valid three-part descendant selector. Nothing is reported and the
`.btn` rule silently stops matching. Keep at-rules out of `.cstyle` files.

**Property inheritance** is not supported: no property cascades from parent to
child. Every element is styled independently, and a context selector constrains
*which* elements a rule reaches rather than passing values down.

### Interaction styling

```css
.btn         { background-color: #292d33; }
.btn:hover   { background-color: #424852; }
.btn:active  { background-color: #d98c26; color: #14161a; }
```

`:hover` is true for the element under the pointer **and its ancestors**, as in
CSS, so a button and the panel containing it are both hovered. `:active` is true
for a single element and never its ancestors — but the press behind it need not
come from a pointer. Confirming with Enter, or with the pad's activate intent,
runs `ActivateFocused`, which raises the press on the **focused** element and
holds it briefly so the flash can be seen; that element may be nowhere near the
cursor. Without it, `:active` — the only press state a stylesheet can express —
was unreachable from a gamepad, so confirming a menu item on the input the menu
exists for changed no pixels at all. Compounds work: `.btn:hover:active`
requires both.

Drive it by calling `RestyleInteractive()` once per frame, **after every input
pass**, because all three decide state a rule can match: the pointer sets hover
and press, and the other two move focus and raise that synthesized press.

```cpp
doc.UpdatePointer(pointer);
doc.UpdateKeyboard(kb);
doc.UpdateNav(nav);
if (assets.RestyleInteractive()) doc.Layout(w, h, font);  // a state rule can change a box
```

Restyling before `UpdateNav` would push a confirm's `:active` to the following
frame — and on a frame longer than the hold, `AdvanceTime` releases the press at
the top of the next one, so the restyle would never see it at all.

**How it works, and the one caveat.** The cascade has no undo — applying a rule
copies its declarations in and records nothing about what they overwrote — so
there is no way to "remove" a `:hover` rule when the pointer leaves. Instead the
element is reset to defaults and the **whole cascade re-runs** for its current
state. Text is carried across explicitly (it is not a cascadable property) and
bindings are re-applied straight after, so both keep working.

Everything else written straight into `style()` from C++ **does not survive a
state change** on such an element. Only elements some pseudo rule can actually
reach are watched, so this caveat is confined to exactly the elements an author
opted in — and the fix is to author it as a class, a binding, or a rule.

`:focus` matches the one element holding keyboard focus. `:disabled` matches a
disabled element **and everything inside it**, so greying out a panel greys out
its contents without repeating the rule.

Those four are all there is: a pseudo-class with nothing behind it would be a
selector that silently never matches, so `:checked` waits for a checkbox.

---

## Keyboard, focus and tab order

The host supplies keystrokes the same way it supplies the pointer, and for the
same reason — only it knows whether the keyboard belongs to the game UI this
frame or to a console, a chat box, or the editor's own panels:

```cpp
UIKeyboardState kb;
kb.keys.push_back({ UIKey::Tab, /*shift=*/false, /*ctrl=*/false, /*alt=*/false });
kb.text = "hello";           // UTF-8, already decoded
doc.UpdateKeyboard(kb);      // AFTER UpdatePointer
```

Both fields are **edge-triggered and per-frame**: presses that happened since
the last update (auto-repeat included) and text that was typed. A UI reacts to
keystrokes, not to key state, so there is no held-key snapshot.

`UIKey` is deliberately tiny — only keys the UI must act on. Anything printable
arrives as `TextInput` already decoded, which is the only way layouts, dead keys
and IMEs work. Use `Font::AppendUTF8` to build `kb.text` from the codepoints
your windowing layer hands you.

**Focus.** `focusable="true"` puts an element in the tab order; `Button` and
`TextField` are focusable by default because that is what those words mean, and
a plain `Element` is not, because a tab order full of panels is worse than none.
Clicking focuses the nearest focusable **ancestor** of what was hit — the same
reasoning that makes events bubble — and clicking nothing focusable clears
focus, which is what makes a field commit when you click away.

`SetFocus` refuses anything hidden, disabled, not focusable, or not in the tree,
so focus can never strand somewhere the user cannot see or Tab out of.

**Tab order is document order.** It is what the author already sees in the
markup, and there is no `tabindex` to fall out of sync with it. Tab and
Shift+Tab wrap, skip disabled and hidden elements, and are only navigation if
nothing consumed them — a handler calling `StopPropagation` on a Tab keeps it,
which is the only way to get a literal tab into a text field. Even a multi-line
field leaves Tab alone by default, exactly as a web `<textarea>` does: a field
that swallowed Tab would strand a keyboard user inside it.

**`disabled`** takes an element and its whole subtree out of hit-testing and the
tab order, and matches `:disabled`. A disabled panel whose buttons still worked
would be a trap.

**Enter activates** whatever has focus, by the same path a click takes -- a
keyboard needs no second mechanism. In a SINGLE-LINE text field it means
"done" instead, and leaves the field; a multi-line one keeps it, because there
it inserts a newline. Space is deliberately not a UI key at all: `InputMap`
binds `Jump` to it, and gameplay input is gated only on a text field having
focus, so Space on a focused menu button would press the button and jump.

**Escape** is `Back` -- see [Directional
navigation](#directional-navigation-gamepad-and-keyboard), which is where the
rules for it live, because the pad's B runs the same code.

| Event | Goes to | Bubbles |
|---|---|---|
| `FocusIn` / `FocusOut` | the element gaining/losing focus | no (like the DOM) |
| `KeyDown` | the focused element | yes |
| `TextInput` | the focused element | yes |
| `ValueChanged` | the control that was edited | yes |

---

---

## Directional navigation (gamepad and keyboard)

Tab order is fine for a form and wrong for a menu. A menu is a **shape**, and
moving through it should follow that shape: down goes to the thing below, not
to whatever happens to be next in the markup.

**The UI never learns what a gamepad is.** `UINavState` carries intents —
"move up", "activate", "back" — not devices, so a host synthesises them from
whatever it likes: a stick, a d-pad, WASD, an on-screen touch pad. Device
knowledge stays in `InputMap`, which is where it already lives.

```cpp
uiWorld.SetNav(navSynth.Poll(input(), dt, !uiWorld.wantsTextInput()));
```

The shipped player does that unconditionally, since a game has no competing
panels. The editor gates it on the Game surface holding focus AND the Scene
viewport not being under the hand -- the editor fly camera reads the same WASD,
and whatever makes the camera eligible for those keys has to make the game UI
ineligible, or one press drives both.

`UINavSynth` reads named actions (`UINavUp`, `UINavX`, `UIConfirm`, `UIBack`,
`UIPagePrev`/`UIPageNext`) and turns them into intents. Rebind the actions and
the same code drives a menu from a flight stick.

### Two doors, one for each grammar

A keyboard reaches the UI **twice**, and keeping those paths from colliding is
the whole design:

- **Arrows and Escape** arrive as `UIKey` events in `UIKeyboardState` and are
  handled at the END of the existing consumption chain: a text caret takes them
  first, then a tab strip, then a focused slider's notch, then page scrolling,
  and only then do they fall through to a focus move. The precedence is free,
  by construction rather than by special case.
- **WASD** goes through `InputMap` and `UINavSynth`, because `UIKey` has no
  letters and never will — a letter is text.

The arrows are deliberately **not** bound as nav actions. They would then
arrive down both paths in one frame, and a focused slider would move two
notches per tap.

That leaves one problem, which is why `InputMap` queries take a source filter:
WASD navigating a menu and WASD typing a name are the same four keys, and the
**pad** half of those actions types nothing and must stay live. Unbinding kills
both halves and `setSuppressed` kills the whole map, so hosts pass
`!wantsTextInput()` and only the key half goes quiet.

### How a move is chosen

`FocusMove` works over the laid-out rectangles, in two stages: **the nearest row
in the direction of travel wins, then the nearest control within it.**

A single weighted score cannot express that, and the shipped menu proved it. A
VSYNC toggle sits at the far right of its row while a text field two rows down
is full width and therefore overlaps the quality chips horizontally — scored
together, the field won on a zero off-axis distance and Down skipped VSYNC
entirely. No choice of penalty fixes a false comparison between a near row and
a far one.

**Edges, not centres**, decide what counts as the next row: a candidate is
"below" only if it starts at or after where the current element *ends*. Same-row
siblings are therefore not below one another by construction, which is why a row
of chips costs **one** press to pass rather than three.

There is **no wrap** — Down at the bottom does nothing, because a menu that
teleports you to the top is disorienting. Tab still wraps; it is a different
gesture. With nothing focused, any direction focuses the first focusable, or the
first press on a freshly opened menu would read as a dead controller.

### Auto-repeat

`UINavRepeater` turns a held direction into the series of edges a UI expects:
one immediately, then a pause, then a run. It is a pure function of a held
direction and `dt`, so the feel of a menu is testable without hardware — and it
lives in one place so a keyboard and a pad cannot drift apart on it.

A new direction fires **at once**. Waiting out the delay before the first move
is the difference between a menu that answers and one that feels broken.

### Focus scopes

```xml
<Element name="verbs" focus-scope="true" if="!panelOpen"> ... </Element>
<Element name="settingsPanel" focus-scope="true"
         if="panelSettings" on-back="menuClosePanel"> ... </Element>
```

`focus-scope="true"` confines navigation to that subtree **while it is visible**.
`on-back="..."` is what B or Escape invoke while that scope is the innermost one.

Visibility is the single source of truth. A panel is shown by an `if=` reading
app state, and the scope stack follows **display**, so "open" and "where
navigation goes" cannot drift apart. Each scope remembers the last thing focused
inside it, so closing SETTINGS returns you to the SETTINGS verb rather than to
the top of the list.

Scopes nest, and backing out unwinds one level at a time.

### What back means

`UIDocument::Back` is one implementation shared by the pad's B and the
keyboard's Escape, so the two cannot drift on what backing out means. In order:

1. **A text field you are typing in is the innermost thing you are in**, so back
   leaves the field and nothing else. A field inside a panel with an `on-back`
   would otherwise lose the whole page to one Escape mid-word.
2. Otherwise the innermost open **scope** is asked, by bubbling a `Back` event
   to it. The document does not close the panel itself — a panel is visible
   because an `if=` reads app state, so the app has to flip that state.
3. With no scope open, back **blurs**.

If nothing handled it, the document says so rather than inventing a meaning.
A scope with no `on-back` declares a navigation region, not a back action, so
back there belongs to the game:

```cpp
uiWorld.Update(reg, w, h, dt);
if (uiWorld.backWentUnhandled()) closeThePauseMenu();   // or quit
```

Without that, back at a root menu would be silently swallowed by the scope that
happens to be on the stack, and "close the menu" would have no way to exist.

### Button prompts that follow the device

Telling a keyboard player to press A is a small lie that makes a menu feel
ported rather than made. `UIWorld` works out which grammar is live from the
pointer, keyboard and nav state it already receives — no fourth thing for a host
to feed and forget — and publishes it to the shared source every frame:

| Property | Value |
|---|---|
| `uiDevice` | `"gamepad"` or `"keyboard"` |
| `uiPad` / `uiKeyboard` | bools, for gating two sets of glyph **art** with `if=` |
| `uiTyping` | a text field has focus |
| `uiGlyphSelect` | `A` / `ENTER` |
| `uiGlyphBack` | `B` / `ESC` |
| `uiGlyphNav` | `L STICK` / `ARROWS` |

```xml
<Label class="keycap" text="{uiGlyphSelect}"/>
<Label class="keyname" text="SELECT"/>
```

It is **sticky**: only activity flips it, so a pad resting on the desk does not
keep stealing the prompts back and the legend does not flicker every time you
stop to read it. Reaching for the **mouse** counts, before any button is
pressed — that is the moment you have switched.

`uiGlyphNav` says `ARROWS` and never `WASD`, even though both drive the menu.
Only one of them always does: W, A, S and D become letters the moment a field
takes focus and the field eats all four, so a prompt naming WASD is wrong at
exactly the moment a player tries it — and one that swaps only while typing
spends most of its life advertising the fragile half. WASD stays a convenience
you find rather than one you are promised. `uiTyping` is published for a game
that wants to say more than one label can.

Size a keycap to its **content**. A fixed 24px circle sized for "A" cuts
"ENTER" clean in half.

---

## Text entry

```xml
<TextField name="nameField" class="field" value="player one" maxlength="24"/>
<TextField name="pin" mask="*" maxlength="8"/>
<TextField name="notes" multiline="true" maxlength="512"/>
```

A `TextField` is focusable by default (a field you cannot focus is a label) and
shows its **`value`** — writing `text=` on one is an error, because the two
would disagree the moment anyone typed. `maxlength` is a **byte** budget, which
is the limit a caller can reason about without knowing what the user will type;
it only ever truncates on a character boundary. `mask` renders one glyph per
*character*, so a masked field never leaks the byte length of non-ASCII input.
It also stops a click from moving the caret at all: a click is measured against
the string that is *drawn*, and an offset into a row of asterisks is not an
offset into the value, so a masked field keeps the caret it had rather than
jumping somewhere arbitrary. Use the arrows, Home and End inside one.

**Every offset is a byte offset into UTF-8, and every operation moves by whole
codepoints.** That is the only representation the renderer and the font can use
without converting, and it means a caret can never land inside a multi-byte
character — which is the classic way text fields corrupt anything but ASCII.

| Input | Does |
|---|---|
| typing | inserts at the caret, replacing any selection |
| Backspace / Delete | removes the selection, else one whole character |
| Left / Right | moves one character; with Shift, extends the selection |
| Up / Down | moves a line, keeping the column (multi-line only) |
| Home / End | jumps to either end of the line; with Shift, selects to it |
| Enter | inserts a newline (multi-line only) |
| Ctrl+A | selects all (a bare `a` stays typeable) |
| Ctrl+C / X / V | copy, cut, paste — see below |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | undo, redo |
| click | places the caret at the nearest character boundary (not on a masked field — see above) |
| Tab | **leaves** the field — it is not consumed |

Editing runs as a **default action**: the `KeyDown` or `TextInput` event is
dispatched and bubbles first, and the field only acts if nothing called
`StopPropagation`. That is the DOM's ordering, and it lets an app pre-empt a key
without the field knowing about it. `ValueChanged` fires only when the value
actually changed — moving the caret is not an edit.

**Multi-line.** `multiline="true"` is what makes Enter, Up and Down mean
anything, and it is what turns Home and End from "the value" into "this line".
The attribute is an error on anything but a `<TextField>`.

A single-line field declines Enter, Up and Down — but they do not simply vanish
into whatever contains it. A handler on the field or an ancestor sees each of
them first and can still claim one, which is how you make Enter submit a form.
If nothing does:

- **Up and Down become directional focus navigation**, like any other unclaimed
  arrow. With one line there is nowhere to move, so the field is not a dead end
  for anyone without a mouse.
- **Enter blurs the field**: commit and leave. That is the same outcome Escape
  and the pad's B give, deliberately — `bind-value` publishes every keystroke as
  it is typed, so there is no pending edit for one key to keep and the other to
  throw away.

Home and End the field keeps either way; single-line they act on the whole
value, which is why they never reach a scroller wrapped around it.

**A field always clips to its own box and always scrolls its text to follow the
caret**, whatever `overflow` says — `overflow` governs children, and a field has
none. So a long value stays inside its box and typing past the width scrolls
rather than spilling.

A field **does** measure itself, like any other text leaf — because that is what
it is. Its `value` becomes the text yoga measures, so the box takes the width of
its widest line, clamped to whatever the parent offers, and the height of its
line count. An unconstrained multi-line field therefore grows a line every time
you press Enter and never scrolls vertically, since its box always already fits
its text; and an empty one has nothing to measure and collapses to its padding.
Give a multi-line field a `height`, or a `max-height` if you want it to grow to
a limit and scroll past that.

A **multiline** field paints a scrollbar once its text is taller than its box,
and that bar drags and takes the wheel like any other. A single-line field never
does: no native one has a bar, and it would eat 8px of a 180px control. The bar
reads from the same offset the glyphs do, so the two can never disagree.
`scroll-behavior` is deliberately ignored by a field — it follows its caret, and
a caret arriving somewhere the view is still travelling toward is worse than no
animation.

**Undo** coalesces a burst of typing into one step, because undoing a word one
letter at a time is not what anyone means by it. A deletion, a Home or End jump,
a line move with Up or Down, or typing after an undo all start a fresh run, and
the history is capped at 64 steps. Left, Right and clicking to place the caret
do **not** break the run today, so typing either side of one of those lands in a
single step. Writing the value from outside — a binding, `setValue`, a load —
**clears the history**: an external write is not an edit the user made, and being
able to undo back to a value you never typed is worse than not being able to
undo.

**Clipboard** goes through the host, because the engine has no business knowing
whether it is running under GLFW, ImGui, or a test:

```cpp
world.SetClipboardHandlers(
    [w](const std::string& t) { glfwSetClipboardString(w, t.c_str()); },
    [w] { const char* s = glfwGetClipboardString(w); return s ? std::string(s) : std::string(); });
```

The Player wires GLFW and the Editor wires ImGui, so a copy in a HUD field and a
paste in an Inspector field are the same clipboard. Wire nothing and the keys do
nothing — that is deliberate: a private buffer the rest of the machine cannot
see would look like a working clipboard right up until you paste elsewhere.
Copy and cut always use the **real** value, never the mask, so a masked field
does not put a row of asterisks on the clipboard.

Reach the editing model with `element->textEdit()` (null on anything that is not
a field). The caret blinks on a one-second cycle and is reset to solid on every
edit, because a caret blinking to its own schedule while you type reads as
dropped input. Drive it by calling `doc.AdvanceTime(dt)` once per frame, and
pass the font to `UpdatePointer` so a click can place the caret.

---

---

## Sliders

A `<Slider>` is an interactive **box**, not a painted widget. It owns a number
and knows how to turn a cursor position into one; the look is whatever you put
inside it and style. That is the same division as everything else here —
structure in markup, appearance in the stylesheet, and only the part that
cannot be authored written in C++.

```xml
<Slider name="volume" class="slider" bind-value="menuVolume"
        min="0" max="1" key-step="0.01" key-step-max="0.05" key-ramp="10">
  <Element name="volumeFill" class="slider-fill"
           bind="width: {menuVolume | percent}"/>
</Slider>
```

```css
.slider      { flex-grow: 1; height: 14px; border-radius: 7px; }
.slider-fill { height: 100%; border-radius: 7px; pointer-events: none; }
```

The fill is an ordinary child bound to the same value. Nothing about it is
special-cased: a thumb, a notched track or a radial dial are the same idea with
different markup.

`bind-value` is two-way through exactly the mechanism a `<TextField>` uses,
because it is the same relationship — the element owns a value, the source
wants it, and either end may move it.

| Attribute | Means |
|---|---|
| `min` / `max` | The range, in **your** units. A volume is 0..1, a field of view is 60..110. |
| `value` | Where it starts, if no binding supplies one. |
| `step` | Quantises the value. `0` (the default) is continuous. A "quality 1..5" slider wants this; a volume slider must not have it. |
| `key-step` | What ONE digital press moves. |
| `key-step-max` | What a HELD press ramps up to. |
| `key-ramp` | How many presses of a held run reach `key-step-max`. |
| `vertical` | A mixer channel. Only the axis the drag reads changes. |

### Three inputs, three grains

This is the part worth understanding, because getting it wrong is what makes a
slider feel broken.

**A pointer is continuous.** Pressing anywhere on the track jumps there and
begins a drag, and the drag is a pointer **capture**: it keeps tracking after
the cursor leaves the element, and past either end of the track, until the
button comes up. Without capture, dragging quickly off the end of a volume bar
would drop the drag and leave the value wherever your hand happened to be.

**A stick is analog**, so it gets an analog response: full deflection crosses
`analogSeconds` worth of range per second (1.5s across the whole range by
default), scaled by how far it is pushed. Stepping a stick in fixed notches
because the input arrived as a d-pad event is what makes a pad feel like it is
snapping.

**A key or d-pad is digital, and it accelerates.** One press moves `key-step`,
because a tap is how you ask for a small change and a control that only moves
in 5% jumps cannot be set to 43%. Holding ramps to `key-step-max` over
`key-ramp` presses, because 1% a notch across a whole range is a long wait.

The ramp is counted rather than timed on purpose. A keyboard's auto-repeat rate
belongs to the operating system and a pad's belongs to `UINavRepeater`, so a
time-based ramp would feel like two different controls; "ten notches to full
speed" is the same promise on both.

A run ends by **reversing** or by **going quiet**. Reversing means you
overshot and want the fine grain back to land where you meant; quiet means you
let go, and without it ten deliberate taps would accelerate as though you had
held. Quiet needs a clock, which a press does not have, so `UISliderState::Tick`
runs from `UIDocument::AdvanceTime` — and its idle window is deliberately longer
than the pad's own 0.40s auto-repeat delay, or a held d-pad would reset its run
between the first press and the second and never accelerate at all.

Declaring only `key-step` keeps the old behaviour, every press the same size:
the ceiling rises to meet a coarser declared floor. Declaring both and
inverting them is a load error, because holding moving *slower* than tapping
has no sensible reading.

### Focus

`:focus` on a slider is worth a word of warning. `border-width` is **paint
only** — it does not inset the content box — so at 100% a full-width fill
covers a focus ring entirely, and a focused slider ends up pixel-identical to
an unfocused one. Light the **fill**, not just the ring:

```css
.slider:focus            { border-color: rgba(255, 196, 72, 1); }
.slider:focus .slider-fill { background-color: rgba(255, 255, 255, 1); }
```

---

## Layout: flexbox

Layout is solved by **yoga** — the same engine Unity's UI Toolkit uses. No yoga
type appears in any engine header: each `UIElement` holds an opaque handle, so
the layout engine can be replaced without touching a line of authored UI.

Everything behaves as CSS flexbox does, which means the usual reflexes apply:
`justify-content: space-between` on a row pushes children to opposite ends at
any width, and an absolutely-positioned child with `left: 0; top: 0; right: 0;
bottom: 0` plus centring alignment stays centred at every viewport shape — no
arithmetic on the viewport size anywhere. (There is no `inset` shorthand; the
four edges are written individually, as `.centre-overlay` in the sample HUD
stylesheet does.)

`UIDocument::Layout(w, h, font)` fills every element's `ComputedLayout`
(absolute position + size, in screen pixels). The `font` may be null: text
elements then measure as empty and still lay out, so a missing font costs you
labels rather than the whole HUD.

**Gotcha:** `font-scale` is the one measurement input the layout engine knows
nothing about — it feeds text measurement but is not a yoga style. The engine
invalidates the cached measurement for you when it changes; if you ever add
another property that only `measureText` reads, it needs the same treatment or
glyphs get drawn at a size their own box was never measured for.

**Gotcha:** yoga snaps to the pixel grid, and rounds sizes and absolute
positions independently — expect results to differ from hand arithmetic by up to
a pixel. Also, `min-*`/`max-*` clamp the flex *base* size before free space is
distributed (Flexbox §9.2 step 4), so a clamped item does not simply lose its
clamped amount from the growth pool.

---

## Scrolling and clipping

`overflow` decides what an element does with content bigger than its box:

| Value | Does |
|---|---|
| `visible` (default) | content paints outside the box |
| `hidden` | clips to the box |
| `scroll` | clips **and** scrolls |

`overflow-x` and `overflow-y` set one axis each; `overflow` is the shorthand for
both, and source order within a rule decides — `overflow: hidden; overflow-y:
scroll` clips across and scrolls down.

One inherited CSS rule matters: **setting one axis to anything but `visible`
promotes the other**. That is not a convenience, it is forced — clipping is a
rectangle, so "clip Y but not X" has no representation. An element therefore
clips on both axes or neither, even though it can *scroll* on just one.

`auto` is **not** a keyword and is reported as an error. CSS `auto` means "a bar
only when needed", which is exactly what `scroll` already does here; the one
thing CSS `scroll` adds is an always-painted bar, and that is a property of the
bar — see `scrollbar-visibility` below.

```css
.log {
  overflow: scroll;
  height: 120px;      /* required — see below */
  width: 320px;
}
```

**A scroller needs a definite height** (a `height`, a `max-height`, or a
shrinkable slot). `scroll` forces `flex-shrink: 0` on the scroller's own in-flow
children, because otherwise flexbox squeezes them to fit, the content extent
always equals the box, and there is nothing left to scroll. That is the *whole*
mechanism: yoga's own overflow flag turns out to have no effect on layout at all,
so this is the only lever there is. The consequence is that a scroller with no
height of its own grows to its content and escapes its parent.

**Absolutely positioned children are pinned to the scroller's box**, not scrolled
with it. That is a deliberate divergence from CSS, taken because there is no
`position: fixed` or `sticky` here to offer instead — and because the alternative
is wrong twice over: an overlay pinned to all four edges that scrolled away would
stop covering *and* stop blocking clicks. It buys sticky headers and lock veils
for free. A scroller's own `text=` does not scroll either; put a header in a
sibling element.

**Scrollbars are overlays.** They reserve no gutter, so they paint over the right
edge of the content — add `padding-right` if that matters. A bar that changed the
layout could make itself disappear, and then reappear, forever. The thumb is
draggable, and pressing it never reaches the content underneath.

They are styleable: `scrollbar-width` (0 hides the bar while leaving the panel
scrollable), `scrollbar-min-thumb`, `scrollbar-color`, `scrollbar-thumb-color`,
and `scrollbar-visibility: auto|always` — `always` paints a bar on a scrollable
axis even while the content fits, for a panel whose scrollability should be
advertised.

The five are per **element**, not per axis: one declaration dresses that
element's horizontal bar and its vertical one together. There is no
`::-webkit-scrollbar-thumb:horizontal` equivalent, because wanting a panel's two
bars to differ is rare enough not to be worth doubling the vocabulary. And
**nothing in this system inherits**, so a rule dresses only the elements it
matches — to restyle every bar in a document, say it once with the universal
selector: `* { scrollbar-thumb-color: #888; }`.

**Chaining is per gesture.** A wheel *gesture* latches onto whatever it first
moved and keeps it until you pause — so flicking through a list to its bottom and
carrying on does **not** drag its container out from under the cursor. Start a
fresh gesture over an exhausted list and it does chain outward, to the first
ancestor with room **in that direction**, exactly as a browser does. Latching is
what makes chaining safe; neither ships without the other.

**One wheel, two axes.** An element that scrolls only horizontally — a hotbar, a
card strip — takes a plain vertical wheel on its horizontal axis, because a mouse
has one wheel and such a panel would otherwise be visibly scrollable and
completely unreachable. An element with range on *both* axes keeps the vertical
wheel vertical; use **Shift+wheel** to reach its horizontal axis. The swap is done
once, in the default action, so the Game view and the shipped player cannot
disagree about it.

**Keyboard.** A scroller with `focusable="true"` is a Tab stop, and then takes
**PageUp/PageDown** (90% of the box, the browser overlap) and **Home/End**. Those
also work from any focused descendant, so a focused row still pages its list.
Inside a focused `<TextField>` the split follows a browser `<textarea>`: Home and
End belong to the *field* (they are line-aware there), while PageUp and PageDown
pass through to the container.

**The scrollbar is a control, not decoration.** Dragging the thumb scrolls;
pressing the **track** pages toward the click — one page per press, with no
hold-to-repeat — and pressing either focuses the scroller if it is focusable.
Neither reaches the content underneath.

`pointer-events: none` disables scrolling too, and lets the wheel reach the
document beneath. Put it on the decorative parts of an overlay, not on a
container you want to scroll.

From C++:

```cpp
el->SetScrollOffset({ 0.f, 120.f });     // clamped; visible at the next Layout
el->ScrollIntoView(child->layout().position, child->layout().size);
el->scrollOffset(); el->maxScroll(); el->contentSize();
```

**Long lists.** The engine does **not** virtualise for you, and the reason is
worth knowing because it is not the one you would guess: a scroller's extent is
*derived from the children that exist*. Keep six live rows out of a thousand and
it honestly reports six rows of content — the range collapses, the thumb
vanishes, and the wheel walks straight past. `SetContentExtent` is the override:

```cpp
list->SetContentExtent({ 0.f, rowCount * rowHeight });   // declare the truth
// then place a window of live rows yourself from list->scrollOffset()
```

With that, a hand-rolled virtual list works today with no bindings anywhere —
declare the extent, keep a window of absolutely-positioned rows, and reposition
them each frame from the offset.

[`repeat=`](#collections-repeat) gives you the same shape declaratively: a fixed
pool of rows with the data sliding through it. What it does *not* do is drive the
scroller — the pool is the only thing in the tree, so the extent it reports
describes the pool rather than the list. Combining the two means declaring the
extent yourself, exactly as above, and feeding `repeat-offset` from
`scrollOffset()`. That pairing is not wired up for you.

Drawing is **culled** against the clip a subtree sits under, so rows scrolled out
of view cost no draw calls. Layout is not: every row still measures and solves
every frame, which is the other half of why a genuinely huge list wants the
windowing above.

`ScrollIntoView` is how you pin a log to its tail. Focus already uses it: Tabbing
to a control below the fold scrolls it into view, because a `:focus` ring nobody
can see — over a control Enter would then activate — is worse than no focus ring.

**Smooth scrolling is opt-in**, through `scroll-behavior: smooth` (`instant` is
the default, and `auto` is accepted as CSS's spelling of it). Every scroll on
that element — wheel, keys, thumb drag, `SetScrollOffset`, `ScrollIntoView` —
then moves a *target* that `AdvanceTime` eases the view toward, frame-rate
independently, snapping inside half a pixel so the document stops relayouting.
It is opt-in because `Layout()` is otherwise a pure function of the tree, and
making every scroll depend on the clock is a large change to buy an animation
most HUDs do not want. Kinetic/touch momentum is a different thing and is **not**
implemented — there is no touch input in this engine to carry it.

**Across a hot reload a scroll position is lost**, because a successful reload
rebuilds the tree and drops focus and hover with it; singling scroll out to
survive would be more surprising than losing it. A *failed* edit keeps the last
good tree and therefore keeps your place.

---

## Behaviour: elements and events

`UIElement` (`Engine/src/ui/UIElement.h`) is retained: build the tree once and
**mutate** it. That is the opposite of ImGui and the right model here, because
layout is expensive, elements need identity for events, and most frames change
nothing.

```cpp
UIElement* btn = doc.root().Find("scoreButton");
btn->OnClick([&](UIEvent& e) { score += 100; });
btn->style().backgroundColor = { 0.2f, 0.2f, 0.2f, 1.0f };
label->setText("SCORE " + std::to_string(score));   // not style().text — see below
```

Events are DOM-style: they fire on the deepest hit element and **bubble** up
through its ancestors, with `e.target` (what was hit) and `e.currentTarget`
(whose handler is running) kept separate, and `e.StopPropagation()` to halt the
walk. Multiple handlers per type run in registration order.

Available: `OnClick`, `OnPointerDown/Up/Move/Enter/Leave`, `OnWheel`,
`OnFocusIn`/`OnFocusOut`, `OnKeyDown`, `OnTextInput` and `OnValueChanged` — a
thin wrapper over `AddEventListener` each.

`Back` is the one event type with no wrapper, because it does not go to whatever
was hit or focused: `UIDocument::Back` bubbles it from the innermost open focus
scope, so it belongs to the few elements that declare one rather than being a
shorthand every element wants. Attach it with `on-back="..."` in markup, or
`AddEventListener(UIEventType::Back, ...)` in C++. A **click** requires press
*and* release on the same element, so sliding off a button before letting go
correctly cancels it. Prefer an authored `on-click="actionName"` where you can —
a bound action survives a hot reload by itself, while a handler attached in C++
has to be re-attached.

`OnWheel` carries the notch count in `e.delta`. Note that an authored
`on-wheel="name"` **observes** the wheel without claiming it: the built-in scroll
still runs afterwards. Only a C++ handler calling `StopPropagation` suppresses
it, which is the same default-action ordering `KeyDown` uses to reach a text
field.

**Gotcha:** use `setText()`, not `style().text = ...`. Writing the style field
directly does not invalidate the measured size, so the label keeps its old width
until something else dirties it.

---

## Input

The UI does not read the mouse itself, because only the host knows where the UI
surface sits. Fill in a `UIPointerState` in **UI-local pixels** and hand it over:

```cpp
UIPointerState p;
p.position   = { mouseX - uiOriginX, mouseY - uiOriginY };
p.inside     = /* pointer is over the UI surface at all */;
p.buttonDown = /* left button held */;
p.wheel      = { horizontalNotches, verticalNotches };  // see below
p.shift      = /* either Shift key held — the only input to the Shift+wheel axis swap */;
```

`wheel` is the odd one out: the first three are level snapshots, but it is an
edge-triggered **delta** in notches, cleared by `UIWorld::Update` the same way
the keyboard is. A positive component means the *content* moves that way, so
rolling the wheel up gives `y > 0`. Hosts convert their platform's sign into that
convention and nothing else — the pixels-per-notch constant lives in one place,
next to the default action, so the Game view and the shipped player cannot
disagree about how far a notch goes.

In the Player the UI covers the window, so window coords *are* UI coords. In the
editor's Game view the UI lives inside a dock panel, so the panel origin is
subtracted and clicks are routed only when ImGui is not using the mouse for its
own dragging.

The keyboard needs the same arbitration and gets it the same way — from the
host. The Game panel holds the editor's own toolbar widgets as well as the
game's UI, so it hands keys to the game only once you have **clicked the game
image**; clicking the toolbar takes them back. Without that a single Tab moved
focus in two places at once. See [Game panel](editor.md#who-owns-the-keyboard).

`UIDocument::HitTest` returns the deepest **pickable** element containing the
point. Topmost wins (children tested in reverse paint order), an
`overflow: hidden` **or `scroll`** parent rejects its whole clipped-away subtree
— a row scrolled out of view is painted nowhere and must not be clickable — and
`pointer-events: none` makes an element *and its subtree* inert, which is what a
full-screen decorative overlay needs, or it swallows every click beneath it.

> **Gotcha:** there is no `position: fixed` and no portal, so a popup or dropdown
> authored *inside* a scroller will be clipped by it. Make it a child of the
> document root instead.

**Order matters** and `UIWorld::Update` enforces it: `Layout` first (hit-testing
reads computed rects), then `UpdatePointer` (handlers may change styles), then
`Draw`. Anything else makes a press visible a frame late.

---

## Data binding

Gameplay writes **values**; the markup decides how they read. That is the fourth
pillar, and it is what lets a HUD's format, units and colour ramp be content
rather than code.

```cpp
// The model. Register it BEFORE Load when you can.
UIDataSource src;
src.SetInt("score", 0);
src.SetNumber("health", 1.0f);
src.SetBool("lowHealth", false);
src.AddAction("addScore", [&] { src.SetInt("score", src.GetInt("score") + 100); });
assets.bindingContext().RegisterSource("hud", &src);
assets.bindingContext().converters().Register("healthTint", ...);  // not a builtin; see DemoUIContent.cpp
```

```xml
<UI name="hud" data-source="hud">
  <Element name="healthFill" class="fill"
           bind="width: {health | percent};
                 background-color: {health | healthTint}"/>
  <Label name="scoreLabel" text="SCORE {score}"/>
  <Button name="scoreButton" text="+100" on-click="addScore"/>
  <Element name="lowHealth" class="warning" if="lowHealth" text="LOW HEALTH"/>
</UI>
```

Then, once per frame, **before** `Layout`:

```cpp
assets.binder().UpdateToTarget();
```

**The load-bearing property: the data source is not in the element tree.** A hot
reload destroys every element and every handler without touching one value, so
unlike a cached `UIElement*`, a binding needs nothing re-attached in C++. The
"re-push everything you cached" step disappears — and forgetting it is the
mistake every hot-reloading UI makes exactly once.

### Holes

```
{ path | converter | converter : decimals }
```

- `path` is `property`, resolved against the nearest `data-source` ancestor, or
  `source.property` to name one explicitly. The dot **always** separates source
  from property — one rule, no ambiguity.
- Converters run left to right. Eleven ship: `percent` (0–1 → a CSS percentage,
  unit included), `ratio` (0–1 → 0–100), `px`, `not`, `round`, `floor`, `ceil`,
  `abs`, `int`, `upper`, `lower`. Register your own on
  `bindingContext().converters()`.
- `:N` formats to N decimals; omitted means `%g`, so an integral number prints
  `100` and not `100.000000`.
- `{{` and `}}` are literal braces.

**There is no expression language, deliberately.** Arithmetic and comparison
would need `<` and `&&` inside XML attribute values, which XML forbids — every
comparison would have to be written backwards (`0.3 > health`) forever. Derived
values are named C++ converters instead: real functions you can breakpoint and
unit-test, whose typos are answered with the list of names that exist.

### Values

`UIValue` carries a bool, int, number, length, colour or string. **Coercion
never guesses**: every conversion that is not obviously correct fails, and the
report names the kind it got and the property it was for (`cannot use colour as
a value for 'width'`) — or, when a string is offered and the declaration grammar
cannot parse it either, that parser's own message (`bad length 'rifle'`).
Without reflection, a value that converted to something plausible but wrong is
indistinguishable from a UI that simply doesn't work.

A string is never a bool, for the same reason — `"false"` is truthy under one
obvious rule and falsy under another. Strings *do* parse as lengths and colours,
through the stylesheet's own parsers, so `"50%"` and `"#d93a3d"` mean the same
thing in a bound value as in a `.cstyle` file.

### Two ways to supply a value

| | Where the value lives | Cost per frame |
|---|---|---|
| `Set*(name, v)` | in the source | one integer compare |
| `Observe(name, getter, setter)` | in **your** object | the getter runs every frame |

`Set*` is equality-gated, so gameplay writing the same health every frame wakes
nothing downstream. Prefer it for anything hot; `Observe` is for a value that
already has a home you cannot hook.

### Diagnostics

| Problem | When | Effect |
|---|---|---|
| unknown attribute, malformed hole, unknown bound property, constant `bind` | load | **fails the load**, running UI untouched |
| unknown source / property / converter | `Rebuild` | reported once **with the names that do exist**; the binding stays pending, and registering the source rebuilds at once |
| unknown **action** | `Rebuild` | reported once with the action names that do exist, and **no listener is attached** — the control is dead, not pending. `AddAction` bumps no version and unresolved actions are not tracked the way bindings are, so registering it later fixes nothing by itself. Register actions before the document loads |
| value won't convert, non-finite, negative size | runtime | reported once per binding, re-armed on a kind change; that write is skipped |
| a bound property that the stylesheet also declares | load | a **note** — the rule is the pre-bind default, and saying so beats editing the `.cstyle` and watching nothing happen |

`binder().Describe()` prints one line per live binding with its current value.
"It is not in this list" is a one-call diagnosis of a frozen readout.

### Toggling classes

```xml
<Label class="readout" classes="alarm: {lowHealth}; boosted: !{tired}"/>
```

Shaped like `bind=` rather than a `class-<name>=` family, which would sit beside
`class=` meaning something quite different, and it goes through the same
brace-aware splitter so the two can never disagree about where an entry ends. A
leading `!` negates; a value with no `{}` is an error (use `class=` for a
constant).

Toggling changes **which rules match**, and the cascade has no undo — so the
element is reset and re-cascaded, exactly as `:hover` is, and its other bindings
are re-applied afterwards because that reset discards what they wrote. Writing
the class an element already has does nothing at all.

### Element to source

Two families flow the other way:

```xml
<TextField bind-value="playerName"/>          <!-- two-way -->
<Button push-hovered="isOver" push-pressed="firing" push-focused="typing"/>
```

Drive them with `PublishToSources()` once per frame, **after** `UpdatePointer`
and `UpdateKeyboard` — that is where the state and the values they publish are
decided.

Both take a **bare path**, never a template, and that is a design limit rather
than an omission: you cannot un-format a rendered string back into a value, so
anything carrying a converter chain or literal text (`"SCORE {score}"`) is
one-directional by construction. Pretending otherwise would ship a binding that
silently only worked one way.

A push target that does not exist yet is **created** — the element owns that
value, so an app should not have to declare it before the UI can publish it. A
**read-only** property (observed with no setter) is reported instead, because a
push that silently did nothing looks exactly like a UI that was never wired up.

Both directions are equality-gated against what the source actually holds, so a
round trip settles immediately instead of oscillating, and an idle element never
writes.

### Collections: `repeat=`

A `{hole}` addresses one property. To show a **list** — an inventory, a scoreboard,
a quest log — repeat one template over it:

```xml
<Element class="bag" repeat="inventory" repeat-count="5" repeat-offset="invSelected">
  <Element class="bag-row" classes="row-selected: {selected}">                     <!-- exactly ONE element child -->
    <Label class="bag-index" text="{$index}"/>
    <Label class="bag-name"  text="{name}"/>    <!-- bare: reads the ROW -->
    <Label class="bag-qty"   text="x{count}"/>
    <Label text="{scene.currency}"/>            <!-- qualified: reaches outside -->
  </Element>
</Element>
```

```cpp
UIList inv;
for (const Item& it : player.inventory) {
    UIRecord& row = inv.Add();
    row.SetString("name", it.name);
    row.SetInt("count", it.count);
}
src.SetList("inventory", std::move(inv));       // equality-gated, like every setter
src.SetInt("invSelected", 0);                   // the window start
```

**The pool is fixed and the window moves.** `repeat-count` elements are built once,
at load, and the tree never changes shape again. Each frame slot *i* is filled with
row `offset + i`.

That is the design, not a shortcut. `UIElement::structureEpoch()` is **process-wide**:
a list that added and removed children would make every binder in *every* document
re-collect and re-resolve on the frames it changed size. Here the elements stand
still and the data slides through them — and because every `UIDataSource` setter is
equality-gated, a slot whose row did not change writes nothing and re-applies
nothing. Sliding a window costs exactly the slots that actually changed. An idle list
costs the compares that prove it is idle — the source pointer, the list's
version and the clamped offset — plus the name lookups that produce them, since
`Refresh` runs every frame and there is no way to read a version without first
resolving it by name. That work is bounded by how many sources and properties
you have declared, not by `repeat-count` or by the length of the list, so a
64-slot pool over a 10,000-row list is still cheap on an idle frame — just not
free.

| Attribute | |
|---|---|
| `repeat="source.list"` | the list to repeat over; a bare name inherits the scope source |
| `repeat-count="5"` | **required.** Pool size, 1..64, fixed for the document's life |
| `repeat-offset="invSelected"` | optional path to a number, read every frame; the window start |

**The window is clamped to `[0, max(0, rows - count)]`**, so it never shows fewer
rows than the list could fill. Without that upper clamp the tail of a list is
unreachable as a group — a 12-row list through a 5-slot pool would end at offset
11 showing one item and four blanks, and the panel would appear to shrink as you
approached either end. A fixed pool displaying a partial window is the one thing
this shape must never do.

The clamp is also what lets you hand in a **selection index** rather than a scroll
position. The cursor walks every row while the window stops at the last full page,
so the chosen row is always on screen — which is exactly how a list view behaves,
for free. Slots only ever go empty when the list is genuinely shorter than the
pool.

Markup has no `==`, deliberately, so *which* row is selected arrives as **data**:
give the chosen row a `selected` column and toggle a class from it, as above.
`SetList` is equality-gated, so rebuilding the list on every cursor move costs one
comparison per row and wakes only the two rows that actually changed.

Inside a repeat, a **bare** hole reads the row and a **qualified** one reaches
outside. Four columns come from the engine rather than your data, named with a `$`
so they can never collide with a column of your own:

| | |
|---|---|
| `{$slot}` | this element's index in the pool, `0..repeat-count-1`. Constant |
| `{$index}` | the absolute row index it is currently showing |
| `{$count}` | how many rows the whole list has |
| `{$present}` | whether this slot has a row at all |

`$present` drives the row's visibility automatically, which is why a template root
may not carry its own `if=` — it would have to lose one or the other, and a surplus
row left visible is still laid out, painted and clickable. Put the condition on a
child, or publish it as a row column.

`$index` and `$count` are written **only when the template reads them**. An absolute
index moves on every window step and change detection is per source, so publishing
one nothing reads would re-apply the whole pool on every notch of a scroll that
changed two visible rows.

**Rejected at load**, each with a message rather than a silent skip: a missing or
out-of-range `repeat-count`; a container whose element-child count is not exactly
one; a template root carrying `if=`, `data-source=` or `repeat=`; a nested `repeat`;
`repeat-count` or `repeat-offset` without `repeat`; `repeat` on the document root;
and a **bare** `push-*`, `bind-value` or `on-*` inside a repeat — those would resolve
against the row, where the next frame's copy overwrites them.

A bare hole that no row provides is reported once, against the data, naming the
columns your rows actually have:

```
hud.cxml: repeat 'inventory': no row has a column 'playerName'
  (row columns: count, name) - inside repeat=, a bare {playerName} reads the ROW;
  write {source.playerName} to reach an outer source
```

Once per repeat, not once per slot: a pool of 64 would otherwise answer one typo
with 64 identical lines. The generated slot sources never appear in a diagnostic
either — a `(registered: ...)` list of 64 machine-made names buries the two you can
actually use.

**Known limits.** Change granularity is per slot, not per column: one column moving
re-applies every binding on that row, which is fine at three or four and caps how
wide a row usefully gets. Every clone shares the template's `name=`, so
`Find(name)` returns the first slot only. Hidden slots still cost a layout pass —
that is why the cap is 64. And a repeat inside `overflow: scroll` does **not**
virtualise: the scroller measures only the rows that are present, so the thumb
would describe the pool rather than the list. Window it by hand with
`SetContentExtent` (see [Scrolling](#scrolling-and-clipping)).

### Cost

An idle frame is one integer compare per source plus one per binding — no
allocations, no tree walks, no string work. Only a write that can change a
**box** triggers a re-layout, so a bound colour never does.

**That price holds only while nothing in play is polled.** A polled property has
no version to compare against, so the binder must read it and re-apply every
frame — and the test it uses, `hasPolled()`, is a property of the **source**,
not of the property. One polled value therefore re-applies *every* binding that
reads that source, every frame, whether or not anything changed. A bound length
re-writes its style and reports a layout as written, so a single polled property
can put a full `Layout` back into every frame.

This is easy to hit by accident, because **`Notify::Poll` is `Observe`'s
default**. Pass `Notify::OnWrite` and call `MarkChanged(name)` when the value
actually moves, or keep polled properties on a source of their own so they
cannot wake the bindings that read your `Set*` values.

---

## Tabs: `<TabView>`

```xml
<TabView name="demo" selected="0">
  <Tab label="Bindings">  ...content... </Tab>
  <Tab label="Scrolling"> ...content... </Tab>
  <Tab label="Inventory"> ...content... </Tab>
</TabView>
```

Expanded **once, at load**, into a generated header strip plus the authored
panels. Switching tabs writes a single bool — the tree never changes shape, for
the same reason `repeat=` expands at load: `structureEpoch()` is process-wide, so
creating and destroying panels would make every binder in every document
re-collect on the frames a user clicked a tab.

| | |
|---|---|
| `name` | **required** on `<TabView>`. Keys the state properties, the C++ lookup and every diagnostic |
| `selected` | optional, `0`-based, must be in range. Default 0 |
| `label` | **required** on `<Tab>`, legal nowhere else |

`<Tab>` is legal only as a direct child of a `<TabView>`, which may contain
1–32 of them and nothing else.

**Every other attribute on a `<Tab>` belongs to the PANEL** — `class`, `style`,
`name`, `data-source`, `on-*`, even `repeat=`. Only `label` is consumed by the
header. Worth knowing, because `<Tab>` is the one tag that produces two elements.

A `<Tab>` may **not** carry its own `if=`: the panel's visibility is an
engine-injected binding, and yours would be silently overwritten — the same rule,
and the same reason, as a `repeat=` template.

### Styling

| class | on |
|---|---|
| `.tab-view` | the `<TabView>` itself |
| `.tab-strip` | the generated header row |
| `.tab` | each header. Also carries `.selected` when it is the current one |
| `.tab-panel` | each panel |

`selected` is a plain **class**, not a `:selected` pseudo-class. A real one would
need a fifth bool on every `UIElement`, a parser entry, a compound-matcher case
and a fifth slot in the interaction styler's watch struct — to buy only that an
author could not remove it by hand. `.tab.selected { }` is the whole styling
story, and it has to sit *after* `.tab` in the file because they weigh the same.

### Keyboard

A header is a Tab stop. Once focus is on one, **Left/Right wrap, Home/End jump**,
and Enter re-selects. Arrows move focus **and** selection together, because Tab
can land on a header without an arrow ever being pressed, and a header that
highlights one panel while showing another is worse than either behaviour alone.

A key pressed *inside* a panel is not stolen: the strip only acts when the event
targets one of its own headers, so Left in a text field still moves the caret.

Switching away from a panel that owns focus moves focus to the **new header**
rather than dropping it — otherwise the next Tab would restart at the top of the
whole HUD, and on a document that is not the keyboard target the `:focus` ring
would stay lit inside a panel nobody can see.

### Reading and writing the selection

```xml
<TabView name="demo" bind-selected="hud.activeTab"> …           <!-- two-way -->
<Label text="TAB {__tabs.demo}"/>                    <!-- read-only, no wiring -->
```
```cpp
ad.tabView("demo")->Select(2);                                  // or from C++
```

`bind-selected` is a **bare path**, like every other write-back binding. Click a
tab and the property is written; write the property from gameplay and that tab
opens.

**The conflict rule**, because there has to be one: the **source wins when it
moved** since the view last looked, and the **element wins otherwise**. Gameplay
writing between frames therefore opens that tab, while a click made after that
check is published on the next pass — the most recent intent survives, which is
the same shape `bind-value` on a `<TextField>` already has.

The property is **created** if the app never declared it, exactly like a `push-*`
target. An out-of-range value is clamped *and written back*, so the game never
ends up holding an index the UI is ignoring. A **read-only** property (observed
with no setter) is reported at load rather than dropped — a link that silently
worked one way looks exactly like a UI that was never wired up.

It is driven by `UITabView`, not by `UIBinder`, because that is where the
selection already lives. The tempting shortcut — reusing the existing
`Kind::Value` channel — resolves cleanly, reports `ok()`, and then never writes
anything, because that path reads a `UITextEdit` and bails when there isn't one.

`__tabs` is a reserved source the document registers for you, with one integer
per TabView (`demo`) and one bool per tab (`demo_0`, `demo_1`, …). It is the
read-only view; `bind-selected` is the one that writes.

Without a `bind-selected`, selection resets to the markup default on a hot
reload. With one it comes back from the source, so a saved menu re-opens where
it was.

---

## Borders, corners and images

The stylesheet's paint vocabulary, beyond a flat `background-color`:

```css
.panel {
  background-color: rgba(28, 32, 42, 0.92);
  border-radius: 10px;
  border-width: 1px;
  border-color: rgba(255, 204, 68, 0.35);
}
.hero {
  background-image: "Exported/UI/art/menu_backdrop.png";
  background-size: cover;                 /* or stretch, the default */
}
```

| | |
|---|---|
| `border-radius` | non-negative **px**. `9999px` is the pill idiom — it clamps to half the shorter side |
| `border-width` | non-negative **px** |
| `border-color` | any colour the sheet understands |
| `background-image` | a **quoted**, project-relative path |
| `background-size` | `stretch` (default) or `cover` |

The radius and the border are **one** signed-distance evaluation in the shader,
so the border is a band of the same field rather than a second quad. That is what
stops a child's layer landing between an element's fill and its own border.

**The border does not affect layout.** In CSS `border-width` sits between padding
and margin and shrinks the content box; here it is paint-only. Making it
layout-participating would move the text origin and the click-to-caret arithmetic
of every `<TextField>`, and a caret shifting because somebody added an outline is
a worse bug than content sitting under a 2px rule. **Keep `padding` ≥
`border-width`.**

Both are authored lengths, so both scale with [the UI scale](#scaling-to-the-screen).

### Gradients

Two stops, on the background, on any axis:

```css
.verb:focus {
  background-color: rgba(255, 196, 72, 0.28);   /* the FROM stop */
  background-color-to: rgba(255, 196, 72, 0.04);/* the TO stop   */
  background-gradient: horizontal;              /* or vertical, or none */
}
```

`background-gradient` defaults to `none`, and with it the `-to` colour is
ignored entirely — so adding the property to a stylesheet changes nothing until
you ask for it.

There is no shader for this and no second draw. The vertex colour is already
interpolated across the quad, so two corner colours **are** the gradient: a
gradient fill costs exactly what a flat one costs. That also bounds what it can
do — two stops, corner to corner. A three-stop ramp, a radial, or a gradient at
an arbitrary angle is not expressible; for those, author the ramp as an image
and stretch it, which is what the menu's scrim does.

A gradient degrades rather than misdraws: with the mode at `none` — or on a
context where the box shader is unavailable — the element falls back to the
plain sprite path and paints its flat `background-color`. Two equal stops are
not a special case; they go through the same path and simply come out flat.

Good places for it: a focus band that fades out across a button, a slider fill
with a lit top edge, a card that is a soft wash rather than a flat slab. Bad
place: anywhere the two stops are far enough apart to band, since there is no
dithering.


### What a rounded element does not do

**It does not clip its children round.** Clipping is the axis-aligned scissor
test, so an opaque child painted into a rounded corner squares it off. Give the
children matching insets.

Scrollbars *are* handled: a rounded scroller pulls both tracks in by its radius,
so a bar cannot paint its square end outside the silhouette.

### Images

`background-image` paints as its own quad **over** `background-color`, never as a
tint of it — tinting is the obvious design and it is fatal, because the default
background is fully transparent and the first panel anybody writes would show
nothing at all.

`cover` centre-crops to fill the box without distorting; `stretch` fills it
exactly and distorts. There is no `contain` (it leaves gaps only the fill colour
can cover) and no 9-slice (nine quads and four more authored insets). A panel
border image will therefore look wrong at any size but its own — use
`border-width` for rules and keep images to backdrops, logos and noise.

Paths are **quoted** and run through the same containment check as models,
scripts and clips, before the file is opened. So is the `.cstyle` path itself.

`background-image` **cannot be bound**. Paths are interned to ids that never
change meaning, so binding one would grow that table without bound. Toggle a
class whose rule names a different image, or swap the element with `if=`.

### Textures and the host

A `UITextureCache` turns interned paths into GL textures, uploading each once and
caching failures so a missing file costs one message rather than one per frame.
The host owns it, **one per GL context** — the editor runs a second renderer for
its Game view — and hands it to `UIWorld::SetTextureCache` the same way it hands
over the font. Without one, images simply do not paint.

### Placeholder art

`Editor/src/Exported/UI/art/` ships four deliberately-fake images, regenerated by
`tools/gen_ui_placeholder_art.py` (needs Pillow). Edit the script, re-run, rebuild
— or write straight into the staged copy next to the executable to iterate live.

---

## Scaling to the screen

Every length in a `.cstyle` is a number of pixels, which is the only thing you can
reasonably write. It is also wrong on every screen but one: a HUD authored against
1920x1080 occupies a quarter of a 4K display and overflows a 1280x720 one.

Set a **reference resolution** on the `UIDocumentComponent` — Inspector ▸ **UI
Document ▸ Scaling** — and the whole document scales by how far the real surface
departs from it.

**Scale Mode is `Constant` by default, and `Constant` ignores everything below
it.** `ComputeUIScale` returns `1.0` for `Constant` before it looks at the
reference or the match, and the Inspector greys both fields out — so a document
left on the default scales by exactly nothing, and dragging Reference does
nothing visible because the control is disabled. Switch **Scale Mode** to *Scale
With Screen* first.

The same applies to any scene saved before this feature existed: a `uiDocument`
with no `uiScaleMode` key loads as `Constant`, because the serializer defaults a
missing key to the component's own default
(`SceneSerializer.cpp`). The shipped sample scene sets it explicitly.

| Field | |
|---|---|
| **Scale Mode** | `Constant` (authored pixels are screen pixels) or `Scale With Screen` |
| **Reference** | the resolution your stylesheet's pixels were written for. Default 1920x1080; both shipped documents declare **1280x720** |
| **Match** | `0` follows width, `1` follows height, between blends the two |

Both shipped documents — the sample HUD and the main menu — declare `1280x720`,
which is the player's own window size, so in the shipped player the scale is
exactly `1.0` whichever match they use: an authored pixel is a real pixel and
nothing resamples. Picking a reference the game never actually runs at is a
quiet way to make all your text soft, which is what the sample HUD did while it
declared 1920x1080.

They differ on the **match**, deliberately, and the difference is instructive.
The HUD follows width (`0`): its furniture is anchored to the edges and runs out
of horizontal room first, and an ultrawide should show *more*, not bigger. The
menu follows height (`1`), because it is a centred column and height is what
constrains it — following width, a 5120x1440 gave a settings panel 2880 real
pixels across and verbs at four times their authored size. Same reference, two
shapes, two answers. The blend is in log space, so a match of
`0.5` on a surface that halved in width and doubled in height gives exactly `1.0` —
the geometric mean. A linear blend would give `1.25` and visibly favour whichever
axis grew.

The scale is computed from the **whole surface**, never from the document's region.
A sidebar occupying a quarter of the screen must scale by how big the *screen* is,
or it would shrink its own text on exactly the large display the feature exists for.

### The unit rule

This is the one thing to know, and it is what keeps hit-testing, clipping and every
scroll offset out of the scale's way:

> **`Style` is in AUTHORED units** (reference-resolution pixels).
> **Everything else is in REAL surface pixels**, always, at every scale —
> `ComputedLayout`, scroll offsets, pointer positions, `ScrollIntoView`,
> `SetOrigin`, `SetContentExtent`.

The conversion happens where an authored length or a text measurement *enters*
layout, and nowhere else. So in app code:

```cpp
el->style().width = StyleLength::Px(100);   // 100 AUTHORED px - scales
el->SetScrollOffset({ 0.0f, 100.0f });      // 100 REAL px - does not
```

That asymmetry is deliberate: one is authoring, the other is geometry. Layout still
solves at the real viewport size, which is why `HitTest` needs no inverse mapping
and `readLayout_` is untouched — scaling on the way *out* instead would have
multiplied the document origin along with everything else, and a document pinned to
the right-hand quarter of a 3840px surface would have landed at 5760.

Percentages are **not** scaled. A percentage is already relative to a parent that
has itself been scaled, so scaling it too would compound and a 50%-wide bar would
become 100%.

A wheel notch is an **authored** amount, so it travels twice as far in real
pixels at scale 2 — that is what keeps a notch covering the same three rows.

A page is not, and does not need to be: it is 90% of the element's own laid-out
box, and that box is already in real pixels, so there is nothing to convert. The
page therefore tracks the **box** rather than the scale. A scroller sized in
authored px doubles at scale 2 and its page doubles with it; one sized as a
percentage, or by `flex-grow` against a real viewport, keeps its real height and
its page is unchanged. Both are right, because a page means *this much of what
you can see*, not a fixed distance.

**Text magnifies rather than re-baking.** The glyph atlas is baked once, at
`kUIFontAtlasPixels` (`Engine/src/render2d/Font.h`), and every size on screen is
that atlas scaled. Measurement stays exact at every scale (`Font::Measure` is
linear in its scale argument), so layout is never wrong — only crispness suffers,
and only when magnifying. A pixel-height-keyed font cache is the real fix and is
not built yet.

That is why the atlas is baked at **48px** rather than at body-text size: a menu
title wants around 40px, and there is no `font-size`, `font-family` or
`font-weight` property to reach for — the atlas *is* the size. Baking large means
ordinary text downsamples (free, and sharper) instead of headings upsampling.

**`font-scale` therefore multiplies the atlas size, not a body size.** `0.375` is
18px and `1.0` is 48px. Nothing in this system inherits, so an element that no
`font-scale` rule matches takes `Style`'s own default of `1.0` — full atlas size.
A stylesheet that wants ordinary text to be ordinary has to say so:

```css
* { font-scale: 0.375; }   /* 18px against the 48px atlas */
```

`hud.cstyle` and `menu.cstyle` both open with that rule. The bake size and the
base scale are two halves of one contract with only one half in C++, so
`tests/test_ui_stylesheet.cpp` asserts they still agree — change the bake without
rewriting the stylesheets and the suite says so instead of every string in the
game quietly changing size.

---

## Hot reload

`UIAssetDocument` (`Engine/src/ui/UIAssetDocument.h`) watches the markup and
stylesheet and rebuilds when either changes. Edit `hud.cstyle`, alt-tab, and the
running game has the new look — no rebuild, no restart, no losing the state you
were testing.

```cpp
UIAssetDocument ui;
ui.Load("Exported/UI/hud.cxml", "Exported/UI/hud.cstyle", [&](UIDocument& doc) {
    // Runs after EVERY successful (re)load, on the finished tree.
    button = doc.root().Find("scoreButton");
    button->OnClick(...);
    ApplyCurrentGameState();
});
...
ui.Update(dt);   // once per frame; stats the files a few times a second
```

The bind callback is not optional politeness — a reload **rebuilds the tree**,
so every element pointer and every registered handler is invalidated. That is
the classic silent failure of hot-reload systems: the UI looks right and the
buttons quietly stop working. The API makes re-binding the only way in.

Behaviour worth knowing:

- **A broken markup edit is a no-op.** Half-typed files are the normal state
  while iterating, so a parse failure reports and keeps running the last good
  tree rather than blanking the screen. Fixing the file reloads normally.
- **A broken stylesheet is not fatal.** Structure loads, the last good styling
  stays applied, and the errors are reported.
- **Inline styles survive.** They are replayed after the sheet on every apply,
  so re-applying a stylesheet can never drop them.
- Polls every 0.25 s by default (`SetPollInterval`); `SetHotReloadEnabled(false)`
  stops watching but leaves `Reload()` working.

**Gotcha:** the running app reads the **staged** copy next to the executable
(`build/bin/<config>/Exported/UI/`), which every build refreshes from
`Editor/src/Exported/UI/`. Edit the staged copy to iterate live, then copy your
changes back into the source tree to keep them.

---

## Text

`Font` (`Engine/src/render2d/Font.h`) bakes a glyph atlas with stb_truetype at a
fixed pixel height and packs it with stb_rect_pack, retrying larger atlases
(512 → 4096) until the requested size fits. `Renderer2D::DrawText` takes the
**top-left** of the text box, not the baseline, so it drops straight into a
layout rect. UTF-8 input; undecodable bytes become U+FFFD. `\n` starts a line.

Glyphs need no separate shader path: the atlas is uploaded with a swizzle that
makes it read as `(1,1,1,coverage)`, so a glyph is just a white sprite with an
alpha mask and batches with everything else.

The engine ships **Roboto** (`Exported/Fonts/Roboto.ttf`, SIL Open Font License
1.1 — see `LICENSE-Roboto.txt`).

---

## UI as scene content

A `UIDocumentComponent` puts UI on an **entity**, so a scene declares its own
interface instead of the executable doing it:

| Field | Means |
|---|---|
| `markup` / `stylesheet` | project-relative paths, both hot-reloading |
| `sortOrder` | higher draws on top; ties break on entity order, so it is stable across a save |
| `enabled` | off hides it and stops it consuming input — what a pause menu wants from everything beneath it |
| `interactive` | off for a decorative overlay, or it swallows clicks meant for what is underneath |
| `region` | x, y, width, height as **fractions** of the UI surface; the default `0,0,1,1` is all of it |

Add it from the Inspector's **Add Component ▸ UI Document**, and it is
serialized and undoable like every other component.

`UIWorld` drives them — the UI's equivalent of `AudioWorld` and `ScriptWorld`:

```cpp
UIWorld world;                     // outlives the draw callback
world.SetFont(&font);
world.shared().SetInt("hp", 100);  // visible to every document as `scene`

world.SetPointer(p);
world.SetKeyboard(kb);
world.Update(scene.registry, w, h, dt);
world.Draw(r2d);
```

Every document sees the shared source under the name **`scene`**, so markup can
bind to gameplay values without per-document wiring; a document may still
register its own through `world.document(entity)->bindingContext()`.

Live documents are **cached by entity**. A `UIAssetDocument` owns a parsed tree,
a binder index and hot-reload stamps, so only a *path* change reloads — toggling
`enabled` keeps all three. Removing the component or destroying the entity drops
the document on the next `Update`.

**Input goes to two targets, and usually they are the same document.** The
pointer goes to the topmost interactive document under it. The keyboard — and
directional navigation with it — goes to whichever interactive document holds
focus, falling back to the pointer's document when nothing does. Otherwise a
pause menu and the HUD beneath it would both react to the same click.

They were one target until the wheel arrived. Focus took the pointer with it,
which was harmless while the pointer only meant clicks, since you had to click
to move focus in the first place; a wheel notch arrives with no click, so a
panel you were merely hovering got an empty state and silently refused to
scroll. Documents that lose input get an empty pointer
state rather than being skipped, so they also drop their hover and press styling
instead of staying lit under a menu.

A document whose markup fails to load is **reported and kept**, so a fixed file
is picked up by the ordinary hot-reload poll and one broken document never takes
the others down with it.

**Regions** let a document occupy part of the surface — a sidebar, a minimap
corner, a split-screen half. They are stored as **fractions**, not pixels, so a
layout does not silently change meaning between 1080p and 4K (the same reason
percentages exist in the stylesheet). Layout runs at the region's size, so a
`width: 50%` inside a half-width region is a quarter of the screen.

The mechanism is one value: `UIDocument::SetOrigin`. Layout already produces
absolute rects, so offsetting the root moves painting, hit-testing and clipping
together — a click outside the region simply misses, with no containment check
anywhere. Nonsense values are clamped rather than producing a negative box.

---

## Using `Renderer2D` directly (2D games)

Nothing about `Renderer2D` (`Engine/src/render2d/Renderer2D.h`) is UI-specific.
The same `SetUIDraw` hook works for a 2D game's sprites.

Two projection modes, each using its own domain's convention on purpose:

| Mode | Origin | +y | Units | For |
|---|---|---|---|---|
| `BeginScreen(w, h)` | top-left | **down** | pixels | UI, HUDs — matches HTML/CSS |
| `BeginWorld(cam, w, h)` | camera centre | **up** | world units | 2D gameplay — matches the 3D world |

`Camera2D` carries position, zoom and rotation. Draws are `DrawQuad`,
`DrawSprite` (with a `TexRegion` for atlases), `DrawSpriteRotated`, and
`DrawText`, each taking a **sort layer**. Clip rects nest by intersection, so a
child can never draw outside its parent.

Batching accumulates draws into one CPU vertex buffer and flushes when the
texture, clip rect, or buffer capacity forces it; sort layer is applied as a
stable sort before flushing, so you can emit in any order. `stats()` reports
draw calls, quads and flushes per frame.

`Begin*`/`End` capture and restore every GL bit the 2D layer touches. That is
load-bearing: the 3D pipeline runs passes in a bare loop with no inter-pass
reset, so a leaked blend or depth state would corrupt the next pass.

---

## Where the pieces live

| File | What |
|---|---|
| `Engine/src/render2d/Renderer2D.h` | Batched 2D renderer (general-purpose) |
| `Engine/src/render2d/Font.h` | stb_truetype glyph atlas, UTF-8, measurement |
| `Engine/src/ui/UIStyle.h` | The `Style` struct — the CSS-shaped subset |
| `Engine/src/ui/UIElement.h` | `UIElement` + `UIDocument` (tree, layout, draw, input) |
| `Engine/src/ui/UIEvent.h` | Event types, `UIEvent`, `UIPointerState` |
| `Engine/src/ui/UIStyleSheet.h` | `.cstyle` parser, selectors, cascade, the property table |
| `Engine/src/ui/UIMarkup.h` | `.cxml` loader |
| `Engine/src/ui/UIValue.h` | The bound-value transport and its coercions |
| `Engine/src/ui/UIDataSource.h` | `UIDataSource`, converters, `UIBindingContext` |
| `Engine/src/ui/UIBinding.h` | The `{hole}` template and `UIBinder` |
| `Engine/src/ui/UITextureCache.h` | `background-image` -> GL textures, one cache per context |
| `Engine/src/ui/UIAssetPath.h` | Authored asset paths, interned to an int |
| `Engine/src/ui/UITabs.h` | `UITabSpec` — what the loader expanded for a `<TabView>` |
| `Engine/src/ui/UITabView.h` | The runtime half: selection, clicks, keyboard |
| `Engine/src/ui/UIScale.h` | `UIScaleSettings` + `ComputeUIScale` — authored px to screen px |
| `Engine/src/ui/UIRepeat.h` | `UIRepeatSpec` — what the loader expanded for a `repeat=` |
| `Engine/src/ui/UIRepeatPool.h` | The fixed slot pool a `repeat=` slides over its list |
| `Engine/src/ui/UIInteractionStyler.h` | pseudo-class re-cascading |
| `Engine/src/ui/UITextField.h` | `UITextEdit` — the text-entry model |
| `Engine/src/ui/UISlider.h` | `UISliderState` — a slider's value and its digital ramp |
| `Engine/src/ui/UINav.h` | `UINavState`, `UINavRepeater` — directional INTENTS, no devices |
| `Engine/src/ui/UINavSynth.h` | Named input actions -> intents, the one file that knows a pad exists |
| `Engine/src/ui/UIComponent.h` | `UIDocumentComponent` — UI on an entity |
| `Engine/src/ui/UIWorld.h` | Drives every document in a scene |
| `Engine/src/ui/UIAssetDocument.h` | Markup + stylesheet assets with hot reload |
| `Engine/src/ui/DemoUIContent.h` | The sample's seeded values, its three actions (`addScore`, `invPrev`, `invNext`) and its `healthTint` converter |
| `Engine/src/ui/MenuUIContent.h` | The sample menu's verbs, and the host hooks they need |
| `Engine/src/render/passes/UIPass.h` | The render pass and the `UIDrawFn` hook |

## Not there yet

Word wrap: text breaks where you put a newline and nowhere else. IME composition
(dead keys and layouts work, because text arrives already decoded, but there is
no composition window). A checkbox, which is why `:checked` is still refused.
Transitions and animation. `position: fixed`, `position: sticky`, and portals.

Gradients are two stops, corner to corner — no three-stop ramps, no radials, no
arbitrary angle. Borders are a single uniform width and are **paint only**: no
per-side widths, and a border does not inset the content box.

On input: no touch, so no kinetic scrolling and no on-screen keyboard; no
rebinding UI from inside the UI, though `InputMap` is rebindable at runtime and
that is where it would go; and one nav focus per document, so two players cannot
drive two cursors through one menu.

Within scrolling specifically: kinetic/touch momentum, which needs touch input
this engine does not have; a reserved scrollbar gutter — CSS invented
`scrollbar-gutter` to break a reflow loop that overlay bars do not have, so
`padding-right` is the answer here; hold-to-repeat on a track press; and
built-in virtualisation — a scroller lays out every row it contains, and a
`repeat=` inside one describes the POOL rather than the list, though
`SetContentExtent` lets you window either by hand (see
[Scrolling](#scrolling-and-clipping)). Drawing off-screen rows is already
culled; laying them out is not.
