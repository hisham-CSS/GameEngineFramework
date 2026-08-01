#pragma once
// Authored asset paths, interned to a small int.
//
// A stylesheet declaration must carry its value as something trivially
// copyable. UIDeclaration is copied by value through the whole cascade and its
// ApplyTo runs per element, per cascade, per :hover edge — a std::string there
// is an allocation on a path that has to stay free.
//
// It must ALSO be reconstructible from the declaration alone, because
// UIStyleSheet::Recascade resets Style to defaults and re-runs the rules.
// Anything the cascade cannot put back is gone the first time the mouse moves,
// which is the same trap that makes a TabView's panel visibility a binding
// rather than a raw display write.
//
// Ids are monotonic and never reused, so an id in a Style can never come to
// mean a different file. The table is bounded by the number of distinct
// authored paths in a project — tens — and is MAIN-THREAD-ONLY: every writer is
// a stylesheet parse, and those only ever run from UIAssetDocument::Reload.
#include "../core/Core.h"

#include <string>

namespace MyCoreEngine::ui {

    // Never returns 0. `normalized` is stored verbatim, so callers must agree
    // on spelling — UIStyleSheet passes the sandbox-checked path.
    ENGINE_API int InternAssetPath(const std::string& normalized);
    // "" for 0 or an unknown id.
    ENGINE_API const std::string& AssetPathOf(int id);

} // namespace MyCoreEngine::ui
