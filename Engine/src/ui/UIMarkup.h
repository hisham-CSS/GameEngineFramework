#pragma once
// CXML-like markup: UI structure as a data asset instead of C++.
//
//   <UI>
//     <Element name="topBar" class="row bar">
//       <Element name="healthTrack" class="track">
//         <Element name="healthFill" class="fill"/>
//       </Element>
//       <Label name="scoreLabel" class="hud-text" text="SCORE 0"/>
//     </Element>
//     <Button name="scoreButton" class="btn" text="+100"
//             style="margin: 12px 0px 0px 0px;"/>
//   </UI>
//
// The TAG NAME becomes the element's type, so a stylesheet can select `Button`
// without the loader knowing what a button is. Nothing here is hard-coded per
// type: markup describes structure and identity, the stylesheet describes
// appearance, and C++ attaches behaviour by looking elements up by name. That
// separation is the whole point of the format.
//
// Attributes: name (#id), class (space-separated), text, style (inline
// declarations, which outrank every stylesheet rule as in CSS).
#include "../core/Core.h"

#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    class UIElement;
    class UIDocument;

    class ENGINE_API UIMarkup {
    public:
        // Replaces the document's tree. On ANY error the document is left
        // completely untouched — the new tree is built in full first and only
        // swapped in once it is known good, so a typo during hot reload cannot
        // blank a working UI.
        static bool LoadInto(UIDocument& doc, const std::string& xml,
                             std::vector<std::string>& errors,
                             const std::string& originName = "<string>");

        // As above, from a file. The path is checked with PathIsContained
        // BEFORE the parser opens it: markup is authored (i.e. untrusted)
        // content feeding a parser, the same class of input as models, scripts,
        // audio clips and HDRis, all of which are gated the same way.
        static bool LoadFileInto(UIDocument& doc, const std::string& path,
                                 std::vector<std::string>& errors);
    };

} // namespace MyCoreEngine::ui
