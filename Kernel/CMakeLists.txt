# The authoritative fighting-game simulation, as its own target.
#
# The point of this file is what is NOT in it. CseKernel calls
# target_link_libraries exactly once, with nothing, and the assertion at the
# bottom makes that a build failure to change rather than a convention to
# remember. docs/ARCHITECTURE.md D2 says the gameplay core contains no Jolt, no
# EnTT, no Lua and no libm; docs/ADR-002-open-decisions.md CHOICE D says to make
# that structural, because the plan itself names "the standing temptation" to
# reach into the physics world for a hitbox and a rule the build enforces beats
# a rule in a document. This repository has the evidence: the fast-math gate
# works for exactly that reason.
#
# It is also why the kernel is a top-level directory rather than Engine/src/game.
# Inside Engine it would inherit Engine's link interface and every one of those
# dependencies, and the guarantee would be worth nothing.

add_library(CseKernel STATIC
    src/Simulate.cpp
    src/Combat.cpp
)

target_include_directories(CseKernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# C++17, matching the rest of the tree. Not a higher standard: this code has to
# compile with whatever a port target offers, and the kernel is the last thing
# that should need a modern toolchain.
target_compile_features(CseKernel PUBLIC cxx_std_17)

# Position-independent so the Engine (a shared library) can absorb it later
# without a relink surprise on Linux.
set_target_properties(CseKernel PROPERTIES POSITION_INDEPENDENT_CODE ON)

# NOTE: there is deliberately no target_link_libraries() call here.

# --- The guarantee, enforced ------------------------------------------------
#
# Configure-time, so it fails before anything compiles and the message explains
# itself rather than surfacing as a mysterious symbol. If you are here because
# this fired: adding a dependency to the kernel is a real architectural decision
# (it ends bit-identical simulation if the dependency does anything in float, or
# iterates a hash container, or allocates), so make it deliberately -- amend
# ARCHITECTURE.md D2 and ADR-002 CHOICE D, then relax this check with the reason.
foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_kernel_links CseKernel ${prop})
    if (_kernel_links)
        message(FATAL_ERROR
            "CseKernel has acquired a link dependency: ${prop} = ${_kernel_links}\n"
            "The gameplay kernel must link nothing -- not Jolt, not EnTT, not Lua, "
            "not the Engine. That is ARCHITECTURE.md D2 made structural (see "
            "ADR-002 CHOICE D). Bit-identical cross-platform simulation is a "
            "property of what this target CANNOT reach.")
    endif()
endforeach()
unset(_kernel_links)
