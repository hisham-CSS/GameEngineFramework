# In-game UI and the 2D layer

The engine ships two related things:

- **`Renderer2D`** — a general-purpose batched 2D renderer. It knows nothing
  about UI, and is deliberately shaped so a **2D game** can be built directly on
  it (world-space camera, sprite atlases, sort layers).
- **The UI system** — a retained element tree with flexbox layout, CSS-like
  stylesheets, XML-like markup, pointer events, data binding, and hot reload. It
  is the first consumer of `Renderer2D`, not a privileged one.

The UI model is modelled on web front-end and Unity's UI Toolkit: markup
(`.uxml`) for structure, a stylesheet (`.uss`) for appearance, bindings for
values, and C++ for behaviour. If you know CSS flexbox, you already know this
system.

> This is not the *editor's* UI. The editor is ImGui (immediate mode); this is
> the UI your **game** draws, and it renders in both the editor's Game view and
> the shipped Player.

---

## Quick start

**UI is scene content.** The shipped sample is an entity named `HUD` in the
default scene, carrying a `UIDocumentComponent` that points at
`Exported/UI/hud.uxml` + `hud.uss`. Nothing in the editor or the player installs
it — select `HUD` in the Hierarchy and you are looking at the whole thing.

To put UI in your own scene: select an entity, **Add Component ▸ UI Document**,
and give it a markup path. That is the entire integration.

A host wires the system up once and never mentions a specific UI again:

```cpp
UIWorld uiWorld;                        // outlives the draw callback
uiWorld.SetFont(&font);
InstallDemoUIContent(uiWorld);          // the sample's action + converter

renderer().SetUIDraw([&](Renderer2D& r2d, int w, int h, float dt) {
    uiWorld.SetPointer(pointerState);   // see "Input" below
    uiWorld.SetKeyboard(keyboardState);
    uiWorld.Update(scene.registry, w, h, dt);
    uiWorld.Draw(r2d);
});
```

The only C++ a UI needs is what a file cannot carry: **named actions** and
**converters**. Everything else — structure, appearance, values, interaction
states — is content that hot-reloads.

`UIWorld::Update` runs each document through `Update(dt)` → `UpdateToTarget()` →
`AdvanceTime(dt)` → `Layout` → `UpdatePointer` → `UpdateKeyboard` →
`PublishToSources()` → `RestyleInteractive()` → a conditional second `Layout`.
Bindings run **before** layout so a changed label is measured at its new width
on the frame it changes; a `setText` from an input handler never was — it lands
after the solve and paints at the previous frame's size.

> **Upgrading an existing scene.** The `HUD` entity ships in the default
> `scene.json`, but a scene you saved earlier will not have it — saved scenes
> are never overwritten by a build. Add it in one step: select any entity and
> use **Add Component ▸ UI Document**, which seeds the sample's paths for you.

---

## Markup: `.uxml`

Abridged from the shipped `hud.uxml`:

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
| `value` / `maxlength` / `mask` / `multiline` | `<TextField>` only — see [Text entry](#text-entry) |
| `bind-value` | `<TextField>` only — the one **two-way** binding |
| `push-hovered` / `push-pressed` / `push-focused` | element state back to the source |
| `classes` | toggles classes from bools — `classes="low-health: {isLow}"` |

Anything else is a **load error**. That matters more than it sounds: this loader
used to read the attributes it knew and ignore the rest, so `nmae="healthFill"`
produced an element no stylesheet rule and no `Find()` could ever locate.

The root tag maps onto the document's existing root, so `<UI name="hud">` names
and styles the root itself rather than creating an extra wrapper.

Tag names are free-form. `Label` and `Button` carry no built-in behaviour — they
are conventions that give stylesheets something to select on. Behaviour comes
from the handlers you attach.

**Gotcha:** the file path is run through the same containment check as models,
scripts, clips and HDRis (`PathIsContained`). Absolute paths and `..` are
refused before the file is opened, because markup is authored content flowing
into a parser.

---

## Stylesheets: `.uss`

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
`.btn:hover`), and the descendant and child combinators:

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
| Paint | `background-color`, `color`, `font-scale` |
| Behaviour | `overflow: visible\|hidden\|scroll`, `pointer-events: auto\|none`, `display: flex\|none` |

Lengths are `auto`, `Npx`, `N%`, or a bare number (treated as px). Colours are
`#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb(r,g,b)`, `rgba(r,g,b,a)` (channels 0–255,
alpha 0–1), or a handful of names.

**Not supported, and reported as errors rather than silently ignored:**
any other pseudo-class, at-rules, variables, and property inheritance. No PROPERTY cascades from parent to child — every element
is styled independently, and a context selector constrains *which* elements a
rule reaches rather than passing values down.

### Interaction styling

```css
.btn         { background-color: #292d33; }
.btn:hover   { background-color: #424852; }
.btn:active  { background-color: #d98c26; color: #14161a; }
```

`:hover` is true for the element under the pointer **and its ancestors**, as in
CSS, so a button and the panel containing it are both hovered. `:active` is true
only for the element the press landed on. Compounds work: `.btn:hover:active`
requires both.

Drive it by calling `RestyleInteractive()` once per frame, **after**
`UpdatePointer` (which is where hover and press are decided):

```cpp
doc.UpdatePointer(pointer);
if (assets.RestyleInteractive()) doc.Layout(w, h, font);  // a state rule can change a box
```

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

| Event | Goes to | Bubbles |
|---|---|---|
| `FocusIn` / `FocusOut` | the element gaining/losing focus | no (like the DOM) |
| `KeyDown` | the focused element | yes |
| `TextInput` | the focused element | yes |
| `ValueChanged` | the control that was edited | yes |

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
| click | places the caret at the nearest character boundary |
| Tab | **leaves** the field — it is not consumed |

Editing runs as a **default action**: the `KeyDown` or `TextInput` event is
dispatched and bubbles first, and the field only acts if nothing called
`StopPropagation`. That is the DOM's ordering, and it lets an app pre-empt a key
without the field knowing about it. `ValueChanged` fires only when the value
actually changed — moving the caret is not an edit.

**Multi-line.** `multiline="true"` is what makes Enter, Up and Down mean
anything, and it is what turns Home and End from "the value" into "this line".
A single-line field leaves all four to whatever contains it, so Enter can still
submit a form. The attribute is an error on anything but a `<TextField>`.

**A field always clips to its own box and always scrolls its text to follow the
caret**, whatever `overflow` says — `overflow` governs children, and a field has
none. So a long value stays inside its box and typing past the width scrolls
rather than spilling. It draws no scrollbar and does not size itself to fit, so
give a multiline field a `height` or `max-height`.

**Undo** coalesces a burst of typing into one step, because undoing a word one
letter at a time is not what anyone means by it. A deletion, a caret jump, or
typing after an undo all start a fresh run, and the history is capped at 64
steps. Writing the value from outside — a binding, `setValue`, a load — **clears
the history**: an external write is not an edit the user made, and being able to
undo back to a value you never typed is worse than not being able to undo.

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

## Layout: flexbox

Layout is solved by **yoga** — the same engine Unity's UI Toolkit uses. No yoga
type appears in any engine header: each `UIElement` holds an opaque handle, so
the layout engine can be replaced without touching a line of authored UI.

Everything behaves as CSS flexbox does, which means the usual reflexes apply:
`justify-content: space-between` on a row pushes children to opposite ends at
any width, and an absolutely-positioned child with `inset: 0` plus centring
alignment stays centred at every viewport shape — no arithmetic on the viewport
size anywhere.

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
is wrong twice over: an `inset: 0` overlay that scrolled away would stop covering
*and* stop blocking clicks. It buys sticky headers and lock veils for free. A
scroller's own `text=` does not scroll either; put a header in a sibling element.

**Scrollbars are overlays.** They reserve no gutter, so they paint over the right
edge of the content — add `padding-right` if that matters. A bar that changed the
layout could make itself disappear, and then reappear, forever. The thumb is
draggable, and pressing it never reaches the content underneath.

**No chaining.** The innermost scrollable ancestor under the pointer keeps the
wheel, even when it is already at its end. Choosing by *remaining room* instead
would teleport the outer panel the moment an inner list hit its bottom — which is
the resting state of every log.

`pointer-events: none` disables scrolling too, and lets the wheel reach the
document beneath. Put it on the decorative parts of an overlay, not on a
container you want to scroll.

From C++:

```cpp
el->SetScrollOffset({ 0.f, 120.f });     // clamped; visible at the next Layout
el->ScrollIntoView(child->layout().position, child->layout().size);
el->scrollOffset(); el->maxScroll(); el->contentSize();
```

`ScrollIntoView` is how you pin a log to its tail. Focus already uses it: Tabbing
to a control below the fold scrolls it into view, because a `:focus` ring nobody
can see — over a control Enter would then activate — is worse than no focus ring.

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

Available: `OnClick`, `OnPointerDown/Up/Move/Enter/Leave`, `OnWheel`. A **click**
requires press *and* release on the same element, so sliding off a button before
letting go correctly cancels it. Prefer an authored `on-click="actionName"` where
you can — a bound action survives a hot reload by itself, while a handler
attached in C++ has to be re-attached.

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
```

```xml
<UI name="hud" data-source="scene">
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
never guesses**: every conversion that is not obviously correct fails and names
both kinds (`cannot use string 'rifle' as a length`). Without reflection, a
value that converted to something plausible but wrong is indistinguishable from
a UI that simply doesn't work.

A string is never a bool, for the same reason — `"false"` is truthy under one
obvious rule and falsy under another. Strings *do* parse as lengths and colours,
through the stylesheet's own parsers, so `"50%"` and `"#d93a3d"` mean the same
thing in a bound value as in a `.uss` file.

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
| unknown source / property / converter / action | `Rebuild` | reported once **with the names that do exist**; that binding stays pending and resolves if you register later |
| value won't convert, non-finite, negative size | runtime | reported once per binding, re-armed on a kind change; that write is skipped |
| a bound property that the stylesheet also declares | load | a **note** — the rule is the pre-bind default, and saying so beats editing the `.uss` and watching nothing happen |

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

### Cost

An idle frame is one integer compare per source plus one per binding — no
allocations, no tree walks, no string work. Only a write that can change a
**box** triggers a re-layout, so a bound colour never does.

---

## Hot reload

`UIAssetDocument` (`Engine/src/ui/UIAssetDocument.h`) watches the markup and
stylesheet and rebuilds when either changes. Edit `hud.uss`, alt-tab, and the
running game has the new look — no rebuild, no restart, no losing the state you
were testing.

```cpp
UIAssetDocument ui;
ui.Load("Exported/UI/hud.uxml", "Exported/UI/hud.uss", [&](UIDocument& doc) {
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

**Input goes to one document**: the topmost interactive one under the pointer,
or whichever holds focus. Otherwise a pause menu and the HUD beneath it would
both react to the same click. Documents that lose input get an empty pointer
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
| `Engine/src/ui/UIStyleSheet.h` | `.uss` parser, selectors, cascade, the property table |
| `Engine/src/ui/UIMarkup.h` | `.uxml` loader |
| `Engine/src/ui/UIValue.h` | The bound-value transport and its coercions |
| `Engine/src/ui/UIDataSource.h` | `UIDataSource`, converters, `UIBindingContext` |
| `Engine/src/ui/UIBinding.h` | The `{hole}` template and `UIBinder` |
| `Engine/src/ui/UIInteractionStyler.h` | pseudo-class re-cascading |
| `Engine/src/ui/UITextField.h` | `UITextEdit` — the text-entry model |
| `Engine/src/ui/UIComponent.h` | `UIDocumentComponent` — UI on an entity |
| `Engine/src/ui/UIWorld.h` | Drives every document in a scene |
| `Engine/src/ui/UIAssetDocument.h` | Markup + stylesheet assets with hot reload |
| `Engine/src/ui/DemoUIContent.h` | The sample's one action and one converter |
| `Engine/src/render/passes/UIPass.h` | The render pass and the `UIDrawFn` hook |

## Not there yet

Word wrap: text breaks where you put a newline and nowhere else. IME composition
(dead keys and layouts work, because text arrives already decoded, but there is
no composition window). A checkbox, which is why `:checked` is still refused.
Repeating a template over a list — bindings address one property, not a
collection. Transitions and animation. `position: fixed`, `position: sticky`, and
portals.

Within scrolling specifically: `overflow-x` / `overflow-y` as separate
properties; the `auto` keyword (reported until it has a meaning of its own that
`scroll` does not already cover); scroll chaining and gesture latching; keyboard
scrolling — PageUp/PageDown/Home/End do nothing and a scroller is not focusable,
so a text-only panel is mouse-only; momentum and smooth-scroll animation;
Shift+wheel axis swap (both hosts ignore it on purpose, so the two agree); a
reserved scrollbar gutter; `scrollbar-*` styling, the bar's width and colours
being constants; clicking the track to page up and down; a scrollbar for a text
field (a multiline `<TextField>` scrolls its text but draws no bar); and
virtualisation, so a scroller lays out every row it contains.
