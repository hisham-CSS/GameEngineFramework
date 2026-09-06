#include "cse/presentation/CycleFrame.h"

namespace cse::presentation {

std::int64_t FloorDiv(std::int64_t a, std::int64_t b) {
    // Truncation and flooring agree unless the signs differ and there is a
    // remainder; correct for exactly that case. Integer on purpose: two peers'
    // pictures agree only if the frame index is computed identically on every
    // toolchain, and an integer floor needs no libm. (DETERMINISM.md scopes K3
    // to the kernel and CseGame; this library is not scanned by the gate, so
    // this is a property kept here, not a rule enforced here.)
    std::int64_t q = a / b;
    const std::int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

std::uint32_t CycleFrame(std::int64_t phase, std::uint32_t n) {
    if (n == 0) return 0;
    const std::int64_t len = static_cast<std::int64_t>(n);
    std::int64_t m = phase % len;
    if (m < 0) m += len;
    return static_cast<std::uint32_t>(m);
}

std::uint32_t WalkCycleFrame(std::int32_t posXSub, std::int32_t strideSub, std::uint32_t n) {
    if (strideSub <= 0) return 0;
    return CycleFrame(FloorDiv(posXSub, strideSub), n);
}

} // namespace cse::presentation
