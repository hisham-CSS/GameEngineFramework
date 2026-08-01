#include "UIAssetPath.h"

#include <vector>

namespace MyCoreEngine::ui {

namespace {
    // Function-local rather than a file static, so there is no
    // static-initialisation-order question with anything that interns during
    // its own construction.
    std::vector<std::string>& table() {
        static std::vector<std::string> t;
        return t;
    }
    const std::string kEmpty;
}

int InternAssetPath(const std::string& normalized) {
    std::vector<std::string>& t = table();
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == normalized) return int(i) + 1;   // ids are 1-based; 0 = none
    }
    t.push_back(normalized);
    return int(t.size());
}

const std::string& AssetPathOf(int id) {
    const std::vector<std::string>& t = table();
    if (id <= 0 || std::size_t(id) > t.size()) return kEmpty;
    return t[std::size_t(id) - 1];
}

} // namespace MyCoreEngine::ui
